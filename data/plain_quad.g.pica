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
.out outpos position
.out outclr color

; setup uniform map (not required)
.fvec projMtx[4]

; input
; v0: XYZ coordinates
; v1: color

.gsh
.entry gmain
.proc gmain
	; turn two vertices into a rectangle
	; setemit: vtxid, primemit winding

	; v0 = vertex 0, position
	; v1 = vertex 0, color
	; v2 = vertex 1, position
	; v3 = vertex 1, color

	; x1 y1
	setemit 0
	mov outpos, v0
	mov outclr, v1
	emit

	; x2 y1
	setemit 1
	mov outpos.xzw, v2
	mov outpos.y, v0.y
	mov outclr, v3
	emit

	; x1 y2
	setemit 2, prim
	mov outpos.xzw, v0
	mov outpos.y, v2.y
	mov outclr, v1
	emit

	; x2 y2
	setemit 0, prim inv
	mov outpos, v2
	mov outclr, v3
	emit

	end
.end
