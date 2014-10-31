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
.text


@ clear 32 bytes (16 samples)
@ r0: left buffer
@ r1: right buffer
@ size should be a multiple of 16

.global zerofillBuffers
zerofillBuffers:
	stmdb sp!, {r4-r5, lr}
	mov r2, #0
	mov r3, #0
	mov r4, #0
	mov r5, #0
	stmia r0!, {r2-r5}
	stmia r0!, {r2-r5}
	stmia r1!, {r2-r5}
	stmia r1!, {r2-r5}
	ldmia sp!, {r4-r5, pc}
