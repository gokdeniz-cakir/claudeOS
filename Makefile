# =============================================================================
# ClaudeOS Makefile - Phase 2
# =============================================================================

# --- Tools -------------------------------------------------------------------
NASM      := nasm
CC        := i686-elf-gcc
LD        := i686-elf-gcc
LD_BIN    := i686-elf-ld
OBJCOPY   := i686-elf-objcopy
QEMU      := qemu-system-i386

# --- Directories -------------------------------------------------------------
BOOT_DIR   := boot
KERNEL_DIR := kernel
USER_DIR   := user
BUILD_DIR  := build
USER_LIBC_DIR := $(USER_DIR)/libc
USER_LIBC_INCLUDE_DIR := $(USER_LIBC_DIR)/include

# --- Source files ------------------------------------------------------------
MBR_SRC        := $(BOOT_DIR)/mbr.asm
STAGE2_SRC     := $(BOOT_DIR)/stage2.asm
KENTRY_SRC     := $(KERNEL_DIR)/kernel_entry.asm
KERNEL_SRC     := $(KERNEL_DIR)/kernel.c
VGA_SRC        := $(KERNEL_DIR)/vga.c
SERIAL_SRC     := $(KERNEL_DIR)/serial.c
IDT_SRC        := $(KERNEL_DIR)/idt.c
ISR_SRC        := $(KERNEL_DIR)/isr.c
ISR_STUBS_SRC  := $(KERNEL_DIR)/isr_stubs.asm
PIC_SRC        := $(KERNEL_DIR)/pic.c
IRQ_SRC        := $(KERNEL_DIR)/irq.c
IRQ_STUBS_SRC  := $(KERNEL_DIR)/irq_stubs.asm
PIT_SRC        := $(KERNEL_DIR)/pit.c
PMM_SRC        := $(KERNEL_DIR)/pmm.c
PAGING_SRC     := $(KERNEL_DIR)/paging.c
HEAP_SRC       := $(KERNEL_DIR)/heap.c
KEYBOARD_SRC   := $(KERNEL_DIR)/keyboard.c
CONSOLE_SRC    := $(KERNEL_DIR)/console.c
PROCESS_SRC    := $(KERNEL_DIR)/process.c
PROCESS_STUBS_SRC := $(KERNEL_DIR)/process_stubs.asm
TSS_SRC        := $(KERNEL_DIR)/tss.c
SPINLOCK_SRC   := $(KERNEL_DIR)/spinlock.c
SYNC_SRC       := $(KERNEL_DIR)/sync.c
USERMODE_SRC   := $(KERNEL_DIR)/usermode.c
SYSCALL_SRC    := $(KERNEL_DIR)/syscall.c
SYSCALL_STUBS_SRC := $(KERNEL_DIR)/syscall_stubs.asm
ELF_SRC        := $(KERNEL_DIR)/elf.c
VFS_SRC        := $(KERNEL_DIR)/vfs.c
INITRD_SRC     := $(KERNEL_DIR)/initrd.c
ATA_SRC        := $(KERNEL_DIR)/ata.c
FAT32_SRC      := $(KERNEL_DIR)/fat32.c
ELF_DEMO_SRC   := $(USER_DIR)/elf_demo.asm
FORK_EXEC_DEMO_SRC := $(USER_DIR)/fork_exec_demo.asm
LIBCTEST_SRC   := $(USER_DIR)/libctest.c
SHELL_SRC      := $(USER_DIR)/shell.c
UHELLO_SRC     := $(USER_DIR)/uhello.asm
UCAT_SRC       := $(USER_DIR)/ucat.asm
UEXEC_SRC      := $(USER_DIR)/uexec.asm
LIBC_CRT0_SRC  := $(USER_LIBC_DIR)/crt0.asm
LIBC_SYSCALL_SRC := $(USER_LIBC_DIR)/syscall.c
LIBC_STDIO_SRC := $(USER_LIBC_DIR)/stdio.c
LIBC_STRING_SRC := $(USER_LIBC_DIR)/string.c
LIBC_MALLOC_SRC := $(USER_LIBC_DIR)/malloc.c
LINKER_SCRIPT  := linker.ld
INITRD_DIR     := initrd
INITRD_INPUTS  := $(shell find $(INITRD_DIR) -type f -o -type d 2>/dev/null)
TOOLS_DIR      := tools
FAT32_IMG_TOOL := $(TOOLS_DIR)/mkfat32_image.py

