; =============================================================================
; ClaudeOS Kernel Entry - Phase 2, Task 10
; =============================================================================
; First code in the kernel binary, linked at virtual 0xC0100000.
; Loaded at physical 0x100000 by stage2.
;
; Before paging: runs at physical 0x100000, uses (label - KERNEL_VIRT_BASE)
; After paging: runs at virtual 0xC01xxxxx, uses labels directly
;
; Boot sequence:
;   1. Fill boot page table (maps first 4MB)
;   2. Set up page directory: PD[0]=identity, PD[768]=higher-half, PD[1023]=recursive
;   3. Load CR3, enable paging
;   4. Jump to higher-half virtual address
;   5. Reload GDT (kernel space), reload segment registers
;   6. Remove identity mapping, flush TLB
;   7. Set up kernel stack
;   8. Zero BSS
;   9. Call kernel_main()
; =============================================================================

[bits 32]

; ---------------------------------------------------------------------------
; Constants
; ---------------------------------------------------------------------------
KERNEL_VIRT_BASE    equ 0xC0000000
KERNEL_STACK_SIZE   equ 16384           ; 16KB kernel stack

; Page entry flags
PG_PRESENT          equ 0x01
PG_WRITABLE         equ 0x02
PG_FLAGS            equ (PG_PRESENT | PG_WRITABLE)

; CR0 paging bit
CR0_PG              equ 0x80000000

; Segment selectors (must match kernel GDT)
KERNEL_CS           equ 0x08
KERNEL_DS           equ 0x10

; ---------------------------------------------------------------------------
; Boot page tables — separate section, NOT zeroed with BSS
; ---------------------------------------------------------------------------
section .boot_pgdir nobits alloc write
align 4096

boot_page_directory:
    resb 4096                           ; 1024 PDE entries x 4 bytes

boot_page_table_0:
    resb 4096                           ; 1024 PTE entries x 4 bytes

; ---------------------------------------------------------------------------
; Kernel stack in BSS (zeroed is fine)
; ---------------------------------------------------------------------------
section .bss
align 16

stack_bottom:
    resb KERNEL_STACK_SIZE
stack_top:

; ---------------------------------------------------------------------------
; Kernel GDT in .data — accessible at virtual address after paging
; ---------------------------------------------------------------------------
section .data
align 8

global kernel_gdt_tss_descriptor

kernel_gdt:
    ; Entry 0: Null descriptor (required)
    dq 0

    ; Entry 1: Code segment (selector 0x08)
    ; Base=0, Limit=4GB, 32-bit, DPL=0, Execute/Read
    dw 0xFFFF               ; Limit[15:0]
    dw 0x0000               ; Base[15:0]
    db 0x00                  ; Base[23:16]
    db 0x9A                  ; Access: P=1 DPL=0 S=1 E=1 DC=0 RW=1 A=0
    db 0xCF                  ; Flags: G=1 D=1, Limit[19:16]=0xF
    db 0x00                  ; Base[31:24]

    ; Entry 2: Data segment (selector 0x10)
    ; Base=0, Limit=4GB, 32-bit, DPL=0, Read/Write
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92                  ; Access: P=1 DPL=0 S=1 E=0 DC=0 RW=1 A=0
    db 0xCF
    db 0x00

kernel_gdt_tss_descriptor:
    ; Entry 3: TSS descriptor placeholder (selector 0x18)
    ; Populated at runtime by tss_init().
    dq 0
kernel_gdt_end:

kernel_gdt_ptr:
    dw kernel_gdt_end - kernel_gdt - 1  ; Size - 1
    dd kernel_gdt                        ; Virtual address (valid after paging)

; ---------------------------------------------------------------------------
; Entry point
; ---------------------------------------------------------------------------
section .text

global _start
extern kernel_main
extern _bss_start
extern _bss_end

_start:
    ; =====================================================================
    ; PHASE 1: Set up paging (running at physical address 0x100000)
    ; All references use (virtual_label - KERNEL_VIRT_BASE) = physical addr
    ; =====================================================================

    ; --- Fill boot_page_table_0: map physical 0x00000000 - 0x003FFFFF ---
    ; 1024 entries x 4KB = 4MB
    mov edi, boot_page_table_0 - KERNEL_VIRT_BASE
    mov eax, PG_FLAGS               ; phys 0x00000000 | present | writable
    mov ecx, 1024
.fill_pt:
    mov [edi], eax
    add eax, 0x1000                 ; Next 4KB frame
    add edi, 4
    dec ecx
    jnz .fill_pt

    ; --- Zero the page directory ---
    mov edi, boot_page_directory - KERNEL_VIRT_BASE
    xor eax, eax
    mov ecx, 1024
    cld
    rep stosd

    ; --- Set page directory entries ---
    mov edi, boot_page_directory - KERNEL_VIRT_BASE

    ; PD[0] = boot_page_table_0 | flags (identity map)
    mov eax, boot_page_table_0 - KERNEL_VIRT_BASE
    or eax, PG_FLAGS
    mov [edi], eax

    ; PD[768] = boot_page_table_0 | flags (higher-half: 0xC0000000)
    mov [edi + 768 * 4], eax

    ; PD[1023] = boot_page_directory itself | flags (recursive mapping)
    mov eax, boot_page_directory - KERNEL_VIRT_BASE
    or eax, PG_FLAGS
    mov [edi + 1023 * 4], eax

    ; --- Load page directory into CR3 ---
    mov eax, boot_page_directory - KERNEL_VIRT_BASE
    mov cr3, eax

    ; --- Enable paging (set CR0.PG) ---
    mov eax, cr0
    or eax, CR0_PG
    mov cr0, eax

    ; =====================================================================
    ; PHASE 2: Jump to higher-half virtual address
    ; Both identity and higher-half mappings are active, so this works.
    ; =====================================================================
    mov eax, .higher_half
    jmp eax

.higher_half:
    ; =====================================================================
    ; PHASE 3: Running in higher half — virtual addresses now work
    ; =====================================================================

    ; --- Reload GDT from kernel virtual address space ---
    lgdt [kernel_gdt_ptr]

    ; Far jump to reload CS
    jmp KERNEL_CS:.reload_segments

.reload_segments:
    ; Reload data segment registers
    mov ax, KERNEL_DS
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; --- Remove identity mapping (PD[0] = 0) ---
    mov dword [boot_page_directory], 0

    ; --- Flush entire TLB ---
    mov eax, cr3
    mov cr3, eax

    ; --- Set up kernel stack ---
    mov esp, stack_top

    ; --- Zero BSS (required for C semantics) ---
    ; Page tables are in .boot_pgdir, NOT in BSS, so this is safe
    cld
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    jz .bss_done
    shr ecx, 2                      ; Byte count -> dword count
    xor eax, eax
    rep stosd
.bss_done:

    ; Align stack to 16 bytes (System V ABI)
    and esp, 0xFFFFFFF0

    ; --- Call C kernel ---
    call kernel_main

    ; kernel_main should never return
    cli
.hang:
    hlt
    jmp .hang
