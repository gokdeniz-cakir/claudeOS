#include "ata.h"

#include <stdint.h>

#include "io.h"
#include "serial.h"
#include "spinlock.h"

#define ATA_PRIMARY_IO_BASE          0x1F0U
#define ATA_PRIMARY_CTRL_BASE        0x3F6U

#define ATA_REG_DATA                 0x00U
#define ATA_REG_ERROR                0x01U
#define ATA_REG_FEATURES             0x01U
#define ATA_REG_SECTOR_COUNT         0x02U
#define ATA_REG_LBA0                 0x03U
#define ATA_REG_LBA1                 0x04U
#define ATA_REG_LBA2                 0x05U
#define ATA_REG_HDDEVSEL             0x06U
#define ATA_REG_STATUS               0x07U
#define ATA_REG_COMMAND              0x07U

#define ATA_REG_ALT_STATUS           0x00U
#define ATA_REG_DEVICE_CONTROL       0x00U

#define ATA_CMD_READ_SECTORS         0x20U
#define ATA_CMD_IDENTIFY             0xECU

#define ATA_STATUS_ERR               0x01U
#define ATA_STATUS_DF                0x20U
#define ATA_STATUS_DRQ               0x08U
#define ATA_STATUS_BSY               0x80U

#define ATA_IDENTIFY_WORDS           256U

#define ATA_POLL_SPINS               1000000U

struct ata_drive_info {
    uint8_t present;
    uint32_t total_sectors;
};

static struct ata_drive_info ata_primary_drives[2];
static struct spinlock ata_lock = SPINLOCK_INITIALIZER;

static void serial_put_u32(uint32_t value)
{
    char buffer[11];
    uint32_t i = 0U;

    if (value == 0U) {
        serial_putchar('0');
        return;
    }

    while (value != 0U && i < (uint32_t)sizeof(buffer)) {
        buffer[i] = (char)('0' + (value % 10U));
        value /= 10U;
        i++;
    }

    while (i > 0U) {
        i--;
        serial_putchar(buffer[i]);
    }
}

static uint16_t ata_io_reg(uint16_t reg)
{
    return (uint16_t)(ATA_PRIMARY_IO_BASE + reg);
}

static uint16_t ata_ctrl_reg(uint16_t reg)
{
    return (uint16_t)(ATA_PRIMARY_CTRL_BASE + reg);
}

static void ata_400ns_delay(void)
{
    (void)inb(ata_ctrl_reg(ATA_REG_ALT_STATUS));
    (void)inb(ata_ctrl_reg(ATA_REG_ALT_STATUS));
    (void)inb(ata_ctrl_reg(ATA_REG_ALT_STATUS));
    (void)inb(ata_ctrl_reg(ATA_REG_ALT_STATUS));
}

static void ata_select_drive(uint8_t drive, uint8_t lba_high_nibble)
{
    uint8_t value = (uint8_t)(0xE0U | ((drive & 0x01U) << 4U) |
                              (lba_high_nibble & 0x0FU));
    outb(ata_io_reg(ATA_REG_HDDEVSEL), value);
    ata_400ns_delay();
}

static int ata_wait_not_busy(void)
{
    uint32_t spins = ATA_POLL_SPINS;

    while (spins > 0U) {
        uint8_t status = inb(ata_io_reg(ATA_REG_STATUS));
        if ((status & ATA_STATUS_BSY) == 0U) {
            return 0;
        }
        spins--;
    }

    return -1;
}

static int ata_poll_data_ready(uint8_t ignore_initial_error)
{
    uint32_t spins = ATA_POLL_SPINS;
    uint8_t status = 0U;

    if (ata_wait_not_busy() != 0) {
        return -1;
    }

    while (spins > 0U) {
        status = inb(ata_io_reg(ATA_REG_STATUS));

        if (ignore_initial_error != 0U && spins > (ATA_POLL_SPINS - 4U)) {
            spins--;
            continue;
        }

        if ((status & ATA_STATUS_BSY) != 0U) {
            spins--;
            continue;
        }

        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0U) {
            return -1;
        }

        if ((status & ATA_STATUS_DRQ) != 0U) {
            return 0;
        }

        spins--;
    }

    return -1;
}

static void ata_read_data_words(uint8_t *dst)
{
    uint32_t i;

    for (i = 0U; i < ATA_IDENTIFY_WORDS; i++) {
        uint16_t word = inw(ata_io_reg(ATA_REG_DATA));
        dst[(i * 2U)] = (uint8_t)(word & 0xFFU);
        dst[(i * 2U) + 1U] = (uint8_t)((word >> 8U) & 0xFFU);
    }
}