# --- Build outputs -----------------------------------------------------------
MBR_BIN        := $(BUILD_DIR)/mbr.bin
STAGE2_BIN     := $(BUILD_DIR)/stage2.bin
KENTRY_OBJ     := $(BUILD_DIR)/kernel_entry.o
KERNEL_OBJ     := $(BUILD_DIR)/kernel.o
VGA_OBJ        := $(BUILD_DIR)/vga.o
SERIAL_OBJ     := $(BUILD_DIR)/serial.o
IDT_OBJ        := $(BUILD_DIR)/idt.o
ISR_OBJ        := $(BUILD_DIR)/isr.o
ISR_STUBS_OBJ  := $(BUILD_DIR)/isr_stubs.o
PIC_OBJ        := $(BUILD_DIR)/pic.o
IRQ_OBJ        := $(BUILD_DIR)/irq.o
IRQ_STUBS_OBJ  := $(BUILD_DIR)/irq_stubs.o
PIT_OBJ        := $(BUILD_DIR)/pit.o
PMM_OBJ        := $(BUILD_DIR)/pmm.o
PAGING_OBJ     := $(BUILD_DIR)/paging.o
HEAP_OBJ       := $(BUILD_DIR)/heap.o
KEYBOARD_OBJ   := $(BUILD_DIR)/keyboard.o
CONSOLE_OBJ    := $(BUILD_DIR)/console.o
PROCESS_OBJ    := $(BUILD_DIR)/process.o
PROCESS_STUBS_OBJ := $(BUILD_DIR)/process_stubs.o
TSS_OBJ        := $(BUILD_DIR)/tss.o
SPINLOCK_OBJ   := $(BUILD_DIR)/spinlock.o
SYNC_OBJ       := $(BUILD_DIR)/sync.o
USERMODE_OBJ   := $(BUILD_DIR)/usermode.o
SYSCALL_OBJ    := $(BUILD_DIR)/syscall.o
SYSCALL_STUBS_OBJ := $(BUILD_DIR)/syscall_stubs.o
ELF_OBJ        := $(BUILD_DIR)/elf.o
VFS_OBJ        := $(BUILD_DIR)/vfs.o
INITRD_OBJ     := $(BUILD_DIR)/initrd.o
ATA_OBJ        := $(BUILD_DIR)/ata.o
FAT32_OBJ      := $(BUILD_DIR)/fat32.o
ELF_DEMO_OBJ   := $(BUILD_DIR)/elf_demo.o
ELF_DEMO_ELF   := $(BUILD_DIR)/elf_demo.elf
ELF_DEMO_BLOB_OBJ := $(BUILD_DIR)/elf_demo_blob.o
FORK_EXEC_DEMO_OBJ := $(BUILD_DIR)/fork_exec_demo.o
FORK_EXEC_DEMO_ELF := $(BUILD_DIR)/fork_exec_demo.elf
FORK_EXEC_DEMO_BLOB_OBJ := $(BUILD_DIR)/fork_exec_demo_blob.o
LIBCTEST_OBJ   := $(BUILD_DIR)/libctest.o
LIBC_CRT0_OBJ  := $(BUILD_DIR)/libc_crt0.o
LIBC_SYSCALL_OBJ := $(BUILD_DIR)/libc_syscall.o
LIBC_STDIO_OBJ := $(BUILD_DIR)/libc_stdio.o
LIBC_STRING_OBJ := $(BUILD_DIR)/libc_string.o
LIBC_MALLOC_OBJ := $(BUILD_DIR)/libc_malloc.o
LIBCTEST_ELF   := $(BUILD_DIR)/libctest.elf
SHELL_OBJ      := $(BUILD_DIR)/shell.o
SHELL_ELF      := $(BUILD_DIR)/shell.elf
UHELLO_OBJ     := $(BUILD_DIR)/uhello.o
UHELLO_ELF     := $(BUILD_DIR)/uhello.elf
UCAT_OBJ       := $(BUILD_DIR)/ucat.o
UCAT_ELF       := $(BUILD_DIR)/ucat.elf
UEXEC_OBJ      := $(BUILD_DIR)/uexec.o
UEXEC_ELF      := $(BUILD_DIR)/uexec.elf
INITRD_ROOT    := $(BUILD_DIR)/initrd_root
INITRD_ROOT_STAMP := $(BUILD_DIR)/initrd_root.stamp
INITRD_TAR     := $(BUILD_DIR)/initrd.tar
INITRD_BLOB_OBJ := $(BUILD_DIR)/initrd_blob.o
FAT32_IMG      := $(BUILD_DIR)/fat32.img
KERNEL_BIN     := $(BUILD_DIR)/kernel.bin
OS_BIN         := $(BUILD_DIR)/os.bin

