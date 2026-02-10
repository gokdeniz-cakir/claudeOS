#ifndef CLAUDE_VBE_H
#define CLAUDE_VBE_H

#include <stdint.h>

/* Stage2 handoff area mapped by higher-half bootstrap paging. */
#define VBE_BOOT_INFO_ADDR_VIRT  0xC0001000U
#define VBE_BOOT_INFO_MAGIC      0x30454256U  /* 'VBE0' */
#define VBE_BOOT_STATUS_ACTIVE   1U

/* Kernel virtual window reserved for framebuffer MMIO mapping. */
#define VBE_LFB_VIRT_BASE        0xD0000000U
#define VBE_LFB_VIRT_MAX_BYTES   0x01000000U  /* 16 MiB */

struct vbe_mode {
    uint32_t mode;
    uint32_t framebuffer_phys;
    uint32_t framebuffer_virt;
    uint32_t framebuffer_size;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

/*
 * Initialize VBE runtime state from stage2 handoff data and map the linear
 * framebuffer into kernel virtual memory.
 * Returns 0 on success, -1 if VBE is unavailable or mapping failed.
 */
int vbe_init(void);

/* Returns 1 if a VBE linear framebuffer is active and mapped, 0 otherwise. */
int vbe_is_active(void);

/* Returns 0 and copies mode metadata on success, -1 if VBE is inactive. */
int vbe_get_mode(struct vbe_mode *out);

/* Returns mapped framebuffer pointer (or NULL if inactive). */
void *vbe_get_framebuffer(void);

#endif /* CLAUDE_VBE_H */
