@ -----------------------------------------------------------------------------
@ Copyright 2014 StapleButter
@
@ This file is part of blargSnes.
@
@ blargSnes is free software: you can redistribute it and/or modify it under
@ the terms of the GNU General Public License as published by the Free
@ Software Foundation, either version 3 of the License, or (at your option)
@ any later version.
@
@ blargSnes is distributed in the hope that it will be useful, but WITHOUT ANY 
@ WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
@ FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
@
@ You should have received a copy of the GNU General Public License along 
@ with blargSnes. If not, see http://www.gnu.org/licenses/.
@ -----------------------------------------------------------------------------

.arm

@ --- TODO --------------------------------------------------------------------
@ * emulate dummy reads (trigger read-sensitive IO ports)
@ * check the implementation of the H flag
@ * REMOVE ASM STMDB/LDMIA IN C CODE!!!!!
@
@ search the code for 'todo' for more
@ -----------------------------------------------------------------------------

#include "spc700.inc"

.section    .data, "aw", %progbits

.align 4
.global SPC_Regs
SPC_Regs:
	.long 0,0,0,0,0,0,0,0
	
.global itercount
itercount:
	.long 0
	
.global SPC_RAM
.global SPC_ROM

SPC_RAM:
	.rept 0xFFC0
	.byte 0
	.endr
SPC_ROM:
	.byte 0xCD,0xEF,0xBD,0xE8,0x00,0xC6,0x1D,0xD0
	.byte 0xFC,0x8F,0xAA,0xF4,0x8F,0xBB,0xF5,0x78
	.byte 0xCC,0xF4,0xD0,0xFB,0x2F,0x19,0xEB,0xF4
	.byte 0xD0,0xFC,0x7E,0xF4,0xD0,0x0B,0xE4,0xF5
	.byte 0xCB,0xF4,0xD7,0x00,0xFC,0xD0,0xF3,0xAB
	.byte 0x01,0x10,0xEF,0x7E,0xF4,0x10,0xEB,0xBA
	.byte 0xF6,0xDA,0x00,0xBA,0xF4,0xC4,0xF4,0xDD
	.byte 0x5D,0xD0,0xDB,0x1F,0x00,0x00,0xC0,0xFF
	.rept 0x40
	.byte 0
	.endr
	
.equ vec_Reset, 0xFFC0

.section    .text, "awx", %progbits

SPC_UpdateMemMap:
	eor r3, r3, spcPSW, lsr #1
	tst r3, #0x80
	bxeq lr
	eor spcPSW, spcPSW, #flagR
	stmdb sp!, {r0-r8}
	ldr r8, =(SPC_RAM+0xFFC0)
	add r12, r8, #0x40
	ldmia r8, {r0-r3}
	ldmia r12, {r4-r7}
	stmia r8!, {r4-r7}
	stmia r12!, {r0-r3}
	ldmia r8, {r0-r3}
	ldmia r12, {r4-r7}
	stmia r8!, {r4-r7}
	stmia r12!, {r0-r3}
	ldmia r8, {r0-r3}
	ldmia r12, {r4-r7}
	stmia r8!, {r4-r7}
	stmia r12!, {r0-r3}
	ldmia r8, {r0-r3}
	ldmia r12, {r4-r7}
	stmia r8!, {r4-r7}
	stmia r12!, {r0-r3}
	ldmia sp!, {r0-r8}
	bx lr
	
@ --- General purpose read/write ----------------------------------------------

.macro MemRead8 addr=r0
	bic r3, \addr, #0x000F
	cmp r3, #0x00F0
	ldrneb r0, [memory, \addr]
	bne 1f
	@ speedhack: when reading timer values, eat cycles
	cmp \addr, #0xFD
	orrge spcPSW, spcPSW, #flagT1
2:
	.ifnc \addr, r0
		mov r0, \addr
	.endif
	stmdb sp!, {r1-r3, r12}
	bl SPC_IORead8
	ldmia sp!, {r1-r3, r12}
1:
.endm

