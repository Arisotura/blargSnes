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
.constf myconst (0.0, 0.0, 0.0078125, 1.0)

; setup outmap
.out outpos position
.out outclr color
.out outtc0 texcoord0.xy

; setup uniform map (not required)

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
	; v2 = vertex 1, position
	; v3 = vertex 1, texcoord

	; x1 y1
	setemit 0
	mov outpos, v0
	mov outclr, myconst.w
	mov outtc0, v1
	emit

	; x2 y1
	setemit 1
	mov outpos.xzw, v2
	mov outpos.y, v0.y
	mov outclr, myconst.w
	mov outtc0.x, v3.x
	mov outtc0.y, v1.y
	emit

	; x1 y2
	setemit 2, prim
	mov outpos.xzw, v0
	mov outpos.y, v2.y
	mov outclr, myconst.w
	mov outtc0.x, v1.x
	mov outtc0.y, v3.y
	emit

	; x2 y2
	setemit 0, prim inv
	mov outpos, v2
	mov outclr, myconst.w
	mov outtc0, v3
	emit

	end
.end
