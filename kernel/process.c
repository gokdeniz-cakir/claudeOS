#include "process.h"

#include <stddef.h>
#include <stdint.h>

#include "elf.h"
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include "spinlock.h"
#include "tss.h"

#define PROCESS_USER_HEAP_BASE    0x09000000U
#define PROCESS_USER_HEAP_LIMIT   0x0A000000U
#define PROCESS_PAGE_DIR_ENTRIES  1024U
#define PROCESS_KERNEL_PD_INDEX   768U
#define PROCESS_RECURSIVE_PD_IDX  1023U
#define PROCESS_RECURSIVE_PD_VA   0xFFFFF000U
#define PROCESS_TMP_PD_VA         0xDFFC0000U
#define PROCESS_TMP_PT_VA         0xDFFC1000U

static struct process process_table[PROCESS_MAX_COUNT];
static uint32_t process_next_pid = 1U;
static uint32_t process_current_index = 0U;
static uint32_t process_total = 0U;
static uint32_t process_initialized = 0U;
static uint8_t process_preemption_enabled = 0U;
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

static void write_cr3(uint32_t value)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(value) : "memory");
}

static int process_map_temp_page(uint32_t virt_addr, uint32_t phys_addr)
{
    if (paging_get_phys_addr(virt_addr) != 0U) {
        return -1;
    }

    return paging_map_page(virt_addr, phys_addr, PAGE_WRITABLE);
}

static void process_unmap_temp_page(uint32_t virt_addr)
{
    (void)paging_unmap_page(virt_addr);
}

static int process_create_address_space(uint32_t *cr3_out)
{
    uint32_t pd_phys;
    uint32_t *new_pd;
    uint32_t *cur_pd;
    uint32_t i;

    if (cr3_out == 0) {
        return -1;
    }

    pd_phys = pmm_alloc_frame();
    if (pd_phys == 0U) {
        return -1;
    }

    if (process_map_temp_page(PROCESS_TMP_PD_VA, pd_phys) != 0) {
        pmm_free_frame(pd_phys);
        return -1;
    }

    new_pd = (uint32_t *)(uintptr_t)PROCESS_TMP_PD_VA;
    for (i = 0U; i < PROCESS_PAGE_DIR_ENTRIES; i++) {
        new_pd[i] = 0U;
    }

    cur_pd = (uint32_t *)(uintptr_t)PROCESS_RECURSIVE_PD_VA;
    for (i = PROCESS_KERNEL_PD_INDEX; i < PROCESS_RECURSIVE_PD_IDX; i++) {
        new_pd[i] = cur_pd[i];
    }

    new_pd[PROCESS_RECURSIVE_PD_IDX] = (pd_phys & PAGE_FRAME_MASK)
                                     | PAGE_PRESENT
                                     | PAGE_WRITABLE;

    process_unmap_temp_page(PROCESS_TMP_PD_VA);
    *cr3_out = pd_phys;
    return 0;
}

