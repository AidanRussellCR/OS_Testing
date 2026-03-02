BITS 32
GLOBAL ctx_switch

; ctx_switch(uint32_t* old_esp, uint32_t new_esp)
ctx_switch:
	mov eax, [esp + 4]	; eax = old_esp pointer
	mov edx, [esp + 8]	; edx = new_esp

	pushfd			; save EFLAGS
	pushad			; save general regs

	mov [eax], esp		; *old_esp = current context pointer
	mov esp, edx		; switch to new task's context

	popad			; restore regs of new task
	popfd			; restore flags of new task
	ret			; return into new task
