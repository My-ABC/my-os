#ifndef _ATA_H
#define _ATA_H

#include "stdint.h"

#define ATA_PRIMARY_IO 0x1F0U

int ata_init(void);
int ata_read_sectors(uint32_t lba, uint32_t count, void *buffer);
int ata_write_sectors(uint32_t lba, uint32_t count, const void *buffer);
uint32_t ata_sector_count(void);

#endif