# --- Flags -------------------------------------------------------------------
NASMFLAGS_BIN  := -f bin
NASMFLAGS_ELF  := -f elf32
CFLAGS         := -std=c99 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
                  -nostdlib -m32 -O2 -g
USER_CFLAGS    := $(filter-out -g,$(CFLAGS)) -I$(USER_LIBC_INCLUDE_DIR)
LDFLAGS        := -T $(LINKER_SCRIPT) -nostdlib -lgcc -Wl,--oformat,binary
QEMUFLAGS      := -drive format=raw,file=$(OS_BIN) \
                  -drive format=raw,file=$(FAT32_IMG),if=ide,index=1 \
                  -no-reboot -no-shutdown -serial stdio

# --- Boot image limits -------------------------------------------------------
KERNEL_MAX_SECTORS := 190
KERNEL_MAX_BYTES   := $(shell echo $$(( $(KERNEL_MAX_SECTORS) * 512 )))
OS_IMAGE_SIZE      := 262144

# --- Phony targets -----------------------------------------------------------
.PHONY: all run clean

# --- Default target ----------------------------------------------------------
all: $(OS_BIN)

# --- MBR (flat binary, 512 bytes) -------------------------------------------
$(MBR_BIN): $(MBR_SRC) Makefile | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_BIN) -D KERNEL_MAX_SECTORS=$(KERNEL_MAX_SECTORS) -o $@ $<

# --- Stage 2 (flat binary, padded to 1024 bytes, 2 sectors) -----------------
$(STAGE2_BIN): $(STAGE2_SRC) Makefile | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_BIN) -D KERNEL_MAX_SECTORS=$(KERNEL_MAX_SECTORS) -o $@ $<

# --- Kernel entry (ELF object) ----------------------------------------------
$(KENTRY_OBJ): $(KENTRY_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_ELF) -o $@ $<

