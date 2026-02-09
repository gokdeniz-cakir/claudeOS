#ifndef CLAUDE_FAT32_H
#define CLAUDE_FAT32_H

#include <stdint.h>

/* Initialize FAT32 reader over ATA PIO and mount it at /fat. */
int32_t fat32_init(void);

#endif /* CLAUDE_FAT32_H */
