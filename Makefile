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

# --- Phony targets -----------------------------------------------------------
.PHONY: all run clean

# --- Default target ----------------------------------------------------------
all: $(OS_BIN)

# --- MBR (flat binary, 512 bytes) -------------------------------------------
$(MBR_BIN): $(MBR_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_BIN) -o $@ $<

# --- Stage 2 (flat binary, padded to 1024 bytes, 2 sectors) -----------------
$(STAGE2_BIN): $(STAGE2_SRC) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS_BIN) -o $@ $<

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

# --- Link kernel (flat binary at 0xC0100000, loaded at physical 0x100000) ---
KERNEL_OBJS := $(KENTRY_OBJ) $(KERNEL_OBJ) $(VGA_OBJ) $(SERIAL_OBJ) \
               $(IDT_OBJ) $(ISR_OBJ) $(ISR_STUBS_OBJ) \
               $(PIC_OBJ) $(IRQ_OBJ) $(IRQ_STUBS_OBJ) \
               $(PIT_OBJ) $(PMM_OBJ)

$(KERNEL_BIN): $(KERNEL_OBJS) $(LINKER_SCRIPT) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

# --- Final disk image: MBR + Stage2 + Kernel, padded to 32KB ----------------
$(OS_BIN): $(MBR_BIN) $(STAGE2_BIN) $(KERNEL_BIN) | $(BUILD_DIR)
	cat $(MBR_BIN) $(STAGE2_BIN) $(KERNEL_BIN) > $@
	dd if=/dev/zero bs=1 count=$$((32768 - $$(wc -c < $@))) >> $@ 2>/dev/null || true

# --- Build directory ---------------------------------------------------------
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# --- Run in QEMU -------------------------------------------------------------
run: $(OS_BIN)
	$(QEMU) $(QEMUFLAGS)

# --- Clean -------------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR)
