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


PPU_CGRAMAddr:
	.hword 0
PPU_CGRAMVal:
	.byte 0
PPU_CGRAM:
	.rept 0x100
	.hword 0
	.endr


PPU_VRAMAddr:
	.hword 0
PPU_VRAMPref:
	.hword 0
PPU_VRAMInc:
	.byte 0
PPU_VRAMStep:
	.byte 0
PPU_VRAM:
	.rept 0x10000
	.byte 0
	.endr



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
	
	w8_211x:
		and r0, r0, #0x0F
		ldr pc, [pc, r0, lsl #2]
		nop
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_2115_VMAIN
		.long w8_2116_VMADDL
		.long w8_2117_VMADDH
		.long w8_2118_VMDATAL
		.long w8_2119_VMDATAH
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		.long w8_undef
		
		w8_2115_VMAIN:
			@ TODO: address translation?
			ldr r3, =PPU_VRAMInc
			and r0, r1, #0x80
			strb r0, [r3]		@ VRAM increment (low/high)
			and r0, r1, #0x03
			cmp r0, #0x00
			moveq r0, #2
			beq _2125_step0
			cmp r0, #0x01
			moveq r0, #64
			movne r0, #256
			_2125_step0:
			strb r0, [r3, #1]	@ VRAM inc step
			bx lr
			
		w8_2116_VMADDL:
			ldr r3, =PPU_VRAM
			ldrh r0, [r3, #(PPU_VRAMAddr-PPU_VRAM)]
			and r0, r0, #0xFE00
			orr r0, r0, r1, lsl #1
			strh r0, [r3, #(PPU_VRAMAddr-PPU_VRAM)]
			ldrh r0, [r3, r0]
			strh r0, [r3, #(PPU_VRAMPref-PPU_VRAM)]	@ VRAM prefetch
			bx lr
		w8_2117_VMADDH:
			ldr r3, =PPU_VRAM
			ldrh r0, [r3, #(PPU_VRAMAddr-PPU_VRAM)]
			bic r0, r0, #0xFE00
			bic r1, r1, #0x80
			orr r0, r0, r1, lsl #9
			strh r0, [r3, #(PPU_VRAMAddr-PPU_VRAM)]
			ldrh r0, [r3, r0]
			strh r0, [r3, #(PPU_VRAMPref-PPU_VRAM)]	@ VRAM prefetch
			bx lr
			
		w8_2118_VMDATAL:
			ldr r3, =PPU_VRAM
			ldrh r0, [r3, #(PPU_VRAMAddr-PPU_VRAM)]
			strb r1, [r3, r0]
			ldrb r1, [r3, #(PPU_VRAMInc-PPU_VRAM)]
			tst r1, #0x80
			bxne lr
			ldrb r1, [r3, #(PPU_VRAMStep-PPU_VRAM)]
			add r0, r0, r1
			strh r0, [r3, #(PPU_VRAMAddr-PPU_VRAM)]
			bx lr
			
		w8_2119_VMDATAH:
			ldr r3, =PPU_VRAM
			ldrh r0, [r3, #(PPU_VRAMAddr-PPU_VRAM)]
			add r0, r0, #1
			strb r1, [r3, r0]
			ldrb r1, [r3, #(PPU_VRAMInc-PPU_VRAM)]
			tst r1, #0x80
			bxeq lr
			ldrb r1, [r3, #(PPU_VRAMStep-PPU_VRAM)]
			sub r0, r0, #1
			add r0, r0, r1
			strh r0, [r3, #(PPU_VRAMAddr-PPU_VRAM)]
			bx lr
	
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
	
	w16_211x:
		and r0, r0, #0x0F
		ldr pc, [pc, r0, lsl #2]
		nop
		.long w16_undef
		.long w16_undef
		.long w16_undef
		.long w16_undef
		.long w16_undef
		.long w16_undef
		.long w16_2116_VMADD
		.long w16_undef
		.long w16_2118_VMDATA
		.long w16_undef
		.long w16_undef
		.long w16_undef
		.long w16_undef
		.long w16_undef
		.long w16_undef
		.long w16_undef
			
		w16_2116_VMADD:
			ldr r3, =PPU_VRAM
			mov r0, r1, lsl #1
			bic r0, r0, #0x10000
			strh r0, [r3, #(PPU_VRAMAddr-PPU_VRAM)]
			ldrh r0, [r3, r0]
			strh r0, [r3, #(PPU_VRAMPref-PPU_VRAM)]	@ VRAM prefetch
			bx lr
			
		w16_2118_VMDATA:
			ldr r3, =PPU_VRAM
			ldrh r0, [r3, #(PPU_VRAMAddr-PPU_VRAM)]
			strh r1, [r3, r0]
			ldrb r1, [r3, #(PPU_VRAMStep-PPU_VRAM)]
			add r0, r0, r1
			strh r0, [r3, #(PPU_VRAMAddr-PPU_VRAM)]
			bx lr
	
	w16_210x:
	w16_212x:
	w16_213x:
	w16_214x:
	w16_218x:
	w16_undef:
		mov r3, r1
		stmdb sp!, {r0, r3, lr}
		and r1, r1, #0xFF
		bl PPU_Write8
		ldmia sp!, {r0, r3, lr}
		add r0, r0, #1
		mov r1, r3, lsr #8
		b PPU_Write8

		
		
@ r0 = line
PPU_RenderScanline:
	stmdb sp!, {r4-r12, lr}
	
	ldr r3, =TopFB
	ldr r3, [r3]
	ldr r12, =35024
	add r3, r3, r12
	sub r3, r3, r0, lsl #1
	ldr r12, =PPU_ColorTable
	ldr r1, =PPU_CGRAM
	mov r9, #0x100
	
	ldrh r2, [r1] @ main backdrop color
	mov r4, r3
	fill_backdrop:
		strh r2, [r4]
		add r4, r4, #PIXEL_OFFSET
		subs r9, r9, #1
		bne fill_backdrop
		
	mov r9, #0x100
	
	@ r12 = color table
	@ r11 = FREE
	@ r10 = tile pixel index
	@ r9  = pixel count
	@ r8  = VRAM map pointer
	@ r7  = VRAM tile pointer
	@ r6  = Y offset within tile
	@ r5  = scratch
	@ r4  = current tile val
	@ r3  = output framebuffer pointer
	@ r1  = pointer to CGRAM
	
	@ test, really
	ldr r8, =(PPU_VRAM+0x1000)
	ldr r7, =(PPU_VRAM+0x8000)
	and r6, r0, #7
	bic r2, r0, #7
	add r8, r8, r2, lsl #3
	
	mov r2, #0
	rsb r10, r2, #8
	
	@ preload tile
	ldrh r4, [r8]
	bic r5, r4, #0xFC00
	tst r4, #0x8000		@ VFlip
	addeq r0, r7, r6, lsl #1
	addne r0, r7, #14
	subne r0, r0, r6, lsl #1
	ldr r0, [r0, r5, lsl #4]
	
	tst r4, #0x4000		@ HFlip
	moveq r0, r0, lsl r2
	movne r0, r0, lsr r2
	
	lolzloop:
	@ 2bpp tile test
		
		mov r2, #0
		tst r4, #0x4000
		bne tile_hflip2
			tst r0, #0x0080
			orrne r2, r2, #0x2
			tst r0, #0x8000
			orrne r2, r2, #0x4
			mov r0, r0, lsl #1
			b tile_nohflip2
		tile_hflip2:
			tst r0, #0x0001
			orrne r2, r2, #0x2
			tst r0, #0x0100
			orrne r2, r2, #0x4
			mov r0, r0, lsr #1
		tile_nohflip2:
		cmp r2, #0
		beq emptypixel2
		
		@ load color from palette, store pixel
		and r5, r4, #0x1C00
		add r2, r2, r5, lsr #7		@ tile palette index
		ldrh r2, [r1, r2]			@ load from CGRAM
		ldr r2, [r12, r2, lsl #1]	@ load from SNES->3DS color table
		strh r2, [r3]				@ store in framebuffer
		emptypixel2:
		add r3, r3, #PIXEL_OFFSET
		
		@ increment
		@ TODO WRAPAROUND
		subs r10, r10, #1
		bne inc_noreload2
			mov r10, #8
			add r8, r8, #2
			ldrh r4, [r8]
			bic r5, r4, #0xFC00
			tst r4, #0x8000		@ VFlip
			addeq r0, r7, r6, lsl #1
			addne r0, r7, #14
			subne r0, r0, r6, lsl #1
			ldr r0, [r0, r5, lsl #4]
		inc_noreload2:
		
		subs r9, r9, #1
		bne lolzloop

	ldmia sp!, {r4-r12, pc}
