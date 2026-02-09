#include "usermode.h"

#include <stdint.h>
#include <stddef.h>

#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "serial.h"
#include "vga.h"

#define USER_TEST_CODE_VADDR   0x08000000U
#define USER_TEST_STACK_VADDR  0x08001000U
#define USER_TEST_STACK_TOP    (USER_TEST_STACK_VADDR + PAGE_SIZE)
#define USER_TEST_STR_VADDR    (USER_TEST_CODE_VADDR + 0x100U)
#define USER_TEST_STR_LEN      15U

/*
 * Ring-3 probe payload:
 *   write(1, "ring3 write ok\n", 15)
 *   sbrk(+4096), sbrk(-4096)
 *   exit(0)      ; expected to return -1 for bootstrap task
 *   cli          ; privileged in ring 3 => #GP (proof of ring3 context)
 *   jmp $
 */
static const uint8_t user_test_code[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,             /* mov eax, 1   ; write */
    0xBB, 0x01, 0x00, 0x00, 0x00,             /* mov ebx, 1   ; fd */
    0xB9, 0x00, 0x01, 0x00, 0x08,             /* mov ecx, USER_TEST_STR_VADDR */
    0xBA, USER_TEST_STR_LEN, 0x00, 0x00, 0x00,/* mov edx, len */
    0xCD, 0x80,                                 /* int 0x80 */
    0xB8, 0x03, 0x00, 0x00, 0x00,             /* mov eax, 3   ; sbrk */
    0xBB, 0x00, 0x10, 0x00, 0x00,             /* mov ebx, +4096 */
    0xCD, 0x80,                                 /* int 0x80 */
    0x89, 0xC6,                                 /* mov esi, eax */
    0xB8, 0x03, 0x00, 0x00, 0x00,             /* mov eax, 3   ; sbrk */
    0xBB, 0x00, 0xF0, 0xFF, 0xFF,             /* mov ebx, -4096 */
    0xCD, 0x80,                                 /* int 0x80 */
    0x89, 0xC7,                                 /* mov edi, eax */
    0xB8, 0x02, 0x00, 0x00, 0x00,             /* mov eax, 2   ; exit */
    0x31, 0xDB,                                 /* xor ebx, ebx ; status 0 */
    0xCD, 0x80,                                 /* int 0x80 */
    0x89, 0xC3,                                 /* mov ebx, eax ; capture return */
    0xFA,                                       /* cli */
    0xEB, 0xFE  /* jmp $ */
};

static const uint8_t user_test_message[] = "ring3 write ok\n";

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t len)
{
    uint32_t i;

    for (i = 0U; i < len; i++) {
        dst[i] = src[i];
    }
}

static int usermode_prepare_ring3_test(void)
{
    uint8_t *code_page;
    uint8_t *stack_page;
    uint32_t code_phys;
    uint32_t stack_phys;
    uint32_t i;

    code_phys = pmm_alloc_frame();
    if (code_phys == 0U) {
        return -1;
    }

    stack_phys = pmm_alloc_frame();
    if (stack_phys == 0U) {
        pmm_free_frame(code_phys);
        return -1;
    }

    if (paging_map_page(USER_TEST_CODE_VADDR, code_phys,
                        PAGE_USER | PAGE_WRITABLE) != 0) {
        pmm_free_frame(stack_phys);
        pmm_free_frame(code_phys);
        return -1;
    }

    if (paging_map_page(USER_TEST_STACK_VADDR, stack_phys,
                        PAGE_USER | PAGE_WRITABLE) != 0) {
        paging_unmap_page(USER_TEST_CODE_VADDR);
        pmm_free_frame(stack_phys);
        pmm_free_frame(code_phys);
        return -1;
    }

    code_page = (uint8_t *)(uintptr_t)USER_TEST_CODE_VADDR;
    stack_page = (uint8_t *)(uintptr_t)USER_TEST_STACK_VADDR;

    for (i = 0U; i < PAGE_SIZE; i++) {
        code_page[i] = 0x90U;
        stack_page[i] = 0x00U;
    }

    copy_bytes(code_page, user_test_code, (uint32_t)sizeof(user_test_code));
    copy_bytes(code_page + 0x100U, user_test_message, USER_TEST_STR_LEN);
    return 0;
}

__attribute__((noreturn)) static void usermode_iret_enter(uint32_t entry_eip,
                                                           uint32_t user_esp)
{
    __asm__ volatile (
        "movw %w[user_ds], %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "pushl %[user_ds]\n\t"
        "pushl %[user_esp]\n\t"
        "pushfl\n\t"
        "popl %%eax\n\t"
        "orl $0x200, %%eax\n\t"
        "pushl %%eax\n\t"
        "pushl %[user_cs]\n\t"
        "pushl %[entry]\n\t"
        "iret\n\t"
        :
        : [user_ds] "r"((uint32_t)USER_DS_SELECTOR_R3),
          [user_esp] "r"(user_esp),
          [user_cs] "r"((uint32_t)USER_CS_SELECTOR_R3),
          [entry] "r"(entry_eip)
        : "eax", "memory");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void usermode_run_ring3_test(void)
{
    if (usermode_prepare_ring3_test() != 0) {
        vga_puts("[USER] ring3 test setup failed.\n");
        serial_puts("[USER] ring3 test setup failed\n");
        return;
    }

    process_refresh_tss_stack();

    vga_puts("[USER] entering ring3 test (write/sbrk/exit + expected #GP).\n");
    serial_puts("[USER] entering ring3 test (write/sbrk/exit + expected #GP)\n");

    usermode_iret_enter(USER_TEST_CODE_VADDR, USER_TEST_STACK_TOP);
}
