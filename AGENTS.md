# ClaudeOS

A 32-bit x86 operating system.

## Overview

ClaudeOS is a project in LLM-driven systems programming. 

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
- **No external dependencies in kernel space.** No hosted libc, no POSIX, and no third-party libraries in the kernel. A userspace libc is planned in Phase 7.

## Milestone Roadmap

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
24. ELF loader (load simple static ELF binaries)

### Phase 6: Filesystem 
25. VFS layer
26. initrd / ramdisk with tar-based format
27. FAT32 read support via ATA PIO
28. Syscalls: open, close, read
29. fork + exec (at least basic versions)

### Phase 7: Shell & Userspace 
30. Basic libc (printf, malloc, string functions)
31. Shell as a userspace process
32. Builtins: ls, cat, echo, clear, help, ps
33. At least 2-3 standalone userspace programs
34. Polish, test, create demo script
