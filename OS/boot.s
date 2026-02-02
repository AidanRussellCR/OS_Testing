; GRUB Handling
BITS 32

section .multiboot
align 4
	dd 0x1BADB002
	dd 0x00000000
	dd -(0x1BADB002 + 0x00000000)

section .text
global _start
extern kmain

_start:
	; Set up stack
	mov esp, stack_top

	; Call kernel
	call kmain

.hang:
	cli
	hlt
	jmp .hang

section .bss
align 16
stack_bottom:
	resb 16384 ; 16 Kb stack
stack_top:
