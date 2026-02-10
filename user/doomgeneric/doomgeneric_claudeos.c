#include "doomgeneric.h"
#include "doomkeys.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * ClaudeOS Task 42 backend polish:
 * - Uses kernel PIT-backed userspace tick syscall for stable timing.
 * - Uses kernel keyboard event queue syscall for press/release mapping.
 * - Presents each frame through a userspace framebuffer syscall.
 */

static char dg_last_title[64];
static uint8_t dg_present_warned = 0U;

static uint8_t dg_has_iwad_arg(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (argv[i] != 0 && strcmp(argv[i], "-iwad") == 0) {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t dg_file_exists(const char *path)
{
    int fd;

    if (path == 0 || path[0] == '\0') {
        return 0U;
    }

    fd = open(path, O_READ);
    if (fd < 0) {
        return 0U;
    }

    (void)close(fd);
    return 1U;
}

static const char *dg_find_default_iwad(void)
{
    static const char *const candidates[] = {
        "/doom1.wad",
        "/DOOM1.WAD",
        "/doom.wad",
        "/DOOM.WAD",
        "/fat/doom1.wad",
        "/fat/DOOM1.WAD",
        "/fat/doom.wad",
        "/fat/DOOM.WAD",
        "/fat/FREEDOOM1.WAD",
        "/fat/FREEDOOM2.WAD"
    };
    uint32_t i;

    for (i = 0U; i < (sizeof(candidates) / sizeof(candidates[0])); i++) {
        if (dg_file_exists(candidates[i]) != 0U) {
            return candidates[i];
        }
    }

    return 0;
}

static int dg_map_key(uint8_t scancode, uint8_t extended)
{
    if (extended != 0U) {
        switch (scancode) {
            case 0x1C: return KEY_ENTER;      /* keypad Enter */
            case 0x1D: return KEY_FIRE;       /* right Ctrl */
            case 0x35: return '/';            /* keypad Slash */
            case 0x38: return KEY_LALT;       /* right Alt -> alt */
            case 0x48: return KEY_UPARROW;
            case 0x4B: return KEY_LEFTARROW;
            case 0x4D: return KEY_RIGHTARROW;
            case 0x50: return KEY_DOWNARROW;
            default: return -1;
        }
    }

    switch (scancode) {
        case 0x01: return KEY_ESCAPE;
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';
        case 0x0C: return '-';
        case 0x0D: return '=';
        case 0x0E: return KEY_BACKSPACE;
        case 0x0F: return KEY_TAB;
        case 0x10: return 'q';
        case 0x11: return 'w';
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';
        case 0x1A: return '[';
        case 0x1B: return ']';
        case 0x1C: return KEY_ENTER;
        case 0x1D: return KEY_FIRE;          /* left Ctrl */
        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';
        case 0x27: return ';';
        case 0x28: return '\'';
        case 0x29: return '`';
        case 0x2A: return KEY_RSHIFT;
        case 0x2B: return '\\';
        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';
        case 0x33: return ',';
        case 0x34: return '.';
        case 0x35: return '/';
        case 0x36: return KEY_RSHIFT;
        case 0x38: return KEY_LALT;
        case 0x39: return KEY_USE;
        case 0x3A: return KEY_CAPSLOCK;
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        case 0x45: return KEY_NUMLOCK;
        default: return -1;
    }
}

void DG_Init(void)
{
    dg_last_title[0] = '\0';
    dg_present_warned = 0U;
    puts("[DOOM] ClaudeOS backend initialized");
}

void DG_DrawFrame(void)
{
    if (fb_present((const void *)DG_ScreenBuffer,
                   DOOMGENERIC_RESX,
                   DOOMGENERIC_RESY) != 0) {
        if (dg_present_warned == 0U) {
            puts("[DOOM] framebuffer present unavailable");
            dg_present_warned = 1U;
        }
    }
}

void DG_SleepMs(uint32_t ms)
{
    uint32_t deadline;

    if (ms == 0U) {
        return;
    }

    deadline = ticks_ms() + ms;
    while ((int32_t)(deadline - ticks_ms()) > 0) {
        __asm__ volatile ("pause");
    }
}

uint32_t DG_GetTicksMs(void)
{
    return ticks_ms();
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    struct kbd_event event;
    int rc;
    int mapped;

    if (pressed == 0 || doomKey == 0) {
        return 0;
    }

    for (;;) {
        rc = kbd_read_event(&event);
        if (rc <= 0) {
            return 0;
        }

        mapped = dg_map_key(event.scancode, event.extended);
        if (mapped < 0) {
            continue;
        }

        *pressed = (event.pressed != 0U) ? 1 : 0;
        *doomKey = (unsigned char)mapped;
        return 1;
    }
}

void DG_SetWindowTitle(const char *title)
{
    if (title == 0) {
        return;
    }

    if (strncmp(dg_last_title, title, sizeof(dg_last_title) - 1U) == 0) {
        return;
    }

    (void)strncpy(dg_last_title, title, sizeof(dg_last_title) - 1U);
    dg_last_title[sizeof(dg_last_title) - 1U] = '\0';
    printf("[DOOM] title: %s\n", dg_last_title);
}

int main(int argc, char **argv)
{
    const char *default_iwad = 0;
    static char arg0_default[] = "doomgeneric";
    char *launch_argv[32];
    int launch_argc = 0;
    int i;
    uint32_t frame_deadline_ms;
    const uint32_t frame_interval_ms = 1000U / 35U;

    puts("[DOOM] starting doomgeneric");

    if (dg_has_iwad_arg(argc, argv) == 0U) {
        default_iwad = dg_find_default_iwad();
    }

    if (default_iwad == 0 && dg_has_iwad_arg(argc, argv) == 0U) {
        puts("[DOOM] IWAD not found.");
        puts("[DOOM] put DOOM1.WAD in /fat/ and rerun 'doom'.");
        return 1;
    }

    if (launch_argc < 31) {
        if (argc > 0 && argv != 0 && argv[0] != 0) {
            launch_argv[launch_argc++] = argv[0];
        } else {
            launch_argv[launch_argc++] = arg0_default;
        }
    }

    for (i = 1; i < argc && launch_argc < 31; i++) {
        launch_argv[launch_argc++] = argv[i];
    }

    if (default_iwad != 0 && launch_argc < 29) {
        launch_argv[launch_argc++] = "-iwad";
        launch_argv[launch_argc++] = (char *)default_iwad;
        printf("[DOOM] using IWAD: %s\n", default_iwad);
    }

    launch_argv[launch_argc] = 0;
    doomgeneric_Create(launch_argc, launch_argv);

    frame_deadline_ms = DG_GetTicksMs();

    for (;;) {
        uint32_t now;

        doomgeneric_Tick();
        frame_deadline_ms += frame_interval_ms;
        now = DG_GetTicksMs();

        if ((int32_t)(frame_deadline_ms - now) > 0) {
            DG_SleepMs(frame_deadline_ms - now);
        } else if ((now - frame_deadline_ms) > (frame_interval_ms * 4U)) {
            frame_deadline_ms = now + frame_interval_ms;
        }
    }
}
