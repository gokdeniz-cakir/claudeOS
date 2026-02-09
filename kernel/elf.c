#include "elf.h"

#include <stdint.h>
#include <stddef.h>

#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "serial.h"
#include "spinlock.h"
#include "usermode.h"
#include "vfs.h"
#include "vga.h"

#define ELF_EI_NIDENT           16U
#define ELFCLASS32              1U
#define ELFDATA2LSB             1U
#define ET_EXEC                 2U
#define EM_386                  3U
#define EV_CURRENT              1U
#define PT_LOAD                 1U
#define PF_W                    0x2U

#define USER_KERNEL_SPLIT       0xC0000000U
#define ELF_USER_STACK_PAGE     0x0BFF0000U
#define ELF_USER_STACK_TOP      (ELF_USER_STACK_PAGE + PAGE_SIZE)
#define ELF_MAX_MAPPED_PAGES    1024U
#define ELF_MAX_FILE_SIZE       (1024U * 1024U)
#define ELF_DEMO_VFS_PATH       "/elf_demo.elf"

/* Single-threaded loader scratch list; avoids 4KB stack frame pressure. */
static uint32_t elf_mapped_pages[ELF_MAX_MAPPED_PAGES];
static uint32_t elf_previous_pages[ELF_MAX_MAPPED_PAGES];
static struct spinlock elf_loader_lock = SPINLOCK_INITIALIZER;

struct elf_space_tracker {
    uint32_t cr3;
    uint32_t active_count;
    uint32_t active_pages[ELF_MAX_MAPPED_PAGES];
};

struct elf_replaced_page {
    uint32_t page;
    uint32_t phys;
    uint32_t flags;
};

static struct elf_space_tracker elf_trackers[PROCESS_MAX_COUNT];
static struct elf_replaced_page elf_replaced_pages[ELF_MAX_MAPPED_PAGES];

struct elf32_ehdr {
    uint8_t e_ident[ELF_EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed));

extern const uint8_t _binary_build_elf_demo_elf_start[];
extern const uint8_t _binary_build_elf_demo_elf_end[];
extern const uint8_t _binary_build_fork_exec_demo_elf_start[];
extern const uint8_t _binary_build_fork_exec_demo_elf_end[];

