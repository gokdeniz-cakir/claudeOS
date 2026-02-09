#include "syscall.h"

#include <stdint.h>

#include "elf.h"
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "serial.h"
#include "usermode.h"
#include "vfs.h"
#include "vga.h"

#define SYSCALL_RET_ENOSYS   0xFFFFFFFFU
#define SYSCALL_RET_EINVAL   0xFFFFFFFFU
#define SYSCALL_RET_EPERM    0xFFFFFFFFU
#define SYSCALL_RET_ENOMEM   0xFFFFFFFFU

#define USER_KERNEL_SPLIT    0xC0000000U

static uint8_t syscall_trace_once = 0U;

struct fork_child_exec_context {
    char path[PROCESS_IMAGE_PATH_MAX];
};

static uint32_t align_up_u32(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static uint8_t syscall_validate_user_range(uint32_t addr, uint32_t len)
{
    uint32_t end;

    if (len == 0U) {
        return 1U;
    }

    if (addr >= USER_KERNEL_SPLIT) {
        return 0U;
    }

    end = addr + len;
    if (end < addr || end > USER_KERNEL_SPLIT) {
        return 0U;
    }

    return 1U;
}

static uint8_t syscall_validate_user_mapping(uint32_t addr, uint32_t len)
{
    uint32_t end;
    uint32_t page;

    if (len == 0U) {
        return 1U;
    }

    if (syscall_validate_user_range(addr, len) == 0U) {
        return 0U;
    }

    end = addr + len;
    page = addr & PAGE_FRAME_MASK;

    while (page < end) {
        if (paging_get_phys_addr(page) == 0U) {
            return 0U;
        }
        page += PAGE_SIZE;
    }

    return 1U;
}

static uint8_t syscall_validate_user_addr(uint32_t addr)
{
    if (addr >= USER_KERNEL_SPLIT) {
        return 0U;
    }

    return (uint8_t)(paging_get_phys_addr(addr) != 0U);
}

static int32_t syscall_copy_user_cstring(uint32_t user_addr, char *dst,
                                         uint32_t dst_size)
{
    uint32_t i;

    if (dst == 0 || dst_size < 2U || syscall_validate_user_addr(user_addr) == 0U) {
        return -1;
    }

    for (i = 0U; i < (dst_size - 1U); i++) {
        uint32_t addr = user_addr + i;
        char c;

        if (addr < user_addr || syscall_validate_user_addr(addr) == 0U) {
            dst[0] = '\0';
            return -1;
        }

        c = *(const char *)(uintptr_t)addr;
        dst[i] = c;
        if (c == '\0') {
            return 0;
        }
    }

    dst[dst_size - 1U] = '\0';
    return -1;
}

static void syscall_copy_kernel_cstring(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t i = 0U;

    if (dst == 0 || dst_size == 0U) {
        return;
    }

    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    while (i + 1U < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void syscall_fork_child_entry(void *arg)
{
    struct fork_child_exec_context *ctx = (struct fork_child_exec_context *)arg;
    struct elf_user_image loaded;
    char path[PROCESS_IMAGE_PATH_MAX];

    if (ctx == 0) {
        return;
    }

    syscall_copy_kernel_cstring(path, sizeof(path), ctx->path);
    kfree(ctx);

    if (path[0] == '\0') {
        return;
    }

    if (elf_load_user_image_from_vfs(path, &loaded) != 0) {
        serial_puts("[PROC] fork child exec load failed\n");
        return;
    }

    (void)process_set_current_image_path(path);
    (void)process_set_current_user_break(process_user_heap_base());
    process_refresh_tss_stack();

    serial_puts("[PROC] fork child entering user image\n");
    usermode_enter_ring3(loaded.entry, loaded.stack_top);
}

static int32_t syscall_write(uint32_t fd, uint32_t user_buf, uint32_t len)
{
    const char *buf = (const char *)(uintptr_t)user_buf;
    uint32_t i;

    if (fd != 1U && fd != 2U) {
        return -1;
    }

    if (len == 0U) {
        return 0;
    }

    if (len > 0x7FFFFFFFU) {
        return -1;
    }

    if (syscall_validate_user_mapping(user_buf, len) == 0U) {
        return -1;
    }

    for (i = 0U; i < len; i++) {
        vga_putchar(buf[i]);
        serial_putchar(buf[i]);
    }

    return (int32_t)len;
}

static int32_t syscall_open(uint32_t user_path, uint32_t flags)
{
    char kernel_path[VFS_PATH_MAX];
    uint32_t vfs_flags = 0U;
    int32_t fd;

    if (syscall_copy_user_cstring(user_path, kernel_path, sizeof(kernel_path)) != 0) {
        return -1;
    }

    if ((flags & SYSCALL_O_READ) != 0U) {
        vfs_flags |= VFS_OPEN_READ;
    }

    if ((flags & SYSCALL_O_WRITE) != 0U) {
        vfs_flags |= VFS_OPEN_WRITE;
    }

    if (vfs_flags == 0U) {
        vfs_flags = VFS_OPEN_READ;
    }

    fd = vfs_open(kernel_path, vfs_flags);
    if (fd < 0) {
        return -1;
    }

    return fd;
}

static int32_t syscall_read(uint32_t fd, uint32_t user_buf, uint32_t len)
{
    int32_t rc;

    if (len == 0U) {
        return 0;
    }

    if (len > 0x7FFFFFFFU) {
        return -1;
    }

    if (syscall_validate_user_mapping(user_buf, len) == 0U) {
        return -1;
    }

    rc = vfs_read((int32_t)fd, (void *)(uintptr_t)user_buf, len);
    if (rc < 0) {
        return -1;
    }

    return rc;
}

static int32_t syscall_close(uint32_t fd)
{
    if (vfs_close((int32_t)fd) != VFS_OK) {
        return -1;
    }

    return 0;
}

static int32_t syscall_fork(void)
{
    char current_path[PROCESS_IMAGE_PATH_MAX];
    struct fork_child_exec_context *ctx;
    int32_t pid;

    if (process_get_current_image_path(current_path, sizeof(current_path)) != 0 ||
        current_path[0] == '\0') {
        return -1;
    }

    ctx = (struct fork_child_exec_context *)kmalloc(sizeof(*ctx));
    if (ctx == 0) {
        return -1;
    }

    syscall_copy_kernel_cstring(ctx->path, sizeof(ctx->path), current_path);
    pid = process_create_kernel("fork_user", syscall_fork_child_entry, ctx);
    if (pid < 0) {
        kfree(ctx);
        return -1;
    }

    return pid;
}

static uint32_t syscall_exec(uint32_t user_path)
{
    struct elf_user_image loaded;
    char kernel_path[PROCESS_IMAGE_PATH_MAX];

    if (syscall_copy_user_cstring(user_path, kernel_path, sizeof(kernel_path)) != 0) {
        return SYSCALL_RET_EINVAL;
    }

    if (elf_load_user_image_from_vfs(kernel_path, &loaded) != 0) {
        return SYSCALL_RET_EINVAL;
    }

    (void)process_set_current_image_path(kernel_path);
    (void)process_set_current_user_break(process_user_heap_base());
    process_refresh_tss_stack();

    usermode_enter_ring3(loaded.entry, loaded.stack_top);
}

static uint32_t syscall_sbrk(int32_t increment)
{
    uint32_t old_break;
    uint32_t new_break;
    uint32_t heap_base;
    uint32_t heap_limit;
    uint32_t old_mapped_top;
    uint32_t new_mapped_top;
    int64_t candidate_break;

    if (process_get_current_user_break(&old_break) != 0) {
        return SYSCALL_RET_EINVAL;
    }

    heap_base = process_user_heap_base();
    heap_limit = process_user_heap_limit();

    candidate_break = (int64_t)(uint64_t)old_break + (int64_t)increment;
    if (candidate_break < (int64_t)(uint64_t)heap_base ||
        candidate_break > (int64_t)(uint64_t)heap_limit) {
        return SYSCALL_RET_ENOMEM;
    }

    new_break = (uint32_t)(uint64_t)candidate_break;
    old_mapped_top = align_up_u32(old_break, PAGE_SIZE);
    new_mapped_top = align_up_u32(new_break, PAGE_SIZE);

    if (new_mapped_top > old_mapped_top) {
        uint32_t page = old_mapped_top;

        while (page < new_mapped_top) {
            uint32_t phys = pmm_alloc_frame();
            if (phys == 0U) {
                while (page > old_mapped_top) {
                    uint32_t rollback_phys;

                    page -= PAGE_SIZE;
                    rollback_phys = paging_unmap_page(page);
                    if (rollback_phys != 0U) {
                        pmm_free_frame(rollback_phys);
                    }
                }
                return SYSCALL_RET_ENOMEM;
            }

            if (paging_map_page(page, phys, PAGE_USER | PAGE_WRITABLE) != 0) {
                pmm_free_frame(phys);
                while (page > old_mapped_top) {
                    uint32_t rollback_phys;

                    page -= PAGE_SIZE;
                    rollback_phys = paging_unmap_page(page);
                    if (rollback_phys != 0U) {
                        pmm_free_frame(rollback_phys);
                    }
                }
                return SYSCALL_RET_ENOMEM;
            }

            page += PAGE_SIZE;
        }
    } else if (new_mapped_top < old_mapped_top) {
        uint32_t page = new_mapped_top;

        while (page < old_mapped_top) {
            uint32_t phys = paging_unmap_page(page);
            if (phys != 0U) {
                pmm_free_frame(phys);
            }
            page += PAGE_SIZE;
        }
    }

    if (process_set_current_user_break(new_break) != 0) {
        return SYSCALL_RET_EINVAL;
    }

    return old_break;
}

static uint32_t syscall_exit(uint32_t status)
{
    (void)status;

    if (process_get_current_pid() == 1U) {
        return SYSCALL_RET_EPERM;
    }

    process_terminate_current();
}

static uint32_t syscall_getpid(void)
{
    return process_get_current_pid();
}

static uint32_t syscall_process_count(void)
{
    return process_count();
}

static uint32_t syscall_dispatch(uint32_t number, uint32_t arg0, uint32_t arg1,
                                 uint32_t arg2, uint32_t arg3, uint32_t arg4,
                                 uint32_t arg5)
{
    (void)arg3;
    (void)arg4;
    (void)arg5;

    switch (number) {
        case SYSCALL_WRITE:
            return (uint32_t)(int32_t)syscall_write(arg0, arg1, arg2);
        case SYSCALL_EXIT:
            return syscall_exit(arg0);
        case SYSCALL_SBRK:
            return syscall_sbrk((int32_t)arg0);
        case SYSCALL_OPEN:
            return (uint32_t)(int32_t)syscall_open(arg0, arg1);
        case SYSCALL_READ:
            return (uint32_t)(int32_t)syscall_read(arg0, arg1, arg2);
        case SYSCALL_CLOSE:
            return (uint32_t)(int32_t)syscall_close(arg0);
        case SYSCALL_FORK:
            return (uint32_t)(int32_t)syscall_fork();
        case SYSCALL_EXEC:
            return syscall_exec(arg0);
        case SYSCALL_GETPID:
            return syscall_getpid();
        case SYSCALL_PCOUNT:
            return syscall_process_count();
        default:
            return SYSCALL_RET_ENOSYS;
    }
}

void syscall_init(void)
{
    syscall_trace_once = 0U;
    serial_puts("[SYSCALL] INT 0x80 interface initialized\n");
}

void syscall_handler(struct isr_regs *regs)
{
    if (regs == 0) {
        return;
    }

    if (syscall_trace_once == 0U) {
        serial_puts("[SYSCALL] first INT 0x80 received\n");
        syscall_trace_once = 1U;
    }

    regs->eax = syscall_dispatch(regs->eax, regs->ebx, regs->ecx, regs->edx,
                                 regs->esi, regs->edi, regs->ebp);
}
