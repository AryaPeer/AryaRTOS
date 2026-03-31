  .syntax unified
  .cpu cortex-m4
  .fpu fpv4-sp-d16
  .thumb

  .global SVC_Handler
  .thumb_func
SVC_Handler:
	TST LR, 4
	ITE EQ
	MRSEQ R0, MSP
	MRSNE R0, PSP
	B SVC_Handler_Main


  .global runFirstThread
  .thumb_func
runFirstThread:
  	POP {R7}
  	POP {R7}

  	MRS R0, PSP
  	LDMIA R0!, {R4-R11, LR}
  	TST LR, #0x10
  	IT EQ
  	VLDMIAEQ R0!, {S16-S31}
  	MSR PSP, R0
  	BX LR

   .global PendSV_Handler
   .thumb_func
PendSV_Handler:
	MRS R0, PSP
	TST LR, #0x10
	IT EQ
	VSTMDBEQ R0!, {S16-S31}
	STMDB R0!, {R4-R11, LR}
	MSR PSP, R0
	BL osSched
	MRS R0, PSP
	LDMIA R0!, {R4-R11, LR}
	TST LR, #0x10
	IT EQ
	VLDMIAEQ R0!, {S16-S31}
	MSR PSP, R0
	BX LR
