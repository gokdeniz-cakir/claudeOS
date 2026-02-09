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

    ; fd = open("/fat/HELLO.TXT", O_READ)
    mov eax, 4
    mov ebx, path_hello
    mov ecx, 1
    int 0x80
    mov esi, eax

    cmp esi, 0xFFFFFFFF
    je .open_fail

.read_loop:
    ; n = read(fd, buf, 128)
    mov eax, 5
    mov ebx, esi
    mov ecx, read_buf
    mov edx, 128
    int 0x80

    cmp eax, 0
    jle .close_fd

    ; write(1, buf, n)
    mov edx, eax
    mov eax, 1
    mov ebx, 1
    mov ecx, read_buf
    int 0x80
    jmp .read_loop

.close_fd:
    ; close(fd)
    mov eax, 6
    mov ebx, esi
    int 0x80
    jmp .exit_ok

.open_fail:
    ; write(1, fail_msg, fail_msg_len)
    mov eax, 1
    mov ebx, 1
    mov ecx, fail_msg
    mov edx, fail_msg_len
    int 0x80

.exit_ok:
    ; exit(0)
    mov eax, 2
    xor ebx, ebx
    int 0x80

.hang:
    jmp .hang

section .bss
read_buf: resb 128

section .rodata
start_msg: db "[APP] ucat: reading /fat/HELLO.TXT", 10
start_msg_len equ $ - start_msg
fail_msg: db "[APP] ucat: open failed", 10
fail_msg_len equ $ - fail_msg
path_hello: db "/fat/HELLO.TXT", 0
