#ifndef CLAUDE_PROCESS_H
#define CLAUDE_PROCESS_H

#include <stdint.h>

#define PROCESS_MAX_COUNT           16U
#define PROCESS_NAME_MAX_LEN        24U
#define PROCESS_KERNEL_STACK_SIZE   4096U

enum process_state {
    PROCESS_STATE_UNUSED = 0,
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_TERMINATED
};

typedef void (*process_entry_t)(void *arg);

struct process {
    uint32_t pid;
    enum process_state state;
    uint32_t esp;
    uint32_t ebp;
    uint32_t eip;
    uint32_t cr3;
    void *kernel_stack_base;
    uint32_t kernel_stack_size;
    process_entry_t entry;
    void *arg;
    uint32_t user_break;
    char name[PROCESS_NAME_MAX_LEN];
};

/* Initialize PCB table and register the bootstrap kernel process. */
void process_init(void);

/* Create a kernel-mode process in READY state.
 * Returns PID (>0) on success, -1 on failure. */
int32_t process_create_kernel(const char *name, process_entry_t entry, void *arg);

/* Cooperative context switch to next READY process.
 * No-op if no other READY process exists. */
void process_yield(void);

/* Enable/disable PIT-driven preemptive scheduling. */
void process_set_preemption(uint8_t enabled);

/* Return 1 when preemptive scheduling is enabled, else 0. */
uint8_t process_is_preemption_enabled(void);

/* Called from IRQ0 path to preempt current process on quantum expiry. */
void process_preempt_from_irq(void);

/* Run cooperative switching until no READY processes remain. */
void process_run_ready(void);

/* Refresh TSS esp0 to match the currently running process kernel stack. */
void process_refresh_tss_stack(void);

/* Process lifecycle helpers used by syscall path. */
uint32_t process_get_current_pid(void);
void process_terminate_current(void) __attribute__((noreturn));

/* Per-process user heap break helpers for sbrk. */
uint32_t process_user_heap_base(void);
uint32_t process_user_heap_limit(void);
int process_get_current_user_break(uint32_t *value);
int process_set_current_user_break(uint32_t value);

/* Access process metadata. */
const struct process *process_get_current(void);
const struct process *process_get_by_pid(uint32_t pid);
uint32_t process_count(void);

/* Dump active PCB entries to serial debug output. */
void process_dump_table(void);

#endif /* CLAUDE_PROCESS_H */
