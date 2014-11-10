; make sure you update aemstro_as for this (27/05/14)
 
; setup constants
	.const 5, 0.0, 0.0, 0.00390625, 1.0
 
; setup outmap
	.out o0, result.position
	.out o1, result.color
 
; setup uniform map (not required)
	.uniform 0x14, 0x17, projMtx
	
; input
; d00: XYZ coordinates
; d01: color
 
;code
	main:
		mov d1A, d00 (0x4)
		mov d1A, d25 (0x3)
		; result.pos = projMtx * in.pos
		dp4 d00, d40, d1A (0x0)
		dp4 d00, d41, d1A (0x1)
		dp4 d00, d42, d1A (0x2)
		dp4 d00, d43, d1A (0x3)
		; result.color = in.color
		mul d01, d25, d01 (0x6)
		flush
		end
	endmain:
 
;operand descriptors
	.opdesc x___, xyzw, xyzw ; 0x0
	.opdesc _y__, xyzw, xyzw ; 0x1
	.opdesc __z_, xyzw, xyzw ; 0x2
	.opdesc ___w, xyzw, xyzw ; 0x3
	.opdesc xyz_, xyzw, xyzw ; 0x4
	.opdesc xyzw, xyzw, xyzw ; 0x5
	.opdesc xyzw, zzzz, xyzw ; 0x6
	.opdesc xyzw, yyyw, xyzw ; 0x7
	.opdesc xyzw, wwww, wwww ; 0x8
