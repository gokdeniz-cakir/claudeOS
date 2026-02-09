bits 32

section .text
global _start

_start:
    ; write(1, start_msg, start_msg_len)
    mov eax, 1
    mov ebx, 1
    mov ecx, start_msg
    mov edx, start_msg_len
    int 0x80

    ; exec("/uhello.elf")
    mov eax, 8
    mov ebx, uhello_path
    int 0x80

    ; If exec returns, it failed.
    mov eax, 1
    mov ebx, 1
    mov ecx, fail_msg
    mov edx, fail_msg_len
    int 0x80

    ; exit(1)
    mov eax, 2
    mov ebx, 1
    int 0x80

.hang:
    jmp .hang

section .rodata
start_msg: db "[APP] uexec: exec /uhello.elf", 10
start_msg_len equ $ - start_msg
fail_msg: db "[APP] uexec: exec failed", 10
fail_msg_len equ $ - fail_msg
uhello_path: db "/uhello.elf", 0
