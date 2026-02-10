#ifndef CLAUDE_MOUSE_H
#define CLAUDE_MOUSE_H

#include <stdint.h>

struct mouse_event {
    int32_t dx;
    int32_t dy;
    int32_t x;
    int32_t y;
    uint8_t buttons;
};

/* Initialize PS/2 mouse support (IRQ12 + packet streaming). */
void mouse_init(void);

/* Non-blocking read of a decoded mouse packet event.
 * Returns 1 when an event is available, 0 otherwise. */
int mouse_read_event(struct mouse_event *out_event);

/* Snapshot current mouse state.
 * Returns 1 when the driver is initialized, 0 otherwise. */
int mouse_get_state(struct mouse_event *out_state);

/* Returns 1 when IRQ12 packet handling is active, 0 otherwise. */
int mouse_is_initialized(void);

#endif /* CLAUDE_MOUSE_H */
