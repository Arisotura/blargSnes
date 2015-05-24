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
.alias const9  c9  as (0.0, 0.0, 0.0078125, 1.0)
.alias const10 c10 as (0.00004, 0.00004, 0.0, 0.0)
.alias const11 c11 as (0.0077725, 0.00004, 0.0, 0.0)
.alias const12 c12 as (0.00004, 0.0077725, 0.0, 0.0)
.alias const13 c13 as (0.0077725, 0.0077725, 0.0, 0.0)

// setup outmap
.alias resultposition o0 as position
.alias resultcolor    o1 as color
.alias resulttex0     o2.xy as texcoord0

// setup uniform map (not required)

// INPUT
// - VERTEX ATTRIBUTES -
// v0: vertex (x, y, and optional z)
// v1: texcoord
// - UNIFORMS -
// c0-c3: projection matrix
// c4: texcoord scale

gmain:
	// turn two vertices into a rectangle
	// setemit: vtxid, primemit, winding

	// v0 = vertex 0, position
	// v1 = vertex 0, texcoord

	// x1 y1
	setemitraw 0
	mov o0, v0
	mov o1, c9.wwww
	mov r3, v1
	add o2, c10, r3
	emit

	// x2 y1
	setemitraw 1
	mov r3, c14
	add o0, v0, r3
	mov o1, c9.wwww
	mov r3, v1
	add o2, c11, r3
	emit

	// x1 y2
	setemitraw 2, prim
	mov r3, c15
	add o0, v0, r3
	mov o1, c9.wwww
	mov r3, v1
	add o2, c12, r3
	emit

	// x2 y2
	setemitraw 0, prim, inv
	mov r4, c14
	add r3, c15, r4
	add o0, v0, r3
	mov o1, c9.wwww
	mov r3, v1
	add o2, c13, r3
	emit

	end
	nop
end_gmain:
