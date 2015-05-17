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
 
; setup outmap
	.out o0, result.position, 0xF
	.out o1, result.color, 0xF
 
; setup uniform map (not required)
	.uniform c0, c3, projMtx
	
	.gsh gmain, end_gmain
	
; input
; v0: XYZ coordinates
; v1: color
 
;code
	gmain:
		; turn two vertices into a rectangle
		; setemit: vtxid, primemit, winding
		
		; v0 = vertex 0, position
		; v1 = vertex 0, color
		; v2 = vertex 1, position
		; v3 = vertex 1, color
		
		; x1 y1
		setemit vtx0, false, false
		mov o0, v0 (0x0)
		mov o1, v1 (0x0)
		emit
		
		; x2 y1
		setemit vtx1, false, false
		mov o0, v2 (0x1)
		mov o0, v0 (0x2)
		mov o1, v3 (0x0)
		emit
		
		; x1 y2
		setemit vtx2, true, false
		mov o0, v0 (0x1)
		mov o0, v2 (0x2)
		mov o1, v1 (0x0)
		emit
		
		; x2 y2
		setemit vtx0, true, true
		mov o0, v2 (0x0)
		mov o1, v3 (0x0)
		emit
		
		end
		nop
	end_gmain:
 
;operand descriptors
	.opdesc xyzw, xyzw, xyzw ; 0x0
	.opdesc x_zw, xyzw, xyzw ; 0x1
	.opdesc _y__, yyyw, xyzw ; 0x2
