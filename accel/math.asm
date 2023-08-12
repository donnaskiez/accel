.code

;
; Note: fild and fistp respectively are used for loading and storing integers in the FPU,
; while fld and fstp are used for floating point numbers. No need to use xmm registers
; as we dont need that level of precision and we need to be as efficient as possible
;
; compiler will take care of saving the SSE state for us and restoring it source:
; 
; https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/using-floating-point-or-mmx-in-a-wdm-driver
;

MySqrt PROC

	push rbp
	mov rbp, rsp
	sub rsp, 16
	mov [rsp + 8], rcx			; cannot directly move from a register into a fp register
	fild qword ptr[rsp + 8]		; push our number onto the FPU stack
	fsqrt  						; perform the square root
	fistp qword ptr[rsp]		; pop the value from the floating point stack into our general purpose stack
	mov rax, qword ptr[rsp]		; store value in rax for return
	add rsp, 16
	pop rbp
	ret

MySqrt ENDP

END