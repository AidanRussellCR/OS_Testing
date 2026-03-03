#pragma once
#include <stdint.h>
#include <stddef.h>

#define ATA_SECTOR_SIZE 512

int ata_pio_read28(uint32_t lba, uint8_t* out512);
int ata_pio_write28(uint32_t lba, const uint8_t* in512);