static uint32_t align_up_u32(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static uint32_t add_overflow_u32(uint32_t a, uint32_t b, uint32_t *sum)
{
    if (sum == 0 || a > (0xFFFFFFFFU - b)) {
        return 1U;
    }

    *sum = a + b;
    return 0U;
}

static uint8_t page_was_mapped_by_loader(const uint32_t *mapped_pages,
                                         uint32_t mapped_count, uint32_t page)
{
    uint32_t i;

    for (i = 0U; i < mapped_count; i++) {
        if (mapped_pages[i] == page) {
            return 1U;
        }
    }

    return 0U;
}

static void cleanup_mapped_pages(const uint32_t *mapped_pages, uint32_t mapped_count)
{
    while (mapped_count > 0U) {
        uint32_t phys;

        mapped_count--;
        phys = paging_unmap_page(mapped_pages[mapped_count]);
        if (phys != 0U) {
            pmm_free_frame(phys);
        }
    }
}

static void restore_replaced_pages(const struct elf_replaced_page *replaced_pages,
                                   uint32_t replaced_count)
{
    while (replaced_count > 0U) {
        uint32_t restored_flags;
        uint32_t new_phys;

        replaced_count--;
        restored_flags = replaced_pages[replaced_count].flags;
        new_phys = paging_unmap_page(replaced_pages[replaced_count].page);
        if (new_phys != 0U) {
            pmm_free_frame(new_phys);
        }

        if (paging_map_page(replaced_pages[replaced_count].page,
                            replaced_pages[replaced_count].phys,
                            restored_flags) != 0) {
            /*
             * Best effort rollback. If remap fails, avoid leaking the frame;
             * caller will still fail the load and keep prior tracker state.
             */
            pmm_free_frame(replaced_pages[replaced_count].phys);
        }
    }
}

static void free_replaced_frames(const struct elf_replaced_page *replaced_pages,
                                 uint32_t replaced_count)
{
    uint32_t i;

    for (i = 0U; i < replaced_count; i++) {
        pmm_free_frame(replaced_pages[i].phys);
    }
}

static uint32_t elf_read_cr3(void)
{
    uint32_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static struct elf_space_tracker *elf_find_tracker(uint32_t cr3, uint8_t create)
{
    uint32_t i;
    struct elf_space_tracker *free_slot = 0;

    for (i = 0U; i < PROCESS_MAX_COUNT; i++) {
        if (elf_trackers[i].cr3 == cr3) {
            return &elf_trackers[i];
        }

        if (free_slot == 0 && elf_trackers[i].cr3 == 0U) {
            free_slot = &elf_trackers[i];
        }
    }

    if (create == 0U || free_slot == 0) {
        return 0;
    }

    free_slot->cr3 = cr3;
    free_slot->active_count = 0U;
    return free_slot;
}

static void drop_previous_active_pages_not_reused(const uint32_t *previous_pages,
                                                  uint32_t previous_count,
                                                  const uint32_t *mapped_pages,
                                                  uint32_t mapped_count)
{
    uint32_t i;

    for (i = 0U; i < previous_count; i++) {
        uint32_t page = previous_pages[i];
        uint32_t phys;

        if (page_was_mapped_by_loader(mapped_pages, mapped_count, page) != 0U) {
            continue;
        }

        phys = paging_unmap_page(page);
        if (phys != 0U) {
            pmm_free_frame(phys);
        }
    }
}

static int elf_load_user_image_locked(const uint8_t *image, uint32_t image_size,
                                      struct elf_user_image *loaded)
{
    const struct elf32_ehdr *ehdr;
    const struct elf32_phdr *phdr_table;
    struct elf_replaced_page *replaced_pages = elf_replaced_pages;
    struct elf_space_tracker *tracker;
    uint32_t active_count;
    uint32_t current_cr3;
    uint32_t previous_count;
    uint32_t phdr_bytes;
    uint32_t *mapped_pages = elf_mapped_pages;
    uint32_t mapped_count = 0U;
    uint32_t replaced_count = 0U;
    uint32_t i;

#define ELF_LOAD_FAIL() do { \
    cleanup_mapped_pages(mapped_pages, mapped_count); \
    restore_replaced_pages(replaced_pages, replaced_count); \
    return -1; \
} while (0)

    if (image == 0 || loaded == 0 || image_size < sizeof(struct elf32_ehdr)) {
        return -1;
    }

    current_cr3 = elf_read_cr3();
    tracker = elf_find_tracker(current_cr3, 1U);
    if (tracker == 0) {
        return -1;
    }

    active_count = tracker->active_count;
    previous_count = active_count;
    for (i = 0U; i < active_count; i++) {
        elf_previous_pages[i] = tracker->active_pages[i];
    }

    ehdr = (const struct elf32_ehdr *)image;

    if (ehdr->e_ident[0] != 0x7FU || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        return -1;
    }

    if (ehdr->e_ident[4] != ELFCLASS32 || ehdr->e_ident[5] != ELFDATA2LSB) {
        return -1;
    }

    if (ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_386 ||
        ehdr->e_version != EV_CURRENT || ehdr->e_phnum == 0U ||
        ehdr->e_phentsize != sizeof(struct elf32_phdr)) {
        return -1;
    }

    if (add_overflow_u32((uint32_t)ehdr->e_phoff,
                         (uint32_t)ehdr->e_phnum * (uint32_t)sizeof(struct elf32_phdr),
                         &phdr_bytes) != 0U || phdr_bytes > image_size) {
        return -1;
    }

    phdr_table = (const struct elf32_phdr *)(const void *)(image + ehdr->e_phoff);

    for (i = 0U; i < (uint32_t)ehdr->e_phnum; i++) {
        const struct elf32_phdr *ph = &phdr_table[i];
        uint32_t seg_end;
        uint32_t file_end;
        uint32_t page_start;
        uint32_t page_end;
        uint32_t page;
        uint32_t copy_flags;

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0U) {
            continue;
        }

        if (ph->p_filesz > ph->p_memsz) {
            ELF_LOAD_FAIL();
        }

        if (add_overflow_u32(ph->p_offset, ph->p_filesz, &file_end) != 0U ||
            file_end > image_size) {
            ELF_LOAD_FAIL();
        }

        if (add_overflow_u32(ph->p_vaddr, ph->p_memsz, &seg_end) != 0U ||
            ph->p_vaddr >= USER_KERNEL_SPLIT || seg_end > USER_KERNEL_SPLIT) {
            ELF_LOAD_FAIL();
        }

        page_start = ph->p_vaddr & PAGE_FRAME_MASK;
        page_end = align_up_u32(seg_end, PAGE_SIZE);
        page = page_start;
        copy_flags = PAGE_USER;
        if ((ph->p_flags & PF_W) != 0U) {
            copy_flags |= PAGE_WRITABLE;
        }

        while (page < page_end) {
            uint32_t existing_phys = paging_get_phys_addr(page);

            if (existing_phys != 0U) {
                if (page_was_mapped_by_loader(mapped_pages, mapped_count, page) == 0U) {
                    uint32_t old_flags;

                    if (page_was_mapped_by_loader(elf_previous_pages,
                                                  previous_count,
                                                  page) == 0U) {
                        ELF_LOAD_FAIL();
                    }

                    if (replaced_count >= ELF_MAX_MAPPED_PAGES) {
                        ELF_LOAD_FAIL();
                    }

                    if (paging_get_page_flags(page, &old_flags) != 0) {
                        ELF_LOAD_FAIL();
                    }

                    existing_phys = paging_unmap_page(page);
                    if (existing_phys == 0U) {
                        ELF_LOAD_FAIL();
                    }

                    replaced_pages[replaced_count].page = page;
                    replaced_pages[replaced_count].phys = existing_phys;
                    replaced_pages[replaced_count].flags = PAGE_USER;
                    if ((old_flags & PAGE_WRITABLE) != 0U) {
                        replaced_pages[replaced_count].flags |= PAGE_WRITABLE;
                    }
                    replaced_count++;
                } else {
                    /* Overlapping PT_LOAD segments may require stronger perms later. */
                    if ((copy_flags & PAGE_WRITABLE) != 0U) {
                        if (paging_or_page_flags(page, PAGE_USER | PAGE_WRITABLE) != 0) {
                            ELF_LOAD_FAIL();
                        }
                    }

                    page += PAGE_SIZE;
                    continue;
                }
            }

            if (mapped_count >= ELF_MAX_MAPPED_PAGES) {
                ELF_LOAD_FAIL();
            }

            existing_phys = pmm_alloc_frame();
            if (existing_phys == 0U) {
                ELF_LOAD_FAIL();
            }

            if (paging_map_page(page, existing_phys, copy_flags) != 0) {
                pmm_free_frame(existing_phys);
                ELF_LOAD_FAIL();
            }

            mapped_pages[mapped_count] = page;
            mapped_count++;
            page += PAGE_SIZE;
        }

        for (page = 0U; page < ph->p_filesz; page++) {
            *((uint8_t *)(uintptr_t)(ph->p_vaddr + page)) = image[ph->p_offset + page];
        }

        for (page = ph->p_filesz; page < ph->p_memsz; page++) {
            *((uint8_t *)(uintptr_t)(ph->p_vaddr + page)) = 0U;
        }
    }

    if (ehdr->e_entry >= USER_KERNEL_SPLIT || paging_get_phys_addr(ehdr->e_entry) == 0U) {
        ELF_LOAD_FAIL();
    }

    if (paging_get_phys_addr(ELF_USER_STACK_PAGE) != 0U) {
        uint32_t stack_existing;
        uint32_t old_flags;

        if (page_was_mapped_by_loader(elf_previous_pages,
                                      previous_count,
                                      ELF_USER_STACK_PAGE) == 0U) {
            ELF_LOAD_FAIL();
        }

        if (replaced_count >= ELF_MAX_MAPPED_PAGES) {
            ELF_LOAD_FAIL();
        }

        if (paging_get_page_flags(ELF_USER_STACK_PAGE, &old_flags) != 0) {
            ELF_LOAD_FAIL();
        }

        stack_existing = paging_unmap_page(ELF_USER_STACK_PAGE);
        if (stack_existing == 0U) {
            ELF_LOAD_FAIL();
        }

        replaced_pages[replaced_count].page = ELF_USER_STACK_PAGE;
        replaced_pages[replaced_count].phys = stack_existing;
        replaced_pages[replaced_count].flags = PAGE_USER;
        if ((old_flags & PAGE_WRITABLE) != 0U) {
            replaced_pages[replaced_count].flags |= PAGE_WRITABLE;
        }
        replaced_count++;
    }

    if (mapped_count >= ELF_MAX_MAPPED_PAGES) {
        ELF_LOAD_FAIL();
    }

    {
        uint32_t stack_phys = pmm_alloc_frame();
        uint32_t j;

        if (stack_phys == 0U) {
            ELF_LOAD_FAIL();
        }

        if (paging_map_page(ELF_USER_STACK_PAGE, stack_phys, PAGE_USER | PAGE_WRITABLE) != 0) {
            pmm_free_frame(stack_phys);
            ELF_LOAD_FAIL();
        }

        mapped_pages[mapped_count] = ELF_USER_STACK_PAGE;
        mapped_count++;

        for (j = 0U; j < PAGE_SIZE; j++) {
            *((uint8_t *)(uintptr_t)(ELF_USER_STACK_PAGE + j)) = 0U;
        }
    }

    drop_previous_active_pages_not_reused(elf_previous_pages, previous_count,
                                          mapped_pages, mapped_count);
    free_replaced_frames(replaced_pages, replaced_count);

    tracker->active_count = mapped_count;
    for (i = 0U; i < mapped_count; i++) {
        tracker->active_pages[i] = mapped_pages[i];
    }

    loaded->entry = ehdr->e_entry;
    loaded->stack_top = ELF_USER_STACK_TOP;
#undef ELF_LOAD_FAIL
    return 0;
}

