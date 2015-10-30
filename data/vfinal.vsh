// -----------------------------------------------------------------------------
// Copyright 2014 StapleButter
//
// This file is part of blargSnes.
//
// blargSnes is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// blargSnes is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with blargSnes. If not, see http://www.gnu.org/licenses/.
// -----------------------------------------------------------------------------

// setup constants
.alias myconst c5 as (0.0, 0.0, 0.0, 1.0)

// setup outmap
// TODO: Changing these to result.position/result.color breaks stuff!
.alias resultposition o0 as position
.alias resultcolor    o1 as color

// setup uniform map (not required)
.alias projMtx c0-c3

vmain:
	mov r1.xyz, v0.xyz
	mov r1.w, myconst.w

    // TODO: ; as the start of a comment doesn't cause an error?!

	// result.pos = projMtx * in.pos
	dp4 o0.x, projMtx[0].xyzw, r1.xyzw
	dp4 o0.y, projMtx[1].xyzw, r1.xyzw
	dp4 o0.z, projMtx[2].xyzw, r1.xyzw
	dp4 o0.w, projMtx[3].xyzw, r1.xyzw

	// result.texcoord = in.texcoord
	mov o1.xyzw, v1.xyzw
	end
	nop
