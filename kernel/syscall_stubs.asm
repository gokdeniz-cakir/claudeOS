; =============================================================================
; ClaudeOS syscall stubs - Phase 5, Task 22
; =============================================================================
; INT 0x80 entry point callable from ring 3.
; Builds an isr_regs-compatible frame and dispatches to syscall_handler().
; =============================================================================

[bits 32]

section .text

extern syscall_handler

global syscall_int80
syscall_int80:
    ; Match isr_regs layout: int_no + err_code below saved registers.
    push dword 0
    push dword 0x80

    pushad

    ; Push segment registers in same order as ISR/IRQ common stubs.
    push ds
    push es
    push fs
    push gs

    ; Switch to kernel data segments for C handler.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld

    push esp
    call syscall_handler
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds

    popad

    add esp, 8
    iret
