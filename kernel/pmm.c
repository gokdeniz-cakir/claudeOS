/* ==========================================================================
 * ClaudeOS Physical Memory Manager - Phase 2, Task 10
 * ==========================================================================
 * Bitmap-based physical page frame allocator.
 *
 * The bitmap uses 1 bit per 4KB frame: bit=0 means free, bit=1 means used.
 * Bit ordering: bit 0 of byte 0 = frame 0, bit 7 of byte 0 = frame 7, etc.
 *
 * Initialization:
 *   1. All frames start as used (bitmap filled with 0xFF).
 *   2. The E820 map (stored at virtual 0xC0000500 by stage2) is scanned.
 *   3. Only Type 1 (usable) regions above 1MB are marked free.
 *   4. Everything below 1MB stays "used" — this implicitly protects
 *      boot structures, BIOS data area, and VGA memory.
 *   5. Kernel physical pages (0x100000 to _kernel_end) are re-marked
 *      as used to prevent allocating over the running kernel.
 * ========================================================================== */

#include "pmm.h"
#include "serial.h"

/* Linker-provided symbol: end of kernel image in virtual address space */
extern uint8_t _kernel_end[];

/* -------------------------------------------------------------------------
 * Bitmap — 32KB in BSS, one bit per 4KB frame, supports up to 1GB RAM
 * ------------------------------------------------------------------------- */
static uint8_t pmm_bitmap[PMM_MAX_FRAMES / 8];

/* Counters */
static uint32_t total_frames;
static uint32_t free_frames;

/* -------------------------------------------------------------------------
 * Bitmap helpers
 *
 * Frame index -> byte index: idx / 8
 * Frame index -> bit mask:   1 << (idx % 8)
 * ------------------------------------------------------------------------- */

/* Mark a frame as used (set bit to 1) */
static inline void pmm_set_frame(uint32_t idx)
{
    pmm_bitmap[idx / 8] |= (uint8_t)(1 << (idx % 8));
}

/* Mark a frame as free (clear bit to 0) */
static inline void pmm_clear_frame(uint32_t idx)
{
    pmm_bitmap[idx / 8] &= (uint8_t)~(1 << (idx % 8));
}

/* Test if a frame is used (bit = 1). Returns non-zero if used. */
static inline int pmm_test_frame(uint32_t idx)
{
    return pmm_bitmap[idx / 8] & (1 << (idx % 8));
}

/* -------------------------------------------------------------------------
 * serial_put_hex32: Print a 32-bit value as "0x" + 8 hex digits to serial
 * ------------------------------------------------------------------------- */
static void serial_put_hex32(uint32_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[11]; /* "0x" + 8 digits + '\0' */
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
    serial_puts(buf);
}

/* -------------------------------------------------------------------------
 * serial_put_dec: Print a 32-bit unsigned value in decimal to serial
 * ------------------------------------------------------------------------- */