int elf_load_user_image(const uint8_t *image, uint32_t image_size,
                        struct elf_user_image *loaded)
{
    uint32_t irq_flags;
    int rc;

    irq_flags = spinlock_lock_irqsave(&elf_loader_lock);
    rc = elf_load_user_image_locked(image, image_size, loaded);
    spinlock_unlock_irqrestore(&elf_loader_lock, irq_flags);
    return rc;
}

void elf_forget_address_space(uint32_t cr3_phys)
{
    uint32_t irq_flags;
    uint32_t i;

    if (cr3_phys == 0U) {
        return;
    }

    irq_flags = spinlock_lock_irqsave(&elf_loader_lock);
    for (i = 0U; i < PROCESS_MAX_COUNT; i++) {
        if (elf_trackers[i].cr3 == cr3_phys) {
            elf_trackers[i].cr3 = 0U;
            elf_trackers[i].active_count = 0U;
            break;
        }
    }
    spinlock_unlock_irqrestore(&elf_loader_lock, irq_flags);
}

int elf_load_user_image_from_vfs(const char *path, struct elf_user_image *loaded)
{
    struct vfs_node node;
    uint8_t *image;
    uint32_t total;
    int32_t fd;
    int rc;

    if (path == 0 || loaded == 0) {
        return -1;
    }

    if (vfs_resolve(path, &node) != VFS_OK || node.type != VFS_NODE_FILE ||
        node.size == 0U || node.size > ELF_MAX_FILE_SIZE) {
        return -1;
    }

    image = (uint8_t *)kmalloc(node.size);
    if (image == 0) {
        return -1;
    }

    fd = vfs_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        kfree(image);
        return -1;
    }

    total = 0U;
    while (total < node.size) {
        int32_t got = vfs_read(fd, image + total, node.size - total);
        if (got <= 0) {
            (void)vfs_close(fd);
            kfree(image);
            return -1;
        }
        total += (uint32_t)got;
    }

    (void)vfs_close(fd);
    rc = elf_load_user_image(image, node.size, loaded);
    kfree(image);
    return rc;
}

