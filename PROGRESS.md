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

## 2026-02-09 00:39:21 +0300 - Phase 2, Task 11: Kernel Heap (`kmalloc`/`kfree`)
- Completed: Added a kernel heap allocator with `kheap_init()`, `kmalloc(size)`, and `kfree(ptr)` in `kernel/heap.c` + `kernel/heap.h`.
- Heap design: first-fit free-list allocator with in-heap block headers, block splitting on allocation, and forward/backward coalescing on free.
- Virtual heap range implemented per project layout: `0xC1000000 - 0xC1FFFFFF` (16MB).
- Added runtime paging helpers in `kernel/paging.c` + `kernel/paging.h` using existing recursive mapping (`0xFFFFF000` PD, `0xFFC00000` PT window):
  - `paging_map_page()`
  - `paging_unmap_page()`
  - `paging_get_phys_addr()`
- Heap grows on demand: when no suitable free block exists, allocator requests physical frames from PMM and maps them into heap virtual space page-by-page.
- Modified `kernel/kernel.c`: initialize kernel heap after PMM init and run a small `kmalloc`/`kfree` self-test.
- Modified `Makefile`: added paging/heap sources and objects to kernel build.
- Reference docs consulted from `docs/core/` while implementing:
  - `Memory_Allocation.md` (allocator layering + kernel heap abstraction)
  - `Paging.md` (recursive mapping manipulation pattern + TLB notes)
  - `TLB.md` (TLB invalidation requirements)
  - `Fractal_Page_Mapping.md` (self-referencing directory concept)
  - `Writing_a_memory_manager.md` (first-fit + split/coalesce structure)
- Verified in QEMU (`-display none -serial stdio`): boot succeeds, PMM initializes, heap initializes, and serial reports `KHEAP: self-test passed`.
- Current scope note: `kfree` returns blocks to the heap free-list (reusable by future `kmalloc`) but does not yet shrink the heap by unmapping/releasing frames back to PMM.

## 2026-02-09 00:57:27 +0300 - Phase 3, Task 12: PS/2 Keyboard Driver (IRQ1, scancode -> ASCII)
- Completed: Added PS/2 keyboard driver in `kernel/keyboard.c` + `kernel/keyboard.h`.
- Driver features:
  - IRQ1 handler registration via `irq_register_handler(1, ...)`.
  - PIC unmasking for IRQ1 via `pic_clear_mask(1)`.
  - Basic PS/2 controller setup using ports `0x60/0x64` (disable ports, flush output, update config byte, re-enable first port).
  - Keyboard scanning enable command (`0xF4`) with ACK check (`0xFA`).
  - Scan code set 1 (US QWERTY) to ASCII translation with `Shift` and `CapsLock` handling.
  - Non-blocking ASCII ring buffer API: `keyboard_read_char()`.
- Integration:
  - Updated `kernel/kernel.c` to initialize keyboard before `sti`.
  - Main loop now drains translated keypresses to serial output for debug visibility.
  - Updated `Makefile` to build/link `kernel/keyboard.c`.
- Reference docs consulted from `docs/core/`:
  - `PS_2_Keyboard.md` (scan code set 1 table, make/break behavior, E0 prefix behavior)
  - `I8042_PS_2_Controller.md` (status bits, command/data port usage, controller config byte behavior)
- Verified:
  - `make` builds cleanly with `-Wall -Wextra -Werror`.
  - Boot tested in QEMU (`-display none -serial stdio`): kernel reaches steady state and logs `[KBD] PS/2 keyboard initialized (IRQ1, set1->ASCII)`.
  - Interactive keypress test in QEMU window on `2026-02-09 01:01:17 +0300` passed; serial echoed typed input (`aAaaaaAAAhello codex this is cool!!`).

## 2026-02-09 01:07:16 +0300 - Phase 3, Task 13: VGA Text Driver (scrolling + cursor polish)
- Completed: Upgraded VGA text driver behavior in `kernel/vga.c` + `kernel/vga.h` for milestone Task 13.
- Added public VGA cursor/screen helpers:
  - `vga_clear()`
  - `vga_set_cursor(row, col)`
  - `vga_get_cursor(&row, &col)`
