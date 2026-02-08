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

/* Run cooperative switching until no READY processes remain. */
void process_run_ready(void);

/* Access process metadata. */
const struct process *process_get_current(void);
const struct process *process_get_by_pid(uint32_t pid);
uint32_t process_count(void);

/* Dump active PCB entries to serial debug output. */
void process_dump_table(void);

#endif /* CLAUDE_PROCESS_H */
