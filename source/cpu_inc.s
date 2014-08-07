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

@ --- Register aliases --------------------------------------------------------

snesA 			.req r12
snesX			.req r11
snesY			.req r10
snesS			.req r9		@ high hword: S
snesPBR			.req r9		@ low hword: PBR
snesD			.req r8		@ high hword: D
snesDBR			.req r8		@ low hword: DBR
snesPC			.req r7		@ high hword: PC
snesP			.req r7		@ low hword: P
snesCycles		.req r6		@ high hword: master cycle count
snesLines		.req r6		@ low hword: scanline count (0-261)
opTable			.req r5		@ pointer to the opcode table in use
memoryMap		.req r4		@ pointer to the memory map table

@ --- Variables and whatever --------------------------------------------------

.equ flagC, 0x01
.equ flagZ, 0x02
.equ flagI, 0x04
.equ flagD, 0x08
.equ flagX, 0x10
.equ flagB, 0x10
.equ flagM, 0x20
.equ flagV, 0x40
.equ flagN, 0x80
.equ flagE, 0x100	@ not actually in P, but hey, we have to keep it somewhere
.equ flagW, 0x200	@ not even an actual flag... set by WAI
.equ flagI2, 0x400	@ set when NMIs are disabled

.equ flagNVZC, 0xC3
.equ flagNZ,   0x82
.equ flagNZC,  0x83
.equ flagNVZ,  0xC2

@ --- Handy macros ------------------------------------------------------------

.macro SafeCall func
	stmdb sp!, {r12}
	bl \func
	ldmia sp!, {r12}
.endm

.macro SafeCall_03 func
	stmdb sp!, {r0, r3, r12}
	bl \func
	ldmia sp!, {r0, r3, r12}
.endm

@ --- Debugging ---------------------------------------------------------------

.macro DbgPrint
	stmdb sp!, {r12, lr}
	bl bprintf
	ldmia sp!, {r12, lr}
.endm

.macro DbgPrintAndHalt stuff
	add r0, pc, #8
	bl bprintf
1:
	swi #0x50000
	b 1b
.ascii \stuff
.byte 0
.align 4
.endm
