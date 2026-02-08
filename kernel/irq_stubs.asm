; =============================================================================
; ClaudeOS IRQ Entry Stubs - Phase 2, Task 7
; =============================================================================
; 16 IRQ entry points (IRQ 0-15, mapped to INT 32-47).
; Each stub pushes a dummy error code (0) and the interrupt number,
; then jumps to irq_common which builds the same register frame
; as the ISR common stub (struct isr_regs) before calling irq_handler().
; =============================================================================

[bits 32]

section .text

extern irq_handler

; ---------------------------------------------------------------------------
; Macro: define an IRQ stub
;   %1 = IRQ number (0-15)
;   %2 = Interrupt number (32-47)
; ---------------------------------------------------------------------------
%macro IRQ_STUB 2
global irq%1
irq%1:
    push dword 0        ; Dummy error code (IRQs have no CPU error code)
    push dword %2       ; Interrupt number
    jmp irq_common
%endmacro

; ---------------------------------------------------------------------------
; Generate 16 IRQ stubs: irq0 through irq15
; ---------------------------------------------------------------------------
IRQ_STUB  0, 32
IRQ_STUB  1, 33
IRQ_STUB  2, 34
IRQ_STUB  3, 35
IRQ_STUB  4, 36
IRQ_STUB  5, 37
IRQ_STUB  6, 38
IRQ_STUB  7, 39
IRQ_STUB  8, 40
IRQ_STUB  9, 41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

; ---------------------------------------------------------------------------
; irq_common: build struct isr_regs frame, call C handler, restore and iret
; ---------------------------------------------------------------------------
; Stack at entry (low address first):
;   esp+0:  int_no     (pushed by stub last)
;   esp+4:  err_code   (pushed by stub first, dummy 0)
;   esp+8:  eip        (pushed by CPU)
;   esp+12: cs
;   esp+16: eflags
; This matches the int_no / err_code layout in struct isr_regs.
; ---------------------------------------------------------------------------
irq_common:
    ; Save general-purpose registers
    pushad

    ; Save segment registers (must match isr_common_stub order for struct isr_regs)
    push ds
    push es
    push fs
    push gs

    ; Load kernel data segment (0x10) into all data segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Clear direction flag per SysV ABI requirement
    cld

    ; Pass pointer to the register frame (struct isr_regs *) to C handler
    push esp
    call irq_handler
    add esp, 4          ; Remove the pushed esp argument

    ; Restore segment registers
    pop gs
    pop fs
    pop es
    pop ds

    ; Restore general-purpose registers
    popad

    ; Skip int_no and err_code on the stack
    add esp, 8

    ; Return from interrupt
    iret
