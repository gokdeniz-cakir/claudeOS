# ClaudeOS

A 32-bit x86 operating system.

## Overview

ClaudeOS is a project in LLM-driven systems programming. 

## Current Project State

- Phases 1-9 are complete (boot, memory, interrupts, scheduling, user mode/syscalls, VFS/initrd/FAT32, userspace shell/apps, GUI stack, Doom port).
- Graphics infrastructure is already in-tree and active:
  - VBE linear framebuffer mode setup in stage2.
  - Linear framebuffer mapped into kernel virtual MMIO space.
  - Framebuffer backend with drawing primitives + double buffering.
  - Framebuffer text console path (not VGA-text-only anymore).
- Input/GUI infrastructure is already in-tree and active:
  - PS/2 keyboard (IRQ1) and PS/2 mouse (IRQ12).
  - Basic window manager with stacking, focus, title bars, dragging.
- Userspace/runtime infrastructure is already in-tree and active:
  - Ring 3 ELF loading + syscall layer.
  - Userspace libc, shell, standalone apps.
  - DoomGeneric userspace port with framebuffer presentation path.
- Agent planning baseline:
  - Do not treat this project as text-mode-only.
  - Build on existing framebuffer/mouse/window-manager/userspace subsystems unless explicitly refactoring them.

## Constraints

- **Agent role:** Debug based on reported errors and CRUCIALLY, consult reference docs frequently. Follow the milestone roadmap as the default plan. Small prerequisite refactors, tests, and tooling/documentation updates are allowed when they directly unblock the current milestone. Do not implement future-milestone features unless explicitly requested.
- **Notation:** Agent must write up the process and progress in the markdown file named `PROGRESS.md` located in the root directory of the project. This includes tasks completed, issues encountered, and any other relevant information, written in a clear and **concise** manner.

## Reference Library

The `docs/core/` directory contains 272 curated articles from the OSDev wiki covering all topics relevant to this project. Agents should consult these articles when implementing any component rather than relying solely on training data. The whole OSDev Wiki articles are also in the `docs/library/` directory if further investigation is needed, agents can also access every article in one file through the `docs/osdev-wiki.xml` XML file if they wish. Agent should prefer the more efficient approach it sees fit when looking for documentation.

## Architecture

- **Target:** i686 (32-bit x86)
- **Boot method:** BIOS, MBR boot sector
- **Kernel model:** Monolithic, higher-half kernel (mapped at 0xC0000000)
- **Language:** C (C99) with minimal assembly where hardware requires it (boot, context switch, interrupt stubs)
- **Userspace:** Ring 3 processes loaded from ELF binaries
- **Graphics:** VBE linear framebuffer path with kernel-side double-buffered rendering
- **Input:** PS/2 keyboard + PS/2 mouse interrupt-driven drivers

## Memory Layout

```
0x00000000 - 0x000FFFFF    Reserved (BIOS, VGA, etc.)
0x00100000 - 0xBFFFFFFF    User space
0xC0000000 - 0xFFFFFFFF    Kernel space (higher half)
  0xC0000000 - 0xC0FFFFFF  Kernel code and data (~16MB)
  0xC1000000 - 0xC1FFFFFF  Kernel heap
  0xD0000000 - 0xDFFFFFFF  Memory-mapped I/O region
  0xFFC00000 - 0xFFFFFFFF  Recursive page directory mapping
```



## Build Environment

- **Host:** macOS
- **Cross-compiler:** i686-elf-gcc, i686-elf-binutils
- **Assembler:** NASM
- **Emulator:** QEMU (qemu-system-i386)
- **Build system:** GNU Make

## Coding Conventions

