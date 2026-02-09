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

## 2026-02-09 01:44:19 +0300 - Phase 4, Task 17: TSS Setup
- Completed: Added protected-mode Task State Segment initialization and task register load for i686.
- New files: `kernel/tss.c`, `kernel/tss.h`.
- GDT integration: `kernel/kernel_entry.asm` now reserves a runtime-populated TSS descriptor at selector `0x18`.
- TSS configuration: `ss0=0x10`, `esp0` points to a dedicated aligned kernel stack, and `iomap_base=sizeof(TSS)`.
- Descriptor configuration: base/limit set at runtime, access byte `0x89`, byte-granularity flags (`0x0`).
- Kernel integration: `kernel/kernel.c` now calls `tss_init()` before enabling interrupts and logs `TSS initialized`.
- Build integration: `Makefile` now compiles and links `kernel/tss.c`.
- Reference docs consulted from `docs/core/`:
  - `Task_State_Segment.md`
  - `GDT_Tutorial.md`
  - `Getting_to_Ring_3.md`
  - `Context_Switching.md`
- Verified:
  - `make` builds cleanly with `-Wall -Wextra -Werror`.
  - QEMU smoke run (`qemu-system-i386 -display none -serial stdio`) reaches console loop and prints `TSS initialized`.
- Future note: `esp0` is currently a bootstrap kernel stack; for ring-3 syscall/interrupt return paths, update `esp0` on task switches in later milestones.

## 2026-02-09 01:54:40 +0300 - Interrupt/TSS Hardening Fixes
- Completed: Fixed two audit findings requested after Task 17.
- TSS ordering hardening:
  - Updated `kernel/tss.c` so `ltr` inline assembly includes a `memory` clobber, preventing compiler reordering across TSS/GDT setup writes.
- Spurious IRQ handling:
  - Added PIC ISR read helpers in `kernel/pic.c` + declarations in `kernel/pic.h`.
  - Added `pic_is_spurious_irq()` handling for IRQ7/IRQ15 using ISR checks from OSDev guidance.
  - Corrected spurious IRQ15 behavior: send EOI to master PIC only, skip slave EOI and IRQ dispatch.
  - Updated `kernel/irq.c` to detect and early-return on spurious IRQ7/15 before normal dispatch/EOI flow.
- Reference docs consulted from `docs/core/`:
  - `8259_PIC.md` (ISR/IRR and spurious IRQ handling)
  - `Task_State_Segment.md` (TSS load sequencing context)
- Verified:
  - `make` builds cleanly with `-Wall -Wextra -Werror`.
  - QEMU serial smoke boot reaches stable console loop after init and process demo run.

## 2026-02-09 02:08:24 +0300 - Phase 4, Task 18: Preemptive Round-Robin Scheduler (PIT-driven)
- Completed: Added PIT-driven preemptive scheduling on top of existing software context switching.
- IRQ integration (`kernel/irq.c`):
  - Added timer quantum accounting (`SCHED_QUANTUM_TICKS = 1`).
  - Scheduler preemption now triggers from IRQ0 after handler dispatch and after PIC EOI is sent.
  - This ordering avoids starving future timer IRQs while a context switch is in progress.
- Process subsystem updates (`kernel/process.c`, `kernel/process.h`):
  - Added preemption controls:
    - `process_set_preemption()`
    - `process_is_preemption_enabled()`
    - `process_preempt_from_irq()`
  - Added bootstrap `sti` when preemption is enabled so a fresh process first entered from IRQ context can continue receiving interrupts.
- Kernel integration (`kernel/kernel.c`):
  - Demo processes no longer call `process_yield()`, so scheduling is driven by PIT preemption rather than cooperative yields.
  - Enabled preemption in `kernel_main` via `process_set_preemption(1)` before `sti`.
  - Kept one-time PCB summary print after demo processes exit (`process_count() == 1`).
- Reference docs consulted from `docs/core/`:
  - `Kernel_Multitasking.md`
  - `Context_Switching.md`
  - `8259_PIC.md` (EOI/spurious behavior and PIC ISR semantics)