.macro MemRead16 addr=r0
	bic r3, \addr, #0x000F
	cmp r3, #0x00F0
	beq 1f
	add r3, memory, \addr
	ldrb r0, [r3]
	ldrb r3, [r3, #0x1]
	orr r0, r0, r3, lsl #0x8
	b 2f
1:
	.ifnc \addr, r0
		mov r0, \addr
	.endif
	stmdb sp!, {r1-r3, r12}
	bl SPC_IORead16
	ldmia sp!, {r1-r3, r12}
2:
.endm

.macro MemWrite8 addr=r0, val=r1
	bic r3, \addr, #0x000F
	cmp r3, #0x00F0
	beq 1f
	add r3, \addr, #0x40
	cmp r3, #0x10000
	andge r3, spcPSW, #flagR
	addge \addr, \addr, r3, lsr #2
	strb \val, [memory, \addr]
	b 2f
1:
	.ifnc \addr, r0
		mov r0, \addr
	.endif
	.ifnc \val, r1
		mov r1, \val
	.endif
	cmp r0, #0xF1
	moveq r3, r1
	bleq SPC_UpdateMemMap
	stmdb sp!, {r1-r3, r12}
	bl SPC_IOWrite8
	ldmia sp!, {r1-r3, r12}
2:
.endm

.macro MemWrite16 addr=r0, val=r1
	bic r3, \addr, #0x000F
	cmp r3, #0x00F0
	beq 1f
	add r3, \addr, #0x40
	cmp r3, #0x10000
	andge r3, spcPSW, #flagR
	addge \addr, \addr, r3, lsr #2
	add r3, memory, \addr
	strb \val, [r3]
	mov \val, \val, lsr #0x8
	strb \val, [r3, #0x1]
	b 2f
1:
	.ifnc \addr, r0
		mov r0, \addr
	.endif
	.ifnc \val, r1
		mov r1, \val
	.endif
	cmp r0, #0xF0
	bne 4f
	mov r3, r1, lsr #0x8
	bl SPC_UpdateMemMap
	b 3f
4:
	cmp r0, #0xF1
	moveq r3, r1
	bleq SPC_UpdateMemMap
3:
	stmdb sp!, {r1-r3, r12}
	bl SPC_IOWrite16
	ldmia sp!, {r1-r3, r12}
2:
.endm

@ --- Stack read/write --------------------------------------------------------
@ they always happen in SPC RAM, page 1
@ increment/decrement SP as well

.macro StackRead8
	add spcSP, spcSP, #1
	and spcSP, spcSP, #0xFF
	orr spcSP, spcSP, #0x100
	ldrb r0, [memory, spcSP]
.endm

.macro StackRead16
	add spcSP, spcSP, #2
	and spcSP, spcSP, #0xFF
	orr spcSP, spcSP, #0x100
	add r12, memory, spcSP
	ldrb r0, [r12, #-1]
	ldrb r3, [r12]
	orr r0, r0, r3, lsl #0x8
.endm

.macro StackWrite8 src=r0
	strb \src, [memory, spcSP]
	sub spcSP, spcSP, #1
	and spcSP, spcSP, #0xFF
	orr spcSP, spcSP, #0x100
.endm

.macro StackWrite16 src=r0
	add r12, memory, spcSP
	strb \src, [r12, #-1]
	mov \src, \src, lsr #0x8
	strb \src, [r12]
	sub spcSP, spcSP, #2
	and spcSP, spcSP, #0xFF
	orr spcSP, spcSP, #0x100
.endm

@ --- Prefetch ----------------------------------------------------------------

.macro Prefetch8 dst=r0
	ldrb \dst, [memory, spcPC, lsr #0x10]
	add spcPC, spcPC, #0x10000
.endm

.macro Prefetch16 dst=r0
	ldrb \dst, [memory, spcPC, lsr #0x10]
	add spcPC, spcPC, #0x10000
	ldrb r3, [memory, spcPC, lsr #0x10]
	add spcPC, spcPC, #0x10000
	orr \dst, \dst, r3, lsl #0x8
.endm

@ --- Misc. functions ---------------------------------------------------------

.global SPC_Reset
.global SPC_Run

.macro LoadRegs
	ldr r0, =SPC_Regs
	ldmia r0, {r5-r11}
.endm

.macro StoreRegs
	ldr r0, =SPC_Regs
	stmia r0, {r5-r11}
.endm


.macro SetPC src=r0
	mov \src, \src, lsl #0x10
	mov spcPC, spcPC, lsl #0x10
	orr spcPC, \src, spcPC, lsr #0x10
.endm


@ add cycles
@ must be called right before returning from the opcode handler
.macro AddCycles num, cond=
	@sub\cond spcCycles, spcCycles, #\num
	mov\cond r3, #\num
.endm


SPC_Reset:
	stmdb sp!, {r3-r11, lr}

	bl SPC_InitMisc
	
	mov spcA, #0
	mov spcX, #0
	mov spcY, #0
	mov spcSP, #0x100
	mov spcPSW, #0x100	@ we'll do PC later
	
	ldr memory, =SPC_RAM
	
	ldr r0, =vec_Reset
	orr spcPC, spcPC, r0, lsl #0x10
	
	mov spcCycles, #0
	StoreRegs
	
	ldr r0, =itercount
	mov r1, #0
	str r1, [r0]
	
	ldmia sp!, {r3-r11, lr}
	bx lr
	
@ --- Main loop ---------------------------------------------------------------
	
SPC_Run:
	stmdb sp!, {r4-r12, lr}
	LoadRegs
	
	ldr r12, =itercount
	ldr r4, =17447			@ ~ SPC cycles per frame
	@mov r4, #65
	ldr r3, [r12]
	add r3, r3, r4
	str r3, [r12]
	
frameloop:
	@add r12, r12, #0x200
	@add r12, r12, #0x0AA
		
bigemuloop:
		add spcCycles, spcCycles, #0x40
		mov r4, spcCycles
			
emuloop:
			
			Prefetch8
			ldr pc, [pc, r0, lsl #0x2]
			nop
	.long OP_NOP, OP_UNK, OP_SET0, OP_BBS_0, OP_OR_A_DP, OP_OR_A_lm, OP_OR_A_mX, OP_OR_A_m_Y	@0
	.long OP_OR_A_Imm, OP_OR_DP_DP, OP_UNK, OP_ASL_DP, OP_ASL_Imm, OP_PUSH_P, OP_TSET, OP_BRK
	.long OP_BPL, OP_UNK, OP_CLR0, OP_BBC_0, OP_OR_A_DP_X, OP_OR_A_lm_X, OP_OR_A_lm_Y, OP_OR_A_m_X	@1
	.long OP_OR_DP_Imm, OP_OR_mX_mY, OP_DECW_DP, OP_ASL_DP_X, OP_ASL_A, OP_DEC_X, OP_CMP_X_mImm, OP_JMP_a_X
	.long OP_CLRP, OP_UNK, OP_SET1, OP_BBS_1, OP_AND_A_DP, OP_AND_A_lm, OP_AND_A_mX, OP_AND_A_m_X	@2
	.long OP_AND_A_Imm, OP_AND_DP_DP, OP_UNK, OP_ROL_DP, OP_ROL_lm, OP_PUSH_A, OP_CBNE_DP, OP_BRA
	.long OP_BMI, OP_UNK, OP_CLR1, OP_BBC_1, OP_AND_A_DP_X, OP_AND_A_lm_X, OP_AND_A_lm_Y, OP_AND_A_m_Y	@3
	.long OP_AND_DP_Imm, OP_AND_mX_mY, OP_INCW_DP, OP_ROL_DP_X, OP_ROL_A, OP_INC_X, OP_CMP_X_DP, OP_CALL
	.long OP_SETP, OP_UNK, OP_SET2, OP_BBS_2, OP_EOR_A_DP, OP_EOR_A_lm, OP_EOR_A_mX, OP_EOR_A_m_X	@4
	.long OP_EOR_A_Imm, OP_EOR_DP_DP, OP_UNK, OP_LSR_DP, OP_LSR_lm, OP_PUSH_X, OP_TCLR, OP_PCALL
	.long OP_BVC, OP_UNK, OP_CLR2, OP_BBC_2, OP_EOR_A_DP_X, OP_EOR_A_lm_X, OP_EOR_A_lm_Y, OP_EOR_A_m_Y	@5
	.long OP_EOR_DP_Imm, OP_EOR_mX_mY, OP_CMPW_YA_DP, OP_LSR_DP_X, OP_LSR_A, OP_MOV_X_A, OP_CMP_Y_mImm, OP_JMP_a
	.long OP_CLRC, OP_UNK, OP_SET3, OP_BBS_3, OP_CMP_A_DP, OP_CMP_A_mImm, OP_UNK, OP_CMP_A_m_X	@6
	.long OP_CMP_A_Imm, OP_CMP_DP_DP, OP_UNK, OP_ROR_DP, OP_ROR_lm, OP_PUSH_Y, OP_DBNZ_DP, OP_RET
	.long OP_BVS, OP_UNK, OP_CLR3, OP_BBC_3, OP_CMP_A_DP_X, OP_CMP_A_mImm_X, OP_CMP_A_mImm_Y, OP_CMP_A_m_Y	@7
	.long OP_CMP_DP_Imm, OP_CMP_mX_mY, OP_ADDW_YA_DP, OP_ROR_DP_X, OP_ROR_A, OP_MOV_A_X, OP_CMP_Y_DP, OP_RET1
	.long OP_SETC, OP_UNK, OP_SET4, OP_BBS_4, OP_ADC_A_DP, OP_ADC_A_lm, OP_ADC_A_mX, OP_ADC_A_m_X	@8
	.long OP_ADC_A_Imm, OP_ADC_DP_DP, OP_UNK, OP_DEC_DP, OP_DEC_lm, OP_MOV_Y_Imm, OP_POP_P, OP_MOV_DP_Imm
	.long OP_BCC, OP_UNK, OP_CLR4, OP_BBC_4, OP_ADC_A_DP_X, OP_ADC_A_lm_X, OP_ADC_A_lm_Y, OP_ADC_A_m_Y	@9
	.long OP_ADC_DP_Imm, OP_ADC_mX_mY, OP_SUBW_YA_DP, OP_DEC_DP_X, OP_DEC_A, OP_MOV_X_SP, OP_DIV_YA, OP_XCN_A
	.long OP_EI, OP_UNK, OP_SET5, OP_BBS_5, OP_SBC_A_DP, OP_SBC_A_lm, OP_SBC_A_mX, OP_SBC_A_m_X	@A
	.long OP_SBC_A_Imm, OP_SBC_DP_DP, OP_MOV1_C_ab, OP_INC_DP, OP_INC_lm, OP_CMP_Y_Imm, OP_POP_A, OP_MOV_mX_A_Inc
	.long OP_BCS, OP_UNK, OP_CLR5, OP_BBC_5, OP_SBC_A_DP_X, OP_SBC_A_lm_X, OP_SBC_A_lm_Y, OP_SBC_A_m_Y	@B
	.long OP_SBC_DP_Imm, OP_SBC_mX_mY, OP_MOVW_YA_DP, OP_INC_DP_X, OP_INC_A, OP_MOV_SP_X, OP_UNK, OP_MOV_A_mX_Inc
	.long OP_DI, OP_UNK, OP_SET6, OP_BBS_6, OP_MOV_DP_A, OP_MOV_Imm_A, OP_MOV_mX_A, OP_MOV_m_X_A	@C
	.long OP_CMP_X_Imm, OP_MOV_Imm_X, OP_UNK, OP_MOV_DP_Y, OP_MOV_Imm_Y, OP_MOV_X_Imm, OP_POP_X, OP_MUL_YA
	.long OP_BNE, OP_UNK, OP_CLR6, OP_BBC_6, OP_MOV_DP_X_A, OP_MOV_lmX_A, OP_MOV_lmY_A, OP_MOV_m_Y_A	@D
	.long OP_MOV_DP_X, OP_MOV_DP_Y_X, OP_MOVW_DP_YA, OP_MOV_DP_X_Y, OP_DEC_Y, OP_MOV_A_Y, OP_CBNE_DP_X, OP_UNK
	.long OP_CLRV, OP_UNK, OP_SET7, OP_BBS_7, OP_MOV_A_DP, OP_MOV_A_lm, OP_MOV_A_mX, OP_MOV_A_m_X	@E
	.long OP_MOV_A_Imm, OP_MOV_X_lm, OP_UNK, OP_MOV_Y_DP, OP_MOV_Y_lm, OP_NOTC, OP_POP_Y, OP_UNK
	.long OP_BEQ, OP_UNK, OP_CLR7, OP_BBC_7, OP_MOV_A_DP_X, OP_MOV_A_lmX, OP_MOV_A_lmY, OP_MOV_A_m_Y	@F
	.long OP_MOV_X_DP, OP_MOV_X_DP_Y, OP_MOV_DP_DP, OP_MOV_Y_DP_X, OP_INC_Y, OP_MOV_Y_A, OP_DBNZ_Y, OP_UNK
	
op_return:

			tst spcPSW, #flagT1
			movne r3, spcCycles
			bicne spcPSW, spcPSW, #flagT1
			
			@ timer 2
		
			ldr r12, =SPC_Timers
			ldrb r0, [r12]
			
			tst r0, #0x04
			beq noTimer2
			ldrh r1, [r12, #0xE]
			subs r1, r1, r3
			strplh r1, [r12, #0xE]
			bpl noTimer2
			ldrh r2, [r12, #0x10]
			add r1, r1, r2
			strh r1, [r12, #0xE]
			ldrb r1, [r12, #0x12]
			add r1, r1, #1
			strb r1, [r12, #0x12]

noTimer2:
			subs spcCycles, spcCycles, r3
			bpl emuloop
			
		@ timers 0 and 1
		
		sub r4, r4, spcCycles
		ldr r12, =SPC_Timers
		ldrb r0, [r12]
		
		tst r0, #0x01
		beq noTimer0
		ldrh r1, [r12, #0x2]
		subs r1, r1, r4
		strplh r1, [r12, #0x2]
		bpl noTimer0
		ldrh r2, [r12, #0x4]
		add r1, r1, r2
		strh r1, [r12, #0x2]
		ldrb r1, [r12, #0x6]
		add r1, r1, #1
		strb r1, [r12, #0x6]
		
noTimer0:
		tst r0, #0x02
		beq noTimer1
		ldrh r1, [r12, #0x8]
		subs r1, r1, r4
		strplh r1, [r12, #0x8]
		bpl noTimer1
		ldrh r2, [r12, #0xA]
		add r1, r1, r2
		strh r1, [r12, #0x8]
		ldrb r1, [r12, #0xC]
		add r1, r1, #1
		strb r1, [r12, #0xC]
		
noTimer1:
		ldr r12, =itercount
		ldr r3, [r12]
		subs r3, r3, r4
		str r3, [r12]
		bpl bigemuloop
		
		@ wait for timer 0
		@ (do not wait if we missed the IRQ)
		@mov r0, #0
		@mov r1, #0x00000008
		@swi #0x40000
		
		@b frameloop
	StoreRegs
	ldmia sp!, {r4-r12, pc}
		
.ltorg
	
@ --- Addressing modes --------------------------------------------------------

.macro GetAddr_Imm dst=r0
	Prefetch16 \dst
.endm

.macro GetOp_Imm dst=r0
	Prefetch8 \dst
.endm

.macro GetAddr_DP dst=r0
	Prefetch8 \dst
	tst spcPSW, #flagP
	orrne \dst, \dst, #0x100
.endm

.macro GetOp_DP
	GetAddr_DP r0
	MemRead8 r0
.endm

.macro GetAddr_DP_X dst=r0
	Prefetch8 \dst
	add \dst, \dst, spcX
	tst spcPSW, #flagP
	andeq \dst, \dst, #0xFF
	orrne \dst, \dst, #0x100
.endm

.macro GetOp_DP_X
	GetAddr_DP_X r0
	MemRead8 r0
.endm

.macro GetAddr_DP_Y dst=r0
	Prefetch8 \dst
	add \dst, \dst, spcY
	tst spcPSW, #flagP
	andeq \dst, \dst, #0xFF
	orrne \dst, \dst, #0x100
.endm

.macro GetOp_DP_Y
	GetAddr_DP_Y r0
	MemRead8 r0
.endm

.macro GetAddr_mX dst=r0
	tst spcPSW, #flagP
	moveq \dst, spcX
	orrne \dst, spcX, #0x100
.endm

.macro GetOp_mX
	GetAddr_mX
	MemRead8
.endm

.macro GetAddr_mY dst=r0
	tst spcPSW, #flagP
	moveq \dst, spcY
	orrne \dst, spcY, #0x100
.endm

.macro GetOp_mY
	GetAddr_mY
	MemRead8
.endm

.macro GetAddr_m_X
	Prefetch8
	add r0, r0, spcX
	MemRead16
.endm

.macro GetOp_m_X
	GetAddr_m_X
	MemRead8
.endm

.macro GetAddr_m_Y
	Prefetch8
	MemRead16
	add r0, r0, spcY
.endm

.macro GetOp_m_Y
	GetAddr_m_Y
	MemRead8
.endm

@ --- Unknown opcode ----------------------------------------------------------

OP_UNK:
	mov r0, spcPC, lsr #0x10
	sub r0, r0, #1
	MemRead8
blarg2:
	b blarg2
	
@ --- ADC ---------------------------------------------------------------------

.macro DO_ADC a, b
	add r12, \a, \b
	tst spcPSW, #flagC
	addne r12, r12, #1
	bic spcPSW, spcPSW, #flagNVHZC
	tst r12, #0x80
	orrne spcPSW, spcPSW, #flagN
	tst r12, #0x100
	orrne spcPSW, spcPSW, #flagC
	eor r3, \a, \b
	tst r3, #0x80
	bne 1f
	@eor r3, \a, r12
	eor r3, \b, r12
	tst r3, #0x80
	orrne spcPSW, spcPSW, #flagVH
1:
	ands \a, r12, #0xFF
	orreq spcPSW, spcPSW, #flagZ
.endm

OP_ADC_mX_mY:
	GetOp_mY
	mov r1, r0
	GetAddr_mX r2
	MemRead8 r2
	DO_ADC r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 5
	b op_return

OP_ADC_A_Imm:
	GetOp_Imm
	DO_ADC spcA, r0
	AddCycles 2
	b op_return

OP_ADC_A_mX:
	GetOp_mX
	DO_ADC spcA, r0
	AddCycles 3
	b op_return
	
OP_ADC_A_m_Y:
	GetOp_m_Y
	DO_ADC spcA, r0
	AddCycles 6
	b op_return
	
OP_ADC_A_m_X:
	GetOp_m_X
	DO_ADC spcA, r0
	AddCycles 6
	b op_return
	
OP_ADC_A_DP:
	GetOp_DP
	DO_ADC spcA, r0
	AddCycles 3
	b op_return
	
OP_ADC_A_DP_X:
	GetOp_DP_X
	DO_ADC spcA, r0
	AddCycles 4
	b op_return
	
OP_ADC_A_lm:
	GetAddr_Imm
	MemRead8
	DO_ADC spcA, r0
	AddCycles 4
	b op_return
	
OP_ADC_A_lm_X:
	GetAddr_Imm
	add r0, r0, spcX
	MemRead8
	DO_ADC spcA, r0
	AddCycles 5
	b op_return
	
OP_ADC_A_lm_Y:
	GetAddr_Imm
	add r0, r0, spcY
	MemRead8
	DO_ADC spcA, r0
	AddCycles 5
	b op_return
	
OP_ADC_DP_DP:
	GetOp_DP
	mov r1, r0
	GetAddr_DP r2
	MemRead8 r2
	DO_ADC r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 6
	b op_return
	
OP_ADC_DP_Imm:
	GetOp_Imm r1
	GetAddr_DP r2
	MemRead8 r2
	DO_ADC r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 5
	b op_return
	
@ --- ADDW --------------------------------------------------------------------

OP_ADDW_YA_DP:
	GetAddr_DP
	MemRead16
	orr r2, spcA, spcY, lsl #0x8
	add r12, r2, r0
	bic spcPSW, spcPSW, #flagNVHZC
	tst r12, #0x8000
	orrne spcPSW, spcPSW, #flagN
	tst r12, #0x10000
	orrne spcPSW, spcPSW, #flagC
	bicne r12, r12, #0x10000
	eor r3, r2, r0
	tst r3, #0x8000
	bne addw_1
	@eor r3, r2, r12
	eor r3, r0, r12
	tst r3, #0x8000
	orrne spcPSW, spcPSW, #flagVH
addw_1:
	cmp r12, #0
	orreq spcPSW, spcPSW, #flagZ
	and spcA, r12, #0xFF
	mov spcY, r12, lsr #0x8
	AddCycles 5
	b op_return
	
@ --- AND ---------------------------------------------------------------------

.macro DO_AND a, b
	ands \a, \a, \b
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst \a, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
.endm

OP_AND_mX_mY:
	GetOp_mY
	mov r1, r0
	GetAddr_mX r2
	MemRead8 r2
	DO_AND r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 5
	b op_return

OP_AND_A_Imm:
	GetOp_Imm
	DO_AND spcA, r0
	AddCycles 2
	b op_return

OP_AND_A_mX:
	GetOp_mX
	DO_AND spcA, r0
	AddCycles 3
	b op_return
	
OP_AND_A_m_Y:
	GetOp_m_Y
	DO_AND spcA, r0
	AddCycles 6
	b op_return
	
OP_AND_A_m_X:
	GetOp_m_X
	DO_AND spcA, r0
	AddCycles 6
	b op_return
	
OP_AND_A_DP:
	GetOp_DP
	DO_AND spcA, r0
	AddCycles 3
	b op_return
	
OP_AND_A_DP_X:
	GetOp_DP_X
	DO_AND spcA, r0
	AddCycles 4
	b op_return
	
OP_AND_A_lm:
	GetAddr_Imm
	MemRead8
	DO_AND spcA, r0
	AddCycles 4
	b op_return
	
OP_AND_A_lm_X:
	GetAddr_Imm
	add r0, r0, spcX
	MemRead8
	DO_AND spcA, r0
	AddCycles 5
	b op_return
	
OP_AND_A_lm_Y:
	GetAddr_Imm
	add r0, r0, spcY
	MemRead8
	DO_AND spcA, r0
	AddCycles 5
	b op_return
	
OP_AND_DP_DP:
	GetOp_DP
	mov r1, r0
	GetAddr_DP r2
	MemRead8 r2
	DO_AND r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 6
	b op_return
	
OP_AND_DP_Imm:
	GetOp_Imm r1
	GetAddr_DP r2
	MemRead8 r2
	DO_AND r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 5
	b op_return
	
@ --- ASL ---------------------------------------------------------------------

.macro DO_ASL src, op
	mov \op, \src, lsl #1
	bic spcPSW, spcPSW, #flagNZC
	tst \op, #0x100
	orrne spcPSW, spcPSW, #flagC
	tst \op, #0x80
	orrne spcPSW, spcPSW, #flagN
	ands \op, \op, #0xFF
	orreq spcPSW, spcPSW, #flagZ
.endm

OP_ASL_A:
	DO_ASL spcA, spcA
	AddCycles 2
	b op_return
	
OP_ASL_DP:
	GetAddr_DP r2
	MemRead8 r2
	DO_ASL r0, r1
	MemWrite8 r2, r1
	AddCycles 4
	b op_return
	
OP_ASL_DP_X:
	GetAddr_DP_X r2
	MemRead8 r2
	DO_ASL r0, r1
	MemWrite8 r2, r1
	AddCycles 5
	b op_return
	
OP_ASL_Imm:
	GetAddr_Imm r2
	MemRead8 r2
	DO_ASL r0, r1
	MemWrite8 r2, r1
	AddCycles 5
	b op_return
	
@ --- Branch ------------------------------------------------------------------

.macro BRANCH cb, cnb=0, flag=0, cond=0
	.ifne \flag
		tst spcPSW, #\flag
		.ifeq \cond
			beq 1f
		.else
			bne 1f
		.endif
			add spcPC, spcPC, #0x10000
			@tst spcPSW, #flagT1
			@AddCycles 128, ne
			AddCycles \cnb
			b op_return
1:
	.endif
	GetOp_Imm
	mov r0, r0, lsl #0x18
	add spcPC, spcPC, r0, asr #0x8
	AddCycles \cb
	b op_return
.endm

OP_BRA:
	BRANCH 4
	
OP_BCC:
	BRANCH 4, 2, flagC, 0
	
OP_BCS:
	BRANCH 4, 2, flagC, 1
	
OP_BEQ:
	BRANCH 4, 2, flagZ, 1
	
OP_BMI:
	BRANCH 4, 2, flagN, 1

OP_BNE:
	BRANCH 4, 2, flagZ, 0
	
OP_BPL:
	BRANCH 4, 2, flagN, 0
	
OP_BVC:
	BRANCH 4, 2, flagV, 0
	
OP_BVS:
	BRANCH 4, 2, flagV, 1
	
@ --- BBC ---------------------------------------------------------------------

.macro DO_BBC mask
	GetOp_DP
	tst r0, #\mask
	beq 1f
	add spcPC, spcPC, #0x10000
	AddCycles 5
	b op_return
1:
	GetOp_Imm
	mov r0, r0, lsl #0x18
	add spcPC, spcPC, r0, asr #0x8
	AddCycles 7
	b op_return
.endm

OP_BBC_0:
	DO_BBC 0x01
	
OP_BBC_1:
	DO_BBC 0x02
	
OP_BBC_2:
	DO_BBC 0x04
	
OP_BBC_3:
	DO_BBC 0x08
	
OP_BBC_4:
	DO_BBC 0x10
	
OP_BBC_5:
	DO_BBC 0x20
	
OP_BBC_6:
	DO_BBC 0x40
	
OP_BBC_7:
	DO_BBC 0x80
	
@ --- BBS ---------------------------------------------------------------------

.macro DO_BBS mask
	GetOp_DP
	tst r0, #\mask
	bne 1f
	add spcPC, spcPC, #0x10000
	AddCycles 5
	b op_return
1:
	GetOp_Imm
	mov r0, r0, lsl #0x18
	add spcPC, spcPC, r0, asr #0x8
	AddCycles 7
	b op_return
.endm

OP_BBS_0:
	DO_BBS 0x01
	
OP_BBS_1:
	DO_BBS 0x02
	
OP_BBS_2:
	DO_BBS 0x04
	
OP_BBS_3:
	DO_BBS 0x08
	
OP_BBS_4:
	DO_BBS 0x10
	
OP_BBS_5:
	DO_BBS 0x20
	
OP_BBS_6:
	DO_BBS 0x40
	
OP_BBS_7:
	DO_BBS 0x80
	
@ --- BRK ---------------------------------------------------------------------

OP_BRK:
	mov r0, #0x55
blarg:
	b blarg
	
@ --- CALL --------------------------------------------------------------------

OP_CALL:
	Prefetch16
	mov r1, spcPC, lsr #0x10
	StackWrite16 r1
	SetPC
	AddCycles 8
	b op_return
	
@ --- CBNE --------------------------------------------------------------------

.macro DO_CBNE cb, cnb
	cmp spcA, r0
	bne 1f
		add spcPC, spcPC, #0x10000
		AddCycles \cnb
		b op_return
1:
	GetOp_Imm
	mov r0, r0, lsl #0x18
	add spcPC, spcPC, r0, asr #0x8
	AddCycles \cb
	b op_return
.endm

OP_CBNE_DP:
	GetOp_DP
	DO_CBNE 7, 5
	
OP_CBNE_DP_X:
	GetOp_DP_X
	DO_CBNE 8, 6
	
@ --- CLRx --------------------------------------------------------------------

.macro DO_CLRx mask
	GetAddr_DP r2
	MemRead8 r2
	bic r1, r0, #\mask
	MemWrite8 r2, r1
	AddCycles 4
	b op_return
.endm

OP_CLR0:
	DO_CLRx 0x01
	
OP_CLR1:
	DO_CLRx 0x02
	
OP_CLR2:
	DO_CLRx 0x04
	
OP_CLR3:
	DO_CLRx 0x08
	
OP_CLR4:
	DO_CLRx 0x10
	
OP_CLR5:
	DO_CLRx 0x20

OP_CLR6:
	DO_CLRx 0x40
	
OP_CLR7:
	DO_CLRx 0x80
	

OP_CLRC:
	bic spcPSW, spcPSW, #flagC
	AddCycles 2
	b op_return
	
OP_CLRP:
	bic spcPSW, spcPSW, #flagP
	AddCycles 2
	b op_return
	
OP_CLRV:
	bic spcPSW, spcPSW, #flagVH
	AddCycles 2
	b op_return

@ --- CMP ---------------------------------------------------------------------

.macro DO_CMP a, b
	subs r12, \a, \b
	bic spcPSW, spcPSW, #flagNZC
	orrpl spcPSW, spcPSW, #flagC
	tst r12, #0xFF
	orreq spcPSW, spcPSW, #flagZ
	tst r12, #0x80
	orrne spcPSW, spcPSW, #flagN
.endm

OP_CMP_A_Imm:
	GetOp_Imm
	DO_CMP spcA, r0
	AddCycles 2
	b op_return
	
OP_CMP_A_DP:
	GetOp_DP
	DO_CMP spcA, r0
	AddCycles 3
	b op_return
	
OP_CMP_A_DP_X:
	GetOp_DP_X
	DO_CMP spcA, r0
	AddCycles 4
	b op_return
	
OP_CMP_A_m_X:
	GetOp_m_X
	DO_CMP spcA, r0
	AddCycles 6
	b op_return
	
OP_CMP_A_m_Y:
	GetOp_m_Y
	DO_CMP spcA, r0
	AddCycles 6
	b op_return
	
OP_CMP_A_mImm:
	GetAddr_Imm
	MemRead8
	DO_CMP spcA, r0
	AddCycles 4
	b op_return
	
OP_CMP_A_mImm_X:
	GetAddr_Imm
	add r0, r0, spcX
	MemRead8
	DO_CMP spcA, r0
	AddCycles 5
	b op_return
	
OP_CMP_A_mImm_Y:
	GetAddr_Imm
	add r0, r0, spcY
	MemRead8
	DO_CMP spcA, r0
	AddCycles 5
	b op_return
	
OP_CMP_DP_DP:
	GetOp_DP
	mov r1, r0
	GetOp_DP
	DO_CMP r0, r1
	AddCycles 6
	b op_return

OP_CMP_DP_Imm:
	GetOp_Imm r1
	GetOp_DP
	DO_CMP r0, r1
	AddCycles 5
	b op_return
	
OP_CMP_mX_mY:
	GetOp_mX
	mov r1, r0
	GetOp_mY
	DO_CMP r1, r0
	AddCycles 5
	b op_return
	
OP_CMP_X_Imm:
	GetOp_Imm
	DO_CMP spcX, r0
	AddCycles 2
	b op_return
	
OP_CMP_X_DP:
	GetOp_DP
	DO_CMP spcX, r0
	AddCycles 3
	b op_return
	
OP_CMP_X_mImm:
	GetAddr_Imm
	MemRead8
	DO_CMP spcX, r0
	AddCycles 4
	b op_return
	
OP_CMP_Y_Imm:
	GetOp_Imm
	DO_CMP spcY, r0
	AddCycles 2
	b op_return
	
OP_CMP_Y_DP:
	GetOp_DP
	DO_CMP spcY, r0
	AddCycles 3
	b op_return
	
OP_CMP_Y_mImm:
	GetAddr_Imm
	MemRead8
	DO_CMP spcY, r0
	AddCycles 4
	b op_return
	
@ --- CMPW --------------------------------------------------------------------

OP_CMPW_YA_DP:
	GetAddr_DP
	MemRead16
	orr r2, spcA, spcY, lsl #0x8
	subs r12, r2, r0
	bic spcPSW, spcPSW, #flagNZC
	orreq spcPSW, spcPSW, #flagZ
	tst r12, #0x8000
	orrne spcPSW, spcPSW, #flagN
	tst r12, #0x10000
	orreq spcPSW, spcPSW, #flagC
	AddCycles 5
	b op_return
	
@ --- DBNZ --------------------------------------------------------------------

OP_DBNZ_DP:
	GetAddr_DP r2
	MemRead8 r2
	subs r1, r0, #1
	bne dbnz1_branch
	MemWrite8 r2, r1
	add spcPC, spcPC, #0x10000
	AddCycles 5
	b op_return
dbnz1_branch:
	MemWrite8 r2, r1
	GetOp_Imm
	mov r0, r0, lsl #0x18
	add spcPC, spcPC, r0, asr #0x8
	AddCycles 7
	b op_return
	
OP_DBNZ_Y:
	sub spcY, spcY, #1
	ands spcY, spcY, #0xFF
	bne dbnz2_branch
	add spcPC, spcPC, #0x10000
	AddCycles 4
	b op_return
dbnz2_branch:
	GetOp_Imm
	mov r0, r0, lsl #0x18
	add spcPC, spcPC, r0, asr #0x8
	AddCycles 6
	b op_return
	
@ --- DEC ---------------------------------------------------------------------

.macro DO_DEC dst
	sub \dst, \dst, #1
	ands \dst, \dst, #0xFF
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst \dst, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
.endm

OP_DEC_A:
	DO_DEC spcA
	AddCycles 2
	b op_return
	
OP_DEC_DP:
	GetAddr_DP r2
	MemRead8 r2
	DO_DEC r0
	mov r1, r0
	mov r0, r2
	MemWrite8
	AddCycles 4
	b op_return
	
OP_DEC_DP_X:
	GetAddr_DP_X r2
	MemRead8 r2
	DO_DEC r0
	mov r1, r0
	mov r0, r2
	MemWrite8
	AddCycles 5
	b op_return
	
OP_DEC_lm:
	GetAddr_Imm r2
	MemRead8 r2
	DO_DEC r0
	mov r1, r0
	mov r0, r2
	MemWrite8
	AddCycles 5
	b op_return

OP_DEC_X:
	DO_DEC spcX
	AddCycles 2
	b op_return
	
OP_DEC_Y:
	DO_DEC spcY
	AddCycles 2
	b op_return
	
@ --- DECW --------------------------------------------------------------------

OP_DECW_DP:
	GetAddr_DP r2
	MemRead16 r2
	sub r1, r0, #1
	mov r1, r1, lsl #0x10
	movs r1, r1, lsr #0x10
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst r1, #0x8000
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
	mov r0, r2
	MemWrite16
	AddCycles 6
	b op_return
	
@ --- DI ----------------------------------------------------------------------

OP_DI:
	bic spcPSW, spcPSW, #flagI
	AddCycles 3
	b op_return
	
@ --- DIV ---------------------------------------------------------------------

OP_DIV_YA:
	cmp spcX, #0
	beq div_zero
	orr r0, spcA, spcY, lsl #0x8
			@mov r1, spcX
			@swi #0x90000
			@ands spcA, r0, #0xFF
			@and spcY, r1, #0xFF
	mov r1, #9
	mov r2, spcX, lsl #9
	div_loop:
		mov r0, r0, lsl #1
		tst r0, #0x20000
		eorne r0, r0, #0x20000
		eorne r0, r0, #0x00001
		cmp r0, r2
		eorge r0, r0, #1
		tst r0, #1
		subne r0, r0, r2
		bicne r0, r0, #0xFF000000
		bicne r0, r0, #0x00FE0000
		subs r1, r1, #1
		bne div_loop
	mov spcY, r0, lsr #9
	ands spcA, r0, #0xFF
	bic spcPSW, spcPSW, #flagNVHZ
	orreq spcPSW, spcPSW, #flagZ
	tst spcA, #0x80
	orrne spcPSW, spcPSW, #flagN
	tst r0, #0x100
	orrne spcPSW, spcPSW, #flagV
	b div_end
div_zero:
	mov spcA, #0xFF
	mov spcY, #0xFF
	orr spcPSW, spcPSW, #flagNVH
	bic spcPSW, spcPSW, #flagZ
div_end:
	AddCycles 12
	b op_return
	
@ --- EI ----------------------------------------------------------------------

OP_EI:
	orr spcPSW, spcPSW, #flagI
	AddCycles 3
	b op_return
	
@ --- EOR ---------------------------------------------------------------------

.macro DO_EOR a, b
	eors \a, \a, \b
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst \a, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
.endm

OP_EOR_mX_mY:
	GetOp_mY
	mov r1, r0
	GetAddr_mX r2
	MemRead8 r2
	DO_EOR r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 5
	b op_return

OP_EOR_A_Imm:
	GetOp_Imm
	DO_EOR spcA, r0
	AddCycles 2
	b op_return

OP_EOR_A_mX:
	GetOp_mX
	DO_EOR spcA, r0
	AddCycles 3
	b op_return
	
OP_EOR_A_m_Y:
	GetOp_m_Y
	DO_EOR spcA, r0
	AddCycles 6
	b op_return
	
OP_EOR_A_m_X:
	GetOp_m_X
	DO_EOR spcA, r0
	AddCycles 6
	b op_return
	
OP_EOR_A_DP:
	GetOp_DP
	DO_EOR spcA, r0
	AddCycles 3
	b op_return
	
OP_EOR_A_DP_X:
	GetOp_DP_X
	DO_EOR spcA, r0
	AddCycles 4
	b op_return
	
OP_EOR_A_lm:
	GetAddr_Imm
	MemRead8
	DO_EOR spcA, r0
	AddCycles 4
	b op_return
	
OP_EOR_A_lm_X:
	GetAddr_Imm
	add r0, r0, spcX
	MemRead8
	DO_EOR spcA, r0
	AddCycles 5
	b op_return
	
OP_EOR_A_lm_Y:
	GetAddr_Imm
	add r0, r0, spcY
	MemRead8
	DO_EOR spcA, r0
	AddCycles 5
	b op_return
	
OP_EOR_DP_DP:
	GetOp_DP
	mov r1, r0
	GetAddr_DP r2
	MemRead8 r2
	DO_EOR r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 6
	b op_return
	
OP_EOR_DP_Imm:
	GetOp_Imm r1
	GetAddr_DP r2
	MemRead8 r2
	DO_EOR r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 5
	b op_return
	
@ --- INC ---------------------------------------------------------------------

.macro DO_INC dst
	add \dst, \dst, #1
	ands \dst, \dst, #0xFF
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst \dst, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
.endm

OP_INC_A:
	DO_INC spcA
	AddCycles 2
	b op_return
	
OP_INC_DP:
	GetAddr_DP r2
	MemRead8 r2
	DO_INC r0
	mov r1, r0
	mov r0, r2
	MemWrite8
	AddCycles 4
	b op_return
	
OP_INC_DP_X:
	GetAddr_DP_X r2
	MemRead8 r2
	DO_INC r0
	mov r1, r0
	mov r0, r2
	MemWrite8
	AddCycles 5
	b op_return
	
OP_INC_lm:
	GetAddr_Imm r2
	MemRead8 r2
	DO_INC r0
	mov r1, r0
	mov r0, r2
	MemWrite8
	AddCycles 5
	b op_return

OP_INC_X:
	DO_INC spcX
	AddCycles 2
	b op_return

OP_INC_Y:
	DO_INC spcY
	AddCycles 2
	b op_return
	
@ --- INCW --------------------------------------------------------------------

OP_INCW_DP:
	GetAddr_DP r2
	MemRead16 r2
	add r1, r0, #1
	bics r1, r1, #0x10000
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst r1, #0x8000
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
	mov r0, r2
	MemWrite16
	AddCycles 6
	b op_return
	
@ --- JMP ---------------------------------------------------------------------

OP_JMP_a_X:
	Prefetch16
	add r0, r0, spcX
	MemRead16
	SetPC r0
	AddCycles 6
	b op_return
	
OP_JMP_a:
	Prefetch16
	SetPC r0
	AddCycles 3
	b op_return
	
@ --- LSR ---------------------------------------------------------------------

.macro DO_LSR a, b
	bic spcPSW, spcPSW, #flagNZC
	tst \a, #0x1
	orrne spcPSW, spcPSW, #flagC
	movs \b, \a, lsr #1
	orreq spcPSW, spcPSW, #flagZ
.endm

OP_LSR_A:
	DO_LSR spcA, spcA
	AddCycles 2
	b op_return
	
OP_LSR_DP:
	GetAddr_DP r2
	MemRead8 r2
	DO_LSR r0, r1
	mov r0, r2
	MemWrite8
	AddCycles 4
	b op_return
	
OP_LSR_DP_X:
	GetAddr_DP_X r2
	MemRead8 r2
	DO_LSR r0, r1
	mov r0, r2
	MemWrite8
	AddCycles 5
	b op_return
	
OP_LSR_lm:
	GetAddr_Imm r2
	MemRead8 r2
	DO_LSR r0, r1
	mov r0, r2
	MemWrite8
	AddCycles 5
	b op_return
	
@ --- MOV ---------------------------------------------------------------------

.macro DO_MOV dst, src
	movs \dst, \src
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst \dst, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
.endm

OP_MOV_A_DP:
	GetOp_DP
	DO_MOV spcA, r0
	AddCycles 3
	b op_return
	
OP_MOV_A_DP_X:
	GetOp_DP_X
	DO_MOV spcA, r0
	AddCycles 4
	b op_return

OP_MOV_A_Imm:
	GetOp_Imm
	DO_MOV spcA, r0
	AddCycles 2
	b op_return
	
OP_MOV_A_lm:
	GetAddr_Imm
	MemRead8
	DO_MOV spcA, r0
	AddCycles 4
	b op_return
	
OP_MOV_A_lmX:
	GetAddr_Imm
	add r0, r0, spcX
	mov r12, r0
	MemRead8
	DO_MOV spcA, r0
	AddCycles 5
	b op_return
	
OP_MOV_A_lmY:
	GetAddr_Imm
	add r0, r0, spcY
	MemRead8
	DO_MOV spcA, r0
	AddCycles 5
	b op_return
	
OP_MOV_A_m_X:
	GetOp_m_X
	DO_MOV spcA, r0
	AddCycles 6
	b op_return

OP_MOV_A_m_Y:
	GetOp_m_Y
	DO_MOV spcA, r0
	AddCycles 6
	b op_return
	
OP_MOV_A_mX:
	GetAddr_mX
	MemRead8
	DO_MOV spcA, r0
	AddCycles 3
	b op_return
	
OP_MOV_A_mX_Inc:
	GetAddr_mX
	MemRead8
	DO_MOV spcA, r0
	add spcX, spcX, #1
	and spcX, spcX, #0xFF
	AddCycles 4
	b op_return
	
OP_MOV_A_X:
	DO_MOV spcA, spcX
	AddCycles 2
	b op_return
	
OP_MOV_A_Y:
	DO_MOV spcA, spcY
	AddCycles 2
	b op_return
	
OP_MOV_DP_Imm:
	GetOp_Imm r1
	GetAddr_DP r0
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_DP_A:
	GetAddr_DP
	mov r1, spcA
	MemWrite8
	AddCycles 4
	b op_return
	
OP_MOV_DP_DP:
	GetOp_DP
	mov r1, r0
	GetAddr_DP
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_DP_X:
	GetAddr_DP
	mov r1, spcX
	MemWrite8
	AddCycles 4
	b op_return
	
OP_MOV_DP_X_A:
	GetAddr_DP_X
	mov r1, spcA
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_DP_X_Y:
	GetAddr_DP_X
	mov r1, spcY
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_DP_Y:
	GetAddr_DP
	mov r1, spcY
	MemWrite8
	AddCycles 4
	b op_return
	
OP_MOV_DP_Y_X:
	GetAddr_DP_Y
	mov r1, spcX
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_lmX_A:
	GetAddr_Imm
	add r0, r0, spcX
	mov r1, spcA
	MemWrite8
	AddCycles 6
	b op_return
	
OP_MOV_lmY_A:
	GetAddr_Imm
	add r0, r0, spcY
	mov r1, spcA
	MemWrite8
	AddCycles 6
	b op_return
	
OP_MOV_Imm_A:
	GetAddr_Imm
	mov r1, spcA
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_Imm_X:
	GetAddr_Imm
	mov r1, spcX
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_Imm_Y:
	GetAddr_Imm
	mov r1, spcY
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_mX_A:
	GetAddr_mX
	mov r1, spcA
	MemWrite8
	AddCycles 4
	b op_return
	
OP_MOV_mX_A_Inc:
	GetAddr_mX
	mov r1, spcA
	MemWrite8
	add spcX, spcX, #1
	and spcX, spcX, #0xFF
	AddCycles 4
	b op_return
	
OP_MOV_m_X_A:
	GetAddr_m_X
	mov r1, spcA
	MemWrite8
	AddCycles 7
	b op_return
	
OP_MOV_m_Y_A:
	GetAddr_m_Y
	mov r1, spcA
	MemWrite8
	AddCycles 7
	b op_return
	
OP_MOV_X_A:
	DO_MOV spcX, spcA
	AddCycles 2
	b op_return
	
OP_MOV_X_DP:
	GetOp_DP
	DO_MOV spcX, r0
	AddCycles 3
	b op_return
	
OP_MOV_X_DP_Y:
	GetOp_DP_Y
	DO_MOV spcX, r0
	AddCycles 4
	b op_return
	
OP_MOV_X_Imm:
	GetOp_Imm
	DO_MOV spcX, r0
	AddCycles 2
	b op_return
	
OP_MOV_X_lm:
	GetAddr_Imm
	MemRead8
	DO_MOV spcX, r0
	AddCycles 4
	b op_return
	
OP_MOV_X_SP:
	ands spcX, spcSP, #0xFF
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst spcX, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
	AddCycles 2
	b op_return
	
OP_MOV_Y_A:
	DO_MOV spcY, spcA
	AddCycles 2
	b op_return
	
OP_MOV_Y_DP:
	GetOp_DP
	DO_MOV spcY, r0
	AddCycles 3
	b op_return
	
OP_MOV_Y_DP_X:
	GetOp_DP_X
	DO_MOV spcY, r0
	AddCycles 4
	b op_return
	
OP_MOV_Y_Imm:
	GetOp_Imm
	DO_MOV spcY, r0
	AddCycles 2
	b op_return
	
OP_MOV_Y_lm:
	GetAddr_Imm
	MemRead8
	DO_MOV spcY, r0
	AddCycles 4
	b op_return
	
OP_MOV_SP_X:
	orr spcSP, spcX, #0x100
	AddCycles 2
	b op_return
	
@ --- MOV1 --------------------------------------------------------------------

OP_MOV1_C_ab:
	Prefetch16
	mov r1, r0, lsr #0xD
	bic r0, r0, #0xE000
	MemRead8
	mov r2, #1
	tst r0, r2, lsl r1
	biceq spcPSW, spcPSW, #flagC
	orrne spcPSW, spcPSW, #flagC
	AddCycles 4
	b op_return
	
@ --- MOVW --------------------------------------------------------------------

OP_MOVW_YA_DP:
	GetAddr_DP
	MemRead16
	cmp r0, #0
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst r0, #0x8000
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
	and spcA, r0, #0xFF
	mov spcY, r0, lsr #0x8
	AddCycles 5
	b op_return

OP_MOVW_DP_YA:
	GetAddr_DP
	orr r1, spcA, spcY, lsl #0x8
	MemWrite16
	AddCycles 5
	b op_return
	
@ --- MUL ---------------------------------------------------------------------

OP_MUL_YA:
	mul r0, spcY, spcA
	and spcA, r0, #0xFF
	movs spcY, r0, lsr #0x8
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst spcY, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
	AddCycles 9
	b op_return
	
@ --- NOP ---------------------------------------------------------------------

OP_NOP:
	AddCycles 2
	b op_return
	
@ --- NOTC --------------------------------------------------------------------

OP_NOTC:
	eor spcPSW, spcPSW, #flagC
	AddCycles 3
	b op_return
	
@ --- OR ----------------------------------------------------------------------

.macro DO_OR a, b
	orrs \a, \a, \b
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst \a, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
.endm

OP_OR_mX_mY:
	GetOp_mY
	mov r1, r0
	GetAddr_mX r2
	MemRead8 r2
	DO_OR r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 5
	b op_return

OP_OR_A_Imm:
	GetOp_Imm
	DO_OR spcA, r0
	AddCycles 2
	b op_return

OP_OR_A_mX:
	GetOp_mX
	DO_OR spcA, r0
	AddCycles 3
	b op_return
	
OP_OR_A_m_Y:
	GetOp_m_Y
	DO_OR spcA, r0
	AddCycles 6
	b op_return
	
OP_OR_A_m_X:
	GetOp_m_X
	DO_OR spcA, r0
	AddCycles 6
	b op_return
	
OP_OR_A_DP:
	GetOp_DP
	DO_OR spcA, r0
	AddCycles 3
	b op_return
	
OP_OR_A_DP_X:
	GetOp_DP_X
	DO_OR spcA, r0
	AddCycles 4
	b op_return
	
OP_OR_A_lm:
	GetAddr_Imm
	MemRead8
	DO_OR spcA, r0
	AddCycles 4
	b op_return
	
OP_OR_A_lm_X:
	GetAddr_Imm
	add r0, r0, spcX
	MemRead8
	DO_OR spcA, r0
	AddCycles 5
	b op_return
	
OP_OR_A_lm_Y:
	GetAddr_Imm
	add r0, r0, spcY
	MemRead8
	DO_OR spcA, r0
	AddCycles 5
	b op_return
	
OP_OR_DP_DP:
	GetOp_DP
	mov r1, r0
	GetAddr_DP r2
	MemRead8 r2
	DO_OR r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 6
	b op_return
	
OP_OR_DP_Imm:
	GetOp_Imm r1
	GetAddr_DP r2
	MemRead8 r2
	DO_OR r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 5
	b op_return
	
@ --- PCALL -------------------------------------------------------------------

OP_PCALL:
	Prefetch8
	mov r1, spcPC, lsr #0x10
	StackWrite16 r1
	orr r0, r0, #0xFF00
	SetPC
	AddCycles 6
	b op_return
	
@ --- POP ---------------------------------------------------------------------

OP_POP_A:
	StackRead8
	and spcA, r0, #0xFF
	AddCycles 4
	b op_return
	
OP_POP_P:
	StackRead8
	bic spcPSW, spcPSW, #0xFF
	and r0, r0, #0xFF
	orr spcPSW, spcPSW, r0
	AddCycles 4
	b op_return
	
OP_POP_X:
	StackRead8
	and spcX, r0, #0xFF
	AddCycles 4
	b op_return
	
OP_POP_Y:
	StackRead8
	and spcY, r0, #0xFF
	AddCycles 4
	b op_return
	
@ --- PUSH --------------------------------------------------------------------

OP_PUSH_A:
	StackWrite8 spcA
	AddCycles 4
	b op_return
	
OP_PUSH_P:
	StackWrite8 spcPSW
	AddCycles 4
	b op_return
	
OP_PUSH_X:
	StackWrite8 spcX
	AddCycles 4
	b op_return
	
OP_PUSH_Y:
	StackWrite8 spcY
	AddCycles 4
	b op_return
	
@ --- RETx --------------------------------------------------------------------

OP_RET:
	StackRead16
	SetPC
	AddCycles 5
	b op_return
	
OP_RET1:
	StackRead8
	bic spcPSW, spcPSW, #0xFF
	and r0, r0, #0xFF
	orr spcPSW, spcPSW, r0
	StackRead16
	SetPC
	AddCycles 6
	b op_return
	
@ --- ROL ---------------------------------------------------------------------

.macro DO_ROL a, b
	bic spcPSW, spcPSW, #flagNZ
	mov \b, \a, lsl #1
	tst spcPSW, #flagC
	orrne \b, \b, #0x01
	tst \b, #0x100
	biceq spcPSW, spcPSW, #flagC
	orrne spcPSW, spcPSW, #flagC
	bics \b, \b, #0x100
	orreq spcPSW, spcPSW, #flagZ
	tst \b, #0x80
	orrne spcPSW, spcPSW, #flagN
.endm

OP_ROL_A:
	DO_ROL spcA, spcA
	AddCycles 2
	b op_return
	
OP_ROL_DP:
	GetAddr_DP r2
	MemRead8 r2
	DO_ROL r0, r1
	mov r0, r2
	MemWrite8
	AddCycles 4
	b op_return
	
OP_ROL_DP_X:
	GetAddr_DP_X r2
	MemRead8 r2
	DO_ROL r0, r1
	mov r0, r2
	MemWrite8
	AddCycles 5
	b op_return
	
OP_ROL_lm:
	GetAddr_Imm r2
	MemRead8 r2
	DO_ROL r0, r1
	mov r0, r2
	MemWrite8
	AddCycles 5
	b op_return
	
@ --- ROR ---------------------------------------------------------------------

.macro DO_ROR a, b
	tst spcPSW, #flagC
	biceq spcPSW, spcPSW, #flagN
	orrne spcPSW, spcPSW, #flagN
	orrne \a, \a, #0x100
	bic spcPSW, spcPSW, #flagZC
	tst \a, #0x1
	orrne spcPSW, spcPSW, #flagC
	movs \b, \a, lsr #1
	orreq spcPSW, spcPSW, #flagZ
.endm

OP_ROR_A:
	DO_ROR spcA, spcA
	AddCycles 2
	b op_return
	
OP_ROR_DP:
	GetAddr_DP r2
	MemRead8 r2
	DO_ROR r0, r1
	mov r0, r2
	MemWrite8
	AddCycles 4
	b op_return
	
OP_ROR_DP_X:
	GetAddr_DP_X r2
	MemRead8 r2
	DO_ROR r0, r1
	mov r0, r2
	MemWrite8
	AddCycles 5
	b op_return
	
OP_ROR_lm:
	GetAddr_Imm r2
	MemRead8 r2
	DO_ROR r0, r1
	mov r0, r2
	MemWrite8
	AddCycles 5
	b op_return
	
@ --- SBC ---------------------------------------------------------------------

.macro DO_SBC a, b
	sub r12, \a, \b
	tst spcPSW, #flagC
	subeq r12, r12, #1
	bic spcPSW, spcPSW, #flagNVHZC
	tst r12, #0x80
	orrne spcPSW, spcPSW, #flagN
	tst r12, #0x100
	orreq spcPSW, spcPSW, #flagC
	eor r3, \a, \b
	tst r3, #0x80
	beq 1f
	eor r3, \a, r12
	tst r3, #0x80
	orrne spcPSW, spcPSW, #flagVH
1:
	ands \a, r12, #0xFF
	orreq spcPSW, spcPSW, #flagZ
.endm

OP_SBC_mX_mY:
	GetOp_mY
	mov r1, r0
	GetAddr_mX r2
	MemRead8 r2
	DO_SBC r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 5
	b op_return

OP_SBC_A_Imm:
	GetOp_Imm
	DO_SBC spcA, r0
	AddCycles 2
	b op_return

OP_SBC_A_mX:
	GetOp_mX
	DO_SBC spcA, r0
	AddCycles 3
	b op_return
	
OP_SBC_A_m_Y:
	GetOp_m_Y
	DO_SBC spcA, r0
	AddCycles 6
	b op_return
	
OP_SBC_A_m_X:
	GetOp_m_X
	DO_SBC spcA, r0
	AddCycles 6
	b op_return
	
OP_SBC_A_DP:
	GetOp_DP
	DO_SBC spcA, r0
	AddCycles 3
	b op_return
	
OP_SBC_A_DP_X:
	GetOp_DP_X
	DO_SBC spcA, r0
	AddCycles 4
	b op_return
	
OP_SBC_A_lm:
	GetAddr_Imm
	MemRead8
	DO_SBC spcA, r0
	AddCycles 4
	b op_return
	
OP_SBC_A_lm_X:
	GetAddr_Imm
	add r0, r0, spcX
	MemRead8
	DO_SBC spcA, r0
	AddCycles 5
	b op_return
	
OP_SBC_A_lm_Y:
	GetAddr_Imm
	add r0, r0, spcY
	MemRead8
	DO_SBC spcA, r0
	AddCycles 5
	b op_return
	
OP_SBC_DP_DP:
	GetOp_DP
	mov r1, r0
	GetAddr_DP r2
	MemRead8 r2
	DO_SBC r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 6
	b op_return
	
OP_SBC_DP_Imm:
	GetOp_Imm r1
	GetAddr_DP r2
	MemRead8 r2
	DO_SBC r0, r1
	mov r1, r0
	MemWrite8 r2, r1
	AddCycles 5
	b op_return
	
@ --- SETx --------------------------------------------------------------------

.macro DO_SETx mask
	GetAddr_DP r2
	MemRead8 r2
	orr r1, r0, #\mask
	MemWrite8 r2, r1
	AddCycles 4
	b op_return
.endm

OP_SET0:
	DO_SETx 0x01
	
OP_SET1:
	DO_SETx 0x02
	
OP_SET2:
	DO_SETx 0x04
	
OP_SET3:
	DO_SETx 0x08
	
OP_SET4:
	DO_SETx 0x10
	
OP_SET5:
	DO_SETx 0x20

OP_SET6:
	DO_SETx 0x40
	
OP_SET7:
	DO_SETx 0x80
	
	
OP_SETC:
	orr spcPSW, spcPSW, #flagC
	AddCycles 2
	b op_return
	
OP_SETP:
	orr spcPSW, spcPSW, #flagP
	AddCycles 2
	b op_return
	
@ --- SUBW --------------------------------------------------------------------

OP_SUBW_YA_DP:
	GetAddr_DP
	MemRead16
	orr r2, spcA, spcY, lsl #0x8
	sub r12, r2, r0
	bic spcPSW, spcPSW, #flagNVHZC
	tst r12, #0x8000
	orrne spcPSW, spcPSW, #flagN
	tst r12, #0x10000
	orreq spcPSW, spcPSW, #flagC
	bicne r12, r12, #0x10000
	eor r3, r2, r0
	tst r3, #0x8000
	beq subw_1
	eor r3, r2, r12
	tst r3, #0x8000
	orrne spcPSW, spcPSW, #flagVH
subw_1:
	cmp r12, #0
	orreq spcPSW, spcPSW, #flagZ
	and spcA, r12, #0xFF
	mov spcY, r12, lsr #0x8
	AddCycles 5
	b op_return
	
@ --- TCLR/TSET ---------------------------------------------------------------

OP_TCLR:
	GetAddr_Imm r2
	MemRead8 r2
	cmp spcA, r0
	bic spcPSW, spcPSW, #flagNZ
	orreq spcPSW, spcPSW, #flagZ
	orrlt spcPSW, spcPSW, #flagN
	bic r1, r0, spcA
	mov r0, r2
	MemWrite8
	AddCycles 6
	b op_return
	
OP_TSET:
	GetAddr_Imm r2
	MemRead8 r2
	cmp spcA, r0
	bic spcPSW, spcPSW, #flagNZ
	orreq spcPSW, spcPSW, #flagZ
	orrlt spcPSW, spcPSW, #flagN
	orr r1, r0, spcA
	mov r0, r2
	MemWrite8
	AddCycles 6
	b op_return
	
@ --- XCN ---------------------------------------------------------------------

OP_XCN_A:
	and r3, spcA, #0x0F
	mov spcA, spcA, lsr #0x4
	orrs spcA, spcA, r3, lsl #0x4
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst spcA, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
	AddCycles 5
	b op_return
