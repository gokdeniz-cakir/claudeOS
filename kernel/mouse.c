#include "mouse.h"

#include <stdint.h>

#include "io.h"
#include "irq.h"
#include "pic.h"
#include "serial.h"
#include "spinlock.h"

#define PS2_DATA_PORT                   0x60
#define PS2_STATUS_PORT                 0x64
#define PS2_COMMAND_PORT                0x64

#define PS2_STATUS_OUTPUT_FULL          0x01
#define PS2_STATUS_INPUT_FULL           0x02
#define PS2_STATUS_AUX_DATA             0x20

#define PS2_CMD_READ_CONFIG             0x20
#define PS2_CMD_WRITE_CONFIG            0x60
#define PS2_CMD_ENABLE_PORT2            0xA8
#define PS2_CMD_DISABLE_PORT2           0xA7
#define PS2_CMD_WRITE_PORT2_INPUT       0xD4

#define PS2_MOUSE_SET_DEFAULTS          0xF6
#define PS2_MOUSE_ENABLE_DATA_REPORTING 0xF4
#define PS2_MOUSE_ACK                   0xFA

#define PS2_TIMEOUT_LOOPS               100000u

#define MOUSE_EVENT_BUFFER_SIZE         32u
#define MOUSE_EVENT_BUFFER_MASK         (MOUSE_EVENT_BUFFER_SIZE - 1u)

#define PS2_MOUSE_PACKET_SYNC_BIT       0x08
#define PS2_MOUSE_PACKET_X_OVERFLOW     0x40
#define PS2_MOUSE_PACKET_Y_OVERFLOW     0x80
#define PS2_MOUSE_BUTTON_MASK           0x07

static struct spinlock mouse_lock = SPINLOCK_INITIALIZER;

static struct mouse_event mouse_state;
static struct mouse_event mouse_event_buffer[MOUSE_EVENT_BUFFER_SIZE];
static uint8_t mouse_event_head = 0;
static uint8_t mouse_event_tail = 0;

static uint8_t mouse_packet[3];
static uint8_t mouse_packet_index = 0;
static uint8_t mouse_initialized = 0;

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
    if (data == 0) {
        return -1;
    }
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
           budget != 0u) {
        (void)inb(PS2_DATA_PORT);
        budget--;
    }
}

static int ps2_send_mouse_byte(uint8_t value, uint8_t *response)
{
    uint8_t ack = 0;

    if (ps2_write_command(PS2_CMD_WRITE_PORT2_INPUT) != 0) {
        return -1;
    }
    if (ps2_write_data(value) != 0) {
        return -1;
    }
    if (ps2_read_data(&ack) != 0) {
        return -1;
    }

    if (response != 0) {
        *response = ack;
    }
    return (ack == PS2_MOUSE_ACK) ? 0 : -1;
}

static void mouse_push_event(int32_t dx, int32_t dy, uint8_t buttons)
{
    uint8_t head;
    uint8_t next;
    uint32_t flags = spinlock_lock_irqsave(&mouse_lock);

    mouse_state.dx = dx;
    mouse_state.dy = dy;
    mouse_state.x += dx;
    mouse_state.y += dy;
    mouse_state.buttons = buttons;

    head = mouse_event_head;
    next = (uint8_t)((head + 1u) & MOUSE_EVENT_BUFFER_MASK);
    if (next != mouse_event_tail) {
        mouse_event_buffer[head] = mouse_state;
        mouse_event_head = next;
    }

    spinlock_unlock_irqrestore(&mouse_lock, flags);
}

static void mouse_handle_packet(uint8_t byte0, uint8_t byte1, uint8_t byte2)
{
    int32_t dx;
    int32_t dy;

    if ((byte0 & (PS2_MOUSE_PACKET_X_OVERFLOW | PS2_MOUSE_PACKET_Y_OVERFLOW)) !=
        0u) {
        return;
    }

    dx = (int32_t)byte1 - (int32_t)(((uint32_t)byte0 << 4) & 0x100u);
    dy = (int32_t)byte2 - (int32_t)(((uint32_t)byte0 << 3) & 0x100u);

    /* Positive Y moves pointer down in screen coordinates. */
    mouse_push_event(dx, -dy, (uint8_t)(byte0 & PS2_MOUSE_BUTTON_MASK));
}

