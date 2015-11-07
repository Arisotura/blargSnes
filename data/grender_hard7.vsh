; -----------------------------------------------------------------------------
; Copyright 2014 StapleButter
;
; This file is part of blargSnes.
;
; blargSnes is free software: you can redistribute it and/or modify it under
; the terms of the GNU General Public License as published by the Free
; Software Foundation, either version 3 of the License, or (at your option)
; any later version.
;
; blargSnes is distributed in the hope that it will be useful, but WITHOUT ANY
; WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
; FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License along
; with blargSnes. If not, see http://www.gnu.org/licenses/.
; -----------------------------------------------------------------------------

; setup constants
.constf const9  (0.0, 0.0, 0.0078125, 1.0)
.constf const10 (0.00004, 0.00004, 0.0, 0.0)
.constf const11 (0.0077725, 0.00004, 0.0, 0.0)
.constf const12 (0.00004, 0.0077725, 0.0, 0.0)
.constf const13 (0.0077725, 0.0077725, 0.0, 0.0)

; setup outmap
.out outpos position
.out outclr color
.out outtc0 texcoord0.xy

; setup uniform map (not required)
.fvec snesM7Matrix[2] ; TODO: use! currently hardcoding c14/c15

; INPUT
; - VERTEX ATTRIBUTES -
; v0: vertex (x, y, and optional z)
; v1: texcoord
; - UNIFORMS -
; c0-c3: projection matrix
; c4: texcoord scale

.gsh
.entry gmain
.proc gmain
	; turn two vertices into a rectangle
	; setemit: vtxid, primemit winding

	; v0 = vertex 0, position
	; v1 = vertex 0, texcoord

	; x1 y1
	setemit 0
	mov outpos, v0
	mov outclr, const9.w
	mov r3, v1
	add outtc0, const10, r3
	emit

	; x2 y1
	setemit 1
	mov r3, c14
	add outpos, v0, r3
	mov outclr, const9.w
	mov r3, v1
	add outtc0, const11, r3
	emit

	; x1 y2
	setemit 2, prim
	mov r3, c15
	add outpos, v0, r3
	mov outclr, const9.w
	mov r3, v1
	add outtc0, const12, r3
	emit

	; x2 y2
	setemit 0, prim inv
	mov r4, c14
	add r3, c15, r4
	add outpos, v0, r3
	mov outclr, const9.w
	mov r3, v1
	add outtc0, const13, r3
	emit

	end
.end
