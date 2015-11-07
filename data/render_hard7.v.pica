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

; setup uniform map (not required)
.fvec projMtx[4], scaler, m7Mtx[4]

; INPUT
; - VERTEX ATTRIBUTES -
; v0: vertex (x, y, and optional z)
; v1: texcoord
; - UNIFORMS -
; c0-c3: projection matrix
; c4: texcoord scale

.entry vmain
.proc vmain
	mov r2.xyz, v0
	mov r2.w, myconst.w

	dp4 r1.x, m7Mtx[0], r2
	dp4 r1.y, m7Mtx[1], r2
	dp4 r1.z, m7Mtx[2], r2
	dp4 r1.w, m7Mtx[3], r2

	; result.pos = projMtx * temp.pos
	dp4 outpos.x, projMtx[0], r1
	dp4 outpos.y, projMtx[1], r1
	dp4 outpos.z, projMtx[2], r1
	dp4 outpos.w, projMtx[3], r1

	; result.texcoord = in.texcoord * scaler
	mul r1, scaler, v1
	mov outclr, r1
	end
.end
