#ifndef CLAUDE_USERMODE_H
#define CLAUDE_USERMODE_H

#include <stdint.h>

#define USER_CS_SELECTOR       0x20U
#define USER_DS_SELECTOR       0x28U
#define USER_CS_SELECTOR_R3    (USER_CS_SELECTOR | 0x3U)
#define USER_DS_SELECTOR_R3    (USER_DS_SELECTOR | 0x3U)

/*
 * Prepare and enter a minimal ring-3 probe.
 * On success this function transitions to ring 3 and intentionally triggers
 * a #GP via a privileged instruction (CLI) as proof of user-mode execution.
 * It returns only when setup fails.
 */
void usermode_run_ring3_test(void);

#endif /* CLAUDE_USERMODE_H */
