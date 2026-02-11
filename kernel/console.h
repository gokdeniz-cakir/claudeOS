#ifndef CLAUDE_CONSOLE_H
#define CLAUDE_CONSOLE_H

#include <stdint.h>

typedef void (*console_output_hook_fn)(const char *text, uint32_t len, void *ctx);

/* Initialize the basic kernel console and print the initial prompt. */
void console_init(void);

/* Handle one translated keyboard character for console input/output. */
void console_handle_char(char c);

/* Reprint console prompt (used after temporary UI mode switches). */
void console_show_prompt(void);

/* Execute one console command line directly (without interactive prompt state). */
void console_execute_command(const char *command);

/* Register/unregister an output hook that receives console text output. */
void console_set_output_hook(console_output_hook_fn hook, void *ctx);

/* Mirror external text output into the active console hook (if any). */
void console_mirror_output(const char *text, uint32_t len);

#endif /* CLAUDE_CONSOLE_H */