- Improved text semantics in `vga_putchar()`:
  - Added backspace (`'\b'`) handling that moves cursor backward and erases the previous character.
  - Retained newline/carriage-return/tab handling and existing bottom-of-screen scrolling behavior.
- Internal cleanup: added cursor clamping helper to guarantee hardware cursor writes stay in bounds.
- Reference docs consulted from `docs/core/`:
  - `Text_Mode_Cursor.md` (hardware cursor control registers and update model)
  - `Printing_To_Screen.md` (text mode memory/cursor basics and scrolling expectations)
- Verified:
  - `make` builds cleanly with `-Wall -Wextra -Werror`.
  - Boot smoke test passed in QEMU (`-display none -serial stdio`) using a copied image (`/tmp/os_task13_test.bin`) because the primary image was locked by another running QEMU session.

## 2026-02-09 01:10:23 +0300 - Phase 3, Task 14: Basic Kernel Console (type and see output)
- Completed: Added a basic interactive kernel console in `kernel/console.c` + `kernel/console.h`.
- Console behavior:
  - Prints an initial prompt (`claudeos> `) on VGA.
  - Echoes typed printable characters to VGA.
  - Handles Enter (submits line, moves to new line, prints next prompt).
  - Handles Backspace with in-line deletion support.
  - Maintains a fixed-size current line buffer (128 bytes).
- Integration:
  - Updated `kernel/kernel.c` to initialize console after keyboard setup.
  - Main loop now routes keyboard input through `console_handle_char()` instead of direct serial echo.
  - Updated `Makefile` to compile and link `kernel/console.c`.
- Reference docs consulted from `docs/core/`:
  - `Terminals.md` (ASCII control semantics for Enter/Backspace and terminal input expectations)
  - `Printing_To_Screen.md` (text-mode output behavior and cursor progression)
  - `Text_Mode_Cursor.md` (hardware cursor update model in text mode)
- Verified:
  - `make` builds cleanly with `-Wall -Wextra -Werror`.
  - Boot smoke test passed in QEMU (`-display none -serial stdio`) with copied image (`/tmp/os_task14_test.bin`), serial log includes `[CONSOLE] Initialized`.
  - Interactive VGA typing validated in QEMU window on `2026-02-09 01:14:23 +0300`: prompt rendering, line echo, and serial mirror logs all worked (`[CONSOLE] hey codex`, `[CONSOLE] this is cool!`).

## 2026-02-09 01:18:01 +0300 - Phase 4, Task 15: Process Control Blocks + Kernel-Mode Processes
- Completed: Added process subsystem in `kernel/process.c` + `kernel/process.h`.
- PCB implementation:
  - Fixed-size PCB table (`PROCESS_MAX_COUNT = 16`).
  - Process metadata fields for upcoming scheduler/context-switch work: PID, state, ESP/EBP/EIP, CR3, kernel stack metadata, entry function, and name.
  - Bootstrap process registration (`kernel_main`) at init time.
- Kernel process support:
  - `process_create_kernel(name, entry, arg)` creates READY kernel-mode processes with allocated kernel stacks.
  - `process_run_kernel(pid)` executes READY kernel process entries and transitions them to TERMINATED when they return.
  - `process_dump_table()` prints active PCB state to serial for debugging.
- Integration:
  - Updated `kernel/kernel.c` to initialize process subsystem.
  - Added two demo kernel processes (`demo_a`, `demo_b`) to validate process creation/execution/state transitions.
  - Updated `Makefile` to compile/link `kernel/process.c`.
- Reference docs consulted from `docs/core/`:
  - `Processes_and_Threads.md` (process/thread model and scheduling context)
  - `Kernel_Multitasking.md` (TCB/stack state expectations)
  - `Context_Switching.md` (state components needed for software switches)
  - `Cooperative_Multitasking.md` (cooperative bootstrap style before preemption)
- Verified:
  - `make` builds cleanly with `-Wall -Wextra -Werror`.
  - Boot smoke test passed in QEMU (`-display none -serial stdio`) with copied image (`/tmp/os_task15_test.bin`).
  - Serial confirms PCB lifecycle: process creation (`pid=2`, `pid=3`), execution, termination, and PCB dump output.
