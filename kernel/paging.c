#include "paging.h"
#include "pmm.h"

/* Recursive paging layout (set by kernel_entry.asm) */
#define RECURSIVE_PD_VADDR  0xFFFFF000U
#define RECURSIVE_PT_VADDR  0xFFC00000U
#define PAGE_TABLE_ENTRIES  1024U

static inline uint32_t *paging_page_directory(void)
{
    return (uint32_t *)RECURSIVE_PD_VADDR;
}

static inline uint32_t *paging_page_table(uint32_t pd_index)
{
    return (uint32_t *)(RECURSIVE_PT_VADDR + (pd_index * PAGE_SIZE));
}

static inline void paging_flush_tlb_single(uint32_t virt_addr)
{
    __asm__ volatile ("invlpg (%0)" : : "r" ((void *)(uintptr_t)virt_addr) : "memory");
}

static inline void paging_flush_tlb_all(void)
{
    uint32_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

int paging_map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags)
{
    if ((virt_addr & (PAGE_SIZE - 1U)) != 0U || (phys_addr & (PAGE_SIZE - 1U)) != 0U) {
        return -1;
    }

    uint32_t pd_index = virt_addr >> 22;
    uint32_t pt_index = (virt_addr >> 12) & 0x3FFU;
    uint32_t *pd = paging_page_directory();
    uint32_t required_pde_flags = PAGE_PRESENT | PAGE_WRITABLE;

    if ((flags & PAGE_USER) != 0U) {
        required_pde_flags |= PAGE_USER;
    }

    if ((pd[pd_index] & PAGE_PRESENT) == 0U) {
        uint32_t pt_phys = pmm_alloc_frame();
        if (pt_phys == 0U) {
            return -1;
        }

        pd[pd_index] = (pt_phys & PAGE_FRAME_MASK) | required_pde_flags;

        /*
         * PDE changes affect 4MB. A full TLB flush is the safe option before
         * touching the new page-table virtual window.
         */
        paging_flush_tlb_all();

        uint32_t *new_pt = paging_page_table(pd_index);
        for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            new_pt[i] = 0U;
        }
    } else if ((pd[pd_index] & required_pde_flags) != required_pde_flags) {
        /* Upgrade existing PDE permissions if caller needs broader access. */
        pd[pd_index] |= required_pde_flags;
        paging_flush_tlb_all();
    }

    uint32_t *pt = paging_page_table(pd_index);

    if ((pt[pt_index] & PAGE_PRESENT) != 0U) {
        return -1;
    }

    pt[pt_index] = (phys_addr & PAGE_FRAME_MASK)
                 | (flags & PAGE_FLAGS_MASK)
                 | PAGE_PRESENT;

    paging_flush_tlb_single(virt_addr);
    return 0;
}

uint32_t paging_unmap_page(uint32_t virt_addr)
{
    if ((virt_addr & (PAGE_SIZE - 1U)) != 0U) {
        return 0U;
    }

    uint32_t pd_index = virt_addr >> 22;
    uint32_t pt_index = (virt_addr >> 12) & 0x3FFU;
    uint32_t *pd = paging_page_directory();

    if ((pd[pd_index] & PAGE_PRESENT) == 0U) {
        return 0U;
    }

    uint32_t *pt = paging_page_table(pd_index);
    uint32_t entry = pt[pt_index];

    if ((entry & PAGE_PRESENT) == 0U) {
        return 0U;
    }

    pt[pt_index] = 0U;
    paging_flush_tlb_single(virt_addr);

    return entry & PAGE_FRAME_MASK;
}

uint32_t paging_get_phys_addr(uint32_t virt_addr)
{
    uint32_t pd_index = virt_addr >> 22;
    uint32_t pt_index = (virt_addr >> 12) & 0x3FFU;
    uint32_t offset = virt_addr & 0xFFFU;
    uint32_t *pd = paging_page_directory();

    if ((pd[pd_index] & PAGE_PRESENT) == 0U) {
        return 0U;
    }

    uint32_t *pt = paging_page_table(pd_index);
    uint32_t entry = pt[pt_index];
    if ((entry & PAGE_PRESENT) == 0U) {
        return 0U;
    }

    return (entry & PAGE_FRAME_MASK) + offset;
}

int paging_or_page_flags(uint32_t virt_addr, uint32_t flags)
{
    uint32_t pd_index;
    uint32_t pt_index;
    uint32_t *pd;
    uint32_t required_pde_flags = PAGE_PRESENT | PAGE_WRITABLE;
    uint32_t *pt;
    uint32_t entry;
    uint32_t new_entry;
    uint32_t upgrade_flags = flags & (PAGE_WRITABLE | PAGE_USER);

    if ((virt_addr & (PAGE_SIZE - 1U)) != 0U) {
        return -1;
    }

    pd_index = virt_addr >> 22;
    pt_index = (virt_addr >> 12) & 0x3FFU;
    pd = paging_page_directory();

    if ((upgrade_flags & PAGE_USER) != 0U) {
        required_pde_flags |= PAGE_USER;
    }

    if ((pd[pd_index] & PAGE_PRESENT) == 0U) {
        return -1;
    }

    if ((pd[pd_index] & required_pde_flags) != required_pde_flags) {
        pd[pd_index] |= required_pde_flags;
        paging_flush_tlb_all();
    }

    pt = paging_page_table(pd_index);
    entry = pt[pt_index];
    if ((entry & PAGE_PRESENT) == 0U) {
        return -1;
    }

    new_entry = entry | upgrade_flags | PAGE_PRESENT;
    if (new_entry != entry) {
        pt[pt_index] = new_entry;
        paging_flush_tlb_single(virt_addr);
    }

    return 0;
}