static uint32_t ata_probe_drive(uint8_t drive, uint32_t *total_sectors)
{
    uint8_t identify_data[512];
    uint8_t status;
    uint8_t lba_mid;
    uint8_t lba_high;
    uint32_t sectors;

    if (total_sectors == 0 || drive > ATA_DRIVE_SLAVE) {
        return 0U;
    }

    ata_select_drive(drive, 0U);

    outb(ata_io_reg(ATA_REG_SECTOR_COUNT), 0U);
    outb(ata_io_reg(ATA_REG_LBA0), 0U);
    outb(ata_io_reg(ATA_REG_LBA1), 0U);
    outb(ata_io_reg(ATA_REG_LBA2), 0U);

    outb(ata_io_reg(ATA_REG_COMMAND), ATA_CMD_IDENTIFY);
    ata_400ns_delay();

    status = inb(ata_io_reg(ATA_REG_STATUS));
    if (status == 0U || status == 0xFFU) {
        return 0U;
    }

    lba_mid = inb(ata_io_reg(ATA_REG_LBA1));
    lba_high = inb(ata_io_reg(ATA_REG_LBA2));
    if (lba_mid != 0U || lba_high != 0U) {
        return 0U;
    }

    if (ata_poll_data_ready(0U) != 0) {
        return 0U;
    }

    ata_read_data_words(identify_data);

    sectors = (uint32_t)identify_data[120] |
              ((uint32_t)identify_data[121] << 8U) |
              ((uint32_t)identify_data[122] << 16U) |
              ((uint32_t)identify_data[123] << 24U);

    if (sectors == 0U) {
        return 0U;
    }

    *total_sectors = sectors;
    return 1U;
}

void ata_init(void)
{
    uint32_t flags;
    uint32_t sectors;
    uint8_t drive;

    flags = spinlock_lock_irqsave(&ata_lock);

    outb(ata_ctrl_reg(ATA_REG_DEVICE_CONTROL), 0U);
    ata_400ns_delay();

    for (drive = ATA_DRIVE_MASTER; drive <= ATA_DRIVE_SLAVE; drive++) {
        ata_primary_drives[drive].present = 0U;
        ata_primary_drives[drive].total_sectors = 0U;

        if (ata_probe_drive(drive, &sectors) != 0U) {
            ata_primary_drives[drive].present = 1U;
            ata_primary_drives[drive].total_sectors = sectors;

            serial_puts("[ATA] primary ");
            serial_puts((drive == ATA_DRIVE_MASTER) ? "master" : "slave");
            serial_puts(" present sectors=");
            serial_put_u32(sectors);
            serial_puts("\n");
        }
    }

    spinlock_unlock_irqrestore(&ata_lock, flags);
}

uint8_t ata_drive_present(uint8_t drive)
{
    if (drive > ATA_DRIVE_SLAVE) {
        return 0U;
    }

    return ata_primary_drives[drive].present;
}

uint32_t ata_drive_total_sectors(uint8_t drive)
{
    if (drive > ATA_DRIVE_SLAVE) {
        return 0U;
    }

    return ata_primary_drives[drive].total_sectors;
}

int ata_pio_read28(uint8_t drive, uint32_t lba, uint8_t sector_count, void *buffer)
{
    uint32_t flags;
    uint8_t *dst;
    uint32_t i;
    uint32_t sector;
    uint32_t total_sectors;
    uint8_t lba_hi;

    if (buffer == 0 || drive > ATA_DRIVE_SLAVE || sector_count == 0U) {
        return -1;
    }

    if ((lba & 0xF0000000U) != 0U) {
        return -1;
    }

    if (ata_primary_drives[drive].present == 0U) {
        return -1;
    }

    total_sectors = ata_primary_drives[drive].total_sectors;
    if (lba >= total_sectors ||
        (uint32_t)sector_count > (total_sectors - lba)) {
        return -1;
    }

    dst = (uint8_t *)buffer;
    flags = spinlock_lock_irqsave(&ata_lock);

    lba_hi = (uint8_t)((lba >> 24U) & 0x0FU);
    ata_select_drive(drive, lba_hi);

    outb(ata_io_reg(ATA_REG_FEATURES), 0U);
    outb(ata_io_reg(ATA_REG_SECTOR_COUNT), sector_count);
    outb(ata_io_reg(ATA_REG_LBA0), (uint8_t)(lba & 0xFFU));
    outb(ata_io_reg(ATA_REG_LBA1), (uint8_t)((lba >> 8U) & 0xFFU));
    outb(ata_io_reg(ATA_REG_LBA2), (uint8_t)((lba >> 16U) & 0xFFU));
    outb(ata_io_reg(ATA_REG_COMMAND), ATA_CMD_READ_SECTORS);

    for (sector = 0U; sector < sector_count; sector++) {
        if (ata_poll_data_ready(1U) != 0) {
            spinlock_unlock_irqrestore(&ata_lock, flags);
            return -1;
        }

        for (i = 0U; i < ATA_IDENTIFY_WORDS; i++) {
            uint16_t word = inw(ata_io_reg(ATA_REG_DATA));
            dst[(sector * 512U) + (i * 2U)] = (uint8_t)(word & 0xFFU);
            dst[(sector * 512U) + (i * 2U) + 1U] =
                (uint8_t)((word >> 8U) & 0xFFU);
        }

        ata_400ns_delay();
    }

    spinlock_unlock_irqrestore(&ata_lock, flags);
    return 0;
}
