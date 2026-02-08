# Progress Log

## 2026-02-08 12:11:26 +03 - Build Environment Audit
- Completed: Audited local tools against `AGENTS.md` build environment.
- Found: `Darwin` host, `nasm 3.01`, `qemu-system-i386 10.2.0`, `GNU Make 3.81`.

## 2026-02-08 12:15:25 +03 - i686 Toolchain Installation
- Completed: Installed and verifiedrequired packages with Homebrew: `i686-elf-binutils`, `i686-elf-gcc`.
- Result: `AGENTS.md` build-environment toolchain requirement is satisfied.

## 2026-02-08 - Phase 1, Task 1: MBR Bootloader
- Completed: MBR boot sector (`boot/mbr.asm`) that prints "ClaudeOS booting..." via BIOS INT 0x10.
- Created `Makefile` with `all`, `run`, and `clean` targets.
- Writer/reviewer workflow caught missing `cld` (direction flag bug), missing `cli` before segment init, and unsaved boot drive (DL).
- Verified: 512 bytes, 0xAA55 signature, boots and prints in QEMU.

## 2026-02-08 - Phase 1, Task 2: A20, GDT, Protected Mode
- Completed: Extended `boot/mbr.asm` with A20 enable (KBC + fast A20 fallback), flat GDT (null/code/data), and protected mode switch.
- Reviewer caught fast A20 logic bug (skipped verification when port bit was already set) and missing `cld` in 32-bit context. Both fixed.
- 32-bit confirmation writes to VGA text buffer at 0xB8000.
- Verified: 512 bytes, boots through PM switch in QEMU, no triple fault.

## 2026-02-08 - Phase 1, Task 3: Stage 2 Loader + C Kernel Entry
- Completed: Split MBR into 3-stage boot chain: MBR → stage2 → C kernel.
- MBR (`boot/mbr.asm`): loads 16 sectors from LBA 1 to 0x7E00 via INT 13h, jumps to stage2.
- Stage 2 (`boot/stage2.asm`): A20/GDT/PM switch, jumps to kernel at 0x8000.
- Kernel entry (`kernel/kernel_entry.asm`): BSS zero, calls `kernel_main()`.
- Kernel (`kernel/kernel.c`): minimal — halts for now (Task 4 adds VGA).
- Linker script (`linker.ld`): flat binary at 0x8000.
- Makefile: builds MBR + stage2 + kernel, concatenates into `os.bin`, pads to 32KB.
- Reviewer caught: need libgcc linking (switched to gcc as linker driver), boot drive passthrough to stage2, and disk image padding issue (fixed: 32KB image for 16-sector read).

## 2026-02-08 - Phase 1, Tasks 4+5: VGA Text Mode + Serial Port Output
- Completed: VGA text-mode driver and COM1 serial debug channel, implemented in parallel by two writer agents and merged.
- New files: `kernel/io.h` (shared port I/O), `kernel/vga.h`/`vga.c` (VGA driver), `kernel/serial.h`/`serial.c` (serial driver).
- VGA: 80x25 text mode, hardware cursor, scrolling, color support, clear screen on init.
- Serial: COM1 at 38400 baud, 8N1, FIFO enabled, THRE busy-wait, `\n` → `\r\n` translation.
- Reviewer found 1 bug: `vga_buffer` needed `volatile` for MMIO correctness at `-O2`. Fixed.
- Updated `kernel/kernel.c`: prints "ClaudeOS v0.1" in white via VGA, sends debug messages via serial.
- Updated Makefile: compiles `vga.o`/`serial.o`, links into kernel, added `-serial stdio` to QEMU.
- Verified: VGA memory shows "ClaudeOS" (0x0F attr = white), serial log shows both init messages.

## 2026-02-08 - Phase 2, Tasks 6+7: IDT/ISR + PIC/IRQ
- Completed: IDT setup with 256 entries, 32 ISR exception stubs, PIC remapping, 16 IRQ stubs.
- Task 6 files: `kernel/idt.h`/`idt.c` (IDT management), `kernel/isr.h`/`isr.c` (exception handler with register dump), `kernel/isr_stubs.asm` (32 stubs, error-code aware).
- Task 7 files: `kernel/pic.h`/`pic.c` (8259 PIC remap: master 0x20, slave 0x28, all masked), `kernel/irq.h`/`irq.c` (dispatch table + IDT gates 32-47), `kernel/irq_stubs.asm` (16 stubs).
- Orchestrator caught merge bug: IRQ stubs had reversed push order (segments-then-pushad vs pushad-then-segments in ISR stubs). Fixed to match `struct isr_regs`.
- Reviewer found 0 bugs, 1 RISK (spurious IRQ handling deferred), 1 STYLE (KERNEL_CS shared constant — fixed).
- `kernel_main()` now calls `idt_init()`, `irq_init()`, then `sti`.
- Verified: boots in QEMU, serial shows all 4 init stages, no triple fault.