- Verified:
  - Clean build: `make` with `-Wall -Wextra -Werror`.
  - QEMU serial smoke run shows PIT-driven interleaving without cooperative yields:
    - `demo process A tick` / `demo process B tick` alternate/interleave
    - both demo processes exit
    - PCB table returns to `kernel_main` only.

## 2026-02-09 02:20:53 +0300 - Phase 4, Task 19: Spinlocks and Basic Synchronization
- Completed: Added reusable spinlock and synchronization primitives for preemptive kernel paths.
- New primitives:
  - `kernel/spinlock.h`, `kernel/spinlock.c`
    - `spinlock_lock`, `spinlock_try_lock`, `spinlock_unlock`
    - IRQ-safe helpers: `spinlock_irq_save/restore`, `spinlock_lock_irqsave`, `spinlock_unlock_irqrestore`
  - `kernel/sync.h`, `kernel/sync.c`
    - Counting semaphore: `semaphore_init`, `semaphore_wait`, `semaphore_signal`, `semaphore_value`
    - Mutex wrapper: `mutex_init`, `mutex_lock`, `mutex_unlock`
- Runtime integration:
  - `kernel/heap.c`: added global heap spinlock; `kmalloc`/`kfree` now execute under IRQ-safe lock to prevent allocator metadata corruption under preemption.
  - `kernel/keyboard.c`: replaced manual IRQ flag save/restore around ring-buffer operations with shared spinlock helpers.
- Build integration:
  - `Makefile` updated to compile and link `spinlock.c` and `sync.c`.
- Reference docs consulted from `docs/core/`:
  - `Spinlock.md`
  - `Semaphore.md`
  - `Kernel_Multitasking.md`
  - `Context_Switching.md`
- Verified:
  - `make` builds cleanly with `-Wall -Wextra -Werror`.
  - QEMU serial smoke boot passes through scheduler/console loop with expected preemptive demo behavior and no regressions.

## 2026-02-09 02:34:16 +0300 - Codebase Audit (Reviewer Pass 1)
- Completed: Performed a full audit of bootloader, memory management, interrupts, scheduler/process code, and device/console paths.
- Validation:
  - `make clean && make -j4` passes with `-Wall -Wextra -Werror`.
  - Headless QEMU serial smoke boot reaches stable console and process demo completion.
- Reference docs consulted from `docs/core/`:
  - `Disk_access_using_the_BIOS_(INT_13h).md`
  - `Detecting_Memory_(x86).md`
  - `Context_Switching.md`
  - `Task_State_Segment.md`
  - `8259_PIC.md`
- Findings captured for follow-up:
  - Hard-coded stage2/kernel load size limits can silently truncate future kernels.
  - Scheduler state transitions are not IRQ-safe in all call paths (reentrancy risk).
  - TSS `esp0` update path exists but is not wired into task switch flow.
  - PMM E820 free-frame accounting can drift if firmware reports overlapping ranges.
  - Bootloader CHS bulk-read path is brittle across BIOS implementations.

## 2026-02-09 02:46:25 +0300 - Audit Hotfixes: Findings #1 and #2
- Completed: Fixed the two high-priority audit findings requested for immediate remediation.
- Fix #1 (kernel load hard-cap / silent truncation risk):
  - `boot/mbr.asm`: replaced single CHS bulk read with INT 13h extensions (AH=41h capability check + AH=42h DAP reads), split into two chunks to avoid 64KB transfer-boundary issues.
  - `boot/stage2.asm`: replaced fixed 30KB copy constant with `KERNEL_MAX_SECTORS`-driven `KERNEL_COPY_DWORDS`.
  - `Makefile`: introduced `KERNEL_MAX_SECTORS`/`KERNEL_MAX_BYTES`, passed define to NASM for both boot stages, and added hard build failures when `kernel.bin` or final `os.bin` exceed supported limits.
  - Removed silent padding fallback (`|| true`) so over-limit images now fail fast with explicit errors.
