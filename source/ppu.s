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

.global PPU_ColorTable

.global PPU_Reset
.global PPU_Read8
.global PPU_Read16
.global PPU_Write8
.global PPU_Write16
.global PPU_RenderScanline

.equ PIXEL_OFFSET, 480
.equ LINE_OFFSET, -122881

.section    .data, "aw", %progbits

PPU_ColorTable:
.rept 0x10000
.hword 0
.endr


PPU_CGRAM:
.rept 0x100
.hword 0
.endr


PPU_CGRAMAddr:
.hword 0
PPU_CGRAMVal:
.byte 0




.section    .text, "awx", %progbits

PPU_Reset:
	@ TODO: reset shit here
	bx lr
	
	

PPU_Read8:
	mov r0, #0
	bx lr
	
	
	
PPU_Read16:
	mov r0, #0
	bx lr
	
	
PPU_Write8:
	and r3, r0, #0xF0
	ldr pc, [pc, r3, lsr #2]
	nop
	.long w8_210x
	.long w8_211x
	.long w8_212x
	.long w8_213x
	.long w8_214x
	.long w8_undef
	.long w8_undef
	.long w8_undef
	.long w8_218x
	.long w8_undef
	.long w8_undef
	.long w8_undef
	.long w8_undef
	.long w8_undef
	.long w8_undef
	.long w8_undef
	
	w8_212x:
		and r0, r0, #0x0F
		ldr pc, [pc, r0, lsl #2]
		nop
		.long w8_undef
		.long w8_2121_CGADD
		.long w8_2122_CGDATA
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		
		w8_2121_CGADD:
			ldr r3, =PPU_CGRAMAddr
			mov r1, r1, lsl #1
			strh r1, [r3]
			bx lr
		
		w8_2122_CGDATA:
			ldr r3, =PPU_CGRAMAddr
			ldrh r0, [r3]
			tst r0, #1
			add r0, r0, #1
			bic r0, r0, #0xFE00
			strh r0, [r3]
			streqb r1, [r3, #2] @ store temp lsb
			bxeq lr
			ldrb r3, [r3, #2] @ temp lsb
			orr r1, r3, r1, lsl #8
			ldr r3, =PPU_CGRAM
			sub r0, r0, #2
			strh r1, [r3, r0]
			bx lr
		
	w8_210x:
	w8_211x:
	w8_213x:
	w8_214x:
	w8_218x:
	w8_undef:
		bx lr
	
	

PPU_Write16:
	and r3, r0, #0xF0
	ldr pc, [pc, r3, lsr #2]
	nop
	.long w16_210x
	.long w16_211x
	.long w16_212x
	.long w16_213x
	.long w16_214x
	.long w16_undef
	.long w16_undef
	.long w16_undef
	.long w16_218x
	.long w16_undef
	.long w16_undef
	.long w16_undef
	.long w16_undef
	.long w16_undef
	.long w16_undef
	.long w16_undef
	
	w16_210x:
	w16_211x:
	w16_212x:
	w16_213x:
	w16_214x:
	w16_218x:
	w16_undef:
		bx lr

		
		
@ r0 = line
PPU_RenderScanline:
	stmdb sp!, {r12}
	
	ldr r3, =TopFB
	ldr r3, [r3]
	ldr r12, =35024
	add r3, r3, r12
	sub r3, r3, r0, lsl #1
	ldr r12, =PPU_ColorTable
	ldr r1, =PPU_CGRAM
	mov r2, #0x200
	
lolzloop:
	subs r2, r2, #2
	ldrh r0, [r1, r2]
	ldr r0, [r12, r0, lsl #1]
	strh r0, [r3]
	add r3, r3, #PIXEL_OFFSET
	bne lolzloop

	ldmia sp!, {r12}
	bx lr