static void process_destroy_address_space(uint32_t cr3_phys)
{
    uint32_t *pd;
    uint32_t pdi;

    if (cr3_phys == 0U || cr3_phys == read_cr3()) {
        return;
    }

    if (process_map_temp_page(PROCESS_TMP_PD_VA, cr3_phys) != 0) {
        return;
    }

    pd = (uint32_t *)(uintptr_t)PROCESS_TMP_PD_VA;

    for (pdi = 0U; pdi < PROCESS_KERNEL_PD_INDEX; pdi++) {
        uint32_t pde = pd[pdi];
        uint32_t pt_phys;
        uint32_t *pt;
        uint32_t pti;

        if ((pde & PAGE_PRESENT) == 0U) {
            continue;
        }

        pt_phys = pde & PAGE_FRAME_MASK;
        if (process_map_temp_page(PROCESS_TMP_PT_VA, pt_phys) != 0) {
            continue;
        }

        pt = (uint32_t *)(uintptr_t)PROCESS_TMP_PT_VA;
        for (pti = 0U; pti < PROCESS_PAGE_DIR_ENTRIES; pti++) {
            uint32_t pte = pt[pti];

            if ((pte & PAGE_PRESENT) == 0U) {
                continue;
            }

            pmm_free_frame(pte & PAGE_FRAME_MASK);
            pt[pti] = 0U;
        }

        process_unmap_temp_page(PROCESS_TMP_PT_VA);
        pmm_free_frame(pt_phys);
        pd[pdi] = 0U;
    }

    process_unmap_temp_page(PROCESS_TMP_PD_VA);
    pmm_free_frame(cr3_phys);
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

    if (proc->owns_address_space != 0U) {
        elf_forget_address_space(proc->cr3);
        process_destroy_address_space(proc->cr3);
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
    proc->owns_address_space = 0U;
    proc->kernel_stack_base = 0;
    proc->kernel_stack_size = 0U;
    proc->entry = 0;
    proc->arg = 0;
    proc->user_break = PROCESS_USER_HEAP_BASE;
    proc->user_image_path[0] = '\0';
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

    if (process_preemption_enabled != 0U) {
        /* Fresh tasks may be first entered via IRQ-time context switch. */
        __asm__ volatile ("sti");
    }

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

static uint32_t process_kernel_stack_top(const struct process *proc, uint8_t use_live_esp)
{
    uint32_t top;

    if (proc != 0 && proc->kernel_stack_base != 0 && proc->kernel_stack_size != 0U) {
        top = (uint32_t)(uintptr_t)proc->kernel_stack_base + proc->kernel_stack_size;
        top &= ~0x0FU;
        return top;
    }

    if (use_live_esp != 0U) {
        return read_esp();
    }

    if (proc != 0 && proc->esp != 0U) {
        return proc->esp;
    }

    return read_esp();
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
        process_table[i].owns_address_space = 0U;
        process_table[i].kernel_stack_base = 0;
        process_table[i].kernel_stack_size = 0U;
        process_table[i].entry = 0;
        process_table[i].arg = 0;
        process_table[i].user_break = PROCESS_USER_HEAP_BASE;
        process_table[i].user_image_path[0] = '\0';
        process_table[i].name[0] = '\0';
    }

    process_next_pid = 1U;
    process_current_index = 0U;
    process_total = 0U;
    process_preemption_enabled = 0U;
    process_zombie_slot = -1;

    bootstrap = &process_table[0];
    bootstrap->pid = process_next_pid++;
    bootstrap->state = PROCESS_STATE_RUNNING;
    bootstrap->esp = read_esp();
    bootstrap->ebp = read_ebp();
    bootstrap->eip = 0U;
    bootstrap->cr3 = read_cr3();
    bootstrap->owns_address_space = 0U;
    bootstrap->user_break = PROCESS_USER_HEAP_BASE;
    bootstrap->user_image_path[0] = '\0';
    copy_name(bootstrap->name, "kernel_main", PROCESS_NAME_MAX_LEN);

    process_total = 1U;
    process_initialized = 1U;

    serial_puts("[PROC] Initialized PCB table\n");
}

void process_set_preemption(uint8_t enabled)
{
    process_preemption_enabled = (uint8_t)(enabled != 0U);
}

uint8_t process_is_preemption_enabled(void)
{
    return process_preemption_enabled;
}

void process_preempt_from_irq(void)
{
    if (process_preemption_enabled == 0U || process_initialized == 0U) {
        return;
    }

    process_yield();
}

int32_t process_create_kernel(const char *name, process_entry_t entry, void *arg)
{
    int32_t slot;
    struct process *proc;
    void *stack;
    uint32_t process_cr3;
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

    if (process_create_address_space(&process_cr3) != 0) {
        kfree(stack);
        serial_puts("[PROC] Failed to allocate process address space\n");
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
    proc->cr3 = process_cr3;
    proc->owns_address_space = 1U;
    proc->kernel_stack_base = stack;
    proc->kernel_stack_size = PROCESS_KERNEL_STACK_SIZE;
    proc->entry = entry;
    proc->arg = arg;
    proc->user_break = PROCESS_USER_HEAP_BASE;
    proc->user_image_path[0] = '\0';
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
    uint32_t irq_flags;
    struct process *current;
    struct process *next;

    if (process_initialized == 0U) {
        return;
    }

    /* Single-CPU scheduler critical section: prevent IRQ-time reentrancy
     * while mutating global run-state and performing the stack switch. */
    irq_flags = spinlock_irq_save();

    process_reap_zombie();

    current_slot = process_current_index;
    current = &process_table[current_slot];
    current->cr3 = read_cr3();
    next_slot = find_next_ready_slot(current_slot);

    if (next_slot < 0) {
        if (current->state == PROCESS_STATE_READY) {
            current->state = PROCESS_STATE_RUNNING;
        }
        tss_set_kernel_stack(process_kernel_stack_top(current, 1U));
        spinlock_irq_restore(irq_flags);
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
    tss_set_kernel_stack(process_kernel_stack_top(next, 0U));

    if (next->cr3 != current->cr3) {
        write_cr3(next->cr3);
    }

    process_switch(&current->esp, next->esp);

    process_reap_zombie();
    spinlock_irq_restore(irq_flags);
}

void process_run_ready(void)
{
    while (process_has_ready() != 0U) {
        process_yield();
    }

    process_reap_zombie();
}

void process_refresh_tss_stack(void)
{
    uint32_t irq_flags;
    struct process *current;

    if (process_initialized == 0U) {
        return;
    }

    /* Keep TSS esp0 selection coherent with scheduler state updates. */
    irq_flags = spinlock_irq_save();
    current = &process_table[process_current_index];
    tss_set_kernel_stack(process_kernel_stack_top(current, 1U));
    spinlock_irq_restore(irq_flags);
}

uint32_t process_get_current_pid(void)
{
    uint32_t pid = 0U;
    uint32_t irq_flags;

    if (process_initialized == 0U) {
        return 0U;
    }

    irq_flags = spinlock_irq_save();
    pid = process_table[process_current_index].pid;
    spinlock_irq_restore(irq_flags);
    return pid;
}

void process_terminate_current(void)
{
    uint32_t irq_flags;
    struct process *current;

    if (process_initialized == 0U) {
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    irq_flags = spinlock_irq_save();
    current = &process_table[process_current_index];
    current->state = PROCESS_STATE_TERMINATED;
    spinlock_irq_restore(irq_flags);

    process_yield();

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

uint32_t process_user_heap_base(void)
{
    return PROCESS_USER_HEAP_BASE;
}

uint32_t process_user_heap_limit(void)
{
    return PROCESS_USER_HEAP_LIMIT;
}

int process_get_current_user_break(uint32_t *value)
{
    uint32_t irq_flags;

    if (value == 0 || process_initialized == 0U) {
        return -1;
    }

    irq_flags = spinlock_irq_save();
    *value = process_table[process_current_index].user_break;
    spinlock_irq_restore(irq_flags);
    return 0;
}

int process_set_current_user_break(uint32_t value)
{
    uint32_t irq_flags;

    if (process_initialized == 0U) {
        return -1;
    }

    if (value < PROCESS_USER_HEAP_BASE || value > PROCESS_USER_HEAP_LIMIT) {
        return -1;
    }

    irq_flags = spinlock_irq_save();
    process_table[process_current_index].user_break = value;
    spinlock_irq_restore(irq_flags);
    return 0;
}

int process_get_current_image_path(char *path, uint32_t path_len)
{
    uint32_t irq_flags;

    if (path == 0 || path_len == 0U || process_initialized == 0U) {
        return -1;
    }

    irq_flags = spinlock_irq_save();
    copy_name(path, process_table[process_current_index].user_image_path, path_len);
    spinlock_irq_restore(irq_flags);
    return 0;
}

int process_set_current_image_path(const char *path)
{
    uint32_t irq_flags;

    if (path == 0 || process_initialized == 0U) {
        return -1;
    }

    irq_flags = spinlock_irq_save();
    copy_name(process_table[process_current_index].user_image_path, path,
              PROCESS_IMAGE_PATH_MAX);
    spinlock_irq_restore(irq_flags);
    return 0;
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
