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
@
@ eventually rewrite the memory mapping table to be a set of function pointers
@  rather than the lolSnes ptr+flags system? (would probably not give a speedup though)
@
@ (low priority-- aka who cares)
@ * for some addressing modes using X/Y, add 1 cycle if adding X/Y crosses page boundary
@ * accessing I/O registers from 0x4000 to 0x4200 should take 12 cycles
@   (already done in mem_io.s although it could cause issues)
@
@ search the code for 'todo' for more
@ -----------------------------------------------------------------------------

#include "cpu.inc"

.section    .data, "aw", %progbits

.align 4
.global CPU_Regs
CPU_Regs:
	.long 0,0,0,0,0,0,0,0,0
	
@.equ vec_Reset, (0x10000-0xFFFC)
@.equ vec_e0_IRQ, (0x10000-0xFFEE)
@.equ vec_e1_IRQ, (0x10000-0xFFFE)
@.equ vec_e0_NMI, (0x10000-0xFFEA)
@.equ vec_e1_NMI, (0x10000-0xFFFA)
.equ vec_Reset, 0x4
.equ vec_e0_IRQ, 0x12
.equ vec_e1_IRQ, 0x2
.equ vec_e0_NMI, 0x16
.equ vec_e1_NMI, 0x6
	
.equ CYCLES_PER_SCANLINE, 1364
	
@ --- General purpose read/write ----------------------------------------------
@ may be slow as they handle any possible case

.section    .text, "awx", %progbits

