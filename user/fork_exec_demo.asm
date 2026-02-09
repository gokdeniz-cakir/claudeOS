bits 32

section .text
global _start

_start:
    ; write(1, msg_start, msg_start_len)
    mov eax, 1
    mov ebx, 1
    mov ecx, msg_start
    mov edx, msg_start_len
    int 0x80

    ; fork() -> child pid in parent, or -1 on error
    mov eax, 7
    int 0x80
    cmp eax, 0xFFFFFFFF
    je .fork_fail

    ; exec("/elf_demo.elf")
    mov eax, 8
    mov ebx, path_exec
    int 0x80

    ; exec should not return on success.
    mov eax, 1
    mov ebx, 1
    mov ecx, msg_exec_fail
    mov edx, msg_exec_fail_len
    int 0x80
    jmp .fatal

.fork_fail:
    mov eax, 1
    mov ebx, 1
    mov ecx, msg_fork_fail
    mov edx, msg_fork_fail_len
    int 0x80

.fatal:
    ; Privileged op in ring 3 => #GP for deterministic failure signal.
    cli

.hang:
    jmp .hang

section .rodata
msg_start: db "[ELF] fork+exec probe", 10
msg_start_len equ $ - msg_start
msg_fork_fail: db "[ELF] fork failed", 10
msg_fork_fail_len equ $ - msg_fork_fail
msg_exec_fail: db "[ELF] exec failed", 10
msg_exec_fail_len equ $ - msg_exec_fail
path_exec: db "/elf_demo.elf", 0
