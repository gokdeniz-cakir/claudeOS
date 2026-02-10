#ifndef CLAUDE_KEYBOARD_H
#define CLAUDE_KEYBOARD_H

#include <stdint.h>

struct keyboard_event {
    uint8_t scancode;
    uint8_t pressed;
    uint8_t extended;
};

/* Initialize the PS/2 keyboard driver (IRQ1 + scan code translation). */
void keyboard_init(void);

/* Non-blocking read of translated ASCII keypresses.
 * Returns 1 if a character was read, 0 if the buffer is empty. */
int keyboard_read_char(char *out_char);

/* Non-blocking read of raw set1 keyboard events.
 * Returns 1 if an event was read, 0 if the queue is empty. */
int keyboard_read_event(struct keyboard_event *out_event);

#endif /* CLAUDE_KEYBOARD_H */