# --- Kernel C source (ELF object) -------------------------------------------
$(KERNEL_OBJ): $(KERNEL_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- VGA driver (ELF object) ------------------------------------------------
$(VGA_OBJ): $(VGA_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Serial driver (ELF object) ---------------------------------------------
$(SERIAL_OBJ): $(SERIAL_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- IDT (ELF object) ------------------------------------------------------
$(IDT_OBJ): $(IDT_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- ISR handler (ELF object) -----------------------------------------------
$(ISR_OBJ): $(ISR_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- ISR stubs (ELF object from NASM) ---------------------------------------
$(ISR_STUBS_OBJ): $(ISR_STUBS_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_ELF) -o $@ $<

# --- PIC driver (ELF object) ------------------------------------------------
$(PIC_OBJ): $(PIC_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- IRQ handler (ELF object) -----------------------------------------------
$(IRQ_OBJ): $(IRQ_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- IRQ stubs (ELF object from NASM) ---------------------------------------
$(IRQ_STUBS_OBJ): $(IRQ_STUBS_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_ELF) -o $@ $<

# --- PIT driver (ELF object) ------------------------------------------------
$(PIT_OBJ): $(PIT_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- PMM driver (ELF object) ------------------------------------------------
$(PMM_OBJ): $(PMM_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Paging helpers (ELF object) --------------------------------------------
$(PAGING_OBJ): $(PAGING_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Kernel heap allocator (ELF object) -------------------------------------
$(HEAP_OBJ): $(HEAP_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- PS/2 keyboard driver (ELF object) --------------------------------------
$(KEYBOARD_OBJ): $(KEYBOARD_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Basic kernel console (ELF object) --------------------------------------
$(CONSOLE_OBJ): $(CONSOLE_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Process subsystem (ELF object) -----------------------------------------
$(PROCESS_OBJ): $(PROCESS_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Process stack helper stubs (ELF object from NASM) ----------------------
$(PROCESS_STUBS_OBJ): $(PROCESS_STUBS_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_ELF) -o $@ $<

# --- Task State Segment setup (ELF object) -----------------------------------
$(TSS_OBJ): $(TSS_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Spinlock primitives (ELF object) ----------------------------------------
$(SPINLOCK_OBJ): $(SPINLOCK_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Basic synchronization primitives (ELF object) ---------------------------
$(SYNC_OBJ): $(SYNC_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- User mode transition helpers (ELF object) -------------------------------
$(USERMODE_OBJ): $(USERMODE_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Syscall interface (ELF object) ------------------------------------------
$(SYSCALL_OBJ): $(SYSCALL_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Syscall stubs (ELF object from NASM) ------------------------------------
$(SYSCALL_STUBS_OBJ): $(SYSCALL_STUBS_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_ELF) -o $@ $<

# --- ELF loader (ELF object) -------------------------------------------------
$(ELF_OBJ): $(ELF_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- VFS core layer (ELF object) ---------------------------------------------
$(VFS_OBJ): $(VFS_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- initrd tarfs layer (ELF object) -----------------------------------------
$(INITRD_OBJ): $(INITRD_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- ATA PIO driver (ELF object) ---------------------------------------------
$(ATA_OBJ): $(ATA_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- FAT32 reader (ELF object) -----------------------------------------------
$(FAT32_OBJ): $(FAT32_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Embedded user ELF demo build chain --------------------------------------
$(ELF_DEMO_OBJ): $(ELF_DEMO_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_ELF) -o $@ $<

$(ELF_DEMO_ELF): $(ELF_DEMO_OBJ) | $(BUILD_DIR)
	$(LD_BIN) -m elf_i386 -nostdlib -s -Ttext 0x08048000 -e _start -o $@ $<

$(ELF_DEMO_BLOB_OBJ): $(ELF_DEMO_ELF) | $(BUILD_DIR)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

$(FORK_EXEC_DEMO_OBJ): $(FORK_EXEC_DEMO_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_ELF) -o $@ $<

$(FORK_EXEC_DEMO_ELF): $(FORK_EXEC_DEMO_OBJ) | $(BUILD_DIR)
	$(LD_BIN) -m elf_i386 -nostdlib -s -Ttext 0x0804C000 -e _start -o $@ $<

$(FORK_EXEC_DEMO_BLOB_OBJ): $(FORK_EXEC_DEMO_ELF) | $(BUILD_DIR)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

# --- Userspace libc smoke-test ELF build chain --------------------------------
$(LIBC_CRT0_OBJ): $(LIBC_CRT0_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_ELF) -o $@ $<

$(LIBC_SYSCALL_OBJ): $(LIBC_SYSCALL_SRC) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(LIBC_STDIO_OBJ): $(LIBC_STDIO_SRC) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(LIBC_STRING_OBJ): $(LIBC_STRING_SRC) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(LIBC_MALLOC_OBJ): $(LIBC_MALLOC_SRC) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(LIBCTEST_OBJ): $(LIBCTEST_SRC) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(SHELL_OBJ): $(SHELL_SRC) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

USER_LIBC_OBJS := $(LIBC_CRT0_OBJ) $(LIBC_SYSCALL_OBJ) $(LIBC_STDIO_OBJ) \
                  $(LIBC_STRING_OBJ) $(LIBC_MALLOC_OBJ)

$(LIBCTEST_ELF): $(USER_LIBC_OBJS) $(LIBCTEST_OBJ) | $(BUILD_DIR)
	$(LD_BIN) -m elf_i386 -nostdlib -s -Ttext 0x08050000 -e _start -o $@ \
		$(USER_LIBC_OBJS) $(LIBCTEST_OBJ)

$(SHELL_ELF): $(USER_LIBC_OBJS) $(SHELL_OBJ) | $(BUILD_DIR)
	$(LD_BIN) -m elf_i386 -nostdlib -s -Ttext 0x08054000 -e _start -o $@ \
		$(USER_LIBC_OBJS) $(SHELL_OBJ)

$(UHELLO_OBJ): $(UHELLO_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_ELF) -o $@ $<

$(UHELLO_ELF): $(UHELLO_OBJ) | $(BUILD_DIR)
	$(LD_BIN) -m elf_i386 -nostdlib -s -Ttext 0x08058000 -e _start -o $@ $<

$(UCAT_OBJ): $(UCAT_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_ELF) -o $@ $<

$(UCAT_ELF): $(UCAT_OBJ) | $(BUILD_DIR)
	$(LD_BIN) -m elf_i386 -nostdlib -s -Ttext 0x0805A000 -e _start -o $@ $<

$(UEXEC_OBJ): $(UEXEC_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_ELF) -o $@ $<

$(UEXEC_ELF): $(UEXEC_OBJ) | $(BUILD_DIR)
	$(LD_BIN) -m elf_i386 -nostdlib -s -Ttext 0x0805C000 -e _start -o $@ $<

# --- Embedded initrd tar image build chain -----------------------------------
$(INITRD_ROOT_STAMP): $(INITRD_INPUTS) $(ELF_DEMO_ELF) $(LIBCTEST_ELF) \
	$(SHELL_ELF) $(UHELLO_ELF) $(UCAT_ELF) $(UEXEC_ELF) | $(BUILD_DIR)
	rm -rf $(INITRD_ROOT)
	mkdir -p $(INITRD_ROOT)
	cp -R $(INITRD_DIR)/. $(INITRD_ROOT)/
	cp $(ELF_DEMO_ELF) $(INITRD_ROOT)/elf_demo.elf
	cp $(LIBCTEST_ELF) $(INITRD_ROOT)/libctest.elf
	cp $(SHELL_ELF) $(INITRD_ROOT)/shell.elf
	cp $(UHELLO_ELF) $(INITRD_ROOT)/uhello.elf
	cp $(UCAT_ELF) $(INITRD_ROOT)/ucat.elf
	cp $(UEXEC_ELF) $(INITRD_ROOT)/uexec.elf
	touch $@

$(INITRD_TAR): $(INITRD_ROOT_STAMP) | $(BUILD_DIR)
	tar --format=ustar -cf $@ -C $(INITRD_ROOT) .

$(INITRD_BLOB_OBJ): $(INITRD_TAR) | $(BUILD_DIR)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

# --- Secondary FAT32 test image for ATA PIO/FAT32 bring-up -------------------
$(FAT32_IMG): $(FAT32_IMG_TOOL) | $(BUILD_DIR)
	python3 $(FAT32_IMG_TOOL) $@

# --- Link kernel (flat binary at 0xC0100000, loaded at physical 0x100000) ---
KERNEL_OBJS := $(KENTRY_OBJ) $(KERNEL_OBJ) $(VGA_OBJ) $(SERIAL_OBJ) \
               $(IDT_OBJ) $(ISR_OBJ) $(ISR_STUBS_OBJ) \
               $(PIC_OBJ) $(IRQ_OBJ) $(IRQ_STUBS_OBJ) \
               $(PIT_OBJ) $(PMM_OBJ) $(PAGING_OBJ) $(HEAP_OBJ) \
               $(KEYBOARD_OBJ) $(CONSOLE_OBJ) $(PROCESS_OBJ) \
               $(PROCESS_STUBS_OBJ) $(TSS_OBJ) $(SPINLOCK_OBJ) $(SYNC_OBJ) \
               $(USERMODE_OBJ) $(SYSCALL_OBJ) $(SYSCALL_STUBS_OBJ) \
               $(ELF_OBJ) $(VFS_OBJ) $(INITRD_OBJ) $(ATA_OBJ) $(FAT32_OBJ) \
               $(ELF_DEMO_BLOB_OBJ) $(FORK_EXEC_DEMO_BLOB_OBJ) $(INITRD_BLOB_OBJ)

$(KERNEL_BIN): $(KERNEL_OBJS) $(LINKER_SCRIPT) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

# --- Final disk image: MBR + Stage2 + Kernel, padded to fixed image size -----
$(OS_BIN): $(MBR_BIN) $(STAGE2_BIN) $(KERNEL_BIN) | $(BUILD_DIR)
	@kernel_size=$$(wc -c < $(KERNEL_BIN)); \
	if [ $$kernel_size -gt $(KERNEL_MAX_BYTES) ]; then \
		echo "ERROR: kernel.bin is $$kernel_size bytes; max supported is $(KERNEL_MAX_BYTES)."; \
		echo "Increase KERNEL_MAX_SECTORS and bootloader copy/read constants together."; \
		exit 1; \
	fi
	cat $(MBR_BIN) $(STAGE2_BIN) $(KERNEL_BIN) > $@
	@image_size=$$(wc -c < $@); \
	if [ $$image_size -gt $(OS_IMAGE_SIZE) ]; then \
		echo "ERROR: os.bin is $$image_size bytes; exceeds $(OS_IMAGE_SIZE)-byte image limit."; \
		exit 1; \
	fi
	@pad_bytes=$$(( $(OS_IMAGE_SIZE) - $$(wc -c < $@) )); \
	dd if=/dev/zero bs=1 count=$$pad_bytes >> $@ 2>/dev/null

# --- Build directory ---------------------------------------------------------
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# --- Run in QEMU -------------------------------------------------------------
run: $(OS_BIN) $(FAT32_IMG)
	$(QEMU) $(QEMUFLAGS)

# --- Clean -------------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR)
