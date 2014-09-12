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

@ cycles per scanline: 1364
@ cycles per frame: 357368 (NTSC), 425568 (PAL)
@ 4 cycles per dot
@
@ -- EVENTS --
@
@ * HBlank start
@ * VBlank start (and NMI)
@ * VBlank end
@ * IRQ (1 = every scanline, at pixel X; 2 = scanline Y, at pixel 0 (hbl end); 3 = scanline Y, at pixel X

.section    .text, "awx", %progbits