- Fix #2 (scheduler reentrancy in mixed IRQ/non-IRQ call paths):
  - `kernel/process.c`: wrapped `process_yield()` scheduler critical section with `spinlock_irq_save()`/`spinlock_irq_restore()` to block IRQ-time reentry while scheduler global state and context switch state are being mutated.
- Reference docs consulted from `docs/core/`:
  - `Disk_access_using_the_BIOS_(INT_13h).md`
  - `Context_Switching.md`
  - `Spinlock.md`
- Verified:
  - Clean rebuild passes: `make -j4` with `-Wall -Wextra -Werror`.
  - QEMU serial smoke run reaches stable init + scheduler + console path with expected process completion and PCB dump.
  - Current image sizing: `kernel.bin` = 16016 bytes, `os.bin` = 65536 bytes.

## 2026-02-09 03:01:42 +0300 - Phase 5, Task 20: Ring 0 -> Ring 3 Transition
- Completed: Added a controlled ring3 transition path using the `iret` method while keeping default boot behavior unchanged.
- GDT updates:
  - `kernel/kernel_entry.asm` now includes user-mode flat segments:
    - User code descriptor at selector `0x20` (DPL=3, access `0xFA`)
    - User data descriptor at selector `0x28` (DPL=3, access `0xF2`)
  - Existing TSS descriptor remains at selector `0x18`.
- User-mode transition module:
  - New files `kernel/usermode.h` and `kernel/usermode.c`.
  - Implements a minimal ring3 probe:
    - maps a user code page and user stack page with `PAGE_USER`
    - sets up an `iret` frame with ring3 selectors (`CS=0x23`, `SS=0x2B`)
    - enters ring3 and intentionally executes `cli` to trigger `#GP` as proof of user privilege level.
  - Updates `tss.esp0` from current kernel ESP immediately before transition.
- Console integration:
  - `kernel/console.c` now supports simple commands:
    - `help` (shows command list)
    - `ring3test` (launches ring3 transition probe)
  - Default `make run` flow remains unchanged unless `ring3test` is entered.
- Build integration:
  - `Makefile` now compiles and links `kernel/usermode.c`.
- Reference docs consulted from `docs/core/`:
  - `Getting_to_Ring_3.md`
  - `GDT_Tutorial.md`
  - `Task_State_Segment.md`
- Verified:
  - `make -j4` builds cleanly with `-Wall -Wextra -Werror`.
  - QEMU serial smoke boot (`make run`) still reaches stable scheduler + console path.
  - Image sizing remains within bootloader cap (`kernel.bin` = 16888 bytes, `os.bin` = 65536 bytes).
  - Full `ring3test` interaction requires manual keyboard input in QEMU window (not automatable via serial-only run).

## 2026-02-09 12:33:40 +0300 - Phase 5, Task 21: TSS Stack Switching on Syscall Path
- Completed: Wired per-task `TSS.esp0` updates into the scheduler/context-switch flow.
- Process scheduler integration:
  - `kernel/process.c` now computes a kernel stack top per task (`process_kernel_stack_top`).
  - `process_yield()` now updates `tss_set_kernel_stack(...)`:
    - on task switches: sets `esp0` for the next scheduled task before `process_switch`.
    - on no-op yields: refreshes `esp0` for the currently running task.
  - Added `process_refresh_tss_stack()` to sync `esp0` to the current task on demand.
  - Added prototype in `kernel/process.h`.
- Kernel init integration:
  - `kernel/kernel.c` now calls `process_refresh_tss_stack()` immediately after `tss_init()` to bind initial `esp0` to the bootstrap task stack context.
- Ring3 test hygiene for upcoming ELF loader work:
  - `kernel/usermode.c` ring3 probe virtual addresses moved away from common ELF base assumptions:
    - code: `0x08000000`
    - stack: `0x08001000`
  - Removed persistent `user_test_prepared` latch and static frame ownership state from ring3 probe setup.
  - `usermode_run_ring3_test()` now refreshes TSS via `process_refresh_tss_stack()` instead of directly writing `esp0` from local inline asm.
