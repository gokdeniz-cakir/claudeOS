; =============================================================================
; ClaudeOS Stage 2 Loader - Phase 2, Task 10
; =============================================================================
; Loaded at 0x7E00 by the MBR. Runs in 16-bit real mode initially.
; Detects memory via E820, enables A20 gate, loads GDT, switches to 32-bit
; protected mode, copies kernel from 0x8200 to 0x100000, then jumps to
; the kernel entry point at 0x100000.
;
; Receives boot drive number in DL from MBR.
; =============================================================================

[bits 16]
[org 0x7E00]

%ifndef KERNEL_MAX_SECTORS
%define KERNEL_MAX_SECTORS 124
%endif

; ---------------------------------------------------------------------------
; Constants
; ---------------------------------------------------------------------------
VIDEO_TELETYPE      equ 0x0E
VIDEO_PAGE          equ 0x00

CODE_SEG            equ 0x08        ; GDT index 1: 32-bit code segment
DATA_SEG            equ 0x10        ; GDT index 2: 32-bit data segment

KBC_DATA_PORT       equ 0x60
KBC_CMD_PORT        equ 0x64

VGA_TEXT_BASE       equ 0xB8000
VGA_WHITE_ON_BLACK  equ 0x0F

STAGE2_PADDED_SIZE  equ 0x400       ; Stage 2 padded to 1024 bytes (2 sectors)
KERNEL_LOAD_ADDR    equ 0x7E00 + STAGE2_PADDED_SIZE  ; = 0x8200 (initial load)
KERNEL_PHYS_ADDR    equ 0x100000    ; Final kernel location (1MB)
KERNEL_MAX_BYTES    equ (KERNEL_MAX_SECTORS * 512)
KERNEL_COPY_DWORDS  equ (KERNEL_MAX_BYTES / 4)

; E820 memory map storage
E820_MAP_ADDR       equ 0x0500      ; Physical address for E820 map
E820_MAX_ENTRIES    equ 32          ; Maximum number of E820 entries
E820_ENTRY_SIZE     equ 24          ; Size of each E820 entry in bytes
E820_SMAP           equ 0x534D4150  ; 'SMAP' signature

; ---------------------------------------------------------------------------
; Entry point — 16-bit real mode
; ---------------------------------------------------------------------------
stage2_start:
    ; Save boot drive number (passed in DL from MBR)
    mov [boot_drive], dl

    ; Ensure DS/ES are correct
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov si, msg_stage2
    call print_string

    ; =====================================================================
    ; Step 1: Enable A20 gate
    ; =====================================================================
    mov si, msg_a20
    call print_string

    ; Check if A20 is already enabled
    call check_a20
    test ax, ax
    jnz .a20_done

    ; Try keyboard controller method
    call enable_a20_kbc
    call check_a20
    test ax, ax
    jnz .a20_done

    ; Fallback: Fast A20 gate (port 0x92)
    in al, 0x92
    test al, 2
    jnz .fast_a20_verify            ; Bit already set — skip write, verify
    or al, 2                        ; Set A20 gate bit
    and al, 0xFE                    ; Clear bit 0 (fast reset) for safety
    out 0x92, al

.fast_a20_verify:
    call check_a20
    test ax, ax
    jnz .a20_done

    ; A20 enable failed
    mov si, msg_a20_fail
    call print_string
    jmp .halt16

.a20_done:
    mov si, msg_ok
    call print_string

    ; =====================================================================
    ; Step 2: Detect memory via E820 BIOS call
    ; =====================================================================
    mov si, msg_e820
    call print_string

    call detect_e820

    mov si, msg_ok
    call print_string

    ; Print number of E820 entries detected (debug)
    mov al, [E820_MAP_ADDR]         ; Entry count (fits in a byte)
    call print_hex_byte
    mov si, msg_e820_count
    call print_string

    ; =====================================================================
    ; Step 3: Load GDT
    ; =====================================================================
    mov si, msg_gdt
    call print_string

    lgdt [gdt_descriptor]

    mov si, msg_ok
    call print_string

    ; =====================================================================
    ; Step 4: Switch to protected mode
    ; =====================================================================
    mov si, msg_pm
    call print_string

    cli                             ; No IDT for protected mode yet

    mov eax, cr0
    or al, 1                        ; Set PE bit
    mov cr0, eax

    ; Far jump flushes pipeline and loads CS with code segment selector
    jmp CODE_SEG:pm_entry

; ---------------------------------------------------------------------------
; 16-bit halt (A20/E820 failure path)
; ---------------------------------------------------------------------------
.halt16:
    cli
    hlt
    jmp .halt16