static void serial_put_dec(uint32_t val)
{
    char buf[12]; /* max 10 digits + '\0' + safety */
    int i = 0;

    if (val == 0) {
        serial_putchar('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (char)(val % 10);
        val /= 10;
    }

    /* Print in reverse */
    while (i > 0) {
        serial_putchar(buf[--i]);
    }
}

/* -------------------------------------------------------------------------
 * pmm_init: Initialize the physical memory manager
 *
 * Reads the E820 memory map from virtual address 0xC0000500 (physical
 * 0x0500), marks usable frames above 1MB as free in the bitmap, then
 * re-marks kernel physical pages as used, and prints a summary via serial.
 * ------------------------------------------------------------------------- */
void pmm_init(void)
{
    /* Step 1: Mark all frames as used */
    for (uint32_t i = 0; i < sizeof(pmm_bitmap); i++) {
        pmm_bitmap[i] = 0xFF;
    }

    total_frames = PMM_MAX_FRAMES;
    free_frames = 0;

    /* Step 2: Read E820 map from virtual memory (mapped via higher-half) */
    volatile uint32_t *count_ptr = (volatile uint32_t *)E820_MAP_ADDR;
    uint32_t entry_count = *count_ptr;

    serial_puts("PMM: E820 entries: ");
    serial_put_dec(entry_count);
    serial_puts("\n");

    /* Clamp to max entries */
    if (entry_count > E820_MAX_ENTRIES) {
        entry_count = E820_MAX_ENTRIES;
    }

    /* Step 3: Process each E820 entry */
    volatile struct e820_entry *entries =
        (volatile struct e820_entry *)(E820_MAP_ADDR + 4);

    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t base_low  = entries[i].base_low;
        uint32_t base_high = entries[i].base_high;
        uint32_t len_low   = entries[i].length_low;
        uint32_t len_high  = entries[i].length_high;
        uint32_t type      = entries[i].type;

        /* Debug: print each entry */
        serial_puts("  E820 [");
        serial_put_dec(i);
        serial_puts("] base=");
        serial_put_hex32(base_low);
        serial_puts(" len=");
        serial_put_hex32(len_low);
        serial_puts(" type=");
        serial_put_dec(type);
        serial_puts("\n");

        /* Only process Type 1 (usable RAM) entries */
        if (type != E820_TYPE_USABLE) {
            continue;
        }

        /* Skip entries above 4GB (base_high != 0) — we're 32-bit */
        if (base_high != 0) {
            continue;
        }

        /* Compute region end address, clamping to 32-bit range */
        uint64_t base64 = base_low;
        uint64_t len64  = ((uint64_t)len_high << 32) | len_low;
        uint64_t end64  = base64 + len64;

        if (end64 > PMM_MAX_ADDR) {
            end64 = PMM_MAX_ADDR;
        }

        uint32_t region_base = base_low;
        uint32_t region_end  = (uint32_t)end64;

        /* Skip regions entirely below 1MB */
        if (region_end <= 0x100000) {
            continue;
        }

        /* Adjust base up to 1MB if region straddles the 1MB boundary */
        if (region_base < 0x100000) {
            region_base = 0x100000;
        }

        /* Align base up to page boundary */
        region_base = (region_base + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);

        /* Align end down to page boundary */
        region_end = region_end & ~(PMM_PAGE_SIZE - 1);

        /* Skip if alignment consumed the entire region */
        if (region_base >= region_end) {
            continue;
        }

        /* Mark frames in this region as free */
        uint32_t first_frame = region_base / PMM_PAGE_SIZE;
        uint32_t last_frame  = region_end / PMM_PAGE_SIZE;

        for (uint32_t frame = first_frame; frame < last_frame; frame++) {
            if (frame < PMM_MAX_FRAMES) {
                pmm_clear_frame(frame);
                free_frames++;
            }
        }

        serial_puts("  -> marked free: ");
        serial_put_hex32(region_base);
        serial_puts(" - ");
        serial_put_hex32(region_end);
        serial_puts(" (");
        serial_put_dec(last_frame - first_frame);
        serial_puts(" frames)\n");
    }

    /* Step 4: Reserve kernel physical pages (0x100000 to _kernel_end)
     * _kernel_end is a virtual address; subtract KERNEL_VIRT_BASE to get
     * the physical end address. */
    uint32_t kernel_phys_end = (uint32_t)_kernel_end - 0xC0000000;
    /* Align up to page boundary */
    kernel_phys_end = (kernel_phys_end + PMM_PAGE_SIZE - 1)
                      & ~(PMM_PAGE_SIZE - 1);

    uint32_t kernel_first_frame = 0x100000 / PMM_PAGE_SIZE;
    uint32_t kernel_last_frame  = kernel_phys_end / PMM_PAGE_SIZE;

    for (uint32_t frame = kernel_first_frame; frame < kernel_last_frame; frame++) {
        if (frame < PMM_MAX_FRAMES && !pmm_test_frame(frame)) {
            pmm_set_frame(frame);
            free_frames--;
        }
    }

    serial_puts("PMM: kernel reserved: ");
    serial_put_hex32(0x100000);
    serial_puts(" - ");
    serial_put_hex32(kernel_phys_end);
    serial_puts(" (");
    serial_put_dec(kernel_last_frame - kernel_first_frame);
    serial_puts(" frames)\n");

    /* Print summary */
    serial_puts("PMM: ");
    serial_put_dec(free_frames);
    serial_puts(" free frames (");
    serial_put_dec(free_frames * 4);
    serial_puts(" KB)\n");
}

/* -------------------------------------------------------------------------
 * pmm_alloc_frame: Allocate a single 4KB page frame
 *
 * Performs a linear scan of the bitmap for the first free frame (bit=0),
 * marks it as used, and returns its physical address.
 *
 * Returns 0 on failure (no free frames).
 * ------------------------------------------------------------------------- */
uint32_t pmm_alloc_frame(void)
{
    if (free_frames == 0) {
        return 0;
    }

    /* Scan bitmap bytes — skip fully-used bytes (0xFF) for speed */
    uint32_t bitmap_bytes = PMM_MAX_FRAMES / 8;
    for (uint32_t byte_idx = 0; byte_idx < bitmap_bytes; byte_idx++) {
        if (pmm_bitmap[byte_idx] == 0xFF) {
            continue; /* All 8 frames in this byte are used */
        }

        /* At least one free bit in this byte — find it */
        for (uint32_t bit = 0; bit < 8; bit++) {
            uint32_t frame_idx = byte_idx * 8 + bit;
            if (frame_idx >= PMM_MAX_FRAMES) {
                return 0;
            }
            if (!pmm_test_frame(frame_idx)) {
                pmm_set_frame(frame_idx);
                free_frames--;
                return frame_idx * PMM_PAGE_SIZE;
            }
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * pmm_free_frame: Free a previously allocated 4KB page frame
 *
 * Validates the address (must be page-aligned and within range) before
 * marking the frame as free.
 * ------------------------------------------------------------------------- */
void pmm_free_frame(uint32_t phys_addr)
{
    /* Never free anything below 1MB (BIOS, VGA, boot code, kernel) */
    if (phys_addr < 0x100000) {
        return;
    }

    /* Validate alignment */
    if (phys_addr & (PMM_PAGE_SIZE - 1)) {
        return;
    }

    /* Validate range */
    if (phys_addr >= PMM_MAX_ADDR) {
        return;
    }

    uint32_t frame_idx = phys_addr / PMM_PAGE_SIZE;

    /* Only free if currently used (prevent double-free) */
    if (pmm_test_frame(frame_idx)) {
        pmm_clear_frame(frame_idx);
        free_frames++;
    }
}

/* -------------------------------------------------------------------------
 * pmm_get_free_frame_count: Return the number of free page frames
 * ------------------------------------------------------------------------- */
uint32_t pmm_get_free_frame_count(void)
{
    return free_frames;
}

/* -------------------------------------------------------------------------
 * pmm_get_total_frame_count: Return the total number of tracked frames
 * ------------------------------------------------------------------------- */
uint32_t pmm_get_total_frame_count(void)
{
    return total_frames;
}