- Reference docs consulted from `docs/core/`:
  - `Task_State_Segment.md`
  - `Context_Switching.md`
  - `System_Calls.md`
- Verified:
  - `make -j4` builds cleanly with `-Wall -Wextra -Werror`.
  - QEMU serial smoke boot (`make run`) reaches stable scheduler + console path with no regressions.
  - Serial log now confirms initial synchronization: `[PROC] TSS esp0 synchronized for current task`.
  - Image sizing remains within cap (`kernel.bin` = 17080 bytes, `os.bin` = 65536 bytes).

## 2026-02-09 03:11:44 +0300 - Codebase Audit (Reviewer Pass 2, post-Task 20)
- Completed: Re-audited boot, scheduler, TSS/ring3 transition path, memory manager, and console command integration after Task 20.
- Validation:
  - `make clean && make -j4` passes with `-Wall -Wextra -Werror`.
  - Headless QEMU serial smoke boot reaches stable scheduler + console loop.
  - Automated ring3 probe run performed via QEMU monitor `sendkey` scripting; serial log confirms:
    - command dispatch (`[CONSOLE] ring3test`)
    - ring3 entry marker (`[USER] entering ring3 test...`)
    - expected `#GP` with `CS=0x23` and `DS/ES/FS/GS=0x2B`.
- Reference docs consulted from `docs/core/`:
  - `Getting_to_Ring_3.md`
  - `Task_State_Segment.md`
  - `Context_Switching.md`
  - `Detecting_Memory_(x86).md`
- Findings captured for follow-up:
  - `tss.esp0` update is still not integrated with scheduler task-switch flow (critical before real multi-task ring3/syscall work).
  - Ring3 test mappings are global/static debug mappings and can conflict with future user program load addresses if left enabled.
  - PMM free-frame accounting remains vulnerable to overlap-induced drift on pathological/unsorted E820 maps.

## 2026-02-09 12:42:58 +0300 - Phase 5, Task 22: INT 0x80 Syscall Interface
- Completed: Added a user-callable `INT 0x80` syscall entry path with an initial kernel dispatcher skeleton.
- IDT integration:
  - `kernel/idt.h`: added `IDT_GATE_INT32_USER` (`0xEE`, DPL=3).
  - `kernel/idt.c`: installed vector `0x80` gate to `syscall_int80` using DPL=3 interrupt gate so ring3 code can invoke it.
- Syscall subsystem:
  - New files:
    - `kernel/syscall.h`
    - `kernel/syscall.c`
    - `kernel/syscall_stubs.asm`
  - `syscall_stubs.asm` builds an `isr_regs`-compatible frame, switches to kernel data segments, calls `syscall_handler`, then `iret`s back.
  - `syscall.c` provides:
    - `syscall_init()` (startup marker)
    - `syscall_handler()` with first-call trace marker
    - `syscall_dispatch()` skeleton returning `0xFFFFFFFF` (`-ENOSYS`) for unknown syscall numbers.
- Kernel integration:
  - `kernel/kernel.c` now calls `syscall_init()` and logs syscall interface initialization during boot.
- Ring3 probe update:
  - `kernel/usermode.c` test payload now executes:
    - `mov eax, 0x1234`
    - `int 0x80`
    - `mov ebx, eax`
    - `cli` (expected `#GP` in ring3)
  - This validates syscall entry/return path before triggering the deliberate privilege fault.
- Build integration:
  - `Makefile` updated to compile/link `syscall.c` and `syscall_stubs.asm`.
- Reference docs consulted from `docs/core/`:
  - `System_Calls.md`
  - `Getting_to_Ring_3.md`
  - `Task_State_Segment.md`
- Verified:
  - `make -j4` builds cleanly with `-Wall -Wextra -Werror`.
  - QEMU serial smoke boot (`make run`) reaches stable scheduler + console path and logs:
    - `[SYSCALL] INT 0x80 interface initialized`
  - Full ring3 syscall-path confirmation still requires interactive `ring3test` in QEMU window.
  - Image sizing remains within cap (`kernel.bin` = 17456 bytes, `os.bin` = 65536 bytes).