## 2026-02-08 - Phase 2, Task 8: PIT Timer Interrupt
- Completed: PIT channel 0 programmed in mode 2 (rate generator) at 100 Hz.
- New files: `kernel/pit.h`/`pit.c` (PIT driver with IRQ0 handler, global tick counter).
- Modified: `kernel/pic.c` — added `pic_clear_mask()` for unmasking individual IRQ lines.
- Reviewer found: divisor comment was 11932 but C integer division gives 11931 — fixed comments/log.
- Reviewer caught: `pic_clear_mask` for slave IRQs should also unmask IRQ2 (cascade) on master — fixed.
- Added bounds check to `pic_clear_mask` (matching `irq_register_handler` pattern).
- Verified: ~190 IRQ0 interrupts in 2 seconds (~95 Hz effective, consistent with 100 Hz target).

## 2026-02-08 - Phase 2, Task 9: Physical Memory Manager
- Completed: E820 BIOS memory detection in stage2 + bitmap-based physical page frame allocator.
- Modified `boot/stage2.asm`: expanded to 1024 bytes (2 sectors), added E820 detection via INT 0x15/EAX=0xE820, stores map at 0x0500.
- Kernel origin moved from 0x8000 to 0x8200 (linker.ld, kernel_entry.asm updated).
- New files: `kernel/pmm.h`/`pmm.c` — bitmap allocator (1 bit per 4KB frame, 32KB bitmap for 1GB max).
- PMM marks all frames used, then frees only E820 Type 1 regions above 1MB.
- Reviewer caught: `pmm_free_frame(0)` could corrupt IVT — added guard rejecting frees below 1MB.
- Orchestrator caught: GCC 15 `-Warray-bounds` false positive on low-address pointers — fixed with targeted `#pragma GCC diagnostic`.
- Verified in QEMU (-m 128): 6 E820 entries detected, 32480 frames (129920 KB) marked free.

## 2026-02-08 - Phase 2, Task 10: Paging Enabled, Higher-Half Kernel
- Completed: Enabled x86 paging and remapped kernel to higher half (virtual 0xC0000000+).
- Boot chain updated: MBR loads 62 sectors; stage2 copies kernel from 0x8200 to physical 0x100000; kernel_entry sets up boot page tables, enables paging, jumps to higher-half.
- Paging design: single boot page table maps first 4MB; PD[0]=identity (removed after jump), PD[768]=higher-half (0xC0000000), PD[1023]=recursive mapping.
- Kernel GDT defined in kernel_entry.asm .data section, reloaded via LGDT after entering higher half (stage2 GDT at 0x7Exx inaccessible after identity map removal).
- Boot page tables placed in `.boot_pgdir` section (after .bss in linker script) to avoid destruction during BSS zeroing while paging is active.
- Modified files: `boot/mbr.asm` (62 sectors), `boot/stage2.asm` (kernel copy to 0x100000), `linker.ld` (virtual 0xC0100000, .boot_pgdir section), `kernel/kernel_entry.asm` (full rewrite: paging setup, GDT reload, identity unmap), `kernel/vga.h` (VGA at 0xC00B8000), `kernel/pmm.h`/`pmm.c` (E820 at 0xC0000500, kernel page reservation), `kernel/kernel.c` (paging status message), `Makefile` (comment update).
- PMM now reserves kernel physical pages (0x100000 to _kernel_end, 17 frames) to prevent allocating over the running kernel. Removed GCC #pragma workaround (no longer needed at high virtual address).
- Verified in QEMU (-m 128): CR0.PG=1, EIP=0xC01xxxxx, GDT at 0xC01xxxxx, PIT timer interrupts firing at higher-half addresses. Serial shows full boot with 32463 free frames (129852 KB). No triple faults.
