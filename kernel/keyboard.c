#include "keyboard.h"

#include <stdint.h>

#include "io.h"
#include "irq.h"
#include "pic.h"
#include "serial.h"

#define PS2_DATA_PORT           0x60
#define PS2_STATUS_PORT         0x64
#define PS2_COMMAND_PORT        0x64

#define PS2_STATUS_OUTPUT_FULL  0x01
#define PS2_STATUS_INPUT_FULL   0x02

#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_PORT1   0xAD
#define PS2_CMD_DISABLE_PORT2   0xA7
#define PS2_CMD_ENABLE_PORT1    0xAE

#define PS2_KBD_ENABLE_SCANNING 0xF4
#define PS2_KBD_ACK             0xFA

#define PS2_TIMEOUT_LOOPS       100000u

#define KBD_BUFFER_SIZE         128u
#define KBD_BUFFER_MASK         (KBD_BUFFER_SIZE - 1u)

/* Scan code set 1, US QWERTY base map (no modifiers). */
static const char scancode_set1[128] = {
    [0x01] = 0x1B, [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9',
    [0x0B] = '0', [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n', [0x1E] = 'a', [0x1F] = 's',
    [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j',
    [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.',
    [0x35] = '/', [0x37] = '*', [0x39] = ' ', [0x47] = '7', [0x48] = '8',
    [0x49] = '9', [0x4A] = '-', [0x4B] = '4', [0x4C] = '5', [0x4D] = '6',
    [0x4E] = '+', [0x4F] = '1', [0x50] = '2', [0x51] = '3', [0x52] = '0',
    [0x53] = '.'
};

/* Shifted symbols for scan code set 1, US QWERTY. */
static const char scancode_set1_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+', [0x1A] = '{', [0x1B] = '}', [0x27] = ':',
    [0x28] = '"', [0x29] = '~', [0x2B] = '|', [0x33] = '<', [0x34] = '>',
    [0x35] = '?'
};

static volatile uint8_t keyboard_head = 0;
static volatile uint8_t keyboard_tail = 0;
static char keyboard_buffer[KBD_BUFFER_SIZE];

static uint8_t keyboard_left_shift = 0;
static uint8_t keyboard_right_shift = 0;
static uint8_t keyboard_caps_lock = 0;
static uint8_t keyboard_extended_prefix = 0;

static inline uint32_t irq_save_disable(void)
{
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint32_t flags)
{
    __asm__ volatile ("push %0; popf" : : "r"(flags) : "memory", "cc");
}

static int ps2_wait_for_write(void)
{
    uint32_t i;
    for (i = 0; i < PS2_TIMEOUT_LOOPS; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0) {
            return 0;
        }
    }
    return -1;
}

static int ps2_wait_for_read(void)
{
    uint32_t i;
    for (i = 0; i < PS2_TIMEOUT_LOOPS; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) {
            return 0;
        }
    }
    return -1;
}

static int ps2_write_command(uint8_t cmd)
{
    if (ps2_wait_for_write() != 0) {
        return -1;
    }

    outb(PS2_COMMAND_PORT, cmd);
    return 0;
}

static int ps2_write_data(uint8_t data)
{
    if (ps2_wait_for_write() != 0) {
        return -1;
    }

    outb(PS2_DATA_PORT, data);
    return 0;
}

static int ps2_read_data(uint8_t *data)
{
    if (ps2_wait_for_read() != 0) {
        return -1;
    }

    *data = inb(PS2_DATA_PORT);
    return 0;
}

static void ps2_flush_output_buffer(void)
{
    uint32_t budget = PS2_TIMEOUT_LOOPS;
    while (((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) &&
           (budget != 0u)) {
        (void)inb(PS2_DATA_PORT);
        budget--;
    }
}

static void keyboard_buffer_push(char c)
{
    uint8_t head = keyboard_head;
    uint8_t next = (uint8_t)((head + 1u) & KBD_BUFFER_MASK);

    if (next == keyboard_tail) {
        return; /* Drop keypress when ring buffer is full. */
    }

    keyboard_buffer[head] = c;
    keyboard_head = next;
}

static char keyboard_translate_scancode(uint8_t scancode)
{
    char c = scancode_set1[scancode];
    uint8_t shift = (uint8_t)((keyboard_left_shift != 0u) ||
                              (keyboard_right_shift != 0u));

    if (c == 0) {
        return 0;
    }

    if (c >= 'a' && c <= 'z') {
        if ((keyboard_caps_lock ^ shift) != 0u) {
            return (char)(c - ('a' - 'A'));
        }
        return c;
    }

    if (shift != 0u && scancode_set1_shift[scancode] != 0) {
        return scancode_set1_shift[scancode];
    }

    return c;
}

static void keyboard_irq_handler(struct isr_regs *regs)
{
    uint8_t scancode;
    uint8_t released;
    uint8_t keycode;
    char ascii;

    (void)regs;

    scancode = inb(PS2_DATA_PORT);

    if (scancode == 0xE0) {
        keyboard_extended_prefix = 1;
        return;
    }

    released = (uint8_t)(scancode & 0x80);
    keycode = (uint8_t)(scancode & 0x7F);

    if (keyboard_extended_prefix != 0u) {
        keyboard_extended_prefix = 0;

        if (released == 0u) {
            if (keycode == 0x1C) {
                keyboard_buffer_push('\n');   /* keypad Enter */
            } else if (keycode == 0x35) {
                keyboard_buffer_push('/');    /* keypad Slash */
            }
        }
        return;
    }

    if (keycode == 0x2A) {
        keyboard_left_shift = (uint8_t)(released == 0u);
        return;
    }
    if (keycode == 0x36) {
        keyboard_right_shift = (uint8_t)(released == 0u);
        return;
    }
    if (keycode == 0x3A && released == 0u) {
        keyboard_caps_lock ^= 1u;
        return;
    }

    if (released != 0u) {
        return;
    }

    ascii = keyboard_translate_scancode(keycode);
    if (ascii != 0) {
        keyboard_buffer_push(ascii);
    }
}

void keyboard_init(void)
{
    uint8_t config = 0;
    uint8_t response = 0;

    keyboard_head = 0;
    keyboard_tail = 0;
    keyboard_left_shift = 0;
    keyboard_right_shift = 0;
    keyboard_caps_lock = 0;
    keyboard_extended_prefix = 0;

    (void)ps2_write_command(PS2_CMD_DISABLE_PORT1);
    (void)ps2_write_command(PS2_CMD_DISABLE_PORT2);
    ps2_flush_output_buffer();

    if (ps2_write_command(PS2_CMD_READ_CONFIG) == 0 &&
        ps2_read_data(&config) == 0) {
        config |= 0x01u;                    /* Enable IRQ1. */
        config &= (uint8_t)~0x10u;          /* Enable first port clock. */
        config |= 0x40u;                    /* Enable translation to set 1. */

        if (ps2_write_command(PS2_CMD_WRITE_CONFIG) == 0) {
            (void)ps2_write_data(config);
        }
    } else {
        serial_puts("[KBD] Failed to read controller config\n");
    }

    (void)ps2_write_command(PS2_CMD_ENABLE_PORT1);

    if (ps2_write_data(PS2_KBD_ENABLE_SCANNING) == 0 &&
        ps2_read_data(&response) == 0 &&
        response != PS2_KBD_ACK) {
        serial_puts("[KBD] Non-ACK response to 0xF4\n");
    }

    irq_register_handler(1, keyboard_irq_handler);
    pic_clear_mask(1);

    serial_puts("[KBD] PS/2 keyboard initialized (IRQ1, set1->ASCII)\n");
}

int keyboard_read_char(char *out_char)
{
    uint8_t tail;
    uint32_t irq_flags;

    if (out_char == 0) {
        return 0;
    }

    /* Protect head/tail ring-buffer operations from concurrent IRQ1 updates. */
    irq_flags = irq_save_disable();

    tail = keyboard_tail;
    if (tail == keyboard_head) {
        irq_restore(irq_flags);
        return 0;
    }

    *out_char = keyboard_buffer[tail];
    keyboard_tail = (uint8_t)((tail + 1u) & KBD_BUFFER_MASK);
    irq_restore(irq_flags);
    return 1;
}
