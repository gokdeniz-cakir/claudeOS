; =============================================================================
; ClaudeOS MBR Bootloader - Phase 1, Task 3
; =============================================================================
; MBR boot sector (512 bytes): initializes segments/stack, prints a boot
; message, loads stage 2 + kernel from disk to 0x7E00, and jumps to stage 2.
; Loaded by BIOS at 0x0000:0x7C00.
; =============================================================================

[bits 16]
[org 0x7C00]

; ---------------------------------------------------------------------------
; Constants
; ---------------------------------------------------------------------------
VIDEO_TELETYPE      equ 0x0E        ; INT 0x10 AH: teletype output
VIDEO_PAGE          equ 0x00        ; Display page number

STAGE2_ADDR         equ 0x7E00      ; Load address for stage 2 + kernel
DISK_READ_SECTORS   equ 62          ; Sectors to load (31 KB, room for stage2+kernel)
DISK_READ_RETRIES   equ 3           ; Retry count for disk reads

; ---------------------------------------------------------------------------
; Entry point (16-bit real mode)
; ---------------------------------------------------------------------------
start:
    jmp 0x0000:.flush_cs
.flush_cs:

    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax                      ; mov ss inhibits interrupts for next insn
    mov sp, 0x7C00                  ; Stack grows down from 0x7BFF

    mov [boot_drive], dl            ; Save boot drive number from BIOS

    sti
    cld

    ; Print boot message
    mov si, msg_boot
    call print_string

    ; =================================================================
    ; Load stage 2 + kernel from disk
    ; INT 13h AH=02h: Read sectors using CHS
    ;   Sector 2 in CHS = LBA 1 (right after MBR)
    ; =================================================================
    mov si, msg_load
    call print_string

    mov di, DISK_READ_RETRIES       ; DI = retry counter

.read_retry:
    ; Reset disk system before each attempt
    xor ax, ax                      ; AH=0x00 reset disk
    mov dl, [boot_drive]
    int 0x13

    ; Set up read parameters
    mov ah, 0x02                    ; Read sectors
    mov al, DISK_READ_SECTORS       ; Number of sectors
    mov ch, 0                       ; Cylinder 0
    mov cl, 2                       ; Sector 2 (CHS sectors are 1-based)
    mov dh, 0                       ; Head 0
    mov dl, [boot_drive]            ; Drive number
    xor bx, bx
    mov es, bx                      ; ES = 0x0000
    mov bx, STAGE2_ADDR             ; ES:BX = 0x0000:0x7E00

    int 0x13
    jnc .read_ok                    ; CF clear = success

    ; Read failed — retry
    dec di
    jnz .read_retry

    ; All retries exhausted
    mov si, msg_disk_err
    call print_string
    jmp .halt

.read_ok:
    mov si, msg_ok
    call print_string

    ; Pass boot drive number in DL to stage 2
    mov dl, [boot_drive]

    ; Jump to stage 2
    jmp 0x0000:STAGE2_ADDR

; ---------------------------------------------------------------------------
; halt: infinite halt loop
; ---------------------------------------------------------------------------
.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; print_string: Print null-terminated string via BIOS INT 0x10
;   Input:  DS:SI = pointer to string
;   Clobbers: AX, BX, SI
; ---------------------------------------------------------------------------
print_string:
    mov ah, VIDEO_TELETYPE
    mov bh, VIDEO_PAGE
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    ret

; ---------------------------------------------------------------------------
; Data
; ---------------------------------------------------------------------------
msg_boot:       db "ClaudeOS booting...", 0x0D, 0x0A, 0
msg_load:       db "Loading ", 0
msg_ok:         db "OK", 0x0D, 0x0A, 0
msg_disk_err:   db "Disk err", 0

boot_drive:     db 0

; ---------------------------------------------------------------------------
; Boot signature
; ---------------------------------------------------------------------------
    times 510 - ($ - $$) db 0
    dw 0xAA55
