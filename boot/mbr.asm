; =============================================================================
; ClaudeOS MBR Bootloader - Phase 1, Task 3
; =============================================================================
; MBR boot sector (512 bytes): initializes segments/stack, prints a boot
; message, loads stage 2 + kernel from disk to 0x7E00, and jumps to stage 2.
; Loaded by BIOS at 0x0000:0x7C00.
; =============================================================================

[bits 16]
[org 0x7C00]

%ifndef KERNEL_MAX_SECTORS
%define KERNEL_MAX_SECTORS 124
%endif

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 4
%endif

; ---------------------------------------------------------------------------
; Constants
; ---------------------------------------------------------------------------
VIDEO_TELETYPE      equ 0x0E        ; INT 0x10 AH: teletype output
VIDEO_PAGE          equ 0x00        ; Display page number

STAGE2_ADDR         equ 0x7E00      ; Load address for stage 2 + kernel
DISK_READ_SECTORS   equ (STAGE2_SECTORS + KERNEL_MAX_SECTORS)
FIRST_READ_SECTORS  equ 65          ; Max sectors from 0x7E00 without 64KB wrap
SECOND_READ_SEG     equ 0x1000      ; Physical 0x10000, contiguous after first chunk
SECOND_READ_MAX     equ 127         ; INT 13h extensions packet read limit

%if ((DISK_READ_SECTORS - FIRST_READ_SECTORS) > SECOND_READ_MAX)
%define SECOND_READ_SECTORS SECOND_READ_MAX
%define THIRD_READ_SECTORS ((DISK_READ_SECTORS - FIRST_READ_SECTORS) - SECOND_READ_MAX)
%else
%define SECOND_READ_SECTORS (DISK_READ_SECTORS - FIRST_READ_SECTORS)
%define THIRD_READ_SECTORS 0
%endif

THIRD_READ_SEG      equ (SECOND_READ_SEG + ((SECOND_READ_SECTORS * 512) / 16))
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
    ; Load stage 2 + kernel from disk via INT 13h extensions (AH=42h)
    ;   LBA 1 onward (right after MBR), split into two reads to avoid
    ;   crossing a 64KB segment boundary in the transfer buffer.
    ; =================================================================
    mov si, msg_load
    call print_string

    ; Verify BIOS supports LBA packet reads
    mov ax, 0x4100
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .disk_ext_fail
    cmp bx, 0xAA55
    jne .disk_ext_fail
    test cx, 1                       ; Bit 0 = extended disk access support
    jz .disk_ext_fail

    mov di, DISK_READ_RETRIES       ; DI = retry counter

.read_retry:
    ; Reset disk system before each attempt
    xor ax, ax                      ; AH=0x00 reset disk
    mov dl, [boot_drive]
    int 0x13

    ; First chunk: LBA 1 .. LBA 65 into 0x0000:0x7E00
    mov byte [dap_size], 0x10
    mov byte [dap_reserved], 0x00
    mov word [dap_sector_count], FIRST_READ_SECTORS
    mov word [dap_buffer_offset], STAGE2_ADDR
    mov word [dap_buffer_segment], 0x0000
    mov dword [dap_lba_low], 1
    mov dword [dap_lba_high], 0

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap_packet
    int 0x13
    jc .read_fail

    ; Second chunk: up to 127 sectors into 0x1000:0x0000 (phys 0x10000)
    mov word [dap_sector_count], SECOND_READ_SECTORS
    mov word [dap_buffer_offset], 0x0000
    mov word [dap_buffer_segment], SECOND_READ_SEG
    mov dword [dap_lba_low], (1 + FIRST_READ_SECTORS)
    mov dword [dap_lba_high], 0

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap_packet
    int 0x13
    jc .read_fail

%if THIRD_READ_SECTORS > 0
    ; Third chunk: any remaining sectors, contiguous after second chunk.
    mov word [dap_sector_count], THIRD_READ_SECTORS
    mov word [dap_buffer_offset], 0x0000
    mov word [dap_buffer_segment], THIRD_READ_SEG
    mov dword [dap_lba_low], (1 + FIRST_READ_SECTORS + SECOND_READ_SECTORS)
    mov dword [dap_lba_high], 0

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap_packet
    int 0x13
    jc .read_fail
%endif

    jmp .read_ok

.read_fail:
    dec di
    jnz .read_retry

    ; All retries exhausted
    mov si, msg_disk_err
    call print_string
    jmp .halt

.disk_ext_fail:
    mov si, msg_disk_ext_err
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
msg_disk_ext_err: db "No INT13 Ext", 0

boot_drive:     db 0

; INT 13h extensions Disk Address Packet (16 bytes)
dap_packet:
dap_size:           db 0
dap_reserved:       db 0
dap_sector_count:   dw 0
dap_buffer_offset:  dw 0
dap_buffer_segment: dw 0
dap_lba_low:        dd 0
dap_lba_high:       dd 0

; ---------------------------------------------------------------------------
; Boot signature
; ---------------------------------------------------------------------------
    times 510 - ($ - $$) db 0
    dw 0xAA55
