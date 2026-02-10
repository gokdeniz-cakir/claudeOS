#include "vbe.h"
#include "paging.h"
#include "serial.h"

#define VBE_BOOT_MAGIC_OFF        0U
#define VBE_BOOT_STATUS_OFF       4U
#define VBE_BOOT_MODE_OFF         8U
#define VBE_BOOT_FB_PHYS_OFF      12U
#define VBE_BOOT_PITCH_OFF        16U
#define VBE_BOOT_WIDTH_OFF        20U
#define VBE_BOOT_HEIGHT_OFF       24U
#define VBE_BOOT_BPP_OFF          28U
#define VBE_BOOT_FB_SIZE_OFF      32U

struct vbe_boot_info_raw {
    uint32_t magic;
    uint32_t status;
    uint32_t mode;
    uint32_t framebuffer_phys;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t framebuffer_size;
} __attribute__((packed));

static struct vbe_mode g_vbe_mode;
static uint8_t g_vbe_active;

static void serial_put_dec(uint32_t value)
{
    char buf[11];
    uint32_t i = 0;

    if (value == 0U) {
        serial_putchar('0');
        return;
    }

    while (value > 0U && i < sizeof(buf)) {
        buf[i] = (char)('0' + (value % 10U));
        value /= 10U;
        i++;
    }

    while (i > 0U) {
        i--;
        serial_putchar(buf[i]);
    }
}

static void serial_put_hex32(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    serial_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        serial_putchar(hex[(value >> (uint32_t)shift) & 0x0FU]);
    }
}

static int vbe_map_framebuffer(uint32_t fb_phys, uint32_t fb_size, uint32_t *fb_virt_out)
{
    uint32_t phys_base;
    uint32_t phys_offset;
    uint32_t map_bytes;
    uint32_t page_count;
    uint32_t mapped_pages = 0U;

    if (fb_virt_out == 0 || fb_size == 0U) {
        return -1;
    }

    if (fb_size > VBE_LFB_VIRT_MAX_BYTES) {
        return -1;
    }

    phys_base = fb_phys & PAGE_FRAME_MASK;
    phys_offset = fb_phys & (PAGE_SIZE - 1U);
    map_bytes = fb_size + phys_offset;

    if (map_bytes < fb_size || map_bytes > VBE_LFB_VIRT_MAX_BYTES) {
        return -1;
    }

    page_count = (map_bytes + PAGE_SIZE - 1U) / PAGE_SIZE;

    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t virt_page = VBE_LFB_VIRT_BASE + (i * PAGE_SIZE);
        uint32_t phys_page = phys_base + (i * PAGE_SIZE);

        if (paging_map_page(virt_page, phys_page, PAGE_WRITABLE) != 0) {
            for (uint32_t j = 0; j < mapped_pages; j++) {
                (void)paging_unmap_page(VBE_LFB_VIRT_BASE + (j * PAGE_SIZE));
            }
            return -1;
        }
        mapped_pages++;
    }

    *fb_virt_out = VBE_LFB_VIRT_BASE + phys_offset;
    return 0;
}

int vbe_init(void)
{
    const volatile struct vbe_boot_info_raw *boot =
        (const volatile struct vbe_boot_info_raw *)(uintptr_t)VBE_BOOT_INFO_ADDR_VIRT;
    uint32_t framebuffer_size;
    uint32_t framebuffer_virt;

    g_vbe_active = 0U;

    if (boot->magic != VBE_BOOT_INFO_MAGIC) {
        serial_puts("[VBE] boot handoff missing\n");
        return -1;
    }

    if (boot->status != VBE_BOOT_STATUS_ACTIVE) {
        serial_puts("[VBE] graphics mode unavailable, using text mode\n");
        return -1;
    }

    if (boot->framebuffer_phys == 0U || boot->pitch == 0U || boot->height == 0U) {
        serial_puts("[VBE] invalid mode metadata from stage2\n");
        return -1;
    }

    framebuffer_size = boot->framebuffer_size;
    if (framebuffer_size == 0U) {
        if (boot->pitch > (0xFFFFFFFFU / boot->height)) {
            serial_puts("[VBE] framebuffer size overflow\n");
            return -1;
        }
        framebuffer_size = boot->pitch * boot->height;
    }

    if (vbe_map_framebuffer(boot->framebuffer_phys, framebuffer_size, &framebuffer_virt) != 0) {
        serial_puts("[VBE] failed to map linear framebuffer\n");
        return -1;
    }

    g_vbe_mode.mode = boot->mode;
    g_vbe_mode.framebuffer_phys = boot->framebuffer_phys;
    g_vbe_mode.framebuffer_virt = framebuffer_virt;
    g_vbe_mode.framebuffer_size = framebuffer_size;
    g_vbe_mode.pitch = boot->pitch;
    g_vbe_mode.width = boot->width;
    g_vbe_mode.height = boot->height;
    g_vbe_mode.bpp = boot->bpp;
    g_vbe_active = 1U;

    serial_puts("[VBE] mode=");
    serial_put_hex32(g_vbe_mode.mode);
    serial_puts(" ");
    serial_put_dec(g_vbe_mode.width);
    serial_putchar('x');
    serial_put_dec(g_vbe_mode.height);
    serial_putchar('x');
    serial_put_dec(g_vbe_mode.bpp);
    serial_puts(" pitch=");
    serial_put_dec(g_vbe_mode.pitch);
    serial_puts(" fb_phys=");
    serial_put_hex32(g_vbe_mode.framebuffer_phys);
    serial_puts(" fb_virt=");
    serial_put_hex32(g_vbe_mode.framebuffer_virt);
    serial_puts("\n");

    return 0;
}

int vbe_is_active(void)
{
    return (g_vbe_active != 0U) ? 1 : 0;
}

int vbe_get_mode(struct vbe_mode *out)
{
    if (out == 0 || g_vbe_active == 0U) {
        return -1;
    }

    *out = g_vbe_mode;
    return 0;
}

void *vbe_get_framebuffer(void)
{
    if (g_vbe_active == 0U) {
        return (void *)0;
    }
    return (void *)(uintptr_t)g_vbe_mode.framebuffer_virt;
}
