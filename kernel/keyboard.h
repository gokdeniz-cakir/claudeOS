#ifndef CLAUDE_KEYBOARD_H
#define CLAUDE_KEYBOARD_H

/* Initialize the PS/2 keyboard driver (IRQ1 + scan code translation). */
void keyboard_init(void);

/* Non-blocking read of translated ASCII keypresses.
 * Returns 1 if a character was read, 0 if the buffer is empty. */
int keyboard_read_char(char *out_char);

#endif /* CLAUDE_KEYBOARD_H */