- **C standard:** C99, `-Wall -Wextra -Werror -ffreestanding -fno-builtin`
- **Naming:** snake_case for functions and variables, UPPER_CASE for macros and constants
- **Types:** Use `uint8_t`, `uint16_t`, `uint32_t` from `<stdint.h>` — no raw `int` for hardware-facing code
- **Comments:** Block comments (`/* */`) for function documentation, inline comments (`//`) for non-obvious logic
- **Headers:** Every `.c` file has a corresponding `.h`, include guards with `#ifndef CLAUDE_COMPONENT_H`
- **Assembly:** NASM syntax (Intel), not GAS (AT&T)
- **No external dependencies in kernel space.** No hosted libc, no POSIX, and no third-party libraries in the kernel. Userspace libc is implemented and should continue evolving in userspace only.

## Milestone Roadmap

Phases 1-9 below are complete and define the current baseline system.

### Phase 1: Boot 
1. MBR bootloader that prints to screen in real mode - Completed: boot/mbr.asm, Makefile
2. A20 gate enable, GDT setup, switch to protected mode - Completed: boot/mbr.asm
3. Stage 2 loader, jump to C kernel entry - Completed: boot/stage2.asm, kernel/kernel_entry.asm, kernel/kernel.c, linker.ld
4. Kernel prints "ClaudeOS" via VGA text mode - Completed: kernel/vga.c, kernel/vga.h, kernel/io.h
5. Serial port output working (debug channel) - Completed: kernel/serial.c, kernel/serial.h

### Phase 2: Interrupts & Memory 
6. IDT setup, ISR stubs, exception handlers - Completed: kernel/idt.c, kernel/isr.c, kernel/isr_stubs.asm
7. PIC remapping, IRQ handling - Completed: kernel/pic.c, kernel/irq.c, kernel/irq_stubs.asm
8. PIT timer interrupt (system tick) - Completed: kernel/pit.c, kernel/pit.h, pic_clear_mask added
9. Physical memory manager (detect memory via BIOS E820, bitmap allocator) - Completed: boot/stage2.asm (E820), kernel/pmm.c, kernel/pmm.h
10. Paging enabled, higher-half kernel mapped - Completed: kernel/kernel_entry.asm, linker.ld, boot/stage2.asm
11. Kernel heap (kmalloc/kfree) Completed

### Phase 3: Keyboard & Console 
12. PS/2 keyboard driver (IRQ1, scancode → ASCII) - Completed: kernel/keyboard.c, kernel/keyboard.h
13. VGA text driver with scrolling, cursor - Completed: kernel/vga.c, kernel/vga.h
14. Basic kernel console (type and see output) - Completed: kernel/console.c, kernel/console.h, kernel/kernel.c

### Phase 4: Processes & Scheduling 
15. Process control blocks, kernel-mode processes - Completed: kernel/process.c, kernel/process.h, kernel/kernel.c
16. Context switching - Completed: kernel/process.c, kernel/process.h, kernel/process_stubs.asm
17. TSS setup - Completed: kernel/tss.c, kernel/tss.h, kernel/kernel_entry.asm, kernel/kernel.c
18. Preemptive round-robin scheduler (PIT-driven) - Completed: kernel/irq.c, kernel/process.c, kernel/process.h, kernel/kernel.c
19. Spinlocks, basic synchronization - Completed: kernel/spinlock.c, kernel/spinlock.h, kernel/sync.c, kernel/sync.h

### Phase 5: User Mode & Syscalls
20. Ring 0 → Ring 3 transition - Completed: kernel/kernel_entry.asm, kernel/usermode.c, kernel/usermode.h, kernel/console.c
21. TSS stack switching on syscall - Completed: kernel/process.c, kernel/process.h, kernel/kernel.c, kernel/usermode.c
22. INT 0x80 syscall interface - Completed: kernel/idt.c, kernel/idt.h, kernel/syscall.c, kernel/syscall.h, kernel/syscall_stubs.asm
23. Basic syscalls: write, exit, sbrk - Completed: kernel/syscall.c, kernel/syscall.h, kernel/process.c, kernel/process.h, kernel/usermode.c
24. ELF loader (load simple static ELF binaries) - Completed: kernel/elf.c, kernel/elf.h, user/elf_demo.asm, Makefile, kernel/console.c

