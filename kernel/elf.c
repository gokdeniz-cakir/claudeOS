#include "elf.h"

#include <stdint.h>
#include <stddef.h>

#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "serial.h"
#include "usermode.h"
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

/* Single-threaded loader scratch list; avoids 4KB stack frame pressure. */
static uint32_t elf_mapped_pages[ELF_MAX_MAPPED_PAGES];

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

int elf_load_user_image(const uint8_t *image, uint32_t image_size,
                        struct elf_user_image *loaded)
{
    const struct elf32_ehdr *ehdr;
    const struct elf32_phdr *phdr_table;
    uint32_t phdr_bytes;
    uint32_t *mapped_pages = elf_mapped_pages;
    uint32_t mapped_count = 0U;
    uint32_t i;

    if (image == 0 || loaded == 0 || image_size < sizeof(struct elf32_ehdr)) {
        return -1;
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
            cleanup_mapped_pages(mapped_pages, mapped_count);
            return -1;
        }

        if (add_overflow_u32(ph->p_offset, ph->p_filesz, &file_end) != 0U ||
            file_end > image_size) {
            cleanup_mapped_pages(mapped_pages, mapped_count);
            return -1;
        }

        if (add_overflow_u32(ph->p_vaddr, ph->p_memsz, &seg_end) != 0U ||
            ph->p_vaddr >= USER_KERNEL_SPLIT || seg_end > USER_KERNEL_SPLIT) {
            cleanup_mapped_pages(mapped_pages, mapped_count);
            return -1;
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
                    cleanup_mapped_pages(mapped_pages, mapped_count);
                    return -1;
                }

                /* Overlapping PT_LOAD segments may require stronger perms later. */
                if ((copy_flags & PAGE_WRITABLE) != 0U) {
                    if (paging_or_page_flags(page, PAGE_USER | PAGE_WRITABLE) != 0) {
                        cleanup_mapped_pages(mapped_pages, mapped_count);
                        return -1;
                    }
                }

                page += PAGE_SIZE;
                continue;
            }

            if (mapped_count >= ELF_MAX_MAPPED_PAGES) {
                cleanup_mapped_pages(mapped_pages, mapped_count);
                return -1;
            }

            existing_phys = pmm_alloc_frame();
            if (existing_phys == 0U) {
                cleanup_mapped_pages(mapped_pages, mapped_count);
                return -1;
            }

            if (paging_map_page(page, existing_phys, copy_flags) != 0) {
                pmm_free_frame(existing_phys);
                cleanup_mapped_pages(mapped_pages, mapped_count);
                return -1;
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
        cleanup_mapped_pages(mapped_pages, mapped_count);
        return -1;
    }

    if (paging_get_phys_addr(ELF_USER_STACK_PAGE) != 0U) {
        cleanup_mapped_pages(mapped_pages, mapped_count);
        return -1;
    }

    if (mapped_count >= ELF_MAX_MAPPED_PAGES) {
        cleanup_mapped_pages(mapped_pages, mapped_count);
        return -1;
    }

    {
        uint32_t stack_phys = pmm_alloc_frame();
        uint32_t j;

        if (stack_phys == 0U) {
            cleanup_mapped_pages(mapped_pages, mapped_count);
            return -1;
        }

        if (paging_map_page(ELF_USER_STACK_PAGE, stack_phys, PAGE_USER | PAGE_WRITABLE) != 0) {
            pmm_free_frame(stack_phys);
            cleanup_mapped_pages(mapped_pages, mapped_count);
            return -1;
        }

        mapped_pages[mapped_count] = ELF_USER_STACK_PAGE;
        mapped_count++;

        for (j = 0U; j < PAGE_SIZE; j++) {
            *((uint8_t *)(uintptr_t)(ELF_USER_STACK_PAGE + j)) = 0U;
        }
    }

    loaded->entry = ehdr->e_entry;
    loaded->stack_top = ELF_USER_STACK_TOP;
    return 0;
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

    (void)process_set_current_user_break(process_user_heap_base());
    process_refresh_tss_stack();

    vga_puts("[ELF] loaded embedded ELF (ring3 jump).\n");
    serial_puts("[ELF] loaded embedded ELF (ring3 jump)\n");

    usermode_enter_ring3(loaded.entry, loaded.stack_top);
}