; ---------------------------------------------------------------------------
; detect_e820: Detect physical memory map using BIOS INT 0x15, EAX=0xE820
;   Stores entry count as uint32_t at E820_MAP_ADDR, followed by entries.
;   Halts on failure (E820 unsupported).
; ---------------------------------------------------------------------------
detect_e820:
    push es
    push di
    push ebx
    push ecx
    push edx
    push ebp

    xor ax, ax
    mov es, ax                      ; ES = 0

    mov di, E820_MAP_ADDR + 4       ; DI = address of first entry (skip count)
    xor ebx, ebx                    ; EBX = continuation value (0 = start)
    xor ebp, ebp                    ; EBP = entry counter
    mov edx, E820_SMAP              ; EDX = 'SMAP' signature

    ; --- First call ---
    mov dword [es:di + 20], 1       ; Force valid ACPI 3.0 entry
    mov eax, 0xE820
    mov ecx, E820_ENTRY_SIZE        ; Request 24 bytes per entry
    int 0x15

    jc .e820_fail                   ; Carry set = unsupported or error
    cmp eax, E820_SMAP              ; EAX must return 'SMAP'
    jne .e820_fail
    test ebx, ebx                   ; EBX = 0 means only one entry (list done)
    jz .e820_fail                   ; At least 2 entries expected for valid map

    ; First entry succeeded — validate and count it
    jmp .e820_check_entry

.e820_loop:
    ; --- Subsequent calls ---
    cmp ebp, E820_MAX_ENTRIES       ; Check max entries limit
    jge .e820_done

    mov dword [es:di + 20], 1       ; Force valid ACPI 3.0 entry
    mov eax, 0xE820
    mov ecx, E820_ENTRY_SIZE
    mov edx, E820_SMAP              ; Some BIOSes trash EDX, restore it
    int 0x15

    jc .e820_done                   ; Carry set = end of list (normal exit)

.e820_check_entry:
    ; Check if ECX returned 20 (no ACPI attrs) — force acpi_attrs = 1
    cmp cl, 20
    jne .e820_check_acpi
    mov dword [es:di + 20], 1       ; No extended attrs, force valid

.e820_check_acpi:
    ; Check ACPI 3.0 "ignore this entry" bit (bit 0 of acpi_attrs = 0 means ignore)
    test dword [es:di + 20], 1
    jz .e820_skip_entry             ; ACPI says ignore this entry

    ; Check for zero-length entry (length_low and length_high both zero)
    mov ecx, [es:di + 8]            ; length_low
    or ecx, [es:di + 12]            ; length_high
    jz .e820_skip_entry             ; Zero length — skip

    ; Valid entry — count it and advance DI
    inc ebp
    add di, E820_ENTRY_SIZE

.e820_skip_entry:
    ; Check if we're done (EBX = 0 means last entry)
    test ebx, ebx
    jnz .e820_loop

.e820_done:
    ; Store entry count at E820_MAP_ADDR
    mov dword [E820_MAP_ADDR], ebp

    ; Check that we got at least 1 entry
    test ebp, ebp
    jz .e820_fail

    pop ebp
    pop edx
    pop ecx
    pop ebx
    pop di
    pop es
    ret

.e820_fail:
    mov si, msg_e820_fail
    call print_string
    cli
    hlt
    jmp .e820_fail

; ---------------------------------------------------------------------------
; check_a20: Test if A20 line is enabled
;   Returns: AX = 0 if disabled, AX = 1 if enabled
; ---------------------------------------------------------------------------
check_a20:
    pushf
    push ds
    push es
    push si
    push di

    cli

    xor ax, ax
    mov ds, ax
    mov si, 0x0500

    not ax                          ; AX = 0xFFFF
    mov es, ax
    mov di, 0x0510

    ; Save original values
    mov al, [ds:si]
    push ax
    mov al, [es:di]
    push ax

    ; Write distinct values
    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF

    ; If A20 off, both alias to same address, so [ds:si] == 0xFF
    cmp byte [ds:si], 0xFF

    ; Restore originals
    pop ax
    mov [es:di], al
    pop ax
    mov [ds:si], al

    mov ax, 0
    je .check_a20_exit              ; ZF set = aliased = A20 off
    mov ax, 1

.check_a20_exit:
    pop di
    pop si
    pop es
    pop ds
    popf
    ret

