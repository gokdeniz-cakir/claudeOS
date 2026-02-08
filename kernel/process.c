#include "process.h"

#include <stddef.h>
#include <stdint.h>

#include "heap.h"
#include "serial.h"

static struct process process_table[PROCESS_MAX_COUNT];
static uint32_t process_next_pid = 1U;
static uint32_t process_current_index = 0U;
static uint32_t process_total = 0U;
static uint32_t process_initialized = 0U;
static int32_t process_zombie_slot = -1;

extern void process_switch(uint32_t *old_esp, uint32_t new_esp);

static uint32_t read_esp(void)
{
    uint32_t value;
    __asm__ volatile ("mov %%esp, %0" : "=r"(value));
    return value;
}

static uint32_t read_ebp(void)
{
    uint32_t value;
    __asm__ volatile ("mov %%ebp, %0" : "=r"(value));
    return value;
}

static uint32_t read_cr3(void)
{
    uint32_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static void copy_name(char *dst, const char *src, uint32_t dst_len)
{
    uint32_t i = 0U;
    const char *fallback = "unnamed";

    if (dst_len == 0U) {
        return;
    }

    if (src == 0) {
        src = fallback;
    }

    while (i + 1U < dst_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void serial_put_u32(uint32_t value)
{
    char buffer[11];
    uint32_t i = 0U;

    if (value == 0U) {
        serial_putchar('0');
        return;
    }

    while (value != 0U && i < (uint32_t)sizeof(buffer)) {
        buffer[i] = (char)('0' + (value % 10U));
        value /= 10U;
        i++;
    }

    while (i > 0U) {
        i--;
        serial_putchar(buffer[i]);
    }
}

static const char *process_state_string(enum process_state state)
{
    switch (state) {
        case PROCESS_STATE_READY:
            return "READY";
        case PROCESS_STATE_RUNNING:
            return "RUNNING";
        case PROCESS_STATE_BLOCKED:
            return "BLOCKED";
        case PROCESS_STATE_TERMINATED:
            return "TERMINATED";
        default:
            return "UNUSED";
    }
}

static int32_t find_slot_by_pid(uint32_t pid)
{
    uint32_t i;

    for (i = 0U; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i].state != PROCESS_STATE_UNUSED &&
            process_table[i].pid == pid) {
            return (int32_t)i;
        }
    }

    return -1;
}

static int32_t find_free_slot(void)
{
    uint32_t i;

    for (i = 0U; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i].state == PROCESS_STATE_UNUSED) {
            return (int32_t)i;
        }
    }

    return -1;
}

static int32_t find_next_ready_slot(uint32_t current_index)
{
    uint32_t step;

    for (step = 1U; step <= PROCESS_MAX_COUNT; step++) {
        uint32_t idx = (current_index + step) % PROCESS_MAX_COUNT;
        if (process_table[idx].state == PROCESS_STATE_READY) {
            return (int32_t)idx;
        }
    }

    return -1;
}

static void release_process_slot(uint32_t index)
{
    struct process *proc;

    if (index >= PROCESS_MAX_COUNT) {
        return;
    }

    proc = &process_table[index];
    if (proc->state == PROCESS_STATE_UNUSED) {
        return;
    }

    if (proc->kernel_stack_base != 0) {
        kfree(proc->kernel_stack_base);
    }

    proc->pid = 0U;
    proc->state = PROCESS_STATE_UNUSED;
    proc->esp = 0U;
    proc->ebp = 0U;
    proc->eip = 0U;
    proc->cr3 = 0U;
    proc->kernel_stack_base = 0;
    proc->kernel_stack_size = 0U;
    proc->entry = 0;
    proc->arg = 0;
    proc->name[0] = '\0';

    if (process_total > 0U) {
        process_total--;
    }
}

static void process_reap_zombie(void)
{
    if (process_zombie_slot < 0) {
        return;
    }

    if ((uint32_t)process_zombie_slot == process_current_index) {
        return;
    }

    release_process_slot((uint32_t)process_zombie_slot);
    process_zombie_slot = -1;
}

static void process_bootstrap(void)
{
    struct process *current;

    process_reap_zombie();

    current = &process_table[process_current_index];
    if (current->entry != 0) {
        current->entry(current->arg);
    }

    current->state = PROCESS_STATE_TERMINATED;

    serial_puts("[PROC] Process pid=");
    serial_put_u32(current->pid);
    serial_puts(" exited\n");

    process_yield();

    /* Should not return here unless no runnable process exists. */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static uint32_t process_has_ready(void)
{
    uint32_t i;

    for (i = 0U; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i].state == PROCESS_STATE_READY) {
            return 1U;
        }
    }

    return 0U;
}

