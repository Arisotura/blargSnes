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

 
; setup outmap
	.out o0, result.position, 0xF
	.out o1, result.color, 0xF

 
; setup uniform map (not required)
	.uniform c0, c3, projMtx
		

	.vsh vmain, end_vmain
	
; INPUT
; - VERTEX ATTRIBUTES -
; v0: vertex (x, y, and optional z)
; v1: texcoord
; - UNIFORMS -
; c0-c3: projection matrix
; c4: texcoord scale
 
;code
	vmain:
		mov r1, v0 (0x4)
		mov r1, c9 (0x3)
		
		; result.pos = projMtx * temp.pos
		dp4 o0, c0, r1 (0x0)
		dp4 o0, c1, r1 (0x1)
		dp4 o0, c2, r1 (0x2)
		dp4 o0, c3, r1 (0x3)

		; result.texcoord = in.texcoord * (uniform scale in c4)
		mul r1, c4, v1 (0x5)
		mov o1, r1 (0x5)
		end
		nop
	end_vmain:
	 
;operand descriptors
	.opdesc x___, xyzw, xyzw ; 0x0
	.opdesc _y__, xyzw, xyzw ; 0x1
	.opdesc __z_, xyzw, xyzw ; 0x2
	.opdesc ___w, xyzw, xyzw ; 0x3
	.opdesc xyz_, xyzw, xyzw ; 0x4
	.opdesc xyzw, xyzw, xyzw ; 0x5
