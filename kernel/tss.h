#ifndef CLAUDE_TSS_H
#define CLAUDE_TSS_H

#include <stdint.h>

#define TSS_SELECTOR 0x18U

/* Initialize the per-CPU protected-mode TSS and load TR. */
void tss_init(void);

/* Update kernel stack pointer used on ring transitions (esp0). */
void tss_set_kernel_stack(uint32_t stack_top);

#endif /* CLAUDE_TSS_H */