void process_init(void)
{
    uint32_t i;
    struct process *bootstrap;

    for (i = 0U; i < PROCESS_MAX_COUNT; i++) {
        process_table[i].pid = 0U;
        process_table[i].state = PROCESS_STATE_UNUSED;
        process_table[i].esp = 0U;
        process_table[i].ebp = 0U;
        process_table[i].eip = 0U;
        process_table[i].cr3 = 0U;
        process_table[i].kernel_stack_base = 0;
        process_table[i].kernel_stack_size = 0U;
        process_table[i].entry = 0;
        process_table[i].arg = 0;
        process_table[i].name[0] = '\0';
    }

    process_next_pid = 1U;
    process_current_index = 0U;
    process_total = 0U;
    process_zombie_slot = -1;

    bootstrap = &process_table[0];
    bootstrap->pid = process_next_pid++;
    bootstrap->state = PROCESS_STATE_RUNNING;
    bootstrap->esp = read_esp();
    bootstrap->ebp = read_ebp();
    bootstrap->eip = 0U;
    bootstrap->cr3 = read_cr3();
    copy_name(bootstrap->name, "kernel_main", PROCESS_NAME_MAX_LEN);

    process_total = 1U;
    process_initialized = 1U;

    serial_puts("[PROC] Initialized PCB table\n");
}

int32_t process_create_kernel(const char *name, process_entry_t entry, void *arg)
{
    int32_t slot;
    struct process *proc;
    void *stack;
    uint32_t stack_top;
    uint32_t *sp;

    if (process_initialized == 0U || entry == 0) {
        return -1;
    }

    slot = find_free_slot();
    if (slot < 0) {
        serial_puts("[PROC] No free PCB slots\n");
        return -1;
    }

    stack = kmalloc(PROCESS_KERNEL_STACK_SIZE);
    if (stack == 0) {
        serial_puts("[PROC] Failed to allocate kernel stack\n");
        return -1;
    }

    stack_top = (uint32_t)(uintptr_t)stack + PROCESS_KERNEL_STACK_SIZE;
    stack_top &= ~0x0FU;

    /* Initial restore frame for process_switch():
     * pop edi, pop esi, pop ebx, pop ebp, ret -> process_bootstrap */
    sp = (uint32_t *)(uintptr_t)stack_top;
    *--sp = (uint32_t)(uintptr_t)process_bootstrap;  /* ret EIP */
    *--sp = 0U;                                      /* ebp */
    *--sp = 0U;                                      /* ebx */
    *--sp = 0U;                                      /* esi */
    *--sp = 0U;                                      /* edi */

    proc = &process_table[(uint32_t)slot];
    proc->pid = process_next_pid++;
    proc->state = PROCESS_STATE_READY;
    proc->esp = (uint32_t)(uintptr_t)sp;
    proc->ebp = proc->esp;
    proc->eip = (uint32_t)(uintptr_t)process_bootstrap;
    proc->cr3 = read_cr3();
    proc->kernel_stack_base = stack;
    proc->kernel_stack_size = PROCESS_KERNEL_STACK_SIZE;
    proc->entry = entry;
    proc->arg = arg;
    copy_name(proc->name, name, PROCESS_NAME_MAX_LEN);

    process_total++;

    serial_puts("[PROC] Created kernel process pid=");
    serial_put_u32(proc->pid);
    serial_puts(" name=");
    serial_puts(proc->name);
    serial_puts("\n");

    return (int32_t)proc->pid;
}

void process_yield(void)
{
    int32_t next_slot;
    uint32_t current_slot;
    struct process *current;
    struct process *next;

    if (process_initialized == 0U) {
        return;
    }

    process_reap_zombie();

    current_slot = process_current_index;
    current = &process_table[current_slot];
    next_slot = find_next_ready_slot(current_slot);

    if (next_slot < 0) {
        if (current->state == PROCESS_STATE_READY) {
            current->state = PROCESS_STATE_RUNNING;
        }
        return;
    }

    next = &process_table[(uint32_t)next_slot];

    if (current->state == PROCESS_STATE_RUNNING) {
        current->state = PROCESS_STATE_READY;
    }

    if (current->state == PROCESS_STATE_TERMINATED) {
        process_zombie_slot = (int32_t)current_slot;
    }

    next->state = PROCESS_STATE_RUNNING;
    process_current_index = (uint32_t)next_slot;

    process_switch(&current->esp, next->esp);

    process_reap_zombie();
}

void process_run_ready(void)
{
    while (process_has_ready() != 0U) {
        process_yield();
    }

    process_reap_zombie();
}

const struct process *process_get_current(void)
{
    if (process_initialized == 0U) {
        return 0;
    }

    return &process_table[process_current_index];
}

const struct process *process_get_by_pid(uint32_t pid)
{
    int32_t slot;

    if (process_initialized == 0U) {
        return 0;
    }

    slot = find_slot_by_pid(pid);
    if (slot < 0) {
        return 0;
    }

    return &process_table[(uint32_t)slot];
}

uint32_t process_count(void)
{
    return process_total;
}

void process_dump_table(void)
{
    uint32_t i;

    if (process_initialized == 0U) {
        serial_puts("[PROC] process_dump_table before init\n");
        return;
    }

    serial_puts("[PROC] ---- PCB Table ----\n");
    for (i = 0U; i < PROCESS_MAX_COUNT; i++) {
        struct process *proc = &process_table[i];

        if (proc->state == PROCESS_STATE_UNUSED) {
            continue;
        }

        serial_puts("[PROC] pid=");
        serial_put_u32(proc->pid);
        serial_puts(" state=");
        serial_puts(process_state_string(proc->state));
        serial_puts(" name=");
        serial_puts(proc->name);
        serial_puts("\n");
    }
    serial_puts("[PROC] -------------------\n");
}
