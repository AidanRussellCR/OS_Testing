PREFIX ?= $(HOME)/opt/cross
CC := $(PREFIX)/bin/i686-elf-gcc
LD := $(PREFIX)/bin/i686-elf-ld
AS := nasm

CFLAGS := -std=c11 -ffreestanding -O2 -Wall -Wextra -m32 -Iinc
LDFLAGS := -T linker.ld -nostdlib

ISO_DIR := isodir
KERNEL := kernel.elf
ISO := os.iso

C_SOURCES := $(shell find src -name '*.c')
ASM_SOURCES := $(shell find src -name '*.asm')
OBJS := $(C_SOURCES:.c=.o) $(ASM_SOURCES:.asm=.o)

all: $(ISO)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(AS) -f elf32 $< -o $@

boot.o: boot.s
	$(AS) -f elf32 boot.s -o boot.o

$(KERNEL): boot.o $(OBJS) linker.ld
	$(LD) -m elf_i386 $(LDFLAGS) boot.o $(OBJS) -o $(KERNEL)

$(ISO): $(KERNEL) grub.cfg
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kernel.elf
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR) >/dev/null

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO)

clean:
	rm -rf $(ISO_DIR) boot.o $(KERNEL) $(ISO) $(shell find src -name '*.o')

.PHONY: all clean run
