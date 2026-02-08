/* ==========================================================================
 * ClaudeOS Kernel Linker Script - Phase 2, Task 10
 * ==========================================================================
 * Links the kernel at virtual address 0xC0100000 (higher-half).
 * Loaded at physical 0x100000 by stage2.
 *
 * The .boot_pgdir section is placed AFTER .bss so that zeroing BSS
 * does not destroy active page tables.
 * ========================================================================== */

OUTPUT_FORMAT(binary)
ENTRY(_start)

SECTIONS
{
    . = 0xC0100000;

    .text : { *(.text) }
    .rodata : ALIGN(4) { *(.rodata*) }
    .data : ALIGN(4) { *(.data) }

    _bss_start = .;
    .bss : ALIGN(4096) {
        *(.bss)
        *(COMMON)
    }
    _bss_end = .;

    /* Boot page tables — MUST NOT be zeroed with BSS */
    .boot_pgdir : ALIGN(4096) {
        *(.boot_pgdir)
    }

    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
