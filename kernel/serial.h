#ifndef CLAUDE_SERIAL_H
#define CLAUDE_SERIAL_H

void serial_init(void);
void serial_putchar(char c);
void serial_puts(const char *str);

#endif /* CLAUDE_SERIAL_H */
