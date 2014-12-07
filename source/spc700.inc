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

spcA 			.req r11
spcX			.req r10
spcY			.req r9
spcSP			.req r8
spcPC			.req r7		@ high hword: PC
spcPSW			.req r7		@ low hword: PSW
spcCycles		.req r6		@ SPC700 cycle count
memory			.req r5		@ pointer to the memory

@ --- Variables and whatever --------------------------------------------------

.equ flagC, 0x01
.equ flagZ, 0x02
.equ flagI, 0x04	@ interrupt enable (unused)
.equ flagH, 0x08	@ half carry
.equ flagB, 0x10	@ break (unused)
.equ flagP, 0x20	@ direct page
.equ flagV, 0x40
.equ flagN, 0x80
.equ flagR, 0x100	@ ROM access flag (0 = RAM, 1 = ROM)
.equ flagPause, 0x200

.equ flagNVHZC, 0xCB
.equ flagNVZC,  0xC3
.equ flagNZ,    0x82
.equ flagNZC,   0x83
.equ flagNVZ,   0xC2
.equ flagVH,    0x48
.equ flagNVH,   0xC8
.equ flagNVHZ,  0xCA
.equ flagZC,    0x03