- Scope note:
  - `process_run_kernel()` is a Task 15 cooperative bootstrap helper that directly invokes process entry functions.
  - Full register/stack context switching is intentionally deferred to Task 16.

## 2026-02-09 01:29:24 +0300 - Stability Hardening Pass (pre-Task 16)
- Completed: Fixed six high-priority future-bug risks identified in a kernel audit.
- Process subsystem hardening (`kernel/process.c`, `kernel/process_stubs.asm`, `Makefile`):
  - Added `process_call_on_stack()` assembly helper so kernel process entries now execute on their own kernel stack (not `kernel_main`'s stack).
  - Added process slot release path: finished processes now free their kernel stack and clear PCB slot back to `UNUSED`, preventing slot exhaustion and heap leaks.
- Paging hardening (`kernel/paging.c`):
  - Added PDE permission propagation/upgrade for user mappings (`PAGE_USER`) so future ring-3 page mappings can succeed correctly.
- PMM safety hardening (`kernel/pmm.c`):
  - Added allocation-ownership bitmap; `pmm_free_frame()` now frees only frames actually returned by `pmm_alloc_frame()`, preventing accidental frees of reserved/foreign frames.
- Keyboard race hardening (`kernel/keyboard.c`):
  - Made ring-buffer pop (`keyboard_read_char`) IRQ-safe using interrupt flag save/restore around head/tail updates.
- Reference docs consulted from `docs/core/`:
  - `Kernel_Multitasking.md` (per-task kernel stack model)
  - `Context_Switching.md` (state/privilege assumptions for future scheduler work)
  - `Processes_and_Threads.md` (PCB/process lifecycle expectations)
- Verified:
  - `make` builds cleanly with `-Wall -Wextra -Werror`.
  - QEMU boot smoke test passed (`/tmp/os_audit_fix_test.bin`), including process creation/execution and post-run PCB dump.
- Deferred risk note:
  - Spurious IRQ7/IRQ15 handling remains deferred by request and should be addressed before broader hardware bring-up.

## 2026-02-09 01:34:30 +0300 - Phase 4, Task 16: Context Switching
- Completed: Implemented cooperative kernel context switching infrastructure for i686.
- New low-level switch primitive:
  - Added `process_switch(old_esp*, new_esp)` in `kernel/process_stubs.asm` (pure assembly).
  - Switch saves/restores callee-saved context and returns into the next process stack frame.
- Process subsystem refactor (`kernel/process.c`, `kernel/process.h`):
  - Added scheduler-facing APIs:
    - `process_yield()`
    - `process_run_ready()`
  - Added per-process bootstrap entry (`process_bootstrap`) so new processes start on their own kernel stacks.
  - `process_create_kernel()` now builds an initial stack frame compatible with `process_switch`.
  - Added deferred zombie reaping to free terminated process stacks only after switching away safely.
- Kernel integration (`kernel/kernel.c`):
  - Demo kernel processes now call `process_yield()` to exercise cooperative switching.
  - `kernel_main` runs the ready queue via `process_run_ready()` before entering console loop.
- Build integration (`Makefile`):
  - Added `kernel/process_stubs.asm` to kernel object list.
- Reference docs consulted from `docs/core/`:
  - `Context_Switching.md` (software switch model, stack/EIP semantics)
  - `Kernel_Multitasking.md` (per-task kernel stack context handling)
  - `Cooperative_Multitasking.md` (cooperative scheduling bootstrap pattern)
- Verified:
  - `make` builds cleanly with `-Wall -Wextra -Werror`.
  - QEMU smoke test (`/tmp/os_task16_test.bin`) shows expected interleaving and completion:
    - `demo process A tick` / `demo process B tick` alternating via yields
    - both processes exit
    - PCB dump returns to only `kernel_main` RUNNING
  - Console still initializes after process demo run.
  - Interactive `make run` validation on `2026-02-09 01:39:21 +0300` passed:
    - process interleaving/exits and PCB dump matched expected output
    - console handled sustained key-hold input without freeze
    - line submit and serial mirror logs remained correct.
