#ifndef CLAUDE_CONSOLE_H
#define CLAUDE_CONSOLE_H

/* Initialize the basic kernel console and print the initial prompt. */
void console_init(void);

/* Handle one translated keyboard character for console input/output. */
void console_handle_char(char c);

#endif /* CLAUDE_CONSOLE_H */