_MemRead8:
	bic r3, r0, #0x1800
	ldr r3, [memoryMap, r3, lsr #0xB]
	tst r3, #0x1
	subeq snesCycles, snesCycles, #0x60000
	subne snesCycles, snesCycles, #0x80000
	tst r3, #0x2
	bne SNES_IORead8
	
	tst r3, #0x8
	ldrne r1, [memoryMap, #-0xC]
	andne r0, r0, r1
	bic r3, r3, #0xF
	mov r0, r0, lsl #0x13
	ldrb r0, [r3, r0, lsr #0x13]
	bx lr

.macro MemRead8
	bl _MemRead8
.endm

_MemRead16:
	bic r3, r0, #0x1800
	ldr r3, [memoryMap, r3, lsr #0xB]
	tst r3, #0x1
	subeq snesCycles, snesCycles, #0xC0000
	subne snesCycles, snesCycles, #0x100000
	tst r3, #0x2
	bne SNES_IORead16
	
	tst r3, #0x8
	ldrne r1, [memoryMap, #-0xC]
	andne r0, r0, r1
	bic r3, r3, #0xF
	mov r0, r0, lsl #0x13
	mov r0, r0, lsr #0x13 @ blarg
	ldrh r0, [r3, r0]
	bx lr

.macro MemRead16
	bl _MemRead16
.endm

_MemRead24:
	bic r3, r0, #0x1800
	ldr r3, [memoryMap, r3, lsr #0xB]
	tst r3, #0x1
	subeq snesCycles, snesCycles, #0x120000
	subne snesCycles, snesCycles, #0x180000
	tst r3, #0x8
	ldrne r2, [memoryMap, #-0xC]
	andne r0, r0, r2
	
	bic r3, r3, #0xF
	mov r0, r0, lsl #0x13
	ldr r0, [r3, r0, lsr #0x13]
	bic r0, r0, #0xFF000000
	bx lr

.macro MemRead24
	bl _MemRead24
.endm

_MemWrite8:
	bic r3, r0, #0x1800
	ldr r3, [memoryMap, r3, lsr #0xB]
	tst r3, #0x1
	subeq snesCycles, snesCycles, #0x60000
	subne snesCycles, snesCycles, #0x80000
	tst r3, #0x4
	bxne lr
	
	tst r3, #0x2
	bne SNES_IOWrite8
	
	tst r3, #0x8
	ldrne r2, [memoryMap, #-0xC]
	andne r0, r0, r2
	strne r3, [memoryMap, #-0x4]
	bic r3, r3, #0xF
	mov r0, r0, lsl #0x13
	strb r1, [r3, r0, lsr #0x13]
	bx lr

.macro MemWrite8
	bl _MemWrite8
.endm

_MemWrite16:
	bic r3, r0, #0x1800
	ldr r3, [memoryMap, r3, lsr #0xB]
	tst r3, #0x1
	subeq snesCycles, snesCycles, #0xC0000
	subne snesCycles, snesCycles, #0x100000
	tst r3, #0x4
	bxne lr
	
	tst r3, #0x2
	bne SNES_IOWrite16

	tst r3, #0x8
	ldrne r2, [memoryMap, #-0xC]
	andne r0, r0, r2
	strne r3, [memoryMap, #-0x4]
	bic r3, r3, #0xF
	mov r0, r0, lsl #0x13
	mov r0, r0, lsr #0x13
	strh r1, [r3, r0]
	bx lr

.macro MemWrite16
	bl _MemWrite16
.endm

@ --- Stack read/write --------------------------------------------------------
@ assume they always happen in system RAM
@ increment/decrement S as well

.macro StackRead8
	add snesS, snesS, #0x10000
	bic r3, snesS, #0x18000000
	ldr r3, [memoryMap, r3, lsr #0x1B]
	sub snesCycles, snesCycles, #0x80000
	bic r3, r3, #0xF
	mov r0, snesS, lsl #0x3
	ldrb r0, [r3, r0, lsr #0x13]
.endm

.macro StackRead16
	add snesS, snesS, #0x20000
	bic r3, snesS, #0x18000000
	ldr r3, [memoryMap, r3, lsr #0x1B]
	sub snesCycles, snesCycles, #0x100000
	bic r3, r3, #0xF
	mov r0, snesS, lsl #0x3
	add r3, r3, r0, lsr #0x13
	ldrh r0, [r3, #-0x1]
.endm

.macro StackWrite8 src=r0
	bic r3, snesS, #0x18000000
	ldr r3, [memoryMap, r3, lsr #0x1B]
	sub snesCycles, snesCycles, #0x80000
	tst r3, #0x4
	bne 1f
	bic r3, r3, #0xF
	mov r2, snesS, lsl #0x3
	strb \src, [r3, r2, lsr #0x13]
1:
	sub snesS, snesS, #0x10000
.endm

.macro StackWrite16 src=r0
	bic r3, snesS, #0x18000000
	ldr r3, [memoryMap, r3, lsr #0x1B]
	sub snesCycles, snesCycles, #0x100000
	tst r3, #0x4
	bne 1f
	bic r3, r3, #0xF
	mov r2, snesS, lsl #0x3
	add r3, r3, r2, lsr #0x13
	strh \src, [r3, #-0x1]
1:
	sub snesS, snesS, #0x20000
.endm

@ --- Prefetch ----------------------------------------------------------------
@ assume that prefetches always occur in system RAM or ROM
@ OpcodePrefetch8 used to prefetch the opcode
@ other prefetches are sequential (to be called right after the opcode prefetch)

.macro OpcodePrefetch8
	mov r3, snesPC, lsr #0x10
	orr r3, r3, snesPBR, lsl #0x10
	bic r3, r3, #0x1800
	ldr r3, [memoryMap, r3, lsr #0xB]
	tst r3, #0x1
	subeq snesCycles, snesCycles, #0x60000
	subne snesCycles, snesCycles, #0x80000
	mov r0, snesPC, lsl #0x3
	bic r2, r3, #0xF
	add r2, r2, r0, lsr #0x13
	ldrb r0, [r2]
	add snesPC, snesPC, #0x10000
.endm

@ can be used multiple times-- r0 or r1 should be used as dest
.macro Prefetch8 dst=r0
	tst r3, #0x1
	subeq snesCycles, snesCycles, #0x60000
	subne snesCycles, snesCycles, #0x80000
	ldrb \dst, [r2, #1]!
	add snesPC, snesPC, #0x10000
.endm

.macro Prefetch16
	tst r3, #0x1
	subeq snesCycles, snesCycles, #0xC0000
	subne snesCycles, snesCycles, #0x100000
	ldrh r0, [r2, #1]
	add snesPC, snesPC, #0x20000
.endm

.macro Prefetch24
	tst r3, #0x1
	subeq snesCycles, snesCycles, #0x120000
	subne snesCycles, snesCycles, #0x180000
	ldr r0, [r2, #1]
	bic r0, r0, #0xFF000000
	add snesPC, snesPC, #0x30000
.endm


@ CHECKME: is this supposed to take memory read cycles?
.macro SkipSignatureByte
	add snesPC, snesPC, #0x10000
.endm

@ --- Opcode tables -----------------------------------------------------------

OpTableStart:
@ Native mode, 16bit accum & index (e=0, m=0, x=0)
	.long OP_e0_BRK, OP_m0_ORA_DPIndIndirectX, OP_e0_COP, OP_m0_ORA_SR, OP_m0_TSB_DP, OP_m0_ORA_DP, OP_m0_ASL_DP, OP_m0_ORA_DPIndirectLong	@ 0
	.long OP_PHP, OP_m0_ORA_Imm, OP_m0_ASL_A, OP_PHD, OP_m0_TSB_Abs, OP_m0_ORA_Abs, OP_m0_ASL_Abs, OP_m0_ORA_AbsLong
	.long OP_BPL, OP_m0_ORA_DPIndirectIndY, OP_m0_ORA_DPIndirect, OP_m0_ORA_SRIndirectIndY, OP_m0_TRB_DP, OP_m0_ORA_DPIndX, OP_m0_ASL_DPIndX, OP_m0_ORA_DPIndirectLongIndY	@ 1
	.long OP_CLC, OP_m0_ORA_AbsIndY, OP_m0_INC_A, OP_e0_TCS, OP_m0_TRB_Abs, OP_m0_ORA_AbsIndX, OP_m0_ASL_AbsIndX, OP_m0_ORA_AbsLongIndX
	.long OP_JSR_Abs, OP_m0_AND_DPIndIndirectX, OP_JSL, OP_m0_AND_SR, OP_m0_BIT_DP, OP_m0_AND_DP, OP_m0_ROL_DP, OP_m0_AND_DPIndirectLong	@ 2
	.long OP_PLP, OP_m0_AND_Imm, OP_m0_ROL_A, OP_PLD, OP_m0_BIT_Abs, OP_m0_AND_Abs, OP_m0_ROL_Abs, OP_m0_AND_AbsLong
	.long OP_BMI, OP_m0_AND_DPIndirectIndY, OP_m0_AND_DPIndirect, OP_m0_AND_SRIndirectIndY, OP_m0_BIT_DPIndX, OP_m0_AND_DPIndX, OP_m0_ROL_DPIndX, OP_m0_AND_DPIndirectLongIndY	@ 3
	.long OP_SEC, OP_m0_AND_AbsIndY, OP_m0_DEC_A, OP_TSC, OP_m0_BIT_AbsIndX, OP_m0_AND_AbsIndX, OP_m0_ROL_AbsIndX, OP_m0_AND_AbsLongIndX
	.long OP_e0_RTI, OP_m0_EOR_DPIndIndirectX, OP_HAX42, OP_m0_EOR_SR, OP_x0_MVP, OP_m0_EOR_DP, OP_m0_LSR_DP, OP_m0_EOR_DPIndirectLong	@ 4
	.long OP_m0_PHA, OP_m0_EOR_Imm, OP_m0_LSR_A, OP_PHK, OP_JMP_Abs, OP_m0_EOR_Abs, OP_m0_LSR_Abs, OP_m0_EOR_AbsLong
	.long OP_BVC, OP_m0_EOR_DPIndirectIndY, OP_m0_EOR_DPIndirect, OP_m0_EOR_SRIndirectIndY, OP_x0_MVN, OP_m0_EOR_DPIndX, OP_m0_LSR_DPIndX, OP_m0_EOR_DPIndirectLongIndY	@ 5
	.long OP_CLI, OP_m0_EOR_AbsIndY, OP_x0_PHY, OP_TCD, OP_JMP_AbsLong, OP_m0_EOR_AbsIndX, OP_m0_LSR_AbsIndX, OP_m0_EOR_AbsLongIndX
	.long OP_RTS, OP_m0_ADC_DPIndIndirectX, OP_PER, OP_m0_ADC_SR, OP_m0_STZ_DP, OP_m0_ADC_DP, OP_m0_ROR_DP, OP_m0_ADC_DPIndirectLong	@ 6
	.long OP_m0_PLA, OP_m0_ADC_Imm, OP_m0_ROR_A, OP_RTL, OP_JMP_AbsIndirect, OP_m0_ADC_Abs, OP_m0_ROR_Abs, OP_m0_ADC_AbsLong
	.long OP_BVS, OP_m0_ADC_DPIndirectIndY, OP_m0_ADC_DPIndirect, OP_m0_ADC_SRIndirectIndY, OP_m0_STZ_DPIndX, OP_m0_ADC_DPIndX, OP_m0_ROR_DPIndX, OP_m0_ADC_DPIndirectLongIndY	@ 7
	.long OP_SEI, OP_m0_ADC_AbsIndY, OP_x0_PLY, OP_TDC, OP_JMP_AbsIndIndirect, OP_m0_ADC_AbsIndX, OP_m0_ROR_AbsIndX, OP_m0_ADC_AbsLongIndX
	.long OP_BRA, OP_m0_STA_DPIndIndirectX, OP_BRL, OP_m0_STA_SR, OP_x0_STY_DP, OP_m0_STA_DP, OP_x0_STX_DP, OP_m0_STA_DPIndirectLong	@ 8
	.long OP_x0_DEY, OP_m0_BIT_Imm, OP_m0_TXA, OP_PHB, OP_x0_STY_Abs, OP_m0_STA_Abs, OP_x0_STX_Abs, OP_m0_STA_AbsLong
	.long OP_BCC, OP_m0_STA_DPIndirectIndY, OP_m0_STA_DPIndirect, OP_m0_STA_SRIndirectIndY, OP_x0_STY_DPIndX, OP_m0_STA_DPIndX, OP_x0_STX_DPIndY, OP_m0_STA_DPIndirectLongIndY	@ 9
	.long OP_m0_TYA, OP_m0_STA_AbsIndY, OP_e0_TXS, OP_TXY, OP_m0_STZ_Abs, OP_m0_STA_AbsIndX, OP_m0_STZ_AbsIndX, OP_m0_STA_AbsLongIndX
	.long OP_x0_LDY_Imm, OP_m0_LDA_DPIndIndirectX, OP_x0_LDX_Imm, OP_m0_LDA_SR, OP_x0_LDY_DP, OP_m0_LDA_DP, OP_x0_LDX_DP, OP_m0_LDA_DPIndirectLong	@ A
	.long OP_x0_TAY, OP_m0_LDA_Imm, OP_x0_TAX, OP_PLB, OP_x0_LDY_Abs, OP_m0_LDA_Abs, OP_x0_LDX_Abs, OP_m0_LDA_AbsLong
	.long OP_BCS, OP_m0_LDA_DPIndirectIndY, OP_m0_LDA_DPIndirect, OP_m0_LDA_SRIndirectIndY, OP_x0_LDY_DPIndX, OP_m0_LDA_DPIndX, OP_x0_LDX_DPIndY, OP_m0_LDA_DPIndirectLongIndY	@ B
	.long OP_CLV, OP_m0_LDA_AbsIndY, OP_x0_TSX, OP_TYX, OP_x0_LDY_AbsIndX, OP_m0_LDA_AbsIndX, OP_x0_LDX_AbsIndY, OP_m0_LDA_AbsLongIndX
	.long OP_x0_CPY_Imm, OP_m0_CMP_DPIndIndirectX, OP_e0_REP, OP_m0_CMP_SR, OP_x0_CPY_DP, OP_m0_CMP_DP, OP_m0_DEC_DP, OP_m0_CMP_DPIndirectLong	@ C
	.long OP_x0_INY, OP_m0_CMP_Imm, OP_x0_DEX, OP_WAI, OP_x0_CPY_Abs, OP_m0_CMP_Abs, OP_m0_DEC_Abs, OP_m0_CMP_AbsLong
	.long OP_BNE, OP_m0_CMP_DPIndirectIndY, OP_m0_CMP_DPIndirect, OP_m0_CMP_SRIndirectIndY, OP_PEI, OP_m0_CMP_DPIndX, OP_m0_DEC_DPIndX, OP_m0_CMP_DPIndirectLongIndY	@ D
	.long OP_CLD, OP_m0_CMP_AbsIndY, OP_x0_PHX, OP_STP, OP_JMP_AbsIndirectLong, OP_m0_CMP_AbsIndX, OP_m0_DEC_AbsIndX, OP_m0_CMP_AbsLongIndX
	.long OP_x0_CPX_Imm, OP_m0_SBC_DPIndIndirectX, OP_e0_SEP, OP_m0_SBC_SR, OP_x0_CPX_DP, OP_m0_SBC_DP, OP_m0_INC_DP, OP_m0_SBC_DPIndirectLong	@ E
	.long OP_x0_INX, OP_m0_SBC_Imm, OP_NOP, OP_XBA, OP_x0_CPX_Abs, OP_m0_SBC_Abs, OP_m0_INC_Abs, OP_m0_SBC_AbsLong
	.long OP_BEQ, OP_m0_SBC_DPIndirectIndY, OP_m0_SBC_DPIndirect, OP_m0_SBC_SRIndirectIndY, OP_PEA, OP_m0_SBC_DPIndX, OP_m0_INC_DPIndX, OP_m0_SBC_DPIndirectLongIndY	@ F
	.long OP_SED, OP_m0_SBC_AbsIndY, OP_x0_PLX, OP_e0_XCE, OP_JSR_AbsIndIndirect, OP_m0_SBC_AbsIndX, OP_m0_INC_AbsIndX, OP_m0_SBC_AbsLongIndX
	
@ Native mode, 16bit accum, 8bit index (e=0, m=0, x=1)
	.long OP_e0_BRK, OP_m0_ORA_DPIndIndirectX, OP_e0_COP, OP_m0_ORA_SR, OP_m0_TSB_DP, OP_m0_ORA_DP, OP_m0_ASL_DP, OP_m0_ORA_DPIndirectLong	@ 0
	.long OP_PHP, OP_m0_ORA_Imm, OP_m0_ASL_A, OP_PHD, OP_m0_TSB_Abs, OP_m0_ORA_Abs, OP_m0_ASL_Abs, OP_m0_ORA_AbsLong
	.long OP_BPL, OP_m0_ORA_DPIndirectIndY, OP_m0_ORA_DPIndirect, OP_m0_ORA_SRIndirectIndY, OP_m0_TRB_DP, OP_m0_ORA_DPIndX, OP_m0_ASL_DPIndX, OP_m0_ORA_DPIndirectLongIndY	@ 1
	.long OP_CLC, OP_m0_ORA_AbsIndY, OP_m0_INC_A, OP_e0_TCS, OP_m0_TRB_Abs, OP_m0_ORA_AbsIndX, OP_m0_ASL_AbsIndX, OP_m0_ORA_AbsLongIndX
	.long OP_JSR_Abs, OP_m0_AND_DPIndIndirectX, OP_JSL, OP_m0_AND_SR, OP_m0_BIT_DP, OP_m0_AND_DP, OP_m0_ROL_DP, OP_m0_AND_DPIndirectLong	@ 2
	.long OP_PLP, OP_m0_AND_Imm, OP_m0_ROL_A, OP_PLD, OP_m0_BIT_Abs, OP_m0_AND_Abs, OP_m0_ROL_Abs, OP_m0_AND_AbsLong
	.long OP_BMI, OP_m0_AND_DPIndirectIndY, OP_m0_AND_DPIndirect, OP_m0_AND_SRIndirectIndY, OP_m0_BIT_DPIndX, OP_m0_AND_DPIndX, OP_m0_ROL_DPIndX, OP_m0_AND_DPIndirectLongIndY	@ 3
	.long OP_SEC, OP_m0_AND_AbsIndY, OP_m0_DEC_A, OP_TSC, OP_m0_BIT_AbsIndX, OP_m0_AND_AbsIndX, OP_m0_ROL_AbsIndX, OP_m0_AND_AbsLongIndX
	.long OP_e0_RTI, OP_m0_EOR_DPIndIndirectX, OP_HAX42, OP_m0_EOR_SR, OP_x1_MVP, OP_m0_EOR_DP, OP_m0_LSR_DP, OP_m0_EOR_DPIndirectLong	@ 4
	.long OP_m0_PHA, OP_m0_EOR_Imm, OP_m0_LSR_A, OP_PHK, OP_JMP_Abs, OP_m0_EOR_Abs, OP_m0_LSR_Abs, OP_m0_EOR_AbsLong
	.long OP_BVC, OP_m0_EOR_DPIndirectIndY, OP_m0_EOR_DPIndirect, OP_m0_EOR_SRIndirectIndY, OP_x1_MVN, OP_m0_EOR_DPIndX, OP_m0_LSR_DPIndX, OP_m0_EOR_DPIndirectLongIndY	@ 5
	.long OP_CLI, OP_m0_EOR_AbsIndY, OP_x1_PHY, OP_TCD, OP_JMP_AbsLong, OP_m0_EOR_AbsIndX, OP_m0_LSR_AbsIndX, OP_m0_EOR_AbsLongIndX
	.long OP_RTS, OP_m0_ADC_DPIndIndirectX, OP_PER, OP_m0_ADC_SR, OP_m0_STZ_DP, OP_m0_ADC_DP, OP_m0_ROR_DP, OP_m0_ADC_DPIndirectLong	@ 6
	.long OP_m0_PLA, OP_m0_ADC_Imm, OP_m0_ROR_A, OP_RTL, OP_JMP_AbsIndirect, OP_m0_ADC_Abs, OP_m0_ROR_Abs, OP_m0_ADC_AbsLong
	.long OP_BVS, OP_m0_ADC_DPIndirectIndY, OP_m0_ADC_DPIndirect, OP_m0_ADC_SRIndirectIndY, OP_m0_STZ_DPIndX, OP_m0_ADC_DPIndX, OP_m0_ROR_DPIndX, OP_m0_ADC_DPIndirectLongIndY	@ 7
	.long OP_SEI, OP_m0_ADC_AbsIndY, OP_x1_PLY, OP_TDC, OP_JMP_AbsIndIndirect, OP_m0_ADC_AbsIndX, OP_m0_ROR_AbsIndX, OP_m0_ADC_AbsLongIndX
	.long OP_BRA, OP_m0_STA_DPIndIndirectX, OP_BRL, OP_m0_STA_SR, OP_x1_STY_DP, OP_m0_STA_DP, OP_x1_STX_DP, OP_m0_STA_DPIndirectLong	@ 8
	.long OP_x1_DEY, OP_m0_BIT_Imm, OP_m0_TXA, OP_PHB, OP_x1_STY_Abs, OP_m0_STA_Abs, OP_x1_STX_Abs, OP_m0_STA_AbsLong
	.long OP_BCC, OP_m0_STA_DPIndirectIndY, OP_m0_STA_DPIndirect, OP_m0_STA_SRIndirectIndY, OP_x1_STY_DPIndX, OP_m0_STA_DPIndX, OP_x1_STX_DPIndY, OP_m0_STA_DPIndirectLongIndY	@ 9
	.long OP_m0_TYA, OP_m0_STA_AbsIndY, OP_e0_TXS, OP_TXY, OP_m0_STZ_Abs, OP_m0_STA_AbsIndX, OP_m0_STZ_AbsIndX, OP_m0_STA_AbsLongIndX
	.long OP_x1_LDY_Imm, OP_m0_LDA_DPIndIndirectX, OP_x1_LDX_Imm, OP_m0_LDA_SR, OP_x1_LDY_DP, OP_m0_LDA_DP, OP_x1_LDX_DP, OP_m0_LDA_DPIndirectLong	@ A
	.long OP_x1_TAY, OP_m0_LDA_Imm, OP_x1_TAX, OP_PLB, OP_x1_LDY_Abs, OP_m0_LDA_Abs, OP_x1_LDX_Abs, OP_m0_LDA_AbsLong
	.long OP_BCS, OP_m0_LDA_DPIndirectIndY, OP_m0_LDA_DPIndirect, OP_m0_LDA_SRIndirectIndY, OP_x1_LDY_DPIndX, OP_m0_LDA_DPIndX, OP_x1_LDX_DPIndY, OP_m0_LDA_DPIndirectLongIndY	@ B
	.long OP_CLV, OP_m0_LDA_AbsIndY, OP_x1_TSX, OP_TYX, OP_x1_LDY_AbsIndX, OP_m0_LDA_AbsIndX, OP_x1_LDX_AbsIndY, OP_m0_LDA_AbsLongIndX
	.long OP_x1_CPY_Imm, OP_m0_CMP_DPIndIndirectX, OP_e0_REP, OP_m0_CMP_SR, OP_x1_CPY_DP, OP_m0_CMP_DP, OP_m0_DEC_DP, OP_m0_CMP_DPIndirectLong	@ C
	.long OP_x1_INY, OP_m0_CMP_Imm, OP_x1_DEX, OP_WAI, OP_x1_CPY_Abs, OP_m0_CMP_Abs, OP_m0_DEC_Abs, OP_m0_CMP_AbsLong
	.long OP_BNE, OP_m0_CMP_DPIndirectIndY, OP_m0_CMP_DPIndirect, OP_m0_CMP_SRIndirectIndY, OP_PEI, OP_m0_CMP_DPIndX, OP_m0_DEC_DPIndX, OP_m0_CMP_DPIndirectLongIndY	@ D
	.long OP_CLD, OP_m0_CMP_AbsIndY, OP_x1_PHX, OP_STP, OP_JMP_AbsIndirectLong, OP_m0_CMP_AbsIndX, OP_m0_DEC_AbsIndX, OP_m0_CMP_AbsLongIndX
	.long OP_x1_CPX_Imm, OP_m0_SBC_DPIndIndirectX, OP_e0_SEP, OP_m0_SBC_SR, OP_x1_CPX_DP, OP_m0_SBC_DP, OP_m0_INC_DP, OP_m0_SBC_DPIndirectLong	@ E
	.long OP_x1_INX, OP_m0_SBC_Imm, OP_NOP, OP_XBA, OP_x1_CPX_Abs, OP_m0_SBC_Abs, OP_m0_INC_Abs, OP_m0_SBC_AbsLong
	.long OP_BEQ, OP_m0_SBC_DPIndirectIndY, OP_m0_SBC_DPIndirect, OP_m0_SBC_SRIndirectIndY, OP_PEA, OP_m0_SBC_DPIndX, OP_m0_INC_DPIndX, OP_m0_SBC_DPIndirectLongIndY	@ F
	.long OP_SED, OP_m0_SBC_AbsIndY, OP_x1_PLX, OP_e0_XCE, OP_JSR_AbsIndIndirect, OP_m0_SBC_AbsIndX, OP_m0_INC_AbsIndX, OP_m0_SBC_AbsLongIndX
	
@ Native mode, 8bit accum, 16bit index (e=0, m=1, x=0)
	.long OP_e0_BRK, OP_m1_ORA_DPIndIndirectX, OP_e0_COP, OP_m1_ORA_SR, OP_m1_TSB_DP, OP_m1_ORA_DP, OP_m1_ASL_DP, OP_m1_ORA_DPIndirectLong	@ 0
	.long OP_PHP, OP_m1_ORA_Imm, OP_m1_ASL_A, OP_PHD, OP_m1_TSB_Abs, OP_m1_ORA_Abs, OP_m1_ASL_Abs, OP_m1_ORA_AbsLong
	.long OP_BPL, OP_m1_ORA_DPIndirectIndY, OP_m1_ORA_DPIndirect, OP_m1_ORA_SRIndirectIndY, OP_m1_TRB_DP, OP_m1_ORA_DPIndX, OP_m1_ASL_DPIndX, OP_m1_ORA_DPIndirectLongIndY	@ 1
	.long OP_CLC, OP_m1_ORA_AbsIndY, OP_m1_INC_A, OP_e0_TCS, OP_m1_TRB_Abs, OP_m1_ORA_AbsIndX, OP_m1_ASL_AbsIndX, OP_m1_ORA_AbsLongIndX
	.long OP_JSR_Abs, OP_m1_AND_DPIndIndirectX, OP_JSL, OP_m1_AND_SR, OP_m1_BIT_DP, OP_m1_AND_DP, OP_m1_ROL_DP, OP_m1_AND_DPIndirectLong	@ 2
	.long OP_PLP, OP_m1_AND_Imm, OP_m1_ROL_A, OP_PLD, OP_m1_BIT_Abs, OP_m1_AND_Abs, OP_m1_ROL_Abs, OP_m1_AND_AbsLong
	.long OP_BMI, OP_m1_AND_DPIndirectIndY, OP_m1_AND_DPIndirect, OP_m1_AND_SRIndirectIndY, OP_m1_BIT_DPIndX, OP_m1_AND_DPIndX, OP_m1_ROL_DPIndX, OP_m1_AND_DPIndirectLongIndY	@ 3
	.long OP_SEC, OP_m1_AND_AbsIndY, OP_m1_DEC_A, OP_TSC, OP_m1_BIT_AbsIndX, OP_m1_AND_AbsIndX, OP_m1_ROL_AbsIndX, OP_m1_AND_AbsLongIndX
	.long OP_e0_RTI, OP_m1_EOR_DPIndIndirectX, OP_HAX42, OP_m1_EOR_SR, OP_x0_MVP, OP_m1_EOR_DP, OP_m1_LSR_DP, OP_m1_EOR_DPIndirectLong	@ 4
	.long OP_m1_PHA, OP_m1_EOR_Imm, OP_m1_LSR_A, OP_PHK, OP_JMP_Abs, OP_m1_EOR_Abs, OP_m1_LSR_Abs, OP_m1_EOR_AbsLong
	.long OP_BVC, OP_m1_EOR_DPIndirectIndY, OP_m1_EOR_DPIndirect, OP_m1_EOR_SRIndirectIndY, OP_x0_MVN, OP_m1_EOR_DPIndX, OP_m1_LSR_DPIndX, OP_m1_EOR_DPIndirectLongIndY	@ 5
	.long OP_CLI, OP_m1_EOR_AbsIndY, OP_x0_PHY, OP_TCD, OP_JMP_AbsLong, OP_m1_EOR_AbsIndX, OP_m1_LSR_AbsIndX, OP_m1_EOR_AbsLongIndX
	.long OP_RTS, OP_m1_ADC_DPIndIndirectX, OP_PER, OP_m1_ADC_SR, OP_m1_STZ_DP, OP_m1_ADC_DP, OP_m1_ROR_DP, OP_m1_ADC_DPIndirectLong	@ 6
	.long OP_m1_PLA, OP_m1_ADC_Imm, OP_m1_ROR_A, OP_RTL, OP_JMP_AbsIndirect, OP_m1_ADC_Abs, OP_m1_ROR_Abs, OP_m1_ADC_AbsLong
	.long OP_BVS, OP_m1_ADC_DPIndirectIndY, OP_m1_ADC_DPIndirect, OP_m1_ADC_SRIndirectIndY, OP_m1_STZ_DPIndX, OP_m1_ADC_DPIndX, OP_m1_ROR_DPIndX, OP_m1_ADC_DPIndirectLongIndY	@ 7
	.long OP_SEI, OP_m1_ADC_AbsIndY, OP_x0_PLY, OP_TDC, OP_JMP_AbsIndIndirect, OP_m1_ADC_AbsIndX, OP_m1_ROR_AbsIndX, OP_m1_ADC_AbsLongIndX
	.long OP_BRA, OP_m1_STA_DPIndIndirectX, OP_BRL, OP_m1_STA_SR, OP_x0_STY_DP, OP_m1_STA_DP, OP_x0_STX_DP, OP_m1_STA_DPIndirectLong	@ 8
	.long OP_x0_DEY, OP_m1_BIT_Imm, OP_m1_TXA, OP_PHB, OP_x0_STY_Abs, OP_m1_STA_Abs, OP_x0_STX_Abs, OP_m1_STA_AbsLong
	.long OP_BCC, OP_m1_STA_DPIndirectIndY, OP_m1_STA_DPIndirect, OP_m1_STA_SRIndirectIndY, OP_x0_STY_DPIndX, OP_m1_STA_DPIndX, OP_x0_STX_DPIndY, OP_m1_STA_DPIndirectLongIndY	@ 9
	.long OP_m1_TYA, OP_m1_STA_AbsIndY, OP_e0_TXS, OP_TXY, OP_m1_STZ_Abs, OP_m1_STA_AbsIndX, OP_m1_STZ_AbsIndX, OP_m1_STA_AbsLongIndX
	.long OP_x0_LDY_Imm, OP_m1_LDA_DPIndIndirectX, OP_x0_LDX_Imm, OP_m1_LDA_SR, OP_x0_LDY_DP, OP_m1_LDA_DP, OP_x0_LDX_DP, OP_m1_LDA_DPIndirectLong	@ A
	.long OP_x0_TAY, OP_m1_LDA_Imm, OP_x0_TAX, OP_PLB, OP_x0_LDY_Abs, OP_m1_LDA_Abs, OP_x0_LDX_Abs, OP_m1_LDA_AbsLong
	.long OP_BCS, OP_m1_LDA_DPIndirectIndY, OP_m1_LDA_DPIndirect, OP_m1_LDA_SRIndirectIndY, OP_x0_LDY_DPIndX, OP_m1_LDA_DPIndX, OP_x0_LDX_DPIndY, OP_m1_LDA_DPIndirectLongIndY	@ B
	.long OP_CLV, OP_m1_LDA_AbsIndY, OP_x0_TSX, OP_TYX, OP_x0_LDY_AbsIndX, OP_m1_LDA_AbsIndX, OP_x0_LDX_AbsIndY, OP_m1_LDA_AbsLongIndX
	.long OP_x0_CPY_Imm, OP_m1_CMP_DPIndIndirectX, OP_e0_REP, OP_m1_CMP_SR, OP_x0_CPY_DP, OP_m1_CMP_DP, OP_m1_DEC_DP, OP_m1_CMP_DPIndirectLong	@ C
	.long OP_x0_INY, OP_m1_CMP_Imm, OP_x0_DEX, OP_WAI, OP_x0_CPY_Abs, OP_m1_CMP_Abs, OP_m1_DEC_Abs, OP_m1_CMP_AbsLong
	.long OP_BNE, OP_m1_CMP_DPIndirectIndY, OP_m1_CMP_DPIndirect, OP_m1_CMP_SRIndirectIndY, OP_PEI, OP_m1_CMP_DPIndX, OP_m1_DEC_DPIndX, OP_m1_CMP_DPIndirectLongIndY	@ D
	.long OP_CLD, OP_m1_CMP_AbsIndY, OP_x0_PHX, OP_STP, OP_JMP_AbsIndirectLong, OP_m1_CMP_AbsIndX, OP_m1_DEC_AbsIndX, OP_m1_CMP_AbsLongIndX
	.long OP_x0_CPX_Imm, OP_m1_SBC_DPIndIndirectX, OP_e0_SEP, OP_m1_SBC_SR, OP_x0_CPX_DP, OP_m1_SBC_DP, OP_m1_INC_DP, OP_m1_SBC_DPIndirectLong	@ E
	.long OP_x0_INX, OP_m1_SBC_Imm, OP_NOP, OP_XBA, OP_x0_CPX_Abs, OP_m1_SBC_Abs, OP_m1_INC_Abs, OP_m1_SBC_AbsLong
	.long OP_BEQ, OP_m1_SBC_DPIndirectIndY, OP_m1_SBC_DPIndirect, OP_m1_SBC_SRIndirectIndY, OP_PEA, OP_m1_SBC_DPIndX, OP_m1_INC_DPIndX, OP_m1_SBC_DPIndirectLongIndY	@ F
	.long OP_SED, OP_m1_SBC_AbsIndY, OP_x0_PLX, OP_e0_XCE, OP_JSR_AbsIndIndirect, OP_m1_SBC_AbsIndX, OP_m1_INC_AbsIndX, OP_m1_SBC_AbsLongIndX
	
@ Native mode, 8bit accum & index (e=0, m=1, x=1)
	.long OP_e0_BRK, OP_m1_ORA_DPIndIndirectX, OP_e0_COP, OP_m1_ORA_SR, OP_m1_TSB_DP, OP_m1_ORA_DP, OP_m1_ASL_DP, OP_m1_ORA_DPIndirectLong	@ 0
	.long OP_PHP, OP_m1_ORA_Imm, OP_m1_ASL_A, OP_PHD, OP_m1_TSB_Abs, OP_m1_ORA_Abs, OP_m1_ASL_Abs, OP_m1_ORA_AbsLong
	.long OP_BPL, OP_m1_ORA_DPIndirectIndY, OP_m1_ORA_DPIndirect, OP_m1_ORA_SRIndirectIndY, OP_m1_TRB_DP, OP_m1_ORA_DPIndX, OP_m1_ASL_DPIndX, OP_m1_ORA_DPIndirectLongIndY	@ 1
	.long OP_CLC, OP_m1_ORA_AbsIndY, OP_m1_INC_A, OP_e0_TCS, OP_m1_TRB_Abs, OP_m1_ORA_AbsIndX, OP_m1_ASL_AbsIndX, OP_m1_ORA_AbsLongIndX
	.long OP_JSR_Abs, OP_m1_AND_DPIndIndirectX, OP_JSL, OP_m1_AND_SR, OP_m1_BIT_DP, OP_m1_AND_DP, OP_m1_ROL_DP, OP_m1_AND_DPIndirectLong	@ 2
	.long OP_PLP, OP_m1_AND_Imm, OP_m1_ROL_A, OP_PLD, OP_m1_BIT_Abs, OP_m1_AND_Abs, OP_m1_ROL_Abs, OP_m1_AND_AbsLong
	.long OP_BMI, OP_m1_AND_DPIndirectIndY, OP_m1_AND_DPIndirect, OP_m1_AND_SRIndirectIndY, OP_m1_BIT_DPIndX, OP_m1_AND_DPIndX, OP_m1_ROL_DPIndX, OP_m1_AND_DPIndirectLongIndY	@ 3
	.long OP_SEC, OP_m1_AND_AbsIndY, OP_m1_DEC_A, OP_TSC, OP_m1_BIT_AbsIndX, OP_m1_AND_AbsIndX, OP_m1_ROL_AbsIndX, OP_m1_AND_AbsLongIndX
	.long OP_e0_RTI, OP_m1_EOR_DPIndIndirectX, OP_HAX42, OP_m1_EOR_SR, OP_x1_MVP, OP_m1_EOR_DP, OP_m1_LSR_DP, OP_m1_EOR_DPIndirectLong	@ 4
	.long OP_m1_PHA, OP_m1_EOR_Imm, OP_m1_LSR_A, OP_PHK, OP_JMP_Abs, OP_m1_EOR_Abs, OP_m1_LSR_Abs, OP_m1_EOR_AbsLong
	.long OP_BVC, OP_m1_EOR_DPIndirectIndY, OP_m1_EOR_DPIndirect, OP_m1_EOR_SRIndirectIndY, OP_x1_MVN, OP_m1_EOR_DPIndX, OP_m1_LSR_DPIndX, OP_m1_EOR_DPIndirectLongIndY	@ 5
	.long OP_CLI, OP_m1_EOR_AbsIndY, OP_x1_PHY, OP_TCD, OP_JMP_AbsLong, OP_m1_EOR_AbsIndX, OP_m1_LSR_AbsIndX, OP_m1_EOR_AbsLongIndX
	.long OP_RTS, OP_m1_ADC_DPIndIndirectX, OP_PER, OP_m1_ADC_SR, OP_m1_STZ_DP, OP_m1_ADC_DP, OP_m1_ROR_DP, OP_m1_ADC_DPIndirectLong	@ 6
	.long OP_m1_PLA, OP_m1_ADC_Imm, OP_m1_ROR_A, OP_RTL, OP_JMP_AbsIndirect, OP_m1_ADC_Abs, OP_m1_ROR_Abs, OP_m1_ADC_AbsLong
	.long OP_BVS, OP_m1_ADC_DPIndirectIndY, OP_m1_ADC_DPIndirect, OP_m1_ADC_SRIndirectIndY, OP_m1_STZ_DPIndX, OP_m1_ADC_DPIndX, OP_m1_ROR_DPIndX, OP_m1_ADC_DPIndirectLongIndY	@ 7
	.long OP_SEI, OP_m1_ADC_AbsIndY, OP_x1_PLY, OP_TDC, OP_JMP_AbsIndIndirect, OP_m1_ADC_AbsIndX, OP_m1_ROR_AbsIndX, OP_m1_ADC_AbsLongIndX
	.long OP_BRA, OP_m1_STA_DPIndIndirectX, OP_BRL, OP_m1_STA_SR, OP_x1_STY_DP, OP_m1_STA_DP, OP_x1_STX_DP, OP_m1_STA_DPIndirectLong	@ 8
	.long OP_x1_DEY, OP_m1_BIT_Imm, OP_m1_TXA, OP_PHB, OP_x1_STY_Abs, OP_m1_STA_Abs, OP_x1_STX_Abs, OP_m1_STA_AbsLong
	.long OP_BCC, OP_m1_STA_DPIndirectIndY, OP_m1_STA_DPIndirect, OP_m1_STA_SRIndirectIndY, OP_x1_STY_DPIndX, OP_m1_STA_DPIndX, OP_x1_STX_DPIndY, OP_m1_STA_DPIndirectLongIndY	@ 9
	.long OP_m1_TYA, OP_m1_STA_AbsIndY, OP_e0_TXS, OP_TXY, OP_m1_STZ_Abs, OP_m1_STA_AbsIndX, OP_m1_STZ_AbsIndX, OP_m1_STA_AbsLongIndX
	.long OP_x1_LDY_Imm, OP_m1_LDA_DPIndIndirectX, OP_x1_LDX_Imm, OP_m1_LDA_SR, OP_x1_LDY_DP, OP_m1_LDA_DP, OP_x1_LDX_DP, OP_m1_LDA_DPIndirectLong	@ A
	.long OP_x1_TAY, OP_m1_LDA_Imm, OP_x1_TAX, OP_PLB, OP_x1_LDY_Abs, OP_m1_LDA_Abs, OP_x1_LDX_Abs, OP_m1_LDA_AbsLong
	.long OP_BCS, OP_m1_LDA_DPIndirectIndY, OP_m1_LDA_DPIndirect, OP_m1_LDA_SRIndirectIndY, OP_x1_LDY_DPIndX, OP_m1_LDA_DPIndX, OP_x1_LDX_DPIndY, OP_m1_LDA_DPIndirectLongIndY	@ B
	.long OP_CLV, OP_m1_LDA_AbsIndY, OP_x1_TSX, OP_TYX, OP_x1_LDY_AbsIndX, OP_m1_LDA_AbsIndX, OP_x1_LDX_AbsIndY, OP_m1_LDA_AbsLongIndX
	.long OP_x1_CPY_Imm, OP_m1_CMP_DPIndIndirectX, OP_e0_REP, OP_m1_CMP_SR, OP_x1_CPY_DP, OP_m1_CMP_DP, OP_m1_DEC_DP, OP_m1_CMP_DPIndirectLong	@ C
	.long OP_x1_INY, OP_m1_CMP_Imm, OP_x1_DEX, OP_WAI, OP_x1_CPY_Abs, OP_m1_CMP_Abs, OP_m1_DEC_Abs, OP_m1_CMP_AbsLong
	.long OP_BNE, OP_m1_CMP_DPIndirectIndY, OP_m1_CMP_DPIndirect, OP_m1_CMP_SRIndirectIndY, OP_PEI, OP_m1_CMP_DPIndX, OP_m1_DEC_DPIndX, OP_m1_CMP_DPIndirectLongIndY	@ D
	.long OP_CLD, OP_m1_CMP_AbsIndY, OP_x1_PHX, OP_STP, OP_JMP_AbsIndirectLong, OP_m1_CMP_AbsIndX, OP_m1_DEC_AbsIndX, OP_m1_CMP_AbsLongIndX
	.long OP_x1_CPX_Imm, OP_m1_SBC_DPIndIndirectX, OP_e0_SEP, OP_m1_SBC_SR, OP_x1_CPX_DP, OP_m1_SBC_DP, OP_m1_INC_DP, OP_m1_SBC_DPIndirectLong	@ E
	.long OP_x1_INX, OP_m1_SBC_Imm, OP_NOP, OP_XBA, OP_x1_CPX_Abs, OP_m1_SBC_Abs, OP_m1_INC_Abs, OP_m1_SBC_AbsLong
	.long OP_BEQ, OP_m1_SBC_DPIndirectIndY, OP_m1_SBC_DPIndirect, OP_m1_SBC_SRIndirectIndY, OP_PEA, OP_m1_SBC_DPIndX, OP_m1_INC_DPIndX, OP_m1_SBC_DPIndirectLongIndY	@ F
	.long OP_SED, OP_m1_SBC_AbsIndY, OP_x1_PLX, OP_e0_XCE, OP_JSR_AbsIndIndirect, OP_m1_SBC_AbsIndX, OP_m1_INC_AbsIndX, OP_m1_SBC_AbsLongIndX

@ Emulation mode (e=1)
	.long OP_e1_BRK, OP_m1_ORA_DPIndIndirectX, OP_e1_COP, OP_m1_ORA_SR, OP_m1_TSB_DP, OP_m1_ORA_DP, OP_m1_ASL_DP, OP_m1_ORA_DPIndirectLong	@ 0
	.long OP_PHP, OP_m1_ORA_Imm, OP_m1_ASL_A, OP_PHD, OP_m1_TSB_Abs, OP_m1_ORA_Abs, OP_m1_ASL_Abs, OP_m1_ORA_AbsLong
	.long OP_BPL, OP_m1_ORA_DPIndirectIndY, OP_m1_ORA_DPIndirect, OP_m1_ORA_SRIndirectIndY, OP_m1_TRB_DP, OP_m1_ORA_DPIndX, OP_m1_ASL_DPIndX, OP_m1_ORA_DPIndirectLongIndY	@ 1
	.long OP_CLC, OP_m1_ORA_AbsIndY, OP_m1_INC_A, OP_e1_TCS, OP_m1_TRB_Abs, OP_m1_ORA_AbsIndX, OP_m1_ASL_AbsIndX, OP_m1_ORA_AbsLongIndX
	.long OP_JSR_Abs, OP_m1_AND_DPIndIndirectX, OP_JSL, OP_m1_AND_SR, OP_m1_BIT_DP, OP_m1_AND_DP, OP_m1_ROL_DP, OP_m1_AND_DPIndirectLong	@ 2
	.long OP_PLP, OP_m1_AND_Imm, OP_m1_ROL_A, OP_PLD, OP_m1_BIT_Abs, OP_m1_AND_Abs, OP_m1_ROL_Abs, OP_m1_AND_AbsLong
	.long OP_BMI, OP_m1_AND_DPIndirectIndY, OP_m1_AND_DPIndirect, OP_m1_AND_SRIndirectIndY, OP_m1_BIT_DPIndX, OP_m1_AND_DPIndX, OP_m1_ROL_DPIndX, OP_m1_AND_DPIndirectLongIndY	@ 3
	.long OP_SEC, OP_m1_AND_AbsIndY, OP_m1_DEC_A, OP_TSC, OP_m1_BIT_AbsIndX, OP_m1_AND_AbsIndX, OP_m1_ROL_AbsIndX, OP_m1_AND_AbsLongIndX
	.long OP_e1_RTI, OP_m1_EOR_DPIndIndirectX, OP_HAX42, OP_m1_EOR_SR, OP_x1_MVP, OP_m1_EOR_DP, OP_m1_LSR_DP, OP_m1_EOR_DPIndirectLong	@ 4
	.long OP_m1_PHA, OP_m1_EOR_Imm, OP_m1_LSR_A, OP_PHK, OP_JMP_Abs, OP_m1_EOR_Abs, OP_m1_LSR_Abs, OP_m1_EOR_AbsLong
	.long OP_BVC, OP_m1_EOR_DPIndirectIndY, OP_m1_EOR_DPIndirect, OP_m1_EOR_SRIndirectIndY, OP_x1_MVN, OP_m1_EOR_DPIndX, OP_m1_LSR_DPIndX, OP_m1_EOR_DPIndirectLongIndY	@ 5
	.long OP_CLI, OP_m1_EOR_AbsIndY, OP_x1_PHY, OP_TCD, OP_JMP_AbsLong, OP_m1_EOR_AbsIndX, OP_m1_LSR_AbsIndX, OP_m1_EOR_AbsLongIndX
	.long OP_RTS, OP_m1_ADC_DPIndIndirectX, OP_PER, OP_m1_ADC_SR, OP_m1_STZ_DP, OP_m1_ADC_DP, OP_m1_ROR_DP, OP_m1_ADC_DPIndirectLong	@ 6
	.long OP_m1_PLA, OP_m1_ADC_Imm, OP_m1_ROR_A, OP_RTL, OP_JMP_AbsIndirect, OP_m1_ADC_Abs, OP_m1_ROR_Abs, OP_m1_ADC_AbsLong
	.long OP_BVS, OP_m1_ADC_DPIndirectIndY, OP_m1_ADC_DPIndirect, OP_m1_ADC_SRIndirectIndY, OP_m1_STZ_DPIndX, OP_m1_ADC_DPIndX, OP_m1_ROR_DPIndX, OP_m1_ADC_DPIndirectLongIndY	@ 7
	.long OP_SEI, OP_m1_ADC_AbsIndY, OP_x1_PLY, OP_TDC, OP_JMP_AbsIndIndirect, OP_m1_ADC_AbsIndX, OP_m1_ROR_AbsIndX, OP_m1_ADC_AbsLongIndX
	.long OP_BRA, OP_m1_STA_DPIndIndirectX, OP_BRL, OP_m1_STA_SR, OP_x1_STY_DP, OP_m1_STA_DP, OP_x1_STX_DP, OP_m1_STA_DPIndirectLong	@ 8
	.long OP_x1_DEY, OP_m1_BIT_Imm, OP_m1_TXA, OP_PHB, OP_x1_STY_Abs, OP_m1_STA_Abs, OP_x1_STX_Abs, OP_m1_STA_AbsLong
	.long OP_BCC, OP_m1_STA_DPIndirectIndY, OP_m1_STA_DPIndirect, OP_m1_STA_SRIndirectIndY, OP_x1_STY_DPIndX, OP_m1_STA_DPIndX, OP_x1_STX_DPIndY, OP_m1_STA_DPIndirectLongIndY	@ 9
	.long OP_m1_TYA, OP_m1_STA_AbsIndY, OP_e1_TXS, OP_TXY, OP_m1_STZ_Abs, OP_m1_STA_AbsIndX, OP_m1_STZ_AbsIndX, OP_m1_STA_AbsLongIndX
	.long OP_x1_LDY_Imm, OP_m1_LDA_DPIndIndirectX, OP_x1_LDX_Imm, OP_m1_LDA_SR, OP_x1_LDY_DP, OP_m1_LDA_DP, OP_x1_LDX_DP, OP_m1_LDA_DPIndirectLong	@ A
	.long OP_x1_TAY, OP_m1_LDA_Imm, OP_x1_TAX, OP_PLB, OP_x1_LDY_Abs, OP_m1_LDA_Abs, OP_x1_LDX_Abs, OP_m1_LDA_AbsLong
	.long OP_BCS, OP_m1_LDA_DPIndirectIndY, OP_m1_LDA_DPIndirect, OP_m1_LDA_SRIndirectIndY, OP_x1_LDY_DPIndX, OP_m1_LDA_DPIndX, OP_x1_LDX_DPIndY, OP_m1_LDA_DPIndirectLongIndY	@ B
	.long OP_CLV, OP_m1_LDA_AbsIndY, OP_x1_TSX, OP_TYX, OP_x1_LDY_AbsIndX, OP_m1_LDA_AbsIndX, OP_x1_LDX_AbsIndY, OP_m1_LDA_AbsLongIndX
	.long OP_x1_CPY_Imm, OP_m1_CMP_DPIndIndirectX, OP_e1_REP, OP_m1_CMP_SR, OP_x1_CPY_DP, OP_m1_CMP_DP, OP_m1_DEC_DP, OP_m1_CMP_DPIndirectLong	@ C
	.long OP_x1_INY, OP_m1_CMP_Imm, OP_x1_DEX, OP_WAI, OP_x1_CPY_Abs, OP_m1_CMP_Abs, OP_m1_DEC_Abs, OP_m1_CMP_AbsLong
	.long OP_BNE, OP_m1_CMP_DPIndirectIndY, OP_m1_CMP_DPIndirect, OP_m1_CMP_SRIndirectIndY, OP_PEI, OP_m1_CMP_DPIndX, OP_m1_DEC_DPIndX, OP_m1_CMP_DPIndirectLongIndY	@ D
	.long OP_CLD, OP_m1_CMP_AbsIndY, OP_x1_PHX, OP_STP, OP_JMP_AbsIndirectLong, OP_m1_CMP_AbsIndX, OP_m1_DEC_AbsIndX, OP_m1_CMP_AbsLongIndX
	.long OP_x1_CPX_Imm, OP_m1_SBC_DPIndIndirectX, OP_e1_SEP, OP_m1_SBC_SR, OP_x1_CPX_DP, OP_m1_SBC_DP, OP_m1_INC_DP, OP_m1_SBC_DPIndirectLong	@ E
	.long OP_x1_INX, OP_m1_SBC_Imm, OP_NOP, OP_XBA, OP_x1_CPX_Abs, OP_m1_SBC_Abs, OP_m1_INC_Abs, OP_m1_SBC_AbsLong
	.long OP_BEQ, OP_m1_SBC_DPIndirectIndY, OP_m1_SBC_DPIndirect, OP_m1_SBC_SRIndirectIndY, OP_PEA, OP_m1_SBC_DPIndX, OP_m1_INC_DPIndX, OP_m1_SBC_DPIndirectLongIndY	@ F
	.long OP_SED, OP_m1_SBC_AbsIndY, OP_x1_PLX, OP_e1_XCE, OP_JSR_AbsIndIndirect, OP_m1_SBC_AbsIndX, OP_m1_INC_AbsIndX, OP_m1_SBC_AbsLongIndX
	

.macro SetOpcodeTable
	ldr opTable, =OpTableStart
	tst snesP, #flagE
	addne opTable, opTable, #0x1000
	bne 1f
		tst snesP, #flagM
		addne opTable, opTable, #0x800
		tst snesP, #flagX
		addne opTable, opTable, #0x400
1:
.endm

.macro UpdateCPUMode
	SetOpcodeTable
	tst snesP, #flagE
	bicne snesS, snesS, #0xFF000000
	orrne snesS, snesS, #0x01000000
	tsteq snesP, #flagX
	andne snesX, snesX, #0xFF
	andne snesY, snesY, #0xFF
.endm

@ --- Misc. functions ---------------------------------------------------------
	
.global CPU_Reset
.global CPU_Run
.global CPU_TriggerIRQ
.global CPU_TriggerNMI

.macro LoadRegs
	ldr r0, =CPU_Regs
	ldmia r0, {r4-r12}
.endm

.macro StoreRegs
	ldr r0, =CPU_Regs
	stmia r0, {r4-r12}
.endm


.macro SetPC src=r0
	mov \src, \src, lsl #0x10
	mov snesPC, snesPC, lsl #0x10
	orr snesPC, \src, snesPC, lsr #0x10
.endm


@ add fast cycles (for CPU IO cycles)
.macro AddCycles num, cond=
	sub\cond snesCycles, snesCycles, #(\num * 0x60000)
.endm


CPU_Reset:
	stmdb sp!, {r3-r12, lr}
	bl SNES_Reset
	
	mov snesA, #0
	mov snesX, #0
	mov snesY, #0
	ldr snesS, =0x01FF0000	@ also PBR
	mov snesD, #0			@ also DBR
	ldr snesP, =0x00000534	@ we'll do PC later
	
	ldr memoryMap, =Mem_PtrTable
	ldr memoryMap, [memoryMap]
	SetOpcodeTable
	
	ldr r0, =ROM_Bank0End
	ldr r0, [r0]
	sub r0, r0, #vec_Reset
	ldrh r0, [r0]
	orr snesPC, snesPC, r0, lsl #0x10
	
	mov snesCycles, #0
	ldr r0, =CPU_Cycles
	mov r1, #0
	str r1, [r0]
	StoreRegs
	
	ldmia sp!, {r3-r12, lr}
	bx lr
	
CPU_TriggerIRQ:
	bic r3, snesS, #0x18000000
	ldr r3, [memoryMap, r3, lsr #0x1B]
	tst snesP, #flagE
	subne snesS, snesS, #0x30000
	subne snesCycles, snesCycles, #0x180000
	subeq snesS, snesS, #0x40000
	subeq snesCycles, snesCycles, #0x200000
	tst r3, #0x4
	bic r3, r3, #0xF
	mov r2, snesS, lsl #0x3
	add r3, r3, r2, lsr #0x13
	bne irq_nostack
	tst snesP, #flagE
	streqb snesPBR, [r3, #4]
	mov r0, snesPC, lsr #0x10
	strh r0, [r3, #2]
	strb snesP, [r3, #1]
irq_nostack:
	bic snesP, snesP, #(flagD|flagW)
	orr snesP, snesP, #flagI
	bic snesPBR, snesPBR, #0xFF
	ldr r0, =ROM_Bank0End
	ldr r0, [r0]
	tst snesP, #flagE
	subeq r0, r0, #vec_e0_IRQ
	subne r0, r0, #vec_e1_IRQ
	ldrh r0, [r0]
	SetPC
	b irq_end
	
CPU_TriggerNMI:
	bic r3, snesS, #0x18000000
	ldr r3, [memoryMap, r3, lsr #0x1B]
	tst snesP, #flagE
	subne snesS, snesS, #0x30000
	subne snesCycles, snesCycles, #0x180000
	subeq snesS, snesS, #0x40000
	subeq snesCycles, snesCycles, #0x200000
	tst r3, #0x4
	bic r3, r3, #0xF
	mov r2, snesS, lsl #0x3
	add r3, r3, r2, lsr #0x13
	bne nmi_nostack
	tst snesP, #flagE
	streqb snesPBR, [r3, #4]
	mov r0, snesPC, lsr #0x10
	strh r0, [r3, #2]
	strb snesP, [r3, #1]
nmi_nostack:
	bic snesP, snesP, #(flagD|flagW)
	orr snesP, snesP, #flagI
	bic snesPBR, snesPBR, #0xFF
	ldr r0, =ROM_Bank0End
	ldr r0, [r0]
	tst snesP, #flagE
	subeq r0, r0, #vec_e0_NMI
	subne r0, r0, #vec_e1_NMI
	ldrh r0, [r0]
	SetPC
	b newline
	
.global CPU_GetPC
CPU_GetPC:
	mov r0, snesPC, lsr #0x10
	orr r0, r0, snesPBR, lsl #0x10
	bx lr
	
.global CPU_GetReg
CPU_GetReg:
	cmp r0, #0xC
	moveq r0, snesA
	bxeq lr
	cmp r0, #0xB
	moveq r0, snesX
	bxeq lr
	cmp r0, #0xA
	moveq r0, snesY
	bxeq lr
	mov r0, #0
	bx lr
	
@ --- Main loop ---------------------------------------------------------------

.section    .data, "aw", %progbits

CPU_Cycles:
	.long 0
	
.global debugpc
debugpc:
	.long 0
	
CPU_TimeInFrame:
	.long 0
	
.section    .text, "awx", %progbits


CPU_Schedule:
	@stmdb r12
	
	ldr r3, =CPU_TimeInFrame
	ldr r0, [r3]
	ldr r1, [memoryMap, #-0x14]
	add r0, r0, r1
	str r0, [r3]
	
	@ r0 = total time within frame
	@ r1 = current scanline
	@ r2 = current scanline * scanline time
	@ r3 = time within scanline
	@ r4 = time to current event
	@ r5 = funcptr of current event
	@ r12 = temp
	
	ldr r1, =PPU_VCount
	ldrh r1, [r1]
	
	ldr r12, =1324			@ minus 40 refresh cycles. TODO: checkme
	mul r2, r1, r12
	sub r3, r0, r2
	
	@ check the most probable events first
	
	ldrb r3, [memoryMap, #-0x5] @ HVBFlags
	tst r3, #0x40
	bne sched_inside_hblank
		@ outside of HBlank
		rsb r4, r3, #1024
		add r4, r4, r2
		@ldr r5, =CPU_Event_HBlankStart
		
		b sched_hbl_end
	sched_inside_hblank:
		@ inside of HBlank
		rsb r4, r3, r12
		add r4, r4, r2
		@ldr r5, =CPU_Event_HBlankEnd
		
	sched_hbl_end:
	
	
	
	@ldmia


_CPU_Run:
	stmdb sp!, {r4-r11, lr}
	LoadRegs

	ldr r3, =CPU_TimeInFrame
	@ldr r0, =357368 @ TODO: PAL timing
	mov r0, #0
	str r0, [r3]
	str r0, [memoryMap, #-0x14]
	bl CPU_Schedule
runloop:
	OpcodePrefetch8
	ldr pc, [opTable, r0, lsl #0x2]
	
	_op_return:
	cmp snesCycles, #0
	bgt runloop
	
	@ -- execute event
	@ -- check if end of frame
	bl CPU_Schedule
	b runloop

frameend:
	StoreRegs
	ldmia sp!, {r4-r11, pc}
	
	
	
CPU_Run:
	stmdb sp!, {r4-r11, lr}
	LoadRegs

frameloop:
		ldr r0, =0x05540000
		add snesCycles, snesCycles, r0
		
		mov r0, #0
		ldr r1, =PPU_VCount
		strh r0, [r1]
		stmdb sp!, {r12}
		SafeCall DMA_ReloadHDMA
		bl DMA_DoHDMA
		mov r0, #0
		bl PPU_RenderScanline
		ldmia sp!, {r12}
		b emuloop
		
newline:
			ldr r0, =0x05540001
			add snesCycles, snesCycles, r0
			
			ldr r0, =PPU_VCount
			strh snesCycles, [r0]
			ldrh r0, [r0]
			cmp r0, #0xE0
			bge emuloop
			SafeCall PPU_RenderScanline
			
emuloop:
				mov r3, snesCycles, asr #0x10
				ldrh r0, [memoryMap, #-0x6] @ IRQ cond in lower bits, flags in higher bits
				tst r0, #0x4000				@ check if we gotta handle the HBlank
				bne hblank_end
				cmp r3, #0x10C
				bgt hblank_end
				orr r0, r0, #0x4000
				strh r0, [memoryMap, #-0x6]
				SafeCall_03 DMA_DoHDMA
				
hblank_end:
				tst r0, #0x0800				@ check if we already triggered an IRQ in this scanline
				bne irq_end
				tst snesP, #flagI
				bne irq_end
				
				and r0, r0, #0x0003
				ldr pc, [pc, r0, lsl #0x2]
				nop
				.long irq_end
				.long irq_h
				.long irq_v
				.long irq_hv
				
irq_h:
				ldr r0, =SNES_HMatch
				ldrh r0, [r0]
				cmp r3, r0					@ r3 = cycle count
				ble irq_trigger
				b irq_end
				
irq_v:
				ldr r0, =SNES_VMatch
				ldrh r0, [r0]
				ldr r1, =PPU_VCount
				ldrh r1, [r1]
				cmp r0, r1
				beq irq_trigger
				@ if this isn't the right scanline, set the flag so we don't bother checking again
				ldrb r0, [memoryMap, #-0x5]
				orr r0, r0, #0x08
				strb r0, [memoryMap, #-0x5]
				b irq_end

irq_hv:
				ldr r0, =SNES_VMatch
				ldrh r0, [r0]
				ldr r1, =PPU_VCount
				ldrh r1, [r1]
				cmp r0, r1
				ldrneb r0, [memoryMap, #-0x5]
				orrne r0, r0, #0x08
				strneb r0, [memoryMap, #-0x5]
				bne irq_end
				ldr r0, =SNES_HMatch
				ldrh r0, [r0]
				cmp r3, r0					@ r3 = cycle count
				bgt irq_end
				
irq_trigger:
				ldrb r0, [memoryMap, #-0x5]
				orr r0, r0, #0x18
				strb r0, [memoryMap, #-0x5]
				b CPU_TriggerIRQ
				
irq_end:
				tst snesP, #flagW
				subne snesPC, snesPC, #0x10000
				
				@ debug code
				@mov r0, snesPC, lsr #0x10
				@orr r0, r0, snesPBR, lsl #0x10
				@ldr r1, =debugpc
				@str r0, [r1]
				@ debug code end

				OpcodePrefetch8
				ldr pc, [opTable, r0, lsl #0x2]
op_return:
				cmp snesCycles, #0x00010000
				bge emuloop
				
emulate_hardware:
			ldrb r2, [memoryMap, #-0x5]
			bic r2, r2, #0x48				@ clear HBlank and per-scanline IRQ flags
			strb r2, [memoryMap, #-0x5]
			mov r1, snesCycles, lsl #0x10
			
			cmp r1, #0xDF0000
			orrge r2, r2, #0x80
			orreq r2, r2, #0x20
			strgeb r2, [memoryMap, #-0x5]
			bge vblank
			bic r2, r2, #0xA0
			strb r2, [memoryMap, #-0x5]
			b newline
			
vblank:
			bne vblank_notfirst
			SafeCall PPU_VBlank
			tst snesP, #flagI2
			beq CPU_TriggerNMI
			
vblank_notfirst:
			ldr r3, =261
			cmp r1, r3, lsl #0x10
			blt newline
			
		sub snesCycles, snesCycles, r3
		ldr r0, =262
		ldr r1, =PPU_VCount
		strh r0, [r1]
		
	StoreRegs
	ldmia sp!, {r4-r11, pc}
		
.ltorg


.macro EatCycles
	cmp snesCycles, #0x00010000
	blt 1f
	mov r3, snesCycles, asr #0x10
	ldr r0, =SNES_HMatch
	ldrh r0, [r0]
	cmp r3, r0
	eor snesCycles, snesCycles, r3, lsl #0x10
	movgt r3, r0
	movle r3, #1
	orr snesCycles, snesCycles, r3, lsl #0x10
1:
.endm
	
@ --- Addressing modes --------------------------------------------------------
@ TODO: indexed addressing modes must add one cycle if adding index crosses a
@ page boundary (haven't yet figured out a fast way to handle it)

.macro _DoPrefetch sixteen
	.ifeq \sixteen-16
		Prefetch16
	.else
		Prefetch8
	.endif
.endm

.macro _DoMemRead sixteen
	.ifeq \sixteen-16
		MemRead16
	.else
		MemRead8
	.endif
.endm


.macro GetAddr_Abs
	Prefetch16
	orr r0, r0, snesDBR, lsl #0x10
.endm

.macro GetAddr_AbsIndirect
	Prefetch16
	MemRead16
.endm

.macro GetAddr_AbsIndIndirect
	Prefetch16
	add r0, r0, snesPBR, lsl #0x10
	add r0, r0, snesX
	MemRead16
.endm

.macro GetAddr_AbsLong
	Prefetch24
.endm

.macro GetAddr_AbsIndirectLong
	Prefetch16
	MemRead24
.endm

.macro GetAddr_DP
	tst snesD, #0xFF0000
	AddCycles 1, ne
	Prefetch8
	add r0, r0, snesD, lsr #0x10
.endm

.macro GetAddr_DPIndirect
	tst snesD, #0xFF0000
	AddCycles 1, ne
	Prefetch8
	add r0, r0, snesD, lsr #0x10
	MemRead16
	orr r0, r0, snesDBR, lsl #0x10
.endm

.macro GetAddr_DPIndirectLong
	tst snesD, #0xFF0000
	AddCycles 1, ne
	Prefetch8
	add r0, r0, snesD, lsr #0x10
	MemRead24
.endm

.macro GetAddr_AbsIndX
	Prefetch16
	add r0, r0, snesX
	orr r0, r0, snesDBR, lsl #0x10
.endm

.macro GetAddr_AbsLongIndX
	Prefetch24
	add r0, r0, snesX
.endm

.macro GetAddr_AbsIndY
	Prefetch16
	add r0, r0, snesY
	orr r0, r0, snesDBR, lsl #0x10
.endm

.macro GetAddr_DPIndX
	tst snesD, #0xFF0000
	AddCycles 2, ne
	AddCycles 1, eq
	Prefetch8
	add r0, r0, snesD, lsr #0x10
	add r0, r0, snesX
.endm

.macro GetAddr_DPIndY
	tst snesD, #0xFF0000
	AddCycles 2, ne
	AddCycles 1, eq
	Prefetch8
	add r0, r0, snesD, lsr #0x10
	add r0, r0, snesY
.endm

.macro GetAddr_DPIndIndirectX
	tst snesD, #0xFF0000
	AddCycles 2, ne
	AddCycles 1, eq
	Prefetch8
	add r0, r0, snesD, lsr #0x10
	add r0, r0, snesX
	MemRead16
	orr r0, r0, snesDBR, lsl #0x10
.endm

.macro GetAddr_DPIndirectIndY
	tst snesD, #0xFF0000
	AddCycles 1, ne
	Prefetch8
	add r0, r0, snesD, lsr #0x10
	MemRead16
	add r0, r0, snesY
	orr r0, r0, snesDBR, lsl #0x10
.endm

.macro GetAddr_DPIndirectLongIndY
	tst snesD, #0xFF0000
	AddCycles 1, ne
	Prefetch8
	add r0, r0, snesD, lsr #0x10
	MemRead24
	add r0, r0, snesY
.endm

.macro GetAddr_SR
	AddCycles 1
	Prefetch8
	add r0, r0, snesS, lsr #0x10
.endm

.macro GetAddr_SRIndirectIndY
	AddCycles 2
	Prefetch8
	add r0, r0, snesS, lsr #0x10
	MemRead16
	add r0, r0, snesY
	orr r0, r0, snesDBR, lsl #0x10
.endm


.macro GetOp_Imm sixteen
	_DoPrefetch \sixteen
.endm

.macro GetOp_Abs sixteen
	GetAddr_Abs
	_DoMemRead \sixteen
.endm

.macro GetOp_AbsLong sixteen
	GetAddr_AbsLong
	_DoMemRead \sixteen
.endm

.macro GetOp_DP sixteen
	GetAddr_DP
	_DoMemRead \sixteen
.endm

.macro GetOp_DPIndirect sixteen
	GetAddr_DPIndirect
	_DoMemRead \sixteen
.endm

.macro GetOp_DPIndirectLong sixteen
	GetAddr_DPIndirectLong
	_DoMemRead \sixteen
.endm

.macro GetOp_AbsIndX sixteen
	GetAddr_AbsIndX
	_DoMemRead \sixteen
.endm

.macro GetOp_AbsLongIndX sixteen
	GetAddr_AbsLongIndX
	_DoMemRead \sixteen
.endm

.macro GetOp_AbsIndY sixteen
	GetAddr_AbsIndY
	_DoMemRead \sixteen
.endm

.macro GetOp_DPIndX sixteen
	GetAddr_DPIndX
	_DoMemRead \sixteen
.endm

.macro GetOp_DPIndY sixteen
	GetAddr_DPIndY
	_DoMemRead \sixteen
.endm

.macro GetOp_DPIndIndirectX sixteen
	GetAddr_DPIndIndirectX
	_DoMemRead \sixteen
.endm

.macro GetOp_DPIndirectIndY sixteen
	GetAddr_DpIndirectIndY
	_DoMemRead \sixteen
.endm

.macro GetOp_DPIndirectLongIndY sixteen
	GetAddr_DPIndirectLongIndY
	_DoMemRead \sixteen
.endm

.macro GetOp_SR sixteen
	GetAddr_SR
	_DoMemRead \sixteen
.endm

.macro GetOp_SRIndirectIndY sixteen
	GetAddr_SRIndirectIndY
	_DoMemRead \sixteen
.endm

@ --- unknown -----------------------------------------------------------------

report_unk:
	stmdb sp!, {r0-r12, lr}
	bl report_unk_lol
	ldmia sp!, {r0-r12, lr}
	bx lr

OP_UNK:
	mov r1, snesPC, lsr #0x10
	orr r1, r1, snesPBR, lsl #0x10
	bl report_unk
	AddCycles 1
	b op_return
	
@ --- ADC ---------------------------------------------------------------------

.macro ADC_8
	and r1, snesA, #0xFF
	add r1, r1, r0
	tst snesP, #flagC
	addne r1, r1, #0x1
	tst snesP, #flagD
	beq 1f
		and r2, r1, #0xF
		cmp r2, #0xA
		addge r1, r1, #0x6
		and r2, r1, #0xF0
		cmp r2, #0xA0
		addge r1, r1, #0x60
1:
	bic snesP, snesP, #flagNVZC
	tst r1, #0x80
	orrne snesP, snesP, #flagN
	tst r1, #0x100
	orrne snesP, snesP, #flagC
	eor r2, snesA, r0
	tst r2, #0x80
	bne 2f
	eor r2, snesA, r1
	tst r2, #0x80
	orrne snesP, snesP, #flagV
2:
	and r1, r1, #0xFF
	bic snesA, snesA, #0xFF
	orr snesA, snesA, r1
	cmp r1, #0
	orreq snesP, snesP, #flagZ
	b op_return
.endm

.macro ADC_16
	add r1, snesA, r0
	tst snesP, #flagC
	addne r1, r1, #0x1
	tst snesP, #flagD
	beq 1f
		and r2, r1, #0xF
		cmp r2, #0xA
		addge r1, r1, #0x6
		and r2, r1, #0xF0
		cmp r2, #0xA0
		addge r1, r1, #0x60
		and r2, r1, #0xF00
		cmp r2, #0xA00
		addge r1, r1, #0x600
		and r2, r1, #0xF000
		cmp r2, #0xA000
		addge r1, r1, #0x6000
1:
	bic snesP, snesP, #flagNVZC
	tst r1, #0x8000
	orrne snesP, snesP, #flagN
	tst r1, #0x10000
	orrne snesP, snesP, #flagC
	eor r2, snesA, r0
	tst r2, #0x8000
	bne 2f
	eor r2, snesA, r1
	tst r2, #0x8000
	orrne snesP, snesP, #flagV
2:
	mov r1, r1, lsl #0x10
	movs snesA, r1, lsr #0x10
	orreq snesP, snesP, #flagZ
	b op_return
.endm

OP_m0_ADC_Imm:
	GetOp_Imm 16
	ADC_16
	
OP_m1_ADC_Imm:
	GetOp_Imm 8
	ADC_8
	
OP_m0_ADC_Abs:
	GetOp_Abs 16
	ADC_16
	
OP_m1_ADC_Abs:
	GetOp_Abs 8
	ADC_8
	
OP_m0_ADC_AbsLong:
	GetOp_AbsLong 16
	ADC_16
	
OP_m1_ADC_AbsLong:
	GetOp_AbsLong 8
	ADC_8
	
OP_m0_ADC_DP:
	GetOp_DP 16
	ADC_16
	
OP_m1_ADC_DP:
	GetOp_DP 8
	ADC_8
	
OP_m0_ADC_DPIndirect:
	GetOp_DPIndirect 16
	ADC_16
	
OP_m1_ADC_DPIndirect:
	GetOp_DPIndirect 8
	ADC_8
	
OP_m0_ADC_DPIndirectLong:
	GetOp_DPIndirectLong 16
	ADC_16
	
OP_m1_ADC_DPIndirectLong:
	GetOp_DPIndirectLong 8
	ADC_8
	
OP_m0_ADC_AbsIndX:
	GetOp_AbsIndX 16
	ADC_16
	
OP_m1_ADC_AbsIndX:
	GetOp_AbsIndX 8
	ADC_8
	
OP_m0_ADC_AbsLongIndX:
	GetOp_AbsLongIndX 16
	ADC_16
	
OP_m1_ADC_AbsLongIndX:
	GetOp_AbsLongIndX 8
	ADC_8
	
OP_m0_ADC_AbsIndY:
	GetOp_AbsIndY 16
	ADC_16
	
OP_m1_ADC_AbsIndY:
	GetOp_AbsIndY 8
	ADC_8
	
OP_m0_ADC_DPIndX:
	GetOp_DPIndX 16
	ADC_16
	
OP_m1_ADC_DPIndX:
	GetOp_DPIndX 8
	ADC_8
	
OP_m0_ADC_DPIndIndirectX:
	GetOp_DPIndIndirectX 16
	ADC_16
	
OP_m1_ADC_DPIndIndirectX:
	GetOp_DPIndIndirectX 8
	ADC_8
	
OP_m0_ADC_DPIndirectIndY:
	GetOp_DPIndirectIndY 16
	ADC_16
	
OP_m1_ADC_DPIndirectIndY:
	GetOp_DPIndirectIndY 8
	ADC_8
	
OP_m0_ADC_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 16
	ADC_16
	
OP_m1_ADC_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 8
	ADC_8
	
OP_m0_ADC_SR:
	GetOp_SR 16
	ADC_16
	
OP_m1_ADC_SR:
	GetOp_SR 8
	ADC_8
	
OP_m0_ADC_SRIndirectIndY:
	GetOp_SRIndirectIndY 16
	ADC_16
	
OP_m1_ADC_SRIndirectIndY:
	GetOp_SRIndirectIndY 8
	ADC_8

@ --- AND ---------------------------------------------------------------------

.macro AND_8
	orr r0, r0, #0xFF00
	and snesA, snesA, r0
	bic snesP, snesP, #flagNZ
	tst snesA, #0xFF
	orreq snesP, snesP, #flagZ
	tst snesA, #0x80
	orrne snesP, snesP, #flagN
	b op_return
.endm

.macro AND_16
	ands snesA, snesA, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesA, #0x8000
	orrne snesP, snesP, #flagN
	b op_return
.endm

OP_m0_AND_Imm:
	GetOp_Imm 16
	AND_16
	
OP_m1_AND_Imm:
	GetOp_Imm 8
	AND_8
	
OP_m0_AND_Abs:
	GetOp_Abs 16
	AND_16
	
OP_m1_AND_Abs:
	GetOp_Abs 8
	AND_8
	
OP_m0_AND_AbsLong:
	GetOp_AbsLong 16
	AND_16
	
OP_m1_AND_AbsLong:
	GetOp_AbsLong 8
	AND_8
	
OP_m0_AND_DP:
	GetOp_DP 16
	AND_16
	
OP_m1_AND_DP:
	GetOp_DP 8
	AND_8
	
OP_m0_AND_DPIndirect:
	GetOp_DPIndirect 16
	AND_16
	
OP_m1_AND_DPIndirect:
	GetOp_DPIndirect 8
	AND_8
	
OP_m0_AND_DPIndirectLong:
	GetOp_DPIndirectLong 16
	AND_16
	
OP_m1_AND_DPIndirectLong:
	GetOp_DPIndirectLong 8
	AND_8
	
OP_m0_AND_AbsIndX:
	GetOp_AbsIndX 16
	AND_16
	
OP_m1_AND_AbsIndX:
	GetOp_AbsIndX 8
	AND_8
	
OP_m0_AND_AbsLongIndX:
	GetOp_AbsLongIndX 16
	AND_16
	
OP_m1_AND_AbsLongIndX:
	GetOp_AbsLongIndX 8
	AND_8
	
OP_m0_AND_AbsIndY:
	GetOp_AbsIndY 16
	AND_16
	
OP_m1_AND_AbsIndY:
	GetOp_AbsIndY 8
	AND_8
	
OP_m0_AND_DPIndX:
	GetOp_DPIndX 16
	AND_16
	
OP_m1_AND_DPIndX:
	GetOp_DPIndX 8
	AND_8
	
OP_m0_AND_DPIndIndirectX:
	GetOp_DPIndIndirectX 16
	AND_16
	
OP_m1_AND_DPIndIndirectX:
	GetOp_DPIndIndirectX 8
	AND_8
	
OP_m0_AND_DPIndirectIndY:
	GetOp_DPIndirectIndY 16
	AND_16
	
OP_m1_AND_DPIndirectIndY:
	GetOp_DPIndirectIndY 8
	AND_8
	
OP_m0_AND_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 16
	AND_16
	
OP_m1_AND_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 8
	AND_8
	
OP_m0_AND_SR:
	GetOp_SR 16
	AND_16
	
OP_m1_AND_SR:
	GetOp_SR 8
	AND_8
	
OP_m0_AND_SRIndirectIndY:
	GetOp_SRIndirectIndY 16
	AND_16
	
OP_m1_AND_SRIndirectIndY:
	GetOp_SRIndirectIndY 8
	AND_8
	
@ --- ASL ---------------------------------------------------------------------

.macro ASL_8 dst=r0
	mov r0, r0, lsl #1
	bic snesP, snesP, #flagNZC
	tst r0, #0x80
	orrne snesP, snesP, #flagN
	tst r0, #0x100
	orrne snesP, snesP, #flagC
	bics \dst, r0, #0x100
	orreq snesP, snesP, #flagZ
	AddCycles 1
.endm

.macro ASL_16 src=r0, dst=r0
	mov r0, \src, lsl #1
	bic snesP, snesP, #flagNZC
	tst r0, #0x8000
	orrne snesP, snesP, #flagN
	tst r0, #0x10000
	orrne snesP, snesP, #flagC
	bics \dst, r0, #0x10000
	orreq snesP, snesP, #flagZ
	AddCycles 1
.endm

OP_m0_ASL_A:
	ASL_16 snesA, snesA
	b op_return
	
OP_m1_ASL_A:
	and r0, snesA, #0xFF
	ASL_8
	bic snesA, snesA, #0xFF
	orr snesA, snesA, r0
	b op_return
	
OP_m0_ASL_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead16
	ASL_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ASL_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead8
	ASL_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_ASL_DP:
	GetAddr_DP
	mov r2, r0
	MemRead16
	ASL_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ASL_DP:
	GetAddr_DP
	mov r2, r0
	MemRead8
	ASL_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_ASL_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead16
	ASL_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ASL_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead8
	ASL_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_ASL_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead16
	ASL_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ASL_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead8
	ASL_8 r1
	mov r0, r2
	MemWrite8
	b op_return

@ --- Bxx (branch) ------------------------------------------------------------
@ TODO: add 1 cycle if branch crosses page boundary when e=1 (who cares?)

.macro BRANCH flag=0, cond=0
	.ifne \flag
		tst snesP, #\flag
		.ifeq \cond
			beq 1f
		.else
			bne 1f
		.endif
			add snesPC, snesPC, #0x10000
			b op_return
1:
	.endif
	Prefetch8
	mov r0, r0, lsl #0x18
	add snesPC, snesPC, r0, asr #0x8
	AddCycles 1
	b op_return
.endm

OP_BCC:
	BRANCH flagC, 0

OP_BCS:
	BRANCH flagC, 1

OP_BEQ:
	BRANCH flagZ, 1

OP_BMI:
	BRANCH flagN, 1

OP_BNE:
	BRANCH flagZ, 0

OP_BPL:
	BRANCH flagN, 0
	
OP_BRA:
	BRANCH
	
OP_BRL:
	Prefetch16
	mov r0, r0, lsl #0x10
	add snesPC, snesPC, r0
	AddCycles 1
	b op_return

OP_BVC:
	BRANCH flagV, 0
	
OP_BVS:
	BRANCH flagV, 1
	
@ --- BIT ---------------------------------------------------------------------

.macro BIT_8
	bic snesP, snesP, #flagNVZ
	tst r0, #0x80
	orrne snesP, snesP, #flagN
	tst r0, #0x40
	orrne snesP, snesP, #flagV
	tst r0, snesA
	orreq snesP, snesP, #flagZ
	b op_return
.endm

.macro BIT_16
	bic snesP, snesP, #flagNVZ
	tst r0, #0x8000
	orrne snesP, snesP, #flagN
	tst r0, #0x4000
	orrne snesP, snesP, #flagV
	tst r0, snesA
	orreq snesP, snesP, #flagZ
	b op_return
.endm

OP_m0_BIT_Imm:
	GetOp_Imm 16
	tst r0, snesA
	orreq snesP, snesP, #flagZ
	bicne snesP, snesP, #flagZ
	b op_return

OP_m1_BIT_Imm:
	GetOp_Imm 8
	tst r0, snesA
	orreq snesP, snesP, #flagZ
	bicne snesP, snesP, #flagZ
	b op_return
	
OP_m0_BIT_Abs:
	GetOp_Abs 16
	BIT_16
	
OP_m1_BIT_Abs:
	GetOp_Abs 8
	BIT_8
	
OP_m0_BIT_DP:
	GetOp_DP 16
	BIT_16
	
OP_m1_BIT_DP:
	GetOp_DP 8
	BIT_8
	
OP_m0_BIT_AbsIndX:
	GetOp_AbsIndX 16
	BIT_16
	
OP_m1_BIT_AbsIndX:
	GetOp_AbsIndX 8
	BIT_8
	
OP_m0_BIT_DPIndX:
	GetOp_DPIndX 16
	BIT_16
	
OP_m1_BIT_DPIndX:
	GetOp_DPIndX 8
	BIT_8

@ --- BRK ---------------------------------------------------------------------

OP_e0_BRK:
	SkipSignatureByte
	and r0, snesPBR, #0xFF
	StackWrite8
	mov r0, snesPC, lsr #0x10
	StackWrite16
	and r0, snesP, #0xFF
	StackWrite8
	
	@mov r0, snesPC, lsr #0x10
	@orr r0, r0, snesPBR, lsl #0x10
	@bl reportBRK
	
	bic snesP, snesP, #flagD
	orr snesP, snesP, #flagI
	bic snesPBR, snesPBR, #0xFF
	ldr r0, =0xFFE6
	MemRead16
	mov r0, r0, lsl #0x10
	mov snesPC, snesPC, lsl #0x10
	orr snesPC, r0, snesPC, lsr #0x10
	b op_return
	
OP_e1_BRK:
	SkipSignatureByte
	mov r0, snesPC, lsr #0x10
	StackWrite16
	orr snesP, snesP, #flagB
	and r0, snesP, #0xFF
	StackWrite8
	bic snesP, snesP, #flagD
	orr snesP, snesP, #flagI
	bic snesPBR, snesPBR, #0xFF
	ldr r0, =0xFFFE
	MemRead16
	mov r0, r0, lsl #0x10
	mov snesPC, snesPC, lsl #0x10
	orr snesPC, r0, snesPC, lsr #0x10
	b op_return
	
.ltorg
	
@ --- CLC ---------------------------------------------------------------------

OP_CLC:
	bic snesP, snesP, #flagC
	AddCycles 1
	b op_return
	
@ --- CLD ---------------------------------------------------------------------

OP_CLD:
	bic snesP, snesP, #flagD
	AddCycles 1
	b op_return
	
@ --- CLI ---------------------------------------------------------------------

OP_CLI:
	bic snesP, snesP, #flagI
	AddCycles 1
	b op_return
	
@ --- CLV ---------------------------------------------------------------------

OP_CLV:
	bic snesP, snesP, #flagV
	AddCycles 1
	b op_return
	
@ --- CMP ---------------------------------------------------------------------

.macro CMP_8
	and r3, snesA, #0xFF
	subs r3, r3, r0
	bic snesP, snesP, #flagNZC
	orrpl snesP, snesP, #flagC
	tst r3, #0x80
	orrne snesP, snesP, #flagN
	ands r3, r3, #0xFF
	orreq snesP, snesP, #flagZ
	b op_return
.endm
	
.macro CMP_16
	subs r3, snesA, r0
	bic snesP, snesP, #flagNZC
	orrpl snesP, snesP, #flagC
	tst r3, #0x8000
	orrne snesP, snesP, #flagN
	movs r3, r3, lsl #0x10
	orreq snesP, snesP, #flagZ
	b op_return
.endm

OP_m0_CMP_Imm:
	GetOp_Imm 16
	CMP_16
	
OP_m1_CMP_Imm:
	GetOp_Imm 8
	CMP_8
	
OP_m0_CMP_Abs:
	GetOp_Abs 16
	CMP_16
	
OP_m1_CMP_Abs:
	GetOp_Abs 8
	CMP_8
	
OP_m0_CMP_AbsLong:
	GetOp_AbsLong 16
	CMP_16
	
OP_m1_CMP_AbsLong:
	GetOp_AbsLong 8
	CMP_8
	
OP_m0_CMP_DP:
	GetOp_DP 16
	CMP_16
	
OP_m1_CMP_DP:
	GetOp_DP 8
	CMP_8
	
OP_m0_CMP_DPIndirect:
	GetOp_DPIndirect 16
	CMP_16
	
OP_m1_CMP_DPIndirect:
	GetOp_DPIndirect 8
	CMP_8
	
OP_m0_CMP_DPIndirectLong:
	GetOp_DPIndirectLong 16
	CMP_16
	
OP_m1_CMP_DPIndirectLong:
	GetOp_DPIndirectLong 8
	CMP_8
	
OP_m0_CMP_AbsIndX:
	GetOp_AbsIndX 16
	CMP_16
	
OP_m1_CMP_AbsIndX:
	GetOp_AbsIndX 8
	CMP_8
	
OP_m0_CMP_AbsLongIndX:
	GetOp_AbsLongIndX 16
	CMP_16
	
OP_m1_CMP_AbsLongIndX:
	GetOp_AbsLongIndX 8
	CMP_8
	
OP_m0_CMP_AbsIndY:
	GetOp_AbsIndY 16
	CMP_16
	
OP_m1_CMP_AbsIndY:
	GetOp_AbsIndY 8
	CMP_8
	
OP_m0_CMP_DPIndX:
	GetOp_DPIndX 16
	CMP_16
	
OP_m1_CMP_DPIndX:
	GetOp_DPIndX 8
	CMP_8
	
OP_m0_CMP_DPIndIndirectX:
	GetOp_DPIndIndirectX 16
	CMP_16
	
OP_m1_CMP_DPIndIndirectX:
	GetOp_DPIndIndirectX 8
	CMP_8
	
OP_m0_CMP_DPIndirectIndY:
	GetOp_DPIndirectIndY 16
	CMP_16
	
OP_m1_CMP_DPIndirectIndY:
	GetOp_DPIndirectIndY 8
	CMP_8
	
OP_m0_CMP_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 16
	CMP_16
	
OP_m1_CMP_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 8
	CMP_8
	
OP_m0_CMP_SR:
	GetOp_SR 16
	CMP_16
	
OP_m1_CMP_SR:
	GetOp_SR 8
	CMP_8
	
OP_m0_CMP_SRIndirectIndY:
	GetOp_SRIndirectIndY 16
	CMP_16
	
OP_m1_CMP_SRIndirectIndY:
	GetOp_SRIndirectIndY 8
	CMP_8
	
@ --- COP ---------------------------------------------------------------------

OP_e0_COP:
	SkipSignatureByte
	and r0, snesPBR, #0xFF
	StackWrite8
	mov r0, snesPC, lsr #0x10
	StackWrite16
	and r0, snesP, #0xFF
	StackWrite8
	bic snesP, snesP, #flagD
	orr snesP, snesP, #flagI
	bic snesPBR, snesPBR, #0xFF
	ldr r0, =0xFFE4
	MemRead16
	mov r0, r0, lsl #0x10
	mov snesPC, snesPC, lsl #0x10
	orr snesPC, r0, snesPC, lsr #0x10
	b op_return
	
OP_e1_COP:
	SkipSignatureByte
	mov r0, snesPC, lsr #0x10
	StackWrite16
	orr snesP, snesP, #flagB
	and r0, snesP, #0xFF
	StackWrite8
	bic snesP, snesP, #flagD
	orr snesP, snesP, #flagI
	bic snesPBR, snesPBR, #0xFF
	ldr r0, =0xFFF4
	MemRead16
	mov r0, r0, lsl #0x10
	mov snesPC, snesPC, lsl #0x10
	orr snesPC, r0, snesPC, lsr #0x10
	b op_return
	
.ltorg

@ --- CPX ---------------------------------------------------------------------

.macro CPX_8
	subs r3, snesX, r0
	bic snesP, snesP, #flagNZC
	orrpl snesP, snesP, #flagC
	tst r3, #0x80
	orrne snesP, snesP, #flagN
	ands r3, r3, #0xFF
	orreq snesP, snesP, #flagZ
	b op_return
.endm
	
.macro CPX_16
	subs r3, snesX, r0
	bic snesP, snesP, #flagNZC
	orrpl snesP, snesP, #flagC
	tst r3, #0x8000
	orrne snesP, snesP, #flagN
	movs r3, r3, lsl #0x10
	orreq snesP, snesP, #flagZ
	b op_return
.endm

OP_x0_CPX_Imm:
	GetOp_Imm 16
	CPX_16
	
OP_x1_CPX_Imm:
	GetOp_Imm 8
	CPX_8
	
OP_x0_CPX_Abs:
	GetOp_Abs 16
	CPX_16
	
OP_x1_CPX_Abs:
	GetOp_Abs 8
	CPX_8
	
OP_x0_CPX_DP:
	GetOp_DP 16
	CPX_16
	
OP_x1_CPX_DP:
	GetOp_DP 8
	CPX_8

@ --- CPY ---------------------------------------------------------------------
	
.macro CPY_8
	subs r3, snesY, r0
	bic snesP, snesP, #flagNZC
	orrpl snesP, snesP, #flagC
	tst r3, #0x80
	orrne snesP, snesP, #flagN
	ands r3, r3, #0xFF
	orreq snesP, snesP, #flagZ
	b op_return
.endm
	
.macro CPY_16
	subs r3, snesY, r0
	bic snesP, snesP, #flagNZC
	orrpl snesP, snesP, #flagC
	tst r3, #0x8000
	orrne snesP, snesP, #flagN
	movs r3, r3, lsl #0x10
	orreq snesP, snesP, #flagZ
	b op_return
.endm

OP_x0_CPY_Imm:
	GetOp_Imm 16
	CPY_16
	
OP_x1_CPY_Imm:
	GetOp_Imm 8
	CPY_8
	
OP_x0_CPY_Abs:
	GetOp_Abs 16
	CPY_16
	
OP_x1_CPY_Abs:
	GetOp_Abs 8
	CPY_8
	
OP_x0_CPY_DP:
	GetOp_DP 16
	CPY_16
	
OP_x1_CPY_DP:
	GetOp_DP 8
	CPY_8
	
@ --- DEC ---------------------------------------------------------------------

.macro DEC_8 dst=r0
	sub \dst, r0, #1
	ands \dst, #0xFF
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst \dst, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 1
.endm

.macro DEC_16 src=r0, dst=r0
	sub \dst, \src, #1
	mov \dst, \dst, lsl #0x10
	movs \dst, \dst, lsr #0x10
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst \dst, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
.endm

OP_m0_DEC_A:
	DEC_16 snesA, snesA
	b op_return
	
OP_m1_DEC_A:
	and r0, snesA, #0xFF
	DEC_8
	bic snesA, snesA, #0xFF
	orr snesA, snesA, r0
	b op_return
	
OP_m0_DEC_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead16
	DEC_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_DEC_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead8
	DEC_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_DEC_DP:
	GetAddr_DP
	mov r2, r0
	MemRead16
	DEC_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_DEC_DP:
	GetAddr_DP
	mov r2, r0
	MemRead8
	DEC_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_DEC_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead16
	DEC_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_DEC_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead8
	DEC_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_DEC_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead16
	DEC_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_DEC_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead8
	DEC_8 r1
	mov r0, r2
	MemWrite8
	b op_return

@ --- DEX ---------------------------------------------------------------------

OP_x0_DEX:
	sub snesX, snesX, #1
	mov snesX, snesX, lsl #0x10
	movs snesX, snesX, lsr #0x10
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
OP_x1_DEX:
	sub snesX, snesX, #1
	ands snesX, snesX, #0xFF
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- DEY ---------------------------------------------------------------------

OP_x0_DEY:
	sub snesY, snesY, #1
	mov snesY, snesY, lsl #0x10
	movs snesY, snesY, lsr #0x10
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesY, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
OP_x1_DEY:
	sub snesY, snesY, #1
	ands snesY, snesY, #0xFF
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesY, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- EOR ---------------------------------------------------------------------

.macro EOR_8
	eor snesA, snesA, r0
	bic snesP, snesP, #flagNZ
	tst snesA, #0xFF
	orreq snesP, snesP, #flagZ
	tst snesA, #0x80
	orrne snesP, snesP, #flagN
	b op_return
.endm

.macro EOR_16
	eors snesA, snesA, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesA, #0x8000
	orrne snesP, snesP, #flagN
	b op_return
.endm

OP_m0_EOR_Imm:
	GetOp_Imm 16
	EOR_16
	
OP_m1_EOR_Imm:
	GetOp_Imm 8
	EOR_8
	
OP_m0_EOR_Abs:
	GetOp_Abs 16
	EOR_16
	
OP_m1_EOR_Abs:
	GetOp_Abs 8
	EOR_8
	
OP_m0_EOR_AbsLong:
	GetOp_AbsLong 16
	EOR_16
	
OP_m1_EOR_AbsLong:
	GetOp_AbsLong 8
	EOR_8
	
OP_m0_EOR_DP:
	GetOp_DP 16
	EOR_16
	
OP_m1_EOR_DP:
	GetOp_DP 8
	EOR_8
	
OP_m0_EOR_DPIndirect:
	GetOp_DPIndirect 16
	EOR_16
	
OP_m1_EOR_DPIndirect:
	GetOp_DPIndirect 8
	EOR_8
	
OP_m0_EOR_DPIndirectLong:
	GetOp_DPIndirectLong 16
	EOR_16
	
OP_m1_EOR_DPIndirectLong:
	GetOp_DPIndirectLong 8
	EOR_8
	
OP_m0_EOR_AbsIndX:
	GetOp_AbsIndX 16
	EOR_16
	
OP_m1_EOR_AbsIndX:
	GetOp_AbsIndX 8
	EOR_8
	
OP_m0_EOR_AbsLongIndX:
	GetOp_AbsLongIndX 16
	EOR_16
	
OP_m1_EOR_AbsLongIndX:
	GetOp_AbsLongIndX 8
	EOR_8
	
OP_m0_EOR_AbsIndY:
	GetOp_AbsIndY 16
	EOR_16
	
OP_m1_EOR_AbsIndY:
	GetOp_AbsIndY 8
	EOR_8
	
OP_m0_EOR_DPIndX:
	GetOp_DPIndX 16
	EOR_16
	
OP_m1_EOR_DPIndX:
	GetOp_DPIndX 8
	EOR_8
	
OP_m0_EOR_DPIndIndirectX:
	GetOp_DPIndIndirectX 16
	EOR_16
	
OP_m1_EOR_DPIndIndirectX:
	GetOp_DPIndIndirectX 8
	EOR_8
	
OP_m0_EOR_DPIndirectIndY:
	GetOp_DPIndirectIndY 16
	EOR_16
	
OP_m1_EOR_DPIndirectIndY:
	GetOp_DPIndirectIndY 8
	EOR_8
	
OP_m0_EOR_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 16
	EOR_16
	
OP_m1_EOR_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 8
	EOR_8
	
OP_m0_EOR_SR:
	GetOp_SR 16
	EOR_16
	
OP_m1_EOR_SR:
	GetOp_SR 8
	EOR_8
	
OP_m0_EOR_SRIndirectIndY:
	GetOp_SRIndirectIndY 16
	EOR_16
	
OP_m1_EOR_SRIndirectIndY:
	GetOp_SRIndirectIndY 8
	EOR_8
	
@ --- INC ---------------------------------------------------------------------

.macro INC_8 dst=r0
	add \dst, r0, #1
	ands \dst, \dst, #0xFF
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst \dst, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 1
.endm

.macro INC_16 src=r0, dst=r0
	add \dst, \src, #1
	mov \dst, \dst, lsl #0x10
	movs \dst, \dst, lsr #0x10
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst \dst, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
.endm

OP_m0_INC_A:
	INC_16 snesA, snesA
	b op_return
	
OP_m1_INC_A:
	and r0, snesA, #0xFF
	INC_8
	bic snesA, snesA, #0xFF
	orr snesA, snesA, r0
	b op_return
	
OP_m0_INC_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead16
	INC_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_INC_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead8
	INC_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_INC_DP:
	GetAddr_DP
	mov r2, r0
	MemRead16
	INC_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_INC_DP:
	GetAddr_DP
	mov r2, r0
	MemRead8
	INC_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_INC_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead16
	INC_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_INC_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead8
	INC_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_INC_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead16
	INC_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_INC_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead8
	INC_8 r1
	mov r0, r2
	MemWrite8
	b op_return

@ --- INX ---------------------------------------------------------------------

OP_x0_INX:
	add snesX, snesX, #1
	mov snesX, snesX, lsl #0x10
	movs snesX, snesX, lsr #0x10
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
OP_x1_INX:
	add snesX, snesX, #1
	ands snesX, snesX, #0xFF
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- INY ---------------------------------------------------------------------

OP_x0_INY:
	add snesY, snesY, #1
	mov snesY, snesY, lsl #0x10
	movs snesY, snesY, lsr #0x10
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesY, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
OP_x1_INY:
	add snesY, snesY, #1
	ands snesY, snesY, #0xFF
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesY, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- JMP ---------------------------------------------------------------------

.macro JMP
	SetPC
	b op_return
.endm

.macro JMP_LONG
	bic snesPBR, snesPBR, #0xFF
	orr snesPBR, snesPBR, r0, lsr #0x10
	bic r0, r0, #0xFF0000
	SetPC
	b op_return
.endm

OP_JMP_Abs:
	GetAddr_Abs
	JMP
	
OP_JMP_AbsIndirect:
	GetAddr_AbsIndirect
	JMP
	
OP_JMP_AbsIndIndirect:
	AddCycles 1
	GetAddr_AbsIndIndirect
	JMP
	
OP_JMP_AbsLong:
	GetAddr_AbsLong
	JMP_LONG
	
OP_JMP_AbsIndirectLong:
	GetAddr_AbsIndirectLong
	JMP_LONG
	
@ --- JSL ---------------------------------------------------------------------

OP_JSL:
	Prefetch24
	and r1, snesPBR, #0xFF
	StackWrite8 r1
	mov r1, snesPC, lsr #0x10
	sub r1, r1, #1
	StackWrite16 r1
	bic snesPBR, snesPBR, #0xFF
	orr snesPBR, snesPBR, r0, lsr #0x10
	SetPC
	b op_return
	
@ --- JSR ---------------------------------------------------------------------

OP_JSR_Abs:
	Prefetch16
	mov r1, snesPC, lsr #0x10
	sub r1, r1, #1
	StackWrite16 r1
	SetPC
	b op_return
	
OP_JSR_AbsIndIndirect:
	GetAddr_AbsIndIndirect
	mov r1, snesPC, lsr #0x10
	sub r1, r1, #1
	StackWrite16 r1
	SetPC
	AddCycles 1
	b op_return
	
@ --- LDA ---------------------------------------------------------------------

.macro LDA_8
	bic snesA, snesA, #0xFF
	orr snesA, snesA, r0
	tst snesA, #0xFF
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesA, #0x80
	orrne snesP, snesP, #flagN
	b op_return
.endm

.macro LDA_16 cycles
	movs snesA, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesA, #0x8000
	orrne snesP, snesP, #flagN
	b op_return
.endm

OP_m0_LDA_Imm:
	GetOp_Imm 16
	LDA_16
	
OP_m1_LDA_Imm:
	GetOp_Imm 8
	LDA_8
	
OP_m0_LDA_Abs:
	GetOp_Abs 16
	LDA_16
	
OP_m1_LDA_Abs:
	GetOp_Abs 8
	LDA_8
	
OP_m0_LDA_AbsLong:
	GetOp_AbsLong 16
	LDA_16
	
OP_m1_LDA_AbsLong:
	GetOp_AbsLong 8
	LDA_8
	
OP_m0_LDA_DP:
	GetOp_DP 16
	LDA_16
	
OP_m1_LDA_DP:
	GetOp_DP 8
	LDA_8
	
OP_m0_LDA_DPIndirect:
	GetOp_DPIndirect 16
	LDA_16
	
OP_m1_LDA_DPIndirect:
	GetOp_DPIndirect 8
	LDA_8
	
OP_m0_LDA_DPIndirectLong:
	GetOp_DPIndirectLong 16
	LDA_16
	
OP_m1_LDA_DPIndirectLong:
	GetOp_DPIndirectLong 8
	LDA_8
	
OP_m0_LDA_AbsIndX:
	GetOp_AbsIndX 16
	LDA_16
	
OP_m1_LDA_AbsIndX:
	GetOp_AbsIndX 8
	LDA_8
	
OP_m0_LDA_AbsLongIndX:
	GetOp_AbsLongIndX 16
	LDA_16
	
OP_m1_LDA_AbsLongIndX:
	GetOp_AbsLongIndX 8
	LDA_8
	
OP_m0_LDA_AbsIndY:
	GetOp_AbsIndY 16
	LDA_16
	
OP_m1_LDA_AbsIndY:
	GetOp_AbsIndY 8
	LDA_8
	
OP_m0_LDA_DPIndX:
	GetOp_DPIndX 16
	LDA_16
	
OP_m1_LDA_DPIndX:
	GetOp_DPIndX 8
	LDA_8
	
OP_m0_LDA_DPIndIndirectX:
	GetOp_DPIndIndirectX 16
	LDA_16
	
OP_m1_LDA_DPIndIndirectX:
	GetOp_DPIndIndirectX 8
	LDA_8
	
OP_m0_LDA_DPIndirectIndY:
	GetOp_DPIndirectIndY 16
	LDA_16
	
OP_m1_LDA_DPIndirectIndY:
	GetOp_DPIndirectIndY 8
	LDA_8

OP_m0_LDA_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 16
	LDA_16
	
OP_m1_LDA_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 8
	LDA_8
	
OP_m0_LDA_SR:
	GetOp_SR 16
	LDA_16
	
OP_m1_LDA_SR:
	GetOp_SR 8
	LDA_8
	
OP_m0_LDA_SRIndirectIndY:
	GetOp_SRIndirectIndY 16
	LDA_16
	
OP_m1_LDA_SRIndirectIndY:
	GetOp_SRIndirectIndY 8
	LDA_8
	
@ --- LDX ---------------------------------------------------------------------

.macro LDX_8
	movs snesX, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x80
	orrne snesP, snesP, #flagN
	b op_return
.endm

.macro LDX_16
	movs snesX, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x8000
	orrne snesP, snesP, #flagN
	b op_return
.endm

OP_x0_LDX_Imm:
	GetOp_Imm 16
	LDX_16
	
OP_x1_LDX_Imm:
	GetOp_Imm 8
	LDX_8
	
OP_x0_LDX_Abs:
	GetOp_Abs 16
	LDX_16
	
OP_x1_LDX_Abs:
	GetOp_Abs 8
	LDX_8
	
OP_x0_LDX_DP:
	GetOp_DP 16
	LDX_16
	
OP_x1_LDX_DP:
	GetOp_DP 8
	LDX_8
	
OP_x0_LDX_AbsIndY:
	GetOp_AbsIndY 16
	LDX_16
	
OP_x1_LDX_AbsIndY:
	GetOp_AbsIndY 8
	LDX_8
	
OP_x0_LDX_DPIndY:
	GetOp_DPIndY 16
	LDX_16
	
OP_x1_LDX_DPIndY:
	GetOp_DPIndY 8
	LDX_8
	
@ --- LDY ---------------------------------------------------------------------

.macro LDY_8
	movs snesY, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesY, #0x80
	orrne snesP, snesP, #flagN
	b op_return
.endm

.macro LDY_16
	movs snesY, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesY, #0x8000
	orrne snesP, snesP, #flagN
	b op_return
.endm

OP_x0_LDY_Imm:
	GetOp_Imm 16
	LDY_16
	
OP_x1_LDY_Imm:
	GetOp_Imm 8
	LDY_8
	
OP_x0_LDY_Abs:
	GetOp_Abs 16
	LDY_16
	
OP_x1_LDY_Abs:
	GetOp_Abs 8
	LDY_8
	
OP_x0_LDY_DP:
	GetOp_DP 16
	LDY_16
	
OP_x1_LDY_DP:
	GetOp_DP 8
	LDY_8
	
OP_x0_LDY_AbsIndX:
	GetOp_AbsIndX 16
	LDY_16
	
OP_x1_LDY_AbsIndX:
	GetOp_AbsIndX 8
	LDY_8
	
OP_x0_LDY_DPIndX:
	GetOp_DPIndX 16
	LDY_16
	
OP_x1_LDY_DPIndX:
	GetOp_DPIndX 8
	LDY_8
	
@ --- LSR ---------------------------------------------------------------------

.macro LSR_8 dst=r0
	bic snesP, snesP, #flagNZC
	tst r0, #0x1
	orrne snesP, snesP, #flagC
	mov r0, r0, lsr #1
	tst r0, #0x80
	orrne snesP, snesP, #flagN
	movs \dst, r0
	orreq snesP, snesP, #flagZ
	AddCycles 1
.endm

.macro LSR_16 src=r0, dst=r0
	bic snesP, snesP, #flagNZC
	tst \src, #0x1
	orrne snesP, snesP, #flagC
	mov r0, \src, lsr #1
	tst r0, #0x8000
	orrne snesP, snesP, #flagN
	movs \dst, r0
	orreq snesP, snesP, #flagZ
	AddCycles 1
.endm

OP_m0_LSR_A:
	LSR_16 snesA, snesA
	b op_return
	
OP_m1_LSR_A:
	and r0, snesA, #0xFF
	LSR_8
	bic snesA, snesA, #0xFF
	orr snesA, snesA, r0
	b op_return
	
OP_m0_LSR_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead16
	LSR_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_LSR_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead8
	LSR_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_LSR_DP:
	GetAddr_DP
	mov r2, r0
	MemRead16
	LSR_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_LSR_DP:
	GetAddr_DP
	mov r2, r0
	MemRead8
	LSR_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_LSR_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead16
	LSR_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_LSR_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead8
	LSR_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_LSR_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead16
	LSR_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_LSR_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead8
	LSR_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
@ --- MVN ---------------------------------------------------------------------

OP_x0_MVN:
	Prefetch8 r1
	Prefetch8
	bic snesDBR, snesDBR, #0xFF
	orr snesDBR, snesDBR, r1
	orr r2, snesY, r1, lsl #0x10
	orr r0, snesX, r0, lsl #0x10
	MemRead8
	mov r1, r0
	mov r0, r2
	MemWrite8
	ldr r0, =0xFFFF
	add snesX, snesX, #1
	and snesX, snesX, r0
	add snesY, snesY, #1
	and snesY, snesY, r0
	subs snesA, snesA, #1
	and snesA, snesA, r0
	subpl snesPC, snesPC, #0x30000
	AddCycles 2
	b op_return
	
OP_x1_MVN:
	Prefetch8 r1
	Prefetch8
	bic snesDBR, snesDBR, #0xFF
	orr snesDBR, snesDBR, r1
	orr r2, snesY, r1, lsl #0x10
	orr r0, snesX, r0, lsl #0x10
	MemRead8
	mov r1, r0
	mov r0, r2
	MemWrite8
	ldr r0, =0xFFFF
	add snesX, snesX, #1
	and snesX, snesX, #0xFF
	add snesY, snesY, #1
	and snesY, snesY, #0xFF
	subs snesA, snesA, #1
	and snesA, snesA, r0
	subpl snesPC, snesPC, #0x30000
	AddCycles 2
	b op_return
	
.ltorg
	
@ --- MVP ---------------------------------------------------------------------

OP_x0_MVP:
	Prefetch8 r1
	Prefetch8
	bic snesDBR, snesDBR, #0xFF
	orr snesDBR, snesDBR, r1
	orr r2, snesY, r1, lsl #0x10
	orr r0, snesX, r0, lsl #0x10
	MemRead8
	mov r1, r0
	mov r0, r2
	MemWrite8
	ldr r0, =0xFFFF
	sub snesX, snesX, #1
	and snesX, snesX, r0
	sub snesY, snesY, #1
	and snesY, snesY, r0
	subs snesA, snesA, #1
	and snesA, snesA, r0
	subpl snesPC, snesPC, #0x30000
	AddCycles 2
	b op_return
	
OP_x1_MVP:
	Prefetch8 r1
	Prefetch8
	bic snesDBR, snesDBR, #0xFF
	orr snesDBR, snesDBR, r1
	orr r2, snesY, r1, lsl #0x10
	orr r0, snesX, r0, lsl #0x10
	MemRead8
	mov r1, r0
	mov r0, r2
	MemWrite8
	ldr r0, =0xFFFF
	sub snesX, snesX, #1
	and snesX, snesX, #0xFF
	sub snesY, snesY, #1
	and snesY, snesY, #0xFF
	subs snesA, snesA, #1
	and snesA, snesA, r0
	subpl snesPC, snesPC, #0x30000
	AddCycles 2
	b op_return
	
.ltorg
	
@ --- NOP ---------------------------------------------------------------------

OP_NOP:
	AddCycles 1
	b op_return
	
@ --- ORA ---------------------------------------------------------------------

.macro ORA_8
	orr snesA, snesA, r0
	bic snesP, snesP, #flagNZ
	tst snesA, #0xFF
	orreq snesP, snesP, #flagZ
	tst snesA, #0x80
	orrne snesP, snesP, #flagN
	b op_return
.endm

.macro ORA_16
	orrs snesA, snesA, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesA, #0x8000
	orrne snesP, snesP, #flagN
	b op_return
.endm

OP_m0_ORA_Imm:
	GetOp_Imm 16
	ORA_16
	
OP_m1_ORA_Imm:
	GetOp_Imm 8
	ORA_8
	
OP_m0_ORA_Abs:
	GetOp_Abs 16
	ORA_16
	
OP_m1_ORA_Abs:
	GetOp_Abs 8
	ORA_8
	
OP_m0_ORA_AbsLong:
	GetOp_AbsLong 16
	ORA_16
	
OP_m1_ORA_AbsLong:
	GetOp_AbsLong 8
	ORA_8
	
OP_m0_ORA_DP:
	GetOp_DP 16
	ORA_16
	
OP_m1_ORA_DP:
	GetOp_DP 8
	ORA_8
	
OP_m0_ORA_DPIndirect:
	GetOp_DPIndirect 16
	ORA_16
	
OP_m1_ORA_DPIndirect:
	GetOp_DPIndirect 8
	ORA_8
	
OP_m0_ORA_DPIndirectLong:
	GetOp_DPIndirectLong 16
	ORA_16
	
OP_m1_ORA_DPIndirectLong:
	GetOp_DPIndirectLong 8
	ORA_8
	
OP_m0_ORA_AbsIndX:
	GetOp_AbsIndX 16
	ORA_16
	
OP_m1_ORA_AbsIndX:
	GetOp_AbsIndX 8
	ORA_8
	
OP_m0_ORA_AbsLongIndX:
	GetOp_AbsLongIndX 16
	ORA_16
	
OP_m1_ORA_AbsLongIndX:
	GetOp_AbsLongIndX 8
	ORA_8
	
OP_m0_ORA_AbsIndY:
	GetOp_AbsIndY 16
	ORA_16
	
OP_m1_ORA_AbsIndY:
	GetOp_AbsIndY 8
	ORA_8
	
OP_m0_ORA_DPIndX:
	GetOp_DPIndX 16
	ORA_16
	
OP_m1_ORA_DPIndX:
	GetOp_DPIndX 8
	ORA_8
	
OP_m0_ORA_DPIndIndirectX:
	GetOp_DPIndIndirectX 16
	ORA_16
	
OP_m1_ORA_DPIndIndirectX:
	GetOp_DPIndIndirectX 8
	ORA_8
	
OP_m0_ORA_DPIndirectIndY:
	GetOp_DPIndirectIndY 16
	ORA_16
	
OP_m1_ORA_DPIndirectIndY:
	GetOp_DPIndirectIndY 8
	ORA_8
	
OP_m0_ORA_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 16
	ORA_16
	
OP_m1_ORA_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 8
	ORA_8
	
OP_m0_ORA_SR:
	GetOp_SR 16
	ORA_16
	
OP_m1_ORA_SR:
	GetOp_SR 8
	ORA_8
	
OP_m0_ORA_SRIndirectIndY:
	GetOp_SRIndirectIndY 16
	ORA_16
	
OP_m1_ORA_SRIndirectIndY:
	GetOp_SRIndirectIndY 8
	ORA_8
	
@ --- PEA ---------------------------------------------------------------------

OP_PEA:
	Prefetch16
	StackWrite16
	b op_return
	
@ --- PEI ---------------------------------------------------------------------

OP_PEI:
	tst snesD, #0xFF0000
	AddCycles 1, ne
	Prefetch8
	add r0, r0, snesD, lsr #0x10
	MemRead16
	StackWrite16
	b op_return
	
@ --- PER ---------------------------------------------------------------------

OP_PER:
	Prefetch16
	add r0, r0, snesPC, lsr #0x10
	StackWrite16
	b op_return
	
@ --- PHA ---------------------------------------------------------------------

OP_m0_PHA:
	mov r0, snesA
	StackWrite16
	AddCycles 1
	b op_return
	
OP_m1_PHA:
	and r0, snesA, #0xFF
	StackWrite8
	AddCycles 1
	b op_return
	
@ --- PHB ---------------------------------------------------------------------

OP_PHB:
	and r0, snesDBR, #0xFF
	StackWrite8
	AddCycles 1
	b op_return
	
@ --- PHD ---------------------------------------------------------------------

OP_PHD:
	mov r0, snesD, lsr #0x10
	StackWrite16
	AddCycles 1
	b op_return
	
@ --- PHK ---------------------------------------------------------------------

OP_PHK:
	and r0, snesPBR, #0xFF
	StackWrite8
	AddCycles 1
	b op_return
	
@ --- PHP ---------------------------------------------------------------------

OP_PHP:
	and r0, snesP, #0xFF
	StackWrite8
	AddCycles 1
	b op_return
	
@ --- PHX ---------------------------------------------------------------------

OP_x0_PHX:
	mov r0, snesX
	StackWrite16
	AddCycles 1
	b op_return
	
OP_x1_PHX:
	mov r0, snesX
	StackWrite8
	AddCycles 1
	b op_return
	
@ --- PHY ---------------------------------------------------------------------

OP_x0_PHY:
	mov r0, snesY
	StackWrite16
	AddCycles 1
	b op_return
	
OP_x1_PHY:
	mov r0, snesY
	StackWrite8
	AddCycles 1
	b op_return
	
@ --- PLA ---------------------------------------------------------------------

OP_m0_PLA:
	StackRead16
	movs snesA, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesA, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 2
	b op_return
	
OP_m1_PLA:
	StackRead8
	bic snesA, snesA, #0xFF
	orr snesA, snesA, r0
	bic snesP, snesP, #flagNZ
	tst snesA, #0xFF
	orreq snesP, snesP, #flagZ
	tst snesA, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 2
	b op_return
	
@ --- PLB ---------------------------------------------------------------------

OP_PLB:
	StackRead8
	bic snesDBR, snesDBR, #0xFF
	orr snesDBR, snesDBR, r0
	bic snesP, snesP, #flagNZ
	tst r0, #0x80
	orrne snesP, snesP, #flagN
	tst r0, #0xFF
	orreq snesP, snesP, #flagZ
	AddCycles 2
	b op_return
	
@ --- PLD ---------------------------------------------------------------------

OP_PLD:
	StackRead16
	bic snesP, snesP, #flagNZ
	movs r0, r0, lsl #0x10
	mov snesD, snesD, lsl #0x10
	orr snesD, r0, snesD, lsr #0x10
	orreq snesP, snesP, #flagZ
	tst snesD, #0x80000000
	orrne snesP, snesP, #flagN
	AddCycles 2
	b op_return
	
@ --- PLP ---------------------------------------------------------------------

OP_PLP:
	StackRead8
	bic snesP, snesP, #0xFF
	orr snesP, snesP, r0
	UpdateCPUMode
	AddCycles 2
	b op_return
	
@ --- PLX ---------------------------------------------------------------------

OP_x0_PLX:
	StackRead16
	movs snesX, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 2
	b op_return
	
OP_x1_PLX:
	StackRead8
	movs snesX, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 2
	b op_return
	
@ --- PLY ---------------------------------------------------------------------

OP_x0_PLY:
	StackRead16
	movs snesY, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesY, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 2
	b op_return
	
OP_x1_PLY:
	StackRead8
	movs snesY, r0
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesY, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 2
	b op_return
	
@ --- REP ---------------------------------------------------------------------

OP_e0_REP:
	GetOp_Imm 8
	bic snesP, snesP, r0
	UpdateCPUMode
	b op_return
	
OP_e1_REP:
	GetOp_Imm 8
	bic r0, r0, #(flagM|flagX)
	bic snesP, snesP, r0
	UpdateCPUMode
	b op_return
	
.ltorg

@ --- ROL ---------------------------------------------------------------------

.macro ROL_8 dst=r0
	mov r0, r0, lsl #1
	tst snesP, #flagC
	orrne r0, r0, #1
	bic snesP, snesP, #flagNZC
	tst r0, #0x80
	orrne snesP, snesP, #flagN
	tst r0, #0x100
	orrne snesP, snesP, #flagC
	bics \dst, r0, #0x100
	orreq snesP, snesP, #flagZ
	AddCycles 1
.endm

.macro ROL_16 src=r0, dst=r0
	mov r0, \src, lsl #1
	tst snesP, #flagC
	orrne r0, r0, #1
	bic snesP, snesP, #flagNZC
	tst r0, #0x8000
	orrne snesP, snesP, #flagN
	tst r0, #0x10000
	orrne snesP, snesP, #flagC
	bics \dst, r0, #0x10000
	orreq snesP, snesP, #flagZ
	AddCycles 1
.endm

OP_m0_ROL_A:
	ROL_16 snesA, snesA
	b op_return
	
OP_m1_ROL_A:
	and r0, snesA, #0xFF
	ROL_8
	bic snesA, snesA, #0xFF
	orr snesA, snesA, r0
	b op_return
	
OP_m0_ROL_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead16
	ROL_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ROL_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead8
	ROL_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_ROL_DP:
	GetAddr_DP
	mov r2, r0
	MemRead16
	ROL_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ROL_DP:
	GetAddr_DP
	mov r2, r0
	MemRead8
	ROL_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_ROL_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead16
	ROL_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ROL_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead8
	ROL_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_ROL_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead16
	ROL_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ROL_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead8
	ROL_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
@ --- ROR ---------------------------------------------------------------------

.macro ROR_8 dst=r0
	tst snesP, #flagC
	orrne r0, r0, #0x100
	bic snesP, snesP, #flagNZC
	tst r0, #0x1
	orrne snesP, snesP, #flagC
	mov r0, r0, lsr #1
	tst r0, #0x80
	orrne snesP, snesP, #flagN
	movs \dst, r0
	orreq snesP, snesP, #flagZ
	AddCycles 1
.endm

.macro ROR_16 src=r0, dst=r0
	tst snesP, #flagC
	orrne r0, \src, #0x10000
	moveq r0, \src
	bic snesP, snesP, #flagNZC
	tst r0, #0x1
	orrne snesP, snesP, #flagC
	mov r0, r0, lsr #1
	tst r0, #0x8000
	orrne snesP, snesP, #flagN
	movs \dst, r0
	orreq snesP, snesP, #flagZ
	AddCycles 1
.endm

OP_m0_ROR_A:
	ROR_16 snesA, snesA
	b op_return
	
OP_m1_ROR_A:
	and r0, snesA, #0xFF
	ROR_8
	bic snesA, snesA, #0xFF
	orr snesA, snesA, r0
	b op_return
	
OP_m0_ROR_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead16
	ROR_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ROR_Abs:
	GetAddr_Abs
	mov r2, r0
	MemRead8
	ROR_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_ROR_DP:
	GetAddr_DP
	mov r2, r0
	MemRead16
	ROR_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ROR_DP:
	GetAddr_DP
	mov r2, r0
	MemRead8
	ROR_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_ROR_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead16
	ROR_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ROR_AbsIndX:
	GetAddr_AbsIndX
	mov r2, r0
	MemRead8
	ROR_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
OP_m0_ROR_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead16
	ROR_16 r0, r1
	mov r0, r2
	MemWrite16
	b op_return
	
OP_m1_ROR_DPIndX:
	GetAddr_DPIndX
	mov r2, r0
	MemRead8
	ROR_8 r1
	mov r0, r2
	MemWrite8
	b op_return
	
@ --- RTI ---------------------------------------------------------------------

OP_e0_RTI:
	StackRead8
	bic snesP, snesP, #0xFF
	orr snesP, snesP, r0
	UpdateCPUMode
	StackRead16
	SetPC
	StackRead8
	bic snesPBR, snesPBR, #0xFF
	orr snesPBR, snesPBR, r0
	AddCycles 2
	b op_return
	
OP_e1_RTI:
	StackRead8
	bic r0, r0, #(flagM|flagX)
	bic snesP, snesP, #0xCF
	orr snesP, snesP, r0
	UpdateCPUMode
	StackRead16
	SetPC
	AddCycles 2
	b op_return
	
.ltorg

@ --- RTL ---------------------------------------------------------------------

OP_RTL:
	StackRead16
	add r0, r0, #1
	SetPC
	StackRead8
	bic snesPBR, snesPBR, #0xFF
	orr snesPBR, snesPBR, r0
	AddCycles 2
	b op_return

@ --- RTS ---------------------------------------------------------------------

OP_RTS:
	StackRead16
	add r0, r0, #1
	SetPC
	AddCycles 3
	b op_return

@ --- SBC ---------------------------------------------------------------------

.macro SBC_8
	and r1, snesA, #0xFF
	sub r1, r1, r0
	tst snesP, #flagC
	subeq r1, r1, #0x1
	tst snesP, #flagD
	beq 1f
		and r2, r1, #0xF
		cmp r2, #0xA
		subge r1, r1, #0x6
		and r2, r1, #0xF0
		cmp r2, #0xA0
		subge r1, r1, #0x60
1:
	bic snesP, snesP, #flagNVZC
	tst r1, #0x80
	orrne snesP, snesP, #flagN
	tst r1, #0x100
	orreq snesP, snesP, #flagC
	eor r2, snesA, r0
	tst r2, #0x80
	beq 2f
	eor r2, snesA, r1
	tst r2, #0x80
	orrne snesP, snesP, #flagV
2:
	and r1, r1, #0xFF
	bic snesA, snesA, #0xFF
	orr snesA, snesA, r1
	cmp r1, #0
	orreq snesP, snesP, #flagZ
	b op_return
.endm

.macro SBC_16
	sub r1, snesA, r0
	tst snesP, #flagC
	subeq r1, r1, #0x1
	tst snesP, #flagD
	beq 1f
		and r2, r1, #0xF
		cmp r2, #0xA
		subge r1, r1, #0x6
		and r2, r1, #0xF0
		cmp r2, #0xA0
		subge r1, r1, #0x60
		and r2, r1, #0xF00
		cmp r2, #0xA00
		subge r1, r1, #0x600
		and r2, r1, #0xF000
		cmp r2, #0xA000
		subge r1, r1, #0x6000
1:
	bic snesP, snesP, #flagNVZC
	tst r1, #0x8000
	orrne snesP, snesP, #flagN
	tst r1, #0x10000
	orreq snesP, snesP, #flagC
	eor r2, snesA, r0
	tst r2, #0x8000
	beq 2f
	eor r2, snesA, r1
	tst r2, #0x8000
	orrne snesP, snesP, #flagV
2:
	mov r1, r1, lsl #0x10
	movs snesA, r1, lsr #0x10
	orreq snesP, snesP, #flagZ
	b op_return
.endm

OP_m0_SBC_Imm:
	GetOp_Imm 16
	SBC_16
	
OP_m1_SBC_Imm:
	GetOp_Imm 8
	SBC_8
	
OP_m0_SBC_Abs:
	GetOp_Abs 16
	SBC_16
	
OP_m1_SBC_Abs:
	GetOp_Abs 8
	SBC_8
	
OP_m0_SBC_AbsLong:
	GetOp_AbsLong 16
	SBC_16
	
OP_m1_SBC_AbsLong:
	GetOp_AbsLong 8
	SBC_8
	
OP_m0_SBC_DP:
	GetOp_DP 16
	SBC_16
	
OP_m1_SBC_DP:
	GetOp_DP 8
	SBC_8
	
OP_m0_SBC_DPIndirect:
	GetOp_DPIndirect 16
	SBC_16
	
OP_m1_SBC_DPIndirect:
	GetOp_DPIndirect 8
	SBC_8
	
OP_m0_SBC_DPIndirectLong:
	GetOp_DPIndirectLong 16
	SBC_16
	
OP_m1_SBC_DPIndirectLong:
	GetOp_DPIndirectLong 8
	SBC_8
	
OP_m0_SBC_AbsIndX:
	GetOp_AbsIndX 16
	SBC_16
	
OP_m1_SBC_AbsIndX:
	GetOp_AbsIndX 8
	SBC_8
	
OP_m0_SBC_AbsLongIndX:
	GetOp_AbsLongIndX 16
	SBC_16
	
OP_m1_SBC_AbsLongIndX:
	GetOp_AbsLongIndX 8
	SBC_8
	
OP_m0_SBC_AbsIndY:
	GetOp_AbsIndY 16
	SBC_16
	
OP_m1_SBC_AbsIndY:
	GetOp_AbsIndY 8
	SBC_8
	
OP_m0_SBC_DPIndX:
	GetOp_DPIndX 16
	SBC_16
	
OP_m1_SBC_DPIndX:
	GetOp_DPIndX 8
	SBC_8
	
OP_m0_SBC_DPIndIndirectX:
	GetOp_DPIndIndirectX 16
	SBC_16
	
OP_m1_SBC_DPIndIndirectX:
	GetOp_DPIndIndirectX 8
	SBC_8
	
OP_m0_SBC_DPIndirectIndY:
	GetOp_DPIndirectIndY 16
	SBC_16
	
OP_m1_SBC_DPIndirectIndY:
	GetOp_DPIndirectIndY 8
	SBC_8
	
OP_m0_SBC_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 16
	SBC_16
	
OP_m1_SBC_DPIndirectLongIndY:
	GetOp_DPIndirectLongIndY 8
	SBC_8
	
OP_m0_SBC_SR:
	GetOp_SR 16
	SBC_16
	
OP_m1_SBC_SR:
	GetOp_SR 8
	SBC_8
	
OP_m0_SBC_SRIndirectIndY:
	GetOp_SRIndirectIndY 16
	SBC_16
	
OP_m1_SBC_SRIndirectIndY:
	GetOp_SRIndirectIndY 8
	SBC_8

@ --- SEC ---------------------------------------------------------------------

OP_SEC:
	orr snesP, snesP, #flagC
	AddCycles 1
	b op_return
	
@ --- SED ---------------------------------------------------------------------

OP_SED:
	orr snesP, snesP, #flagD
	AddCycles 1
	b op_return
	
@ --- SEI ---------------------------------------------------------------------

OP_SEI:
	orr snesP, snesP, #flagI
	AddCycles 1
	b op_return
	
@ --- SEP ---------------------------------------------------------------------

OP_e0_SEP:
	GetOp_Imm 8
	orr snesP, snesP, r0
	UpdateCPUMode
	b op_return
	
OP_e1_SEP:
	GetOp_Imm 8
	bic r0, r0, #(flagM|flagX)
	orr snesP, snesP, r0
	UpdateCPUMode
	b op_return
	
.ltorg
	
@ --- STA ---------------------------------------------------------------------

.macro STA_8
	and r1, snesA, #0xFF
	MemWrite8
	b op_return
.endm

.macro STA_16
	mov r1, snesA
	MemWrite16
	b op_return
.endm

OP_m0_STA_Abs:
	GetAddr_Abs
	STA_16
	
OP_m1_STA_Abs:
	GetAddr_Abs
	STA_8
	
OP_m0_STA_AbsLong:
	GetAddr_AbsLong
	STA_16
	
OP_m1_STA_AbsLong:
	GetAddr_AbsLong
	STA_8
	
OP_m0_STA_DP:
	GetAddr_DP
	STA_16
	
OP_m1_STA_DP:
	GetAddr_DP
	STA_8
	
OP_m0_STA_DPIndirect:
	GetAddr_DPIndirect
	STA_16
	
OP_m1_STA_DPIndirect:
	GetAddr_DPIndirect
	STA_8
	
OP_m0_STA_DPIndirectLong:
	GetAddr_DPIndirectLong
	STA_16
	
OP_m1_STA_DPIndirectLong:
	GetAddr_DPIndirectLong
	STA_8
	
OP_m0_STA_AbsIndX:
	GetAddr_AbsIndX
	STA_16
	
OP_m1_STA_AbsIndX:
	GetAddr_AbsIndX
	STA_8

OP_m0_STA_AbsLongIndX:
	GetAddr_AbsLongIndX
	STA_16
	
OP_m1_STA_AbsLongIndX:
	GetAddr_AbsLongIndX
	STA_8

OP_m0_STA_AbsIndY:
	GetAddr_AbsIndY
	STA_16
	
OP_m1_STA_AbsIndY:
	GetAddr_AbsIndY
	STA_8

OP_m0_STA_DPIndX:
	GetAddr_DPIndX
	STA_16
	
OP_m1_STA_DPIndX:
	GetAddr_DPIndX
	STA_8

OP_m0_STA_DPIndIndirectX:
	GetAddr_DPIndIndirectX
	STA_16
	
OP_m1_STA_DPIndIndirectX:
	GetAddr_DPIndIndirectX
	STA_8

OP_m0_STA_DPIndirectIndY:
	GetAddr_DPIndirectIndY
	STA_16
	
OP_m1_STA_DPIndirectIndY:
	GetAddr_DPIndirectIndY
	STA_8

OP_m0_STA_DPIndirectLongIndY:
	GetAddr_DPIndirectLongIndY
	STA_16
	
OP_m1_STA_DPIndirectLongIndY:
	GetAddr_DPIndirectLongIndY
	STA_8

OP_m0_STA_SR:
	GetAddr_SR
	STA_16
	
OP_m1_STA_SR:
	GetAddr_SR
	STA_8

OP_m0_STA_SRIndirectIndY:
	GetAddr_SRIndirectIndY
	STA_16
	
OP_m1_STA_SRIndirectIndY:
	GetAddr_SRIndirectIndY
	STA_8
	
@ --- STP ---------------------------------------------------------------------
@ I don't think it should be implemented this way, but hey, is it even used
@ at all in first place?

OP_STP:
	mov r1, snesPC, lsr #0x10
	mov r0, #0xDB
	bl report_unk
stoploop:
	@swi #0x50000
	b stoploop
	
@ --- STX ---------------------------------------------------------------------

.macro STX_8
	mov r1, snesX
	MemWrite8
	b op_return
.endm

.macro STX_16
	mov r1, snesX
	MemWrite16
	b op_return
.endm

OP_x0_STX_Abs:
	GetAddr_Abs
	STX_16
	
OP_x1_STX_Abs:
	GetAddr_Abs
	STX_8
	
OP_x0_STX_DP:
	GetAddr_DP
	STX_16
	
OP_x1_STX_DP:
	GetAddr_DP
	STX_8
	
OP_x0_STX_DPIndY:
	GetAddr_DPIndY
	STX_16
	
OP_x1_STX_DPIndY:
	GetAddr_DPIndY
	STX_8
	
@ --- STY ---------------------------------------------------------------------

.macro STY_8
	mov r1, snesY
	MemWrite8
	b op_return
.endm

.macro STY_16
	mov r1, snesY
	MemWrite16
	b op_return
.endm

OP_x0_STY_Abs:
	GetAddr_Abs
	STY_16
	
OP_x1_STY_Abs:
	GetAddr_Abs
	STY_8
	
OP_x0_STY_DP:
	GetAddr_DP
	STY_16
	
OP_x1_STY_DP:
	GetAddr_DP
	STY_8
	
OP_x0_STY_DPIndX:
	GetAddr_DPIndX
	STY_16
	
OP_x1_STY_DPIndX:
	GetAddr_DPIndX
	STY_8
	
@ --- STZ ---------------------------------------------------------------------

.macro STZ_8
	mov r1, #0
	MemWrite8
	b op_return
.endm

.macro STZ_16
	mov r1, #0
	MemWrite16
	b op_return
.endm

OP_m0_STZ_Abs:
	GetAddr_Abs
	STZ_16

OP_m1_STZ_Abs:
	GetAddr_Abs
	STZ_8
	
OP_m0_STZ_DP:
	GetAddr_DP
	STZ_16

OP_m1_STZ_DP:
	GetAddr_DP
	STZ_8
	
OP_m0_STZ_AbsIndX:
	GetAddr_AbsIndX
	STZ_16

OP_m1_STZ_AbsIndX:
	GetAddr_AbsIndX
	STZ_8
	
OP_m0_STZ_DPIndX:
	GetAddr_DPIndX
	STZ_16

OP_m1_STZ_DPIndX:
	GetAddr_DPIndX
	STZ_8
	
@ --- TAX ---------------------------------------------------------------------

OP_x0_TAX:
	movs snesX, snesA
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
OP_x1_TAX:
	ands snesX, snesA, #0xFF
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- TAY ---------------------------------------------------------------------

OP_x0_TAY:
	movs snesY, snesA
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesY, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
OP_x1_TAY:
	ands snesY, snesA, #0xFF
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesY, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- TCD ---------------------------------------------------------------------

OP_TCD:
	mov snesD, snesD, lsl #0x10
	mov snesD, snesD, lsr #0x10
	orr snesD, snesD, snesA, lsl #0x10
	bic snesP, snesP, #flagNZ
	tst snesA, #0x8000
	orrne snesP, snesP, #flagN
	cmp snesA, #0
	orreq snesP, snesP, #flagZ
	AddCycles 1
	b op_return
	
@ --- TCS ---------------------------------------------------------------------

OP_e0_TCS:
	mov snesS, snesS, lsl #0x10
	mov snesS, snesS, lsr #0x10
	orr snesS, snesS, snesA, lsl #0x10
	AddCycles 1
	b op_return
	
OP_e1_TCS:
	mov snesS, snesS, lsl #0x10
	mov snesS, snesS, lsr #0x10
	and r4, snesA, #0xFF
	orr snesS, snesS, r4, lsl #0x10
	orr snesS, snesS, #0x01000000
	AddCycles 1
	b op_return
	
@ --- TDC ---------------------------------------------------------------------

OP_TDC:
	movs snesA, snesD, lsr #0x10
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesA, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- TRB ---------------------------------------------------------------------

.macro TRB_8
	mov r2, r0
	MemRead8
	and r1, snesA, #0xFF
	tst r0, r1
	orreq snesP, snesP, #flagZ
	bicne snesP, snesP, #flagZ
	bic r1, r0, r1
	mov r0, r2
	MemWrite8
	AddCycles 1
	b op_return
.endm

.macro TRB_16
	mov r2, r0
	MemRead16
	tst r0, snesA
	orreq snesP, snesP, #flagZ
	bicne snesP, snesP, #flagZ
	bic r1, r0, snesA
	mov r0, r2
	MemWrite16
	AddCycles 1
	b op_return
.endm

OP_m0_TRB_Abs:
	GetAddr_Abs
	TRB_16
	
OP_m1_TRB_Abs:
	GetAddr_Abs
	TRB_8
	
OP_m0_TRB_DP:
	GetAddr_DP
	TRB_16
	
OP_m1_TRB_DP:
	GetAddr_DP
	TRB_8
	
@ --- TSB ---------------------------------------------------------------------

.macro TSB_8
	mov r2, r0
	MemRead8
	and r1, snesA, #0xFF
	tst r0, r1
	orreq snesP, snesP, #flagZ
	bicne snesP, snesP, #flagZ
	orr r1, r0, r1
	mov r0, r2
	MemWrite8
	AddCycles 1
	b op_return
.endm

.macro TSB_16
	mov r2, r0
	MemRead16
	tst r0, snesA
	orreq snesP, snesP, #flagZ
	bicne snesP, snesP, #flagZ
	orr r1, r0, snesA
	mov r0, r2
	MemWrite16
	AddCycles 1
	b op_return
.endm

OP_m0_TSB_Abs:
	GetAddr_Abs
	TSB_16
	
OP_m1_TSB_Abs:
	GetAddr_Abs
	TSB_8
	
OP_m0_TSB_DP:
	GetAddr_DP
	TSB_16
	
OP_m1_TSB_DP:
	GetAddr_DP
	TSB_8
	
@ --- TSC ---------------------------------------------------------------------

OP_TSC:
	movs snesA, snesS, lsr #0x10
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesA, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- TSX ---------------------------------------------------------------------

OP_x0_TSX:
	movs snesX, snesS, lsr #0x10
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
OP_x1_TSX:
	mov snesX, snesS, lsr #0x10
	ands snesX, snesX, #0xFF
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- TXS ---------------------------------------------------------------------

OP_e0_TXS:
	mov snesS, snesS, lsl #0x10
	mov snesS, snesS, lsr #0x10
	orr snesS, snesS, snesX, lsl #0x10
	AddCycles 1
	b op_return
	
OP_e1_TXS:
	bic snesS, snesS, #0xFF0000
	orr snesS, snesS, snesX, lsl #0x10
	bic snesS, snesS, #0xFF000000
	orr snesS, snesS, #0x01000000
	AddCycles 1
	b op_return
	
@ --- TXY ---------------------------------------------------------------------

OP_TXY:
	movs snesY, snesX
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesY, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- TXA ---------------------------------------------------------------------

OP_m0_TXA:
	movs snesA, snesX
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesA, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
OP_m1_TXA:
	bic snesA, snesA, #0xFF
	orr snesA, snesA, snesX
	bic snesP, snesP, #flagNZ
	tst snesA, #0xFF
	orreq snesP, snesP, #flagZ
	tst snesA, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- TYA ---------------------------------------------------------------------

OP_m0_TYA:
	movs snesA, snesY
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesA, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
OP_m1_TYA:
	bic snesA, snesA, #0xFF
	orr snesA, snesA, snesY
	bic snesP, snesP, #flagNZ
	tst snesA, #0xFF
	orreq snesP, snesP, #flagZ
	tst snesA, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- TYX ---------------------------------------------------------------------

OP_TYX:
	movs snesX, snesY
	bic snesP, snesP, #flagNZ
	orreq snesP, snesP, #flagZ
	tst snesX, #0x8000
	orrne snesP, snesP, #flagN
	AddCycles 1
	b op_return
	
@ --- WAI ---------------------------------------------------------------------

OP_WAI:
	orr snesP, snesP, #flagW
	EatCycles
	b op_return
	
@ --- XBA ---------------------------------------------------------------------

OP_XBA:
	mov r0, snesA, lsl #0x8
	and r0, #0xFF00
	mov snesA, snesA, lsr #0x8
	orr snesA, snesA, r0
	bic snesP, snesP, #flagNZ
	tst snesA, #0xFF
	orreq snesP, snesP, #flagZ
	tst snesA, #0x80
	orrne snesP, snesP, #flagN
	AddCycles 2
	b op_return
	
@ --- XCE ---------------------------------------------------------------------

OP_e0_XCE:
	tst snesP, #flagC
	AddCycles 1, eq
	beq op_return
	bic snesP, snesP, #flagC
	orr snesP, snesP, #(flagE|flagM)
	UpdateCPUMode
	AddCycles 1
	b op_return
	
OP_e1_XCE:
	tst snesP, #flagC
	AddCycles 1, ne
	bne op_return
	bic snesP, snesP, #flagE
	orr snesP, snesP, #(flagC|flagM|flagX)
	UpdateCPUMode
	AddCycles 1
	b op_return
	
@ --- Speed hack opcodes ------------------------------------------------------
@
@ opcode 42: eat cycles and branch back
@ opcode DB: eat cycles and branch (TODO)
@
@ -----------------------------------------------------------------------------

OP_HAX42:
	EatCycles
	ldrb r0, [r2, #1]
	add snesPC, snesPC, #0x10000
	and r1, r0, #0xE0
	and r0, r0, #0x0F
	sub r0, r0, #0x10
	add pc, pc, r1, lsr #1
	nop
	tst snesP, #flagN
	addeq snesPC, snesPC, r0, lsl #0x10
	b op_return
	nop
	tst snesP, #flagN
	addne snesPC, snesPC, r0, lsl #0x10
	b op_return
	nop
	tst snesP, #flagV
	addeq snesPC, snesPC, r0, lsl #0x10
	b op_return
	nop
	tst snesP, #flagV
	addne snesPC, snesPC, r0, lsl #0x10
	b op_return
	nop
	tst snesP, #flagC
	addeq snesPC, snesPC, r0, lsl #0x10
	b op_return
	nop
	tst snesP, #flagC
	addne snesPC, snesPC, r0, lsl #0x10
	b op_return
	nop
	tst snesP, #flagZ
	addeq snesPC, snesPC, r0, lsl #0x10
	b op_return
	nop
	tst snesP, #flagZ
	addne snesPC, snesPC, r0, lsl #0x10
	b op_return
