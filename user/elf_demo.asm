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

    ; sbrk(+4096), use returned region as read buffer
    mov eax, 3
    mov ebx, 0x1000
    int 0x80
    mov esi, eax

    cmp esi, 0xFFFFFFFF
    je .skip_fs_ops

    ; open("/fat/HELLO.TXT", O_READ)
    mov eax, 4
    mov ebx, path_hello
    mov ecx, 1
    int 0x80
    mov ebp, eax

    cmp ebp, 0xFFFFFFFF
    je .skip_fs_ops

    ; read(fd, buf, 128)
    mov eax, 5
    mov ebx, ebp
    mov ecx, esi
    mov edx, 128
    int 0x80
    mov edi, eax

    cmp edi, 0
    jle .close_fd

    ; write(1, buf, bytes_read)
    mov eax, 1
    mov ebx, 1
    mov ecx, esi
    mov edx, edi
    int 0x80

.close_fd:
    ; close(fd)
    mov eax, 6
    mov ebx, ebp
    int 0x80

.skip_fs_ops:
    ; sbrk(-4096) to release temporary buffer region
    cmp esi, 0xFFFFFFFF
    je .exit_path
    mov eax, 3
    mov ebx, -0x1000
    int 0x80
    mov edi, eax

.exit_path:
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
path_hello: db "/fat/HELLO.TXT", 0
