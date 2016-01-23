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
.constf myconst (0.0, 0.0, 0.0, 1.0)

; setup outmap
.out outpos position
.out outclr color

; setup uniform map (not required)
.fvec projMtx[4]

.entry vmain
.proc vmain
	mov r1.xyz, v0
	mov r1.w, myconst.w

	; result.pos = projMtx * in.pos
	dp4 outpos.x, projMtx[0], r1
	dp4 outpos.y, projMtx[1], r1
	dp4 outpos.z, projMtx[2], r1
	dp4 outpos.w, projMtx[3], r1

	; result.color = in.color
	mov outclr, v1
	end
.end
