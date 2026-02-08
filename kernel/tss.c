#include "tss.h"

#include <stdint.h>

#define KERNEL_DS_SELECTOR       0x10U
#define TSS_DESCRIPTOR_ACCESS    0x89U
#define TSS_KERNEL_STACK_SIZE    4096U

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdt_system_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

extern struct gdt_system_descriptor kernel_gdt_tss_descriptor;

static struct tss_entry kernel_tss;
static uint8_t tss_kernel_stack[TSS_KERNEL_STACK_SIZE] __attribute__((aligned(16)));

static void clear_tss(void)
{
    uint32_t i;
    uint8_t *bytes = (uint8_t *)&kernel_tss;

    for (i = 0U; i < (uint32_t)sizeof(kernel_tss); i++) {
        bytes[i] = 0U;
    }
}

static void write_tss_descriptor(uint32_t base, uint32_t limit)
{
    kernel_gdt_tss_descriptor.limit_low = (uint16_t)(limit & 0xFFFFU);
    kernel_gdt_tss_descriptor.base_low = (uint16_t)(base & 0xFFFFU);
    kernel_gdt_tss_descriptor.base_mid = (uint8_t)((base >> 16) & 0xFFU);
    kernel_gdt_tss_descriptor.access = TSS_DESCRIPTOR_ACCESS;
    kernel_gdt_tss_descriptor.granularity = (uint8_t)((limit >> 16) & 0x0FU);
    kernel_gdt_tss_descriptor.base_high = (uint8_t)((base >> 24) & 0xFFU);
}

void tss_set_kernel_stack(uint32_t stack_top)
{
    kernel_tss.esp0 = stack_top;
}

void tss_init(void)
{
    uint32_t tss_base = (uint32_t)(uintptr_t)&kernel_tss;
    uint32_t tss_limit = (uint32_t)sizeof(kernel_tss) - 1U;
    uint32_t stack_top = (uint32_t)(uintptr_t)&tss_kernel_stack[TSS_KERNEL_STACK_SIZE];

    clear_tss();
    kernel_tss.ss0 = KERNEL_DS_SELECTOR;
    kernel_tss.esp0 = stack_top;
    kernel_tss.iomap_base = (uint16_t)sizeof(kernel_tss);

    write_tss_descriptor(tss_base, tss_limit);

    __asm__ volatile ("ltr %%ax" : : "a"((uint16_t)TSS_SELECTOR) : "memory");
}
