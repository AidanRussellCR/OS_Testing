PREFIX ?= $(HOME)/opt/cross
CC := $(PREFIX)/bin/i686-elf-gcc
LD := $(PREFIX)/bin/i686-elf-ld
AS := nasm

CFLAGS := -std=c11 -ffreestanding -O2 -Wall -Wextra
LDFLAGS := -T linker.ld -nostdlib

ISO_DIR := isodir
KERNEL := kernel.elf
ISO := helloworld_os.iso

all: $(ISO)

boot.o: boot.s
	$(AS) -f elf32 boot.s -o boot.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) -m32 -c kernel.c -o kernel.o

$(KERNEL): boot.o kernel.o linker.ld
	$(LD) -m elf_i386 $(LDFLAGS) boot.o kernel.o -o $(KERNEL)

$(ISO): $(KERNEL) grub.cfg
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kernel.elf
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR) >/dev/null

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO)

clean:
	rm -rf $(ISO_DIR) *.o *.elf *.iso

.PHONY: all clean run
