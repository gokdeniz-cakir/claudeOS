#ifndef CLAUDE_PAGING_H
#define CLAUDE_PAGING_H

#include <stdint.h>

/* x86 4KB paging constants */
#define PAGE_SIZE           4096U
#define PAGE_PRESENT        0x001U
#define PAGE_WRITABLE       0x002U
#define PAGE_USER           0x004U
#define PAGE_FLAGS_MASK     0x0FFFU
#define PAGE_FRAME_MASK     0xFFFFF000U

/*
 * Map one 4KB virtual page to one 4KB physical frame.
 * Both addresses must be page-aligned.
 * Returns 0 on success, -1 on failure.
 */
int paging_map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);

/*
 * Unmap one 4KB virtual page.
 * Returns previous physical frame base, or 0 if not mapped/invalid.
 */
uint32_t paging_unmap_page(uint32_t virt_addr);

/*
 * Translate a virtual address to a physical address.
 * Returns 0 if not mapped.
 */
uint32_t paging_get_phys_addr(uint32_t virt_addr);

#endif /* CLAUDE_PAGING_H */
