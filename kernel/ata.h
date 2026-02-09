#ifndef CLAUDE_ATA_H
#define CLAUDE_ATA_H

#include <stdint.h>

#define ATA_DRIVE_MASTER    0U
#define ATA_DRIVE_SLAVE     1U

/* Probe primary-bus ATA drives and cache geometry/capabilities. */
void ata_init(void);

/* Return non-zero when selected primary-bus drive is present. */
uint8_t ata_drive_present(uint8_t drive);

/* Return total 28-bit addressable sectors for selected drive. */
uint32_t ata_drive_total_sectors(uint8_t drive);

/* Read sectors using primary-bus 28-bit LBA PIO path. Returns 0 on success. */
int ata_pio_read28(uint8_t drive, uint32_t lba, uint8_t sector_count, void *buffer);

#endif /* CLAUDE_ATA_H */
