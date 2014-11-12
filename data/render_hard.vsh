; make sure you update aemstro_as for this (27/05/14)
 
; setup constants
	.const 5, 0.0, 0.0, 0.0078125, 1.0
 
; setup outmap
	.out o0, result.position
	.out o1, result.color
	.out o2, result.texcoord0
 
; setup uniform map (not required)
	.uniform 0x14, 0x17, projMtx
	
; INPUT
; - VERTEX ATTRIBUTES -
; d00: vertex (x, y, and optional z)
; d01: texcoord
; - UNIFORMS -
; d40-d43: projection matrix
; d44: texcoord scale
; d45: x -> weight for vertex Z, y -> weight for uniform Z, z -> uniform Z
 
;code
	main:
		mov d1A, d00 (0x4)
		mov d1A, d25 (0x3)
		mul d1A, d45, d1A (0x6)
		mul d1B, d46, d46 (0x7)
		add d1A, d1A, d1B (0x2)
		; result.pos = projMtx * in.pos
		dp4 d00, d40, d1A (0x0)
		dp4 d00, d41, d1A (0x1)
		dp4 d00, d42, d1A (0x2)
		dp4 d00, d43, d1A (0x3)
		; result.texcoord = in.texcoord * (uniform scale in d44)
		mul d1A, d44, d01 (0x5)
		mov d02, d1A (0x5)
		; result.color = white
		mov d01, d25 (0x8)
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
	.opdesc __z_, xxxx, xyzw ; 0x6
	.opdesc __z_, yyyy, xyzw ; 0x7
	.opdesc xyzw, wwww, wwww ; 0x8