## 2026-02-09 12:55:36 +0300 - Post-Task 22 Hardening (Scheduler/TSS and Syscall Gate)
- Completed: Applied follow-up fixes from audit feedback after Task 22 validation.
- `process_refresh_tss_stack()` race fix:
  - `kernel/process.c` now wraps `process_current_index` read + `tss_set_kernel_stack(...)` in `spinlock_irq_save()`/`spinlock_irq_restore()`.
  - This aligns with `process_yield()` scheduler critical-section semantics and prevents TSS `esp0` being refreshed against stale task index under preemption.
- Syscall gate behavior hardening:
  - `kernel/idt.h` added `IDT_GATE_TRAP32_USER` (`0xEF`).
  - `kernel/idt.c` switched vector `0x80` from user interrupt gate to user trap gate.
  - Trap gate preserves `IF`, reducing risk of IRQ starvation/latency spikes during longer syscall handlers.
- Verified:
  - `make -j4` builds cleanly with `-Wall -Wextra -Werror`.
  - QEMU serial smoke boot remains stable through init/scheduler/console path.

## 2026-02-09 12:50:50 +0300 - Codebase Audit (Reviewer Pass 3, post-Task 22)
- Completed: Re-audited Task 21/22 changes (`TSS.esp0` switching + `INT 0x80` syscall path), including scheduler/TSS interactions and syscall entry/return behavior.
- Validation:
  - `make clean && make -j4` passes with `-Wall -Wextra -Werror`.
  - Headless QEMU serial smoke boot reaches stable scheduler + console loop.
  - Automated `ring3test` run via QEMU monitor `sendkey` confirmed:
    - syscall entry marker (`[SYSCALL] first INT 0x80 received`)
    - return value propagation (`EAX=0xFFFFFFFF`, copied to `EBX`)
    - expected post-syscall ring3 `#GP` at user `CS=0x23`.
- Reference docs consulted from `docs/core/`:
  - `System_Calls.md`
  - `Interrupt_Descriptor_Table.md`
  - `Interrupt_Service_Routines.md`
  - `Context_Switching.md`
- Findings captured for follow-up:
  - `process_refresh_tss_stack()` updates `esp0` without an IRQ-safe critical section (can race with scheduler state changes under preemption).
  - Syscall gate uses an interrupt gate (IF cleared for full syscall duration), which can become a latency/deadlock hazard for longer/blocking syscall handlers.
  - Residual pre-existing risks remain: E820 single-entry rejection edge case and PMM free-frame accounting drift on overlapping E820 ranges.

## 2026-02-09 13:00:48 +0300 - Phase 5, Task 23: Basic Syscalls (write, exit, sbrk)
- Completed: Implemented first usable syscall set on top of `INT 0x80`.
- Syscall API updates:
  - `kernel/syscall.h`: added syscall number constants:
    - `SYSCALL_WRITE = 1`
    - `SYSCALL_EXIT = 2`
    - `SYSCALL_SBRK = 3`
  - `kernel/syscall.c`:
    - Added `write(fd, buf, len)` for `fd` 1/2 (stdout/stderr path to VGA+serial).
    - Added `exit(status)` with current safety rule: bootstrap `pid=1` cannot exit (returns error).
    - Added `sbrk(increment)` with per-process break tracking and page map/unmap using PMM+paging.
    - Added user-range + mapped-page validation for user pointers before `write`.
- Process subsystem support for syscall state:
  - `kernel/process.h` / `kernel/process.c`:
    - Added per-process `user_break` field.
    - Added `process_get_current_pid()`.
    - Added `process_terminate_current()` (non-returning helper for syscall exit path).
    - Added per-process break helpers:
      - `process_user_heap_base()`
      - `process_user_heap_limit()`
      - `process_get_current_user_break()`
      - `process_set_current_user_break()`
    - Introduced initial user-heap range constants:
      - base: `0x09000000`
      - limit: `0x0A000000`
