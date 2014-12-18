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
	.const c5, 0.0, 0.0, 0.00390625, 1.0
 
; setup outmap
	.out o0, result.position
	.out o1, result.color
 
; setup uniform map (not required)
	.uniform c0, c3, projMtx
	
	.vsh vmain, end_vmain
	.gsh gmain, end_gmain
	
; input
; v0: XY coordinates
; v1: 'alpha' in X
 
;code
	vmain:
		mov r1, v0 (0x4)
		mov r1, c5 (0x3)
		; result.pos = projMtx * in.pos
		dp4 o0, c0, r1 (0x0)
		dp4 o0, c1, r1 (0x1)
		dp4 o0, c2, r1 (0x2)
		dp4 o0, c3, r1 (0x3)
		; result.color = in.color
		mul o1, c5, v1 (0x8)
		flush
	end_vmain:
	
	gmain:
		; turn two vertices into a rectangle
		; setemit: vtxid, primemit, winding
		
		; v0 = vertex 0, position
		; v1 = vertex 0, color
		; v2 = vertex 1, position
		; v3 = vertex 1, color
		
		; x1 y1
		setemit vtx0, false, false
		mov o0, v0 (0x5)
		mov o1, v1 (0x5)
		emit
		
		; x2 y1
		setemit vtx1, false, false
		mov o0, v2 (0x6)
		mov o0, v0 (0x7)
		mov o1, v3 (0x5)
		emit
		
		; x1 y2
		setemit vtx2, true, false
		mov o0, v0 (0x6)
		mov o0, v2 (0x7)
		mov o1, v1 (0x5)
		emit
		
		; x2 y2
		setemit vtx0, true, true
		mov o0, v2 (0x5)
		mov o1, v3 (0x5)
		emit
		
		flush
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
	.opdesc xyzw, zzzz, xxxx ; 0x8
