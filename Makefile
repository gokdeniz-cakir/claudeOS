# =============================================================================
# ClaudeOS Makefile - Phase 2
# =============================================================================

# --- Tools -------------------------------------------------------------------
NASM      := nasm
CC        := i686-elf-gcc
LD        := i686-elf-gcc
QEMU      := qemu-system-i386

# --- Directories -------------------------------------------------------------
BOOT_DIR   := boot
KERNEL_DIR := kernel
BUILD_DIR  := build

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
LINKER_SCRIPT  := linker.ld

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
KERNEL_BIN     := $(BUILD_DIR)/kernel.bin
OS_BIN         := $(BUILD_DIR)/os.bin

# --- Flags -------------------------------------------------------------------
NASMFLAGS_BIN  := -f bin
NASMFLAGS_ELF  := -f elf32
CFLAGS         := -std=c99 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
                  -nostdlib -m32 -O2 -g
LDFLAGS        := -T $(LINKER_SCRIPT) -nostdlib -lgcc -Wl,--oformat,binary
QEMUFLAGS      := -drive format=raw,file=$(OS_BIN) -no-reboot -no-shutdown \
                  -serial stdio

# --- Boot image limits -------------------------------------------------------
KERNEL_MAX_SECTORS := 124
KERNEL_MAX_BYTES   := 63488
OS_IMAGE_SIZE      := 65536

# --- Phony targets -----------------------------------------------------------
.PHONY: all run clean

# --- Default target ----------------------------------------------------------
all: $(OS_BIN)

# --- MBR (flat binary, 512 bytes) -------------------------------------------
$(MBR_BIN): $(MBR_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_BIN) -D KERNEL_MAX_SECTORS=$(KERNEL_MAX_SECTORS) -o $@ $<

# --- Stage 2 (flat binary, padded to 1024 bytes, 2 sectors) -----------------
$(STAGE2_BIN): $(STAGE2_SRC) | $(BUILD_DIR)
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

# --- Link kernel (flat binary at 0xC0100000, loaded at physical 0x100000) ---
KERNEL_OBJS := $(KENTRY_OBJ) $(KERNEL_OBJ) $(VGA_OBJ) $(SERIAL_OBJ) \
               $(IDT_OBJ) $(ISR_OBJ) $(ISR_STUBS_OBJ) \
               $(PIC_OBJ) $(IRQ_OBJ) $(IRQ_STUBS_OBJ) \
               $(PIT_OBJ) $(PMM_OBJ) $(PAGING_OBJ) $(HEAP_OBJ) \
               $(KEYBOARD_OBJ) $(CONSOLE_OBJ) $(PROCESS_OBJ) \
               $(PROCESS_STUBS_OBJ) $(TSS_OBJ) $(SPINLOCK_OBJ) $(SYNC_OBJ) \
               $(USERMODE_OBJ) $(SYSCALL_OBJ) $(SYSCALL_STUBS_OBJ)

$(KERNEL_BIN): $(KERNEL_OBJS) $(LINKER_SCRIPT) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

# --- Final disk image: MBR + Stage2 + Kernel, padded to 64KB ----------------
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
run: $(OS_BIN)
	$(QEMU) $(QEMUFLAGS)

# --- Clean -------------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR)
