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


// setup outmap
.alias resultposition o0 as position
.alias resultcolor    o1 as color

// setup uniform map (not required)

// input
// v0: XY coordinates
// v1: 'alpha' in X

gmain:
	// turn two vertices into a rectangle
	// setemit: vtxid, primemit, winding

	// v0 = vertex 0, position
	// v1 = vertex 0, color
	// v2 = vertex 1, position
	// v3 = vertex 1, color

	// x1 y1
	setemitraw 0
	mov o0, v0
	mov o1, v1
	emit

	// x2 y1
	setemitraw 1
	mov o0.xzw, v2.xzw
	mov o0.y, v0.y
	mov o1, v3
	emit

	// x1 y2
	setemitraw 2, prim
	mov o0.xzw, v0.xzw
	mov o0.y, v2.y
	mov o1, v1
	emit

	// x2 y2
	setemitraw 0, prim, inv
	mov o0, v2
	mov o1, v3
	emit

	end
	nop
end_gmain:
