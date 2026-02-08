#ifndef CLAUDE_PMM_H
#define CLAUDE_PMM_H

/* ==========================================================================
 * ClaudeOS Physical Memory Manager - Phase 2, Task 10
 * ==========================================================================
 * Bitmap-based physical page frame allocator.
 * Reads the E820 memory map left by the stage2 bootloader at physical 0x0500
 * (virtual 0xC0000500 after paging) to determine which physical frames are
 * usable.
 *
 * Only RAM above 1MB is managed. Everything below 1MB (BIOS, VGA, boot
 * code) is left permanently marked as used. The kernel at virtual
 * 0xC0100000 (physical 0x100000) is also reserved.
 * ========================================================================== */

#include <stdint.h>

#define PMM_PAGE_SIZE       4096
#define PMM_MAX_ADDR        0x40000000  /* Support up to 1GB */
#define PMM_MAX_FRAMES      (PMM_MAX_ADDR / PMM_PAGE_SIZE)

/* E820 memory map location (set by stage2 bootloader at phys 0x0500,
 * accessible at virtual 0xC0000500 via higher-half mapping) */
#define E820_MAP_ADDR       0xC0000500
#define E820_MAX_ENTRIES    32
#define E820_ENTRY_SIZE     24

/* E820 memory region types */
#define E820_TYPE_USABLE    1
#define E820_TYPE_RESERVED  2
#define E820_TYPE_ACPI      3
#define E820_TYPE_NVS       4
#define E820_TYPE_BAD       5

/* E820 entry structure — matches the layout stored by stage2 at phys 0x0504 */
struct e820_entry {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed));

/* Initialize the physical memory manager using E820 data */
void pmm_init(void);

/* Allocate a single 4KB page frame. Returns physical address, or 0 on failure */
uint32_t pmm_alloc_frame(void);

/* Free a previously allocated 4KB page frame */
void pmm_free_frame(uint32_t phys_addr);

/* Get the count of free page frames */
uint32_t pmm_get_free_frame_count(void);

/* Get the total number of frames tracked */
uint32_t pmm_get_total_frame_count(void);

#endif /* CLAUDE_PMM_H */
