#include "syscall.h"

#include <stdint.h>

#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "serial.h"
#include "vga.h"

#define SYSCALL_RET_ENOSYS   0xFFFFFFFFU
#define SYSCALL_RET_EINVAL   0xFFFFFFFFU
#define SYSCALL_RET_EPERM    0xFFFFFFFFU
#define SYSCALL_RET_ENOMEM   0xFFFFFFFFU

#define USER_KERNEL_SPLIT    0xC0000000U

static uint8_t syscall_trace_once = 0U;

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