- Ring3 probe update for syscall verification:
  - `kernel/usermode.c` ring3 payload now performs:
    - `write(1, "ring3 write ok\\n", 15)`
    - `sbrk(+4096)`
    - `sbrk(-4096)`
    - `exit(0)` (expected to fail for bootstrap task)
    - then executes `cli` to trigger expected ring3 `#GP`.
  - This keeps the existing ring3 proof path while exercising Task 23 syscalls in sequence.
- Reference docs consulted from `docs/core/`:
  - `System_Calls.md`
  - `Task_State_Segment.md`
  - `Context_Switching.md`
- Verified:
  - `make -j4` builds cleanly with `-Wall -Wextra -Werror`.
  - QEMU serial smoke boot (`make run`) reaches stable init/scheduler/console path with no regressions.
  - Interactive `ring3test` confirmation is required in QEMU window to observe:
    - ring3 write output
    - syscall trace + expected final `#GP` in ring3.
  - Image sizing remains within cap (`kernel.bin` = 18640 bytes, `os.bin` = 65536 bytes).

## 2026-02-09 13:12:31 +0300 - Phase 5, Task 24: ELF Loader (Simple Static ELF)
- Completed: Added a minimal ELF32 executable loader that maps PT_LOAD segments into user space and jumps to ELF entry in ring 3.
- New loader module:
  - `kernel/elf.h`
  - `kernel/elf.c`
  - Implements:
    - ELF header validation (magic/class/endianness/type/machine/version)
    - Program header parsing (`PT_LOAD` only)
    - Per-segment user page allocation/mapping and file/bss copy semantics (`p_filesz` + zero-fill to `p_memsz`)
    - Entry-point validation
    - Dedicated user stack mapping for loaded ELF image (`0x0BFF0000` page)
    - Cleanup of newly mapped pages on load failure
- Embedded static ELF test binary:
  - New file `user/elf_demo.asm` (simple static ring3 program using `write`, `sbrk`, `exit`, then intentional `cli` for deterministic end-of-test `#GP`).
  - Build pipeline in `Makefile`:
    - assemble user ELF object
    - link to executable ELF (`build/elf_demo.elf`, entry `0x08048000`)
    - convert ELF file to linkable blob object (`build/elf_demo_blob.o`) via `objcopy`
    - link blob object into kernel image
- Ring3 execution integration:
  - `kernel/usermode.h` / `kernel/usermode.c`: exported reusable `usermode_enter_ring3(entry, esp)` iret transition helper.
  - `kernel/console.c`: added new command `elftest` and updated `help` output.
  - `elftest` path loads embedded ELF and transfers to ELF entry in ring 3.
- Reference docs consulted from `docs/core/`:
  - `ELF.md`
  - `ELF_Tutorial.md`
  - `System_Calls.md`
- Verified:
  - `make -j4` builds cleanly with `-Wall -Wextra -Werror`.
  - ELF sample inspection (`i686-elf-readelf -h -l build/elf_demo.elf`) confirms valid 32-bit `ET_EXEC` image with one `PT_LOAD` segment and expected entry point.
  - QEMU serial smoke boot (`make run`) reaches stable init/scheduler/console path with no regressions.
  - Interactive `elftest` execution must be run in QEMU window to validate loader jump and user ELF runtime behavior.
  - Image sizing remains within cap (`kernel.bin` = 25408 bytes, `os.bin` = 65536 bytes; embedded `elf_demo.elf` = 4688 bytes).

## 2026-02-09 13:23:56 +0300 - Codebase Audit (Reviewer Pass 4, post-Task 23/24)
- Completed: Re-audited syscall (`write/exit/sbrk`) and embedded ELF loader paths with static review + runtime validation.
- Runtime validation performed:
  - `make -j4` successful with `-Wall -Wextra -Werror`.
  - Headless QEMU run with monitor `sendkey` automation for:
    - `ring3test` (observed syscall path + expected ring3 `#GP`)
    - `elftest` (observed ELF load, syscall activity, and expected ring3 `#GP`)
- Reference docs consulted from `docs/core/`:
  - `System_Calls.md`
  - `ELF.md`
  - `ELF_Tutorial.md`
  - `Paging.md`
  - `Detecting_Memory_(x86).md`