static void mouse_irq_handler(struct isr_regs *regs)
{
    uint8_t status;
    uint8_t data;

    (void)regs;

    status = inb(PS2_STATUS_PORT);
    if ((status & PS2_STATUS_OUTPUT_FULL) == 0u) {
        return;
    }

    data = inb(PS2_DATA_PORT);
    if ((status & PS2_STATUS_AUX_DATA) == 0u || mouse_initialized == 0u) {
        return;
    }

    if (mouse_packet_index == 0u &&
        (data & PS2_MOUSE_PACKET_SYNC_BIT) == 0u) {
        return;
    }

    mouse_packet[mouse_packet_index] = data;
    mouse_packet_index++;

    if (mouse_packet_index < 3u) {
        return;
    }

    mouse_packet_index = 0u;
    mouse_handle_packet(mouse_packet[0], mouse_packet[1], mouse_packet[2]);
}

void mouse_init(void)
{
    uint8_t config = 0;
    uint8_t response = 0;
    int init_ok = 1;
    uint32_t flags;

    spinlock_init(&mouse_lock);
    flags = spinlock_lock_irqsave(&mouse_lock);
    mouse_state.dx = 0;
    mouse_state.dy = 0;
    mouse_state.x = 0;
    mouse_state.y = 0;
    mouse_state.buttons = 0;
    mouse_event_head = 0;
    mouse_event_tail = 0;
    mouse_packet_index = 0;
    mouse_initialized = 0;
    spinlock_unlock_irqrestore(&mouse_lock, flags);

    (void)ps2_write_command(PS2_CMD_DISABLE_PORT2);
    ps2_flush_output_buffer();

    if (ps2_write_command(PS2_CMD_READ_CONFIG) != 0 ||
        ps2_read_data(&config) != 0) {
        serial_puts("[MOUSE] Failed to read controller config\n");
        return;
    }

    config |= 0x02u;            /* Enable IRQ12 in controller config byte. */
    config &= (uint8_t)~0x20u;  /* Enable second PS/2 port clock. */

    if (ps2_write_command(PS2_CMD_WRITE_CONFIG) != 0 ||
        ps2_write_data(config) != 0) {
        serial_puts("[MOUSE] Failed to write controller config\n");
        return;
    }

    if (ps2_write_command(PS2_CMD_ENABLE_PORT2) != 0) {
        serial_puts("[MOUSE] Failed to enable second PS/2 port\n");
        return;
    }

    if (ps2_send_mouse_byte(PS2_MOUSE_SET_DEFAULTS, &response) != 0) {
        serial_puts("[MOUSE] Mouse defaults command failed\n");
        init_ok = 0;
    }
    if (init_ok != 0 &&
        ps2_send_mouse_byte(PS2_MOUSE_ENABLE_DATA_REPORTING, &response) != 0) {
        serial_puts("[MOUSE] Mouse data-reporting enable failed\n");
        init_ok = 0;
    }

    if (init_ok == 0) {
        return;
    }

    irq_register_handler(12, mouse_irq_handler);
    pic_clear_mask(12);

    flags = spinlock_lock_irqsave(&mouse_lock);
    mouse_initialized = 1;
    spinlock_unlock_irqrestore(&mouse_lock, flags);

    serial_puts("[MOUSE] PS/2 mouse initialized (IRQ12, 3-byte packets)\n");
}

int mouse_read_event(struct mouse_event *out_event)
{
    uint32_t flags;

    if (out_event == 0) {
        return 0;
    }

    flags = spinlock_lock_irqsave(&mouse_lock);
    if (mouse_event_head == mouse_event_tail) {
        spinlock_unlock_irqrestore(&mouse_lock, flags);
        return 0;
    }

    *out_event = mouse_event_buffer[mouse_event_tail];
    mouse_event_tail = (uint8_t)((mouse_event_tail + 1u) & MOUSE_EVENT_BUFFER_MASK);
    spinlock_unlock_irqrestore(&mouse_lock, flags);
    return 1;
}

int mouse_get_state(struct mouse_event *out_state)
{
    int initialized;
    uint32_t flags;

    if (out_state == 0) {
        return 0;
    }

    flags = spinlock_lock_irqsave(&mouse_lock);
    *out_state = mouse_state;
    initialized = (mouse_initialized != 0u) ? 1 : 0;
    spinlock_unlock_irqrestore(&mouse_lock, flags);
    return initialized;
}

int mouse_is_initialized(void)
{
    int initialized;
    uint32_t flags = spinlock_lock_irqsave(&mouse_lock);
    initialized = (mouse_initialized != 0u) ? 1 : 0;
    spinlock_unlock_irqrestore(&mouse_lock, flags);
    return initialized;
}
