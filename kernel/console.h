#ifndef CLAUDE_CONSOLE_H
#define CLAUDE_CONSOLE_H

/* Initialize the basic kernel console and print the initial prompt. */
void console_init(void);

/* Handle one translated keyboard character for console input/output. */
void console_handle_char(char c);

/* Reprint console prompt (used after temporary UI mode switches). */
void console_show_prompt(void);

#endif /* CLAUDE_CONSOLE_H */