- Findings captured for follow-up:
  - ELF loader does not reconcile permissions for overlapping PT_LOAD pages (can leave writable data page read-only in user mode).
  - Process teardown still does not reclaim user mappings created by `sbrk`/ELF load (future memory/frame leakage risk).
  - Syscall user-pointer validation checks presence but not U/S access bit, leaving a future kernel-memory exposure hazard if supervisor mappings appear below split.
  - ELF loader uses a 4KB on-stack mapped-page scratch array while kernel process stacks are also 4KB, creating stack-overflow risk once loader is used from non-bootstrap process context.
  - Residual pre-existing boot risk remains: stage2 E820 first-call path still fails if BIOS returns a valid single-entry map (`EBX=0`).

## 2026-02-09 13:28:38 +0300 - Post-Task 24 Audit Hotfixes (#1 and #5)
- Completed: Fixed the two requested ELF loader issues from reviewer pass 4.
- Fix #1 (PT_LOAD permission merge for overlapping pages):
  - Added `paging_or_page_flags()` in:
    - `kernel/paging.h`
    - `kernel/paging.c`
  - `kernel/elf.c` now upgrades flags for already-loader-mapped overlap pages when a later segment requires writable access.
  - This prevents valid overlapping RX/RW ELF layouts from leaving data pages read-only.
- Fix #5 (ELF loader stack footprint):
  - Moved ELF mapped-page scratch list off stack:
    - replaced local `uint32_t mapped_pages[1024]` with file-scope loader scratch buffer.
  - Keeps loader within 4KB kernel-stack constraints when invoked outside bootstrap context.
- Deferred by request:
  - #2 and #3 are intentionally postponed and tagged for Task 29/fork+exec audit:
    - user mapping reclamation on process teardown
    - per-process `user_break` correctness relative to global CR3 sharing
- Explicitly not changed in this hotfix round:
  - #4 (U/S-bit pointer hardening) deferred.
  - #6 (E820 single-entry edge case) deferred.
- Verified:
  - `make -j4` builds cleanly with `-Wall -Wextra -Werror`.
  - QEMU serial smoke boot (`make run`) reaches stable init/scheduler/console path with no regressions.
  - Image sizing remains within cap (`kernel.bin` = 25504 bytes, `os.bin` = 65536 bytes).

## 2026-02-09 13:37:41 +0300 - Phase 6, Task 25: VFS Layer
- Completed: Added an initial VFS core layer with mount-point, vnode, and open-file abstractions.
- New files:
  - `kernel/vfs.h`
  - `kernel/vfs.c`
- VFS core capabilities implemented:
  - Vnode + ops interface (`lookup`, `read`, `write`) with node type metadata.
  - Path normalization and absolute-path resolution across a mount table (longest-prefix mount match).
  - Mount API (`vfs_mount`) with mount replacement on existing mount path.
  - Kernel file-handle API (`vfs_open`, `vfs_read`, `vfs_write`, `vfs_close`) with position tracking.
  - IRQ-safe internal state protection (mount table + open-file table) via spinlock helpers.
  - Default empty root mount at `/` installed during `vfs_init()`.
- Integration:
  - `kernel/kernel.c` now initializes VFS during boot (`vfs_init()`), with VGA/serial status output.
  - `Makefile` now compiles/links `kernel/vfs.c`.
- Reference docs consulted from `docs/core/`:
  - `VFS.md`
  - `Hierarchical_VFS_Theory.md`
  - `File_Systems.md`
- Verified:
  - `make -j4` builds cleanly with `-Wall -Wextra -Werror`.
  - `make run` reaches stable boot, including `[VFS] initialized`, and continues through scheduler + console initialization.

## 2026-02-09 13:46:28 +0300 - Phase 6, Task 26: initrd / Ramdisk (tar-based)
- Completed: Added a read-only tar-backed initrd filesystem and mounted it on VFS root (`/`) at boot.
- New files:
  - `kernel/initrd.h`
  - `kernel/initrd.c`
  - `initrd/hello.txt`
  - `initrd/etc/motd.txt`
