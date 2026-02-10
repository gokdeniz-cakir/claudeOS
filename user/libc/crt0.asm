bits 32

section .text
global _start
extern main
extern exit

_start:
    ; Provide a valid empty argv/argc frame for hosted-style main().
    xor eax, eax
    push eax                ; argv[0] = NULL terminator
    mov eax, esp
    push eax                ; argv
    push dword 0            ; argc
    call main
    add esp, 12
    push eax
    call exit

.hang:
    cli
    hlt
    jmp .hang