void elf_run_embedded_test(void)
{
    struct elf_user_image loaded;
    uint32_t image_size;

    image_size = (uint32_t)(uintptr_t)(_binary_build_elf_demo_elf_end -
                                       _binary_build_elf_demo_elf_start);
    if (image_size == 0U) {
        vga_puts("[ELF] embedded image missing.\n");
        serial_puts("[ELF] embedded image missing\n");
        return;
    }

    if (elf_load_user_image(_binary_build_elf_demo_elf_start, image_size, &loaded) != 0) {
        vga_puts("[ELF] load failed.\n");
        serial_puts("[ELF] load failed\n");
        return;
    }

    (void)process_set_current_image_path(ELF_DEMO_VFS_PATH);
    (void)process_set_current_user_break(process_user_heap_base());
    process_refresh_tss_stack();

    vga_puts("[ELF] loaded embedded ELF (ring3 jump).\n");
    serial_puts("[ELF] loaded embedded ELF (ring3 jump)\n");

    usermode_enter_ring3(loaded.entry, loaded.stack_top);
}

void elf_run_fork_exec_test(void)
{
    struct elf_user_image loaded;
    uint32_t image_size;

    image_size = (uint32_t)(uintptr_t)(_binary_build_fork_exec_demo_elf_end -
                                       _binary_build_fork_exec_demo_elf_start);
    if (image_size == 0U) {
        vga_puts("[ELF] fork/exec probe missing.\n");
        serial_puts("[ELF] fork/exec probe missing\n");
        return;
    }

    if (elf_load_user_image(_binary_build_fork_exec_demo_elf_start, image_size, &loaded) != 0) {
        vga_puts("[ELF] fork/exec probe load failed.\n");
        serial_puts("[ELF] fork/exec probe load failed\n");
        return;
    }

    /*
     * Basic fork semantics in this phase are spawn-like: child task execs the
     * caller's recorded image path from VFS.
     */
    (void)process_set_current_image_path(ELF_DEMO_VFS_PATH);
    (void)process_set_current_user_break(process_user_heap_base());
    process_refresh_tss_stack();

    vga_puts("[ELF] loaded fork+exec probe (ring3 jump).\n");
    serial_puts("[ELF] loaded fork+exec probe (ring3 jump)\n");

    usermode_enter_ring3(loaded.entry, loaded.stack_top);
}