### Phase 6: Filesystem 
25. VFS layer - Completed: kernel/vfs.c, kernel/vfs.h, kernel/kernel.c, Makefile
26. initrd / ramdisk with tar-based format - Completed: kernel/initrd.c, kernel/initrd.h, initrd/hello.txt, initrd/etc/motd.txt, kernel/kernel.c, Makefile
27. FAT32 read support via ATA PIO - Completed: kernel/ata.c, kernel/ata.h, kernel/fat32.c, kernel/fat32.h, kernel/io.h, kernel/kernel.c, Makefile, tools/mkfat32_image.py
28. Syscalls: open, close, read - Completed: kernel/syscall.c, kernel/syscall.h, user/elf_demo.asm
29. fork + exec (at least basic versions) - Completed: kernel/syscall.c, kernel/process.c, kernel/elf.c, kernel/vfs.c, user/fork_exec_demo.asm, Makefile

### Phase 7: Shell & Userspace 
30. Basic libc (printf, malloc, string functions) - Completed: user/libc/*, user/libctest.c, kernel/console.c, kernel/elf.c, Makefile
31. Shell as a userspace process - Completed: user/shell.c, user/libc/*, kernel/console.c, kernel/elf.c, kernel/elf.h, Makefile
32. Builtins: ls, cat, echo, clear, help, ps - Completed: user/shell.c, user/libc/syscall.c, user/libc/include/unistd.h, kernel/syscall.c, kernel/syscall.h
33. At least 2-3 standalone userspace programs - Completed: user/uhello.asm, user/ucat.asm, user/uexec.asm, kernel/console.c, kernel/elf.c, kernel/elf.h, user/shell.c, Makefile
34. Polish, test, create demo script - Completed: tools/run_task34_demo.sh, Makefile, PROGRESS.md, AGENTS.md

### Phase 8: GUI

35. VBE framebuffer setup (real-mode VBE call in stage2, linear framebuffer mapped into kernel space) - Completed: boot/stage2.asm, boot/mbr.asm, kernel/vbe.c, kernel/vbe.h, kernel/kernel.c, Makefile
36. Graphics primitives + framebuffer console (pixel ops, bitmap font, double buffering) - Completed: kernel/fb.c, kernel/fb.h, kernel/vga.c, kernel/kernel.c, Makefile
37. PS/2 mouse driver (IRQ12) - Completed: kernel/mouse.c, kernel/mouse.h, kernel/kernel.c, Makefile
38. Basic window manager (stacking windows, event dispatch, title bars, dragging) - Completed: kernel/wm.c, kernel/wm.h, kernel/fb.c, kernel/fb.h, kernel/console.c, kernel/console.h, kernel/kernel.c, Makefile

### Phase 9: DOOM

39. Expand libc (sprintf, fopen/fread/fclose, full malloc, and whatever else DOOM needs) - Completed: user/libc/include/stdio.h, user/libc/include/stdlib.h, user/libc/include/string.h, user/libc/stdio.c, user/libc/malloc.c, user/libc/string.c, user/libctest.c, kernel/syscall.c
40. Large userspace memory support (ensure VMM can give a process 16MB+ heap) - Completed: kernel/process.c, kernel/elf.c, user/libctest.c
41. Port doomgeneric (implement the ~5 platform functions, patch DOOM source, compile as ELF) - Completed: user/doomgeneric/*, user/libc/*, Makefile, boot/mbr.asm, boot/stage2.asm
42. Polish (sound stub, FPS timing, input mapping, demo) - Completed: user/doomgeneric/doomgeneric_claudeos.c, user/doomgeneric/i_sound.c, kernel/syscall.c, kernel/keyboard.c, kernel/console.c, kernel/elf.c, tools/mkfat32_image.py, tools/run_task42_demo.sh, Makefile
