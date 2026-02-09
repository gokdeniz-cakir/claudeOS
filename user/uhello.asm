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

    ; exit(0)
    mov eax, 2
    xor ebx, ebx
    int 0x80

.hang:
    jmp .hang

section .rodata
msg: db "[APP] uhello: hello from standalone userspace program", 10
msg_len equ $ - msg
