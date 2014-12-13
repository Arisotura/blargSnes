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
.align 4

#include "cpu.inc"

.text

.align 4
.global SNES_IORead8
.global SNES_IORead16
.global SNES_IOWrite8
.global SNES_IOWrite16

SNES_IORead8:
	stmdb sp!, {r12, lr}
	ldr lr, =ior8_ret
	and r12, r0, #0xFF00
	and r0, r0, #0xFF
	
	cmp r12, #0x2100
	beq PPU_Read8
	
	cmp r12, #0x4200
	beq SNES_GIORead8
	
	cmp r12, #0x4300
	beq DMA_Read8
	
	cmp r12, #0x4000
	addeq snesCycles, snesCycles, #0x60000
	beq SNES_JoyRead8
	
	ldrb r0, [snesStatus, #LastBusVal]
ior8_ret:
	ldmia sp!, {r12, pc}
	
	
SNES_IORead16:
	stmdb sp!, {r12, lr}
	ldr lr, =ior16_ret
	and r12, r0, #0xFF00
	and r0, r0, #0xFF
	
	cmp r12, #0x2100
	beq PPU_Read16
	
	cmp r12, #0x4200
	beq SNES_GIORead16
	
	cmp r12, #0x4300
	beq DMA_Read16
	
	cmp r12, #0x4000
	addeq snesCycles, snesCycles, #0xC0000
	beq SNES_JoyRead16
	
	ldrb r0, [snesStatus, #LastBusVal]
	orr r0, r0, r0, lsl #8
ior16_ret:
	ldmia sp!, {r12, pc}
	
	
SNES_IOWrite8:
	stmdb sp!, {r12, lr}
	ldr lr, =iow8_ret
	and r12, r0, #0xFF00
	and r0, r0, #0xFF
	
	cmp r12, #0x2100
	beq PPU_Write8
	
	cmp r12, #0x4300
	beq DMA_Write8
	
	cmp r12, #0x4000
	addeq snesCycles, snesCycles, #0x60000
	beq SNES_JoyWrite8
	
	cmp r12, #0x4200
	beq SNES_GIOWrite8

iow8_ret:
	ldmia sp!, {r12, pc}

	
SNES_IOWrite16:
	stmdb sp!, {r12, lr}
	ldr lr, =iow16_ret
	and r12, r0, #0xFF00
	and r0, r0, #0xFF
	
	cmp r12, #0x2100
	beq PPU_Write16
	
	cmp r12, #0x4300
	beq DMA_Write16
	
	cmp r12, #0x4000
	addeq snesCycles, snesCycles, #0xC0000
	beq SNES_JoyWrite16
	
	cmp r12, #0x4200
	beq SNES_GIOWrite16

iow16_ret:
	ldmia sp!, {r12, pc}
