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
	.const c9, 0.0, 0.0, 0.0078125, 1.0
	.const c10, 0.00004, 0.00004, 0.0, 0.0
 	.const c11, 0.0077725, 0.00004, 0.0, 0.0
	.const c12, 0.00004, 0.0077725, 0.0, 0.0
	.const c13, 0.0077725, 0.0077725, 0.0, 0.0
 
; setup outmap
	.out o0, result.position, 0xF
	.out o1, result.color, 0xF
	.out o2, result.texcoord0, 0x3
 
; setup uniform map (not required)
		
	.gsh gmain, end_gmain
	
; INPUT
; - VERTEX ATTRIBUTES -
; v0: vertex (x, y, and optional z)
; v1: texcoord
; - UNIFORMS -
; c0-c3: projection matrix
; c4: texcoord scale
 
;code
	gmain:
		; turn two vertices into a rectangle
		; setemit: vtxid, primemit, winding
		
		; v0 = vertex 0, position
		; v1 = vertex 0, texcoord
		
		; x1 y1
		setemit vtx0, false, false
		mov o0, v0 (0x5)
		mov o1, c9 (0x8)
		mov r3, v1 (0x5)
		add o2, c10, r3 (0x5)
		emit
		
		; x2 y1
		setemit vtx1, false, false
		mov r3, c14 (0x5)
		add o0, v0, r3 (0x5)
		mov o1, c9 (0x8)
		mov r3, v1 (0x5)
		add o2, c11, r3 (0x5)
		emit
		
		; x1 y2
		setemit vtx2, true, false
		mov r3, c15 (0x5)
		add o0, v0, r3 (0x5)
		mov o1, c9 (0x8)
		mov r3, v1 (0x5)
		add o2, c12, r3 (0x5)
		emit
		
		; x2 y2
		setemit vtx0, true, true
		mov r4, c14 (0x5)
		add r3, c15, r4 (0x5)
		add o0, v0, r3 (0x5)
		mov o1, c9 (0x8)
		mov r3, v1 (0x5)
		add o2, c13, r3 (0x5)
		emit

		
		end
		nop
	end_gmain:
 
;operand descriptors
	.opdesc x___, xyzw, xyzw ; 0x0
	.opdesc _y__, xyzw, xyzw ; 0x1
	.opdesc __z_, xyzw, xyzw ; 0x2
	.opdesc ___w, xyzw, xyzw ; 0x3
	.opdesc xyz_, xyzw, xyzw ; 0x4
	.opdesc xyzw, xyzw, xyzw ; 0x5
	.opdesc x_zw, xyzw, xyzw ; 0x6
	.opdesc _y__, yyyw, xyzw ; 0x7
	.opdesc xyzw, wwww, wwww ; 0x8