; =============================================================================
; ClaudeOS ISR Stubs - Phase 2, Task 6
; =============================================================================
; 32 ISR entry points for CPU exceptions (vectors 0-31).
; Each stub pushes a dummy error code (if the CPU doesn't push one),
; then pushes the interrupt number, and jumps to a common handler
; that saves all registers and calls the C-level isr_handler().
; =============================================================================

[bits 32]

section .text

; Import the C handler
extern isr_handler

; ---------------------------------------------------------------------------
; Macro: ISR stub that pushes a dummy error code (0)
; Used for exceptions that do NOT push an error code on the stack.
; ---------------------------------------------------------------------------
%macro ISR_NO_ERRCODE 1
global isr%1
isr%1:
    push dword 0            ; Dummy error code
    push dword %1           ; Interrupt number
    jmp isr_common_stub
%endmacro

; ---------------------------------------------------------------------------
; Macro: ISR stub for exceptions that DO push an error code
; The CPU already pushed the error code, so we only push the int number.
; ---------------------------------------------------------------------------
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push dword %1           ; Interrupt number (error code already on stack)
    jmp isr_common_stub
%endmacro

; ---------------------------------------------------------------------------
; Exception stubs 0-31
; Error-code exceptions: 8, 10-14, 17, 21, 29, 30
; All others push a dummy 0.
; ---------------------------------------------------------------------------
ISR_NO_ERRCODE 0            ; #DE  Division Error
ISR_NO_ERRCODE 1            ; #DB  Debug
ISR_NO_ERRCODE 2            ;      Non-Maskable Interrupt
ISR_NO_ERRCODE 3            ; #BP  Breakpoint
ISR_NO_ERRCODE 4            ; #OF  Overflow
ISR_NO_ERRCODE 5            ; #BR  Bound Range Exceeded
ISR_NO_ERRCODE 6            ; #UD  Invalid Opcode
ISR_NO_ERRCODE 7            ; #NM  Device Not Available
ISR_ERRCODE    8            ; #DF  Double Fault
ISR_NO_ERRCODE 9            ;      Coprocessor Segment Overrun
ISR_ERRCODE    10           ; #TS  Invalid TSS
ISR_ERRCODE    11           ; #NP  Segment Not Present
ISR_ERRCODE    12           ; #SS  Stack-Segment Fault
ISR_ERRCODE    13           ; #GP  General Protection Fault
ISR_ERRCODE    14           ; #PF  Page Fault
ISR_NO_ERRCODE 15           ;      Reserved
ISR_NO_ERRCODE 16           ; #MF  x87 Floating-Point Exception
ISR_ERRCODE    17           ; #AC  Alignment Check
ISR_NO_ERRCODE 18           ; #MC  Machine Check
ISR_NO_ERRCODE 19           ; #XM  SIMD Floating-Point Exception
ISR_NO_ERRCODE 20           ; #VE  Virtualization Exception
ISR_ERRCODE    21           ; #CP  Control Protection Exception
ISR_NO_ERRCODE 22           ;      Reserved
ISR_NO_ERRCODE 23           ;      Reserved
ISR_NO_ERRCODE 24           ;      Reserved
ISR_NO_ERRCODE 25           ;      Reserved
ISR_NO_ERRCODE 26           ;      Reserved
ISR_NO_ERRCODE 27           ;      Reserved
ISR_NO_ERRCODE 28           ; #HV  Hypervisor Injection Exception
ISR_ERRCODE    29           ; #VC  VMM Communication Exception
ISR_ERRCODE    30           ; #SX  Security Exception
ISR_NO_ERRCODE 31           ;      Reserved

; ---------------------------------------------------------------------------
; Common ISR stub
; ---------------------------------------------------------------------------
; Stack at entry (top to bottom):
;   [esp+0]  = interrupt number  (pushed by stub)
;   [esp+4]  = error code        (pushed by CPU or dummy 0)
;   [esp+8]  = EIP               (pushed by CPU)
;   [esp+12] = CS                (pushed by CPU)
;   [esp+16] = EFLAGS            (pushed by CPU)
;   (optional: user ESP, SS if privilege change)
;
; We save all general-purpose and segment registers so the C handler
; receives a complete snapshot via a pointer to struct isr_regs.
; ---------------------------------------------------------------------------
isr_common_stub:
    ; Save general-purpose registers
    pushad

    ; Save segment registers
    push ds
    push es
    push fs
    push gs

    ; Load kernel data segment (0x10 from GDT)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Pass pointer to isr_regs struct (current stack pointer)
    push esp

    ; C calling convention requires DF clear
    cld

    ; Call the C handler: void isr_handler(struct isr_regs *regs)
    call isr_handler

    ; Clean up the pushed esp argument
    add esp, 4

    ; Restore segment registers
    pop gs
    pop fs
    pop es
    pop ds

    ; Restore general-purpose registers
    popad

    ; Remove interrupt number and error code from stack
    add esp, 8

    ; Return from interrupt
    iret