; ---------------------------------------------------------------------------
; enable_a20_kbc: Enable A20 via 8042 keyboard controller
; ---------------------------------------------------------------------------
enable_a20_kbc:
    cli

    call .kbc_wait_input
    mov al, 0xAD                    ; Disable keyboard
    out KBC_CMD_PORT, al

    call .kbc_wait_input
    mov al, 0xD0                    ; Read output port
    out KBC_CMD_PORT, al

    call .kbc_wait_output
    in al, KBC_DATA_PORT
    push ax                         ; Save output port value

    call .kbc_wait_input
    mov al, 0xD1                    ; Write output port
    out KBC_CMD_PORT, al

    call .kbc_wait_input
    pop ax
    or al, 2                        ; Set A20 gate bit
    out KBC_DATA_PORT, al

    call .kbc_wait_input
    mov al, 0xAE                    ; Re-enable keyboard
    out KBC_CMD_PORT, al

    call .kbc_wait_input
    sti
    ret

.kbc_wait_input:
    in al, KBC_CMD_PORT
    test al, 2
    jnz .kbc_wait_input
    ret

.kbc_wait_output:
    in al, KBC_CMD_PORT
    test al, 1
    jz .kbc_wait_output
    ret

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
; print_hex_byte: Print AL as two hex digits via BIOS INT 0x10
;   Input:  AL = byte to print
;   Clobbers: AX, BX, CX
; ---------------------------------------------------------------------------
print_hex_byte:
    mov cl, al                      ; Save byte in CL
    shr al, 4                       ; High nibble
    call .print_nibble
    mov al, cl                      ; Restore byte
    and al, 0x0F                    ; Low nibble
    call .print_nibble
    ret

.print_nibble:
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    jmp .emit
.digit:
    add al, '0'
.emit:
    mov ah, VIDEO_TELETYPE
    mov bh, VIDEO_PAGE
    int 0x10
    ret

; ---------------------------------------------------------------------------
; Data (16-bit)
; ---------------------------------------------------------------------------
msg_stage2:     db "Stage2", 0x0D, 0x0A, 0
msg_a20:        db "A20 ", 0
msg_gdt:        db "GDT ", 0
msg_pm:         db "PM..", 0x0D, 0x0A, 0
msg_ok:         db "OK", 0x0D, 0x0A, 0
msg_a20_fail:   db "A20 FAIL", 0
msg_e820:       db "E820 ", 0
msg_e820_fail:  db "E820 FAIL", 0
msg_e820_count: db " entries", 0x0D, 0x0A, 0

boot_drive:     db 0

; ---------------------------------------------------------------------------
; GDT — 3 entries: null, code (ring 0), data (ring 0)
; Flat 4 GB address space, 32-bit, 4K granularity.
; ---------------------------------------------------------------------------
gdt_start:

gdt_null:   dq 0x0000000000000000

; Code: base=0, limit=0xFFFFF, access=0x9A, flags=0xC
gdt_code:
    dw 0xFFFF                       ; Limit[15:0]
    dw 0x0000                       ; Base[15:0]
    db 0x00                         ; Base[23:16]
    db 0x9A                         ; Access: P=1 DPL=0 S=1 E=1 RW=1
    db 0xCF                         ; Flags(0xC) << 4 | Limit[19:16](0xF)
    db 0x00                         ; Base[31:24]

; Data: base=0, limit=0xFFFFF, access=0x92, flags=0xC
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92                         ; Access: P=1 DPL=0 S=1 E=0 RW=1
    db 0xCF
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1     ; Size - 1
    dd gdt_start                    ; Linear address (DS=0, org=0x7E00)

; ===========================================================================
; 32-bit protected mode entry
; ===========================================================================
[bits 32]

pm_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, 0x90000

    cld

    ; Write PM confirmation to VGA text buffer (row 4)
    mov edi, VGA_TEXT_BASE + (4 * 160)
    mov esi, msg_pm_ok
    mov ah, VGA_WHITE_ON_BLACK
.print_pm:
    lodsb
    test al, al
    jz .copy_kernel
    mov [edi], ax
    add edi, 2
    jmp .print_pm

.copy_kernel:
    ; Copy loaded kernel window from 0x8200 to 0x100000.
    ; Size is capped by KERNEL_MAX_SECTORS and enforced by Makefile.
    mov esi, KERNEL_LOAD_ADDR
    mov edi, KERNEL_PHYS_ADDR
    mov ecx, KERNEL_COPY_DWORDS
    cld
    rep movsd

    ; Jump to kernel at its new physical address
    jmp KERNEL_PHYS_ADDR

.halt32:
    cli
.halt_loop:
    hlt
    jmp .halt_loop

msg_pm_ok:
    db "PM OK -> kernel", 0

; ---------------------------------------------------------------------------
; Pad to exactly 1024 bytes (2 sectors)
; ---------------------------------------------------------------------------
    times STAGE2_PADDED_SIZE - ($ - $$) db 0
