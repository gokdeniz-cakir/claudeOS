bits 32

section .text
global _start
extern main
extern exit

_start:
    call main
    push eax
    call exit

.hang:
    cli
    hlt
    jmp .hang
