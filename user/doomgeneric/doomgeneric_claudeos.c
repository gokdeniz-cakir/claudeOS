#include "doomgeneric.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * ClaudeOS Task 41 backend:
 * - No userspace framebuffer syscall yet, so DG_DrawFrame is a no-op.
 * - No userspace keyboard syscall yet, so DG_GetKey returns no events.
 * - Timing uses a synthetic monotonic counter that advances each poll/sleep.
 */

static uint32_t dg_ticks_ms = 0U;
static uint32_t dg_frame_counter = 0U;
static char dg_last_title[64];

void DG_Init(void)
{
    dg_ticks_ms = 0U;
    dg_frame_counter = 0U;
    dg_last_title[0] = '\0';
    puts("[DOOM] ClaudeOS backend initialized");
}

void DG_DrawFrame(void)
{
    dg_frame_counter++;

    if ((dg_frame_counter % 70U) == 0U) {
        printf("[DOOM] frames=%u\n", (unsigned)dg_frame_counter);
    }
}

void DG_SleepMs(uint32_t ms)
{
    dg_ticks_ms += ms;
}

uint32_t DG_GetTicksMs(void)
{
    dg_ticks_ms += 16U;
    return dg_ticks_ms;
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    (void)pressed;
    (void)doomKey;
    return 0;
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
    puts("[DOOM] starting doomgeneric");
    doomgeneric_Create(argc, argv);

    for (;;) {
        doomgeneric_Tick();
    }
}
