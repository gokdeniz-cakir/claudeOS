bits 32

section .text
global _start

_start:
    ; write(1, msg, msg_len)
    mov eax, 1
    mov ebx, 1
    mov ecx, msg
    mov edx, msg_len
    int 0x80

    ; sbrk(+4096), keep old break in ESI
    mov eax, 3
    mov ebx, 0x1000
    int 0x80
    mov esi, eax

    ; sbrk(-4096), keep old break in EDI
    mov eax, 3
    mov ebx, -0x1000
    int 0x80
    mov edi, eax

    ; exit(0) (bootstrap task path currently returns error)
    mov eax, 2
    xor ebx, ebx
    int 0x80
    mov ebx, eax

    ; Privileged op in ring3 to terminate test with #GP.
    cli

.hang:
    jmp .hang

section .rodata
msg: db "[ELF] hello from embedded user ELF", 10
msg_len equ $ - msg
