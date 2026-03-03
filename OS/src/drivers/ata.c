#include <stdint.h>
#include <stddef.h>
#include "drivers/ata.h"
#include "arsc/i386/ports.h"

#define ATA_IO_BASE	0x1F0
#define ATA_CTL_BASE	0x3F6

#define ATA_REG_DATA		0x00
#define ATA_REG_ERROR		0x01
#define ATA_REG_SECCOUNT	0x02
#define ATA_REG_LBA0		0x03
#define ATA_REG_LBA1		0x04
#define ATA_REG_LBA2		0x05
#define ATA_REG_HDDEVSEL	0x06
#define ATA_REG_STATUS		0x07
#define ATA_REG_COMMAND		0x07

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30

static void io_wait_400ns(void) {
	(void)inb(ATA_CTL_BASE);
	(void)inb(ATA_CTL_BASE);
	(void)inb(ATA_CTL_BASE);
	(void)inb(ATA_CTL_BASE);
}

static int ata_wait_not_busy(void) {
	for (int i = 0; i < 1000000; i++) {
		uint8_t st = inb(ATA_IO_BASE + ATA_REG_STATUS);
		if ((st & ATA_SR_BSY) == 0) return 0;
	}
	return 1;
}

static int ata_wait_drq(void) {
	for (int i = 0; i < 1000000; i++) {
		uint8_t st = inb(ATA_IO_BASE + ATA_REG_STATUS);
		if (st & ATA_SR_ERR) return 2;
		if (st & ATA_SR_DF)  return 3;
		if ((st & ATA_SR_BSY) == 0 && (st & ATA_SR_DRQ)) return 0;
	}
	return 1;
}

static void ata_select_lba28(uint32_t lba) {
	outb(ATA_IO_BASE + ATA_REG_HDDEVSEL, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
	io_wait_400ns();
}

int ata_pio_read28(uint32_t lba, uint8_t* out512) {
	if (!out512) return 1;
	if (lba > 0x0FFFFFFF) return 2;

	if (ata_wait_not_busy() != 0) return 3;
	ata_select_lba28(lba);

	outb(ATA_IO_BASE + ATA_REG_SECCOUNT, 1);
	outb(ATA_IO_BASE + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
	outb(ATA_IO_BASE + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
	outb(ATA_IO_BASE + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
	outb(ATA_IO_BASE + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

	if (ata_wait_drq() != 0) return 4;

	for (int i = 0; i < 256; i++) {
		uint16_t w = inw(ATA_IO_BASE + ATA_REG_DATA);
		out512[i * 2 + 0] = (uint8_t)(w & 0xFF);
		out512[i * 2 + 1] = (uint8_t)((w >> 8) & 0xFF);
	}
	return 0;
}

int ata_pio_write28(uint32_t lba, const uint8_t* in512) {
	if (!in512) return 1;
	if (lba > 0x0FFFFFFF) return 2;

	if (ata_wait_not_busy() != 0) return 3;
	ata_select_lba28(lba);

	outb(ATA_IO_BASE + ATA_REG_SECCOUNT, 1);
	outb(ATA_IO_BASE + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
	outb(ATA_IO_BASE + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
	outb(ATA_IO_BASE + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
	outb(ATA_IO_BASE + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

	if (ata_wait_drq() != 0) return 4;

	for (int i = 0; i < 256; i++) {
		uint16_t w = (uint16_t)in512[i * 2 + 0] | ((uint16_t)in512[i * 2 + 1] << 8);
		outw(ATA_IO_BASE + ATA_REG_DATA, w);
	}

	// Flush cache
	outb(ATA_IO_BASE + ATA_REG_COMMAND, 0xE7);

	if (ata_wait_not_busy() != 0) return 5;
	return 0;
}