- Implementation details:
  - Embedded initrd archive build pipeline in `Makefile`:
    - create `build/initrd.tar` from `initrd/` using `tar --format=ustar`
    - convert to linkable blob object (`build/initrd_blob.o`) via `objcopy`
    - link blob into kernel image
  - `kernel/initrd.c` implements:
    - USTAR header parsing (including octal size parsing and 512-byte block stepping)
    - in-memory initrd node table (directories + regular files)
    - hierarchical lookup via VFS vnode ops (`lookup`) and read-only file reads (`read`)
    - root mount via `vfs_mount("/")`
  - `kernel/kernel.c` now calls `initrd_init()` immediately after `vfs_init()`.
  - Added boot self-test: open/read `/hello.txt` through VFS and log the content to serial.
- Reference docs consulted:
  - `docs/core/Initrd.md`
  - `docs/library/USTAR.md`
  - `docs/library/Tar.md`
- Verified:
  - `make -j4` builds cleanly with `-Wall -Wextra -Werror`.
  - `make run` serial output confirms:
    - `[INITRD] mounted tar initrd entries=4`
    - `[INITRD] self-test /hello.txt: Hello from ClaudeOS initrd.`
  - Boot continues through scheduler and console initialization with no regressions.

## 2026-02-09 14:01:43 +0300 - Phase 6, Task 27: FAT32 Read Support via ATA PIO
- Completed: Added ATA PIO sector-read support and a read-only FAT32 filesystem driver mounted through VFS.
- New files:
  - `kernel/ata.h`
  - `kernel/ata.c`
  - `kernel/fat32.h`
  - `kernel/fat32.c`
  - `tools/mkfat32_image.py`
- Implementation details:
  - ATA PIO layer (`kernel/ata.c`):
    - Primary-bus IDENTIFY probing for master/slave drives.
    - 28-bit LBA PIO sector read path (`ata_pio_read28`) with BSY/DRQ polling, status checks, and 400ns delays.
    - IRQ-safe bus serialization using existing spinlock helpers.
  - FAT32 layer (`kernel/fat32.c`):
    - FAT32 BPB parsing and validation (boot sector at partition start).
    - Partition detection supporting both superfloppy FAT32 and MBR FAT32 partition types (`0x0B`, `0x0C`).
    - Directory traversal via 8.3 entries (LFN skipped), cluster-chain walking via FAT table, and file reads across cluster boundaries.
    - Mounted at `/fat` via VFS node ops.
  - Host-side deterministic FAT32 test image:
    - `tools/mkfat32_image.py` generates `build/fat32.img` (64MiB FAT32 superfloppy) with:
      - `/HELLO.TXT`
      - `/DOCS/INFO.TXT`
  - Build/run integration (`Makefile`):
    - Added ATA/FAT32 kernel objects.
    - Added FAT32 image build target.
    - `make run` now attaches the FAT32 image as a second IDE disk.
  - Minor I/O support extension:
    - `kernel/io.h` now provides `inw`/`outw` helpers for ATA PIO word transfers.
- Kernel integration:
  - `kernel/kernel.c` now runs `fat32_init()` after VFS/initrd setup and reports mount status on VGA.
- Reference docs consulted:
  - `docs/core/ATA_PIO_Mode.md`
  - `docs/core/ATA_read_write_sectors.md`
  - `docs/core/FAT.md`
  - `docs/core/FAT32.md`
- Verified:
  - `make -j4` builds cleanly with `-Wall -Wextra -Werror`.
  - `make run` confirms:
    - ATA probe (`primary master` + `primary slave`)
    - FAT32 mounted at `/fat`
    - Read self-tests passed:
      - `[FAT32] self-test /fat/HELLO.TXT: Hello from ClaudeOS FAT32 via ATA PIO.`
      - `[FAT32] self-test /fat/DOCS/INFO.TXT: Subdirectory read path works.`
  - Boot continues through scheduler + console path without regressions.
