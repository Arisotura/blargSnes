/*
    Copyright 2014 StapleButter

    This file is part of blargSnes.

    blargSnes is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    blargSnes is distributed in the hope that it will be useful, but WITHOUT ANY 
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along 
    with blargSnes. If not, see http://www.gnu.org/licenses/.
*/

#include <3ds.h>

#include "blargGL.h"
#include "mem.h"
#include "snes.h"
#include "ppu.h"


#define SET_UNIFORM(n, v0, v1, v2, v3) \
	{ \
		float blarg[4] = {(v0), (v1), (v2), (v3)}; \
		bglUniform(n, blarg); \
	}


extern void* vertexBuf;
extern void* vertexPtr;

extern DVLB_s* hardRenderShader;
extern DVLB_s* plainQuadShader;

extern float snesProjMatrix[16];

extern u32* gpuOut;
extern u32* gpuDOut;
extern u32* SNESFrame;
extern u16* MainScreenTex;
extern u16* SubScreenTex;

u32* OBJColorBuffer;
u32* OBJDepthBuffer;

u32* Mode7ColorBuffer;

u16 TempPalette[256];

int doingBG = 0;


// tile cache:
// 1024x1024 RGBA5551 texture (2 MB)
// -> can hold 16384 tiles
// FIFO

// tile is recognized by:
// * absolute address
// * type (color depth, normal/hires)
// * palette #

// maps needed:
// * tile key (addr/type/pal) -> u/v in tilecache texture
// * u/v (tilecache index) -> tile key (for removing tiles from the cache)
// * VRAM address -> dirty flag (for VRAM updates)
// * palette offset -> dirty flag (for palette updates)

// system for palette updates? (tile doesn't need to be redecoded, only recolored)
// -> needs storing the tile in indexed format (might slow shit down when palette isn't updated)


// SHIT THAT CAN BE CHANGED MIDFRAME

// + video mode
// + layer enable/disable

// * mosaic

// + master brightness
// + color math add/sub

// + layer scroll
// + layer tileset/tilemap address
// * mode7 scroll/matrix/etc
// + color math layer sel

// * OBJ tileset address

// * window registers

// + sub backdrop color


// STENCIL BUFFER USAGE
// * bit0: window mask
// * bit1: 0=clear alpha, 1=keep alpha

// DEPTH BUFFER USAGE (probably not right -- TODO update)
// * 0x00-0x30: OBJ pal 0-3 (prio 0-3 resp.)
// * 0x40-0x70: OBJ pal 4-7 (prio 0-3 resp.)

// (note that final depth values are transformed to range between 0 and -1 
//  -- GPU_GREATER in the depth test actually means LESS)


#define TILE_2BPP 0
#define TILE_4BPP 1
#define TILE_8BPP 2


u16* PPU_TileCache;
u32 PPU_TileCacheIndex;

u16 PPU_TileCacheList[0x10000];
u32 PPU_TileCacheReverseList[16384];

u32 PPU_TileVRAMUpdate[0x10000];
u32 PPU_TilePalUpdate[0x10000];


void PPU_Init_Hard()
{
	int i;
	
	// main/sub screen buffers, RGBA8
	MainScreenTex = (u16*)VRAM_Alloc(256*512*4);
	SubScreenTex = (u16*)&((u32*)MainScreenTex)[256*256];
	
	OBJColorBuffer = (u32*)VRAM_Alloc(256*256*4);
	OBJDepthBuffer = (u32*)VRAM_Alloc(256*256*4);
	
	Mode7ColorBuffer = (u32*)linearAlloc(256*256*4);
	
	PPU_TileCache = (u16*)linearAlloc(1024*1024*sizeof(u16));
	PPU_TileCacheIndex = 0;
	
	for (i = 0; i < 0x10000; i++)
	{
		PPU_TileCacheList[i] = 0x8000;
		PPU_TileVRAMUpdate[i] = 0;
		PPU_TilePalUpdate[i] = 0;
	}
	
	for (i = 0; i < 16384; i++)
		PPU_TileCacheReverseList[i] = 0x80000000;
		
	for (i = 0; i < 1024*1024; i++)
		PPU_TileCache[i] = 0xF800;
	for (i = 0; i < 64; i++)
		PPU_TileCache[i] = 0x07C0;
}

void PPU_DeInit_Hard()
{
	linearFree(PPU_TileCache);
}


// tile decoding
// note: tiles are directly converted to PICA200 tiles (zcurve)

void PPU_DecodeTile_2bpp(u16* vram, u16* pal, u32* dst)
{
	int i;
	u8 p1, p2, p3, p4;
	u32 col1, col2;
	
	u16 oldcolor0 = pal[0];
	pal[0] = 0;
	
#define DO_MINIBLOCK(l1, l2) \
	p1 = 0; p2 = 0; p3 = 0; p4 = 0; \
	if (l2 & 0x0080) p1 |= 0x01; \
	if (l2 & 0x8000) p1 |= 0x02; \
	if (l2 & 0x0040) p2 |= 0x01; \
	if (l2 & 0x4000) p2 |= 0x02; \
	l2 <<= 2; \
	col1 = pal[p1] | (pal[p2] << 16); \
	if (l1 & 0x0080) p3 |= 0x01; \
	if (l1 & 0x8000) p3 |= 0x02; \
	if (l1 & 0x0040) p4 |= 0x01; \
	if (l1 & 0x4000) p4 |= 0x02; \
	l1 <<= 2; \
	col2 = pal[p3] | (pal[p4] << 16); \
	*dst++ = col1; \
	*dst++ = col2; 
	
	for (i = 4; i >= 0; i -= 4)
	{
		u16 line1 = vram[i+0];
		u16 line2 = vram[i+1];
		u16 line3 = vram[i+2];
		u16 line4 = vram[i+3];
		
		DO_MINIBLOCK(line3, line4);
		DO_MINIBLOCK(line3, line4);
		DO_MINIBLOCK(line1, line2);
		DO_MINIBLOCK(line1, line2);
		DO_MINIBLOCK(line3, line4);
		DO_MINIBLOCK(line3, line4);
		DO_MINIBLOCK(line1, line2);
		DO_MINIBLOCK(line1, line2);
	}
	
	pal[0] = oldcolor0;
	
#undef DO_MINIBLOCK
}

void PPU_DecodeTile_4bpp(u16* vram, u16* pal, u32* dst)
{
	int i;
	u8 p1, p2, p3, p4;
	u32 col1, col2;
	
	u16 oldcolor0 = pal[0];
	pal[0] = 0;
	
#define DO_MINIBLOCK(l1, l2) \
	p1 = 0; p2 = 0; p3 = 0; p4 = 0; \
	if (l2 & 0x00000080) p1 |= 0x01; \
	if (l2 & 0x00008000) p1 |= 0x02; \
	if (l2 & 0x00800000) p1 |= 0x04; \
	if (l2 & 0x80000000) p1 |= 0x08; \
	if (l2 & 0x00000040) p2 |= 0x01; \
	if (l2 & 0x00004000) p2 |= 0x02; \
	if (l2 & 0x00400000) p2 |= 0x04; \
	if (l2 & 0x40000000) p2 |= 0x08; \
	l2 <<= 2; \
	col1 = pal[p1] | (pal[p2] << 16); \
	if (l1 & 0x00000080) p3 |= 0x01; \
	if (l1 & 0x00008000) p3 |= 0x02; \
	if (l1 & 0x00800000) p3 |= 0x04; \
	if (l1 & 0x80000000) p3 |= 0x08; \
	if (l1 & 0x00000040) p4 |= 0x01; \
	if (l1 & 0x00004000) p4 |= 0x02; \
	if (l1 & 0x00400000) p4 |= 0x04; \
	if (l1 & 0x40000000) p4 |= 0x08; \
	l1 <<= 2; \
	col2 = pal[p3] | (pal[p4] << 16); \
	*dst++ = col1; \
	*dst++ = col2; 
	
	for (i = 4; i >= 0; i -= 4)
	{
		u32 line1 = vram[i+0] | (vram[i+8 ] << 16);
		u32 line2 = vram[i+1] | (vram[i+9 ] << 16);
		u32 line3 = vram[i+2] | (vram[i+10] << 16);
		u32 line4 = vram[i+3] | (vram[i+11] << 16);
		
		DO_MINIBLOCK(line3, line4);
		DO_MINIBLOCK(line3, line4);
		DO_MINIBLOCK(line1, line2);
		DO_MINIBLOCK(line1, line2);
		DO_MINIBLOCK(line3, line4);
		DO_MINIBLOCK(line3, line4);
		DO_MINIBLOCK(line1, line2);
		DO_MINIBLOCK(line1, line2);
	}
	
	pal[0] = oldcolor0;
	
#undef DO_MINIBLOCK
}

void PPU_DecodeTile_8bpp(u16* vram, u16* pal, u32* dst)
{
	int i;
	u8 p1, p2, p3, p4;
	u32 col1, col2;
	
	u16 oldcolor0 = pal[0];
	pal[0] = 0;
	
#define DO_MINIBLOCK(l11, l12, l21, l22) \
	p1 = 0; p2 = 0; p3 = 0; p4 = 0; \
	if (l21 & 0x00000080) p1 |= 0x01; \
	if (l21 & 0x00008000) p1 |= 0x02; \
	if (l21 & 0x00800000) p1 |= 0x04; \
	if (l21 & 0x80000000) p1 |= 0x08; \
	if (l22 & 0x00000080) p1 |= 0x10; \
	if (l22 & 0x00008000) p1 |= 0x20; \
	if (l22 & 0x00800000) p1 |= 0x40; \
	if (l22 & 0x80000000) p1 |= 0x80; \
	if (l21 & 0x00000040) p2 |= 0x01; \
	if (l21 & 0x00004000) p2 |= 0x02; \
	if (l21 & 0x00400000) p2 |= 0x04; \
	if (l21 & 0x40000000) p2 |= 0x08; \
	if (l22 & 0x00000040) p2 |= 0x10; \
	if (l22 & 0x00004000) p2 |= 0x20; \
	if (l22 & 0x00400000) p2 |= 0x40; \
	if (l22 & 0x40000000) p2 |= 0x80; \
	l21 <<= 2; l22 <<= 2; \
	col1 = pal[p1] | (pal[p2] << 16); \
	if (l11 & 0x00000080) p3 |= 0x01; \
	if (l11 & 0x00008000) p3 |= 0x02; \
	if (l11 & 0x00800000) p3 |= 0x04; \
	if (l11 & 0x80000000) p3 |= 0x08; \
	if (l12 & 0x00000080) p3 |= 0x10; \
	if (l12 & 0x00008000) p3 |= 0x20; \
	if (l12 & 0x00800000) p3 |= 0x40; \
	if (l12 & 0x80000000) p3 |= 0x80; \
	if (l11 & 0x00000040) p4 |= 0x01; \
	if (l11 & 0x00004000) p4 |= 0x02; \
	if (l11 & 0x00400000) p4 |= 0x04; \
	if (l11 & 0x40000000) p4 |= 0x08; \
	if (l12 & 0x00000040) p4 |= 0x10; \
	if (l12 & 0x00004000) p4 |= 0x20; \
	if (l12 & 0x00400000) p4 |= 0x40; \
	if (l12 & 0x40000000) p4 |= 0x80; \
	l11 <<= 2; l12 <<= 2; \
	col2 = pal[p3] | (pal[p4] << 16); \
	*dst++ = col1; \
	*dst++ = col2; 
	
	for (i = 4; i >= 0; i -= 4)
	{
		u32 line11 = vram[i+0 ] | (vram[i+8 ] << 16);
		u32 line12 = vram[i+16] | (vram[i+24] << 16);
		u32 line21 = vram[i+1 ] | (vram[i+9 ] << 16);
		u32 line22 = vram[i+17] | (vram[i+25] << 16);
		u32 line31 = vram[i+2 ] | (vram[i+10] << 16);
		u32 line32 = vram[i+18] | (vram[i+26] << 16);
		u32 line41 = vram[i+3 ] | (vram[i+11] << 16);
		u32 line42 = vram[i+19] | (vram[i+27] << 16);
		
		DO_MINIBLOCK(line31, line32, line41, line42);
		DO_MINIBLOCK(line31, line32, line41, line42);
		DO_MINIBLOCK(line11, line12, line21, line22);
		DO_MINIBLOCK(line11, line12, line21, line22);
		DO_MINIBLOCK(line31, line32, line41, line42);
		DO_MINIBLOCK(line31, line32, line41, line42);
		DO_MINIBLOCK(line11, line12, line21, line22);
		DO_MINIBLOCK(line11, line12, line21, line22);
	}
	
	pal[0] = oldcolor0;
	
#undef DO_MINIBLOCK
}



u32 PPU_StoreTileInCache(u32 type, u32 palid, u32 addr)
{
	u32 key;
	u32* dst;
	u32 paldirty = 0;
	u32 vramdirty = 0;
	
	switch (type)
	{
		case TILE_2BPP: 
			paldirty = PPU.PaletteUpdateCount[palid];
			vramdirty = PPU.VRAMUpdateCount[addr >> 4];
			break;
			
		case TILE_4BPP: 
			paldirty = *(u32*)&PPU.PaletteUpdateCount[palid << 2];
			vramdirty = *(u16*)&PPU.VRAMUpdateCount[addr >> 4];
			break;
			
		case TILE_8BPP: 
			paldirty = PPU.PaletteUpdateCount256;
			vramdirty = *(u32*)&PPU.VRAMUpdateCount[addr >> 4];
			break;
		
		default:
			bprintf("unknown tile type %d\n", type);
			return 0xFFFF;
	}
	
	key = (addr >> 4) | (palid << 12);
	
	u16 coord = PPU_TileCacheList[key];
	u32 tileidx;
	
	if (coord != 0x8000) // tile already exists
	{
		// if the VRAM hasn't been modified in the meantime, just return the old tile
		if (vramdirty == PPU_TileVRAMUpdate[key] && paldirty == PPU_TilePalUpdate[key])
			return coord;
		
		tileidx = (coord & 0x7F) | ((0x7F00 - (coord & 0x7F00)) >> 1);
	}
	else
	{
		tileidx = PPU_TileCacheIndex;
		PPU_TileCacheIndex++;
		PPU_TileCacheIndex &= ~0x3FC000; // prevent overflow
		
		coord = (tileidx & 0x7F) | (0x7F00 - ((tileidx & 0x3F80) << 1));
		PPU_TileCacheList[key] = coord;
	}
	
	dst = (u32*)&PPU_TileCache[tileidx * 64];
	
	switch (type)
	{
		case TILE_2BPP: PPU_DecodeTile_2bpp(&PPU.VRAM[addr], &TempPalette[palid << 2], dst); break;
		case TILE_4BPP: PPU_DecodeTile_4bpp(&PPU.VRAM[addr], &TempPalette[palid << 4], dst); break;
		
		case TILE_8BPP: 
			// TODO: direct color!
			PPU_DecodeTile_8bpp(&PPU.VRAM[addr], &TempPalette[0], dst); 
			break;
	}
	
	PPU_TileVRAMUpdate[key] = vramdirty;
	PPU_TilePalUpdate[key] = paldirty;
	
	// invalidate previous tile if need be
	u32 oldkey = PPU_TileCacheReverseList[tileidx];
	PPU_TileCacheReverseList[tileidx] = key;
	if (oldkey != key && oldkey != 0x80000000)
		PPU_TileCacheList[oldkey] = 0x8000;
	
	return coord;
}


void PPU_ApplyPaletteChanges(u32 num, PPU_PaletteChange* changes)
{
	u32 i;
	
	for (i = 0; i < num; i++)
	{
		TempPalette[changes[i].Address] = changes[i].Color;
	}
}


void PPU_StartBG()
{
	if (doingBG) return;
	doingBG = 1;
	
	//bglEnableStencilTest(true);
	//bglStencilFunc(GPU_ALWAYS, 0x00, 0xFF, 0x02);
	bglEnableStencilTest(false);
	bglStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(true);
	bglAlphaFunc(GPU_GREATER, 0);
	
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglScissorMode(GPU_SCISSOR_NORMAL);
	
	bglColorDepthMask(GPU_WRITE_COLOR);
	
	SET_UNIFORM(0x24, 1.0f/128.0f, 1.0f/128.0f, 1.0f, 1.0f);
	
	bglEnableTextures(GPU_TEXUNIT0);
	
	bglDummyTexEnv(1);
	bglDummyTexEnv(2);
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
		
	bglTexImage(GPU_TEXUNIT0, PPU_TileCache,1024,1024,0,GPU_RGBA5551);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 2);	// vertex
	bglAttribType(1, GPU_UNSIGNED_BYTE, 2);	// texcoord
}



void PPU_ClearMainScreen()
{
	u8* vptr = (u8*)vertexPtr;
	
	u16 col = TempPalette[0];
	u8 r = (col & 0xF800) >> 8; r |= (r >> 5);
	u8 g = (col & 0x07C0) >> 3; g |= (g >> 5);
	u8 b = (col & 0x003E) << 2; b |= (b >> 5);
	
#define ADDVERTEX(x, y, z, r, g, b, a) \
	*(u16*)vptr = x; vptr += 2; \
	*(u16*)vptr = y; vptr += 2; \
	*(u16*)vptr = z; vptr += 2; \
	*vptr++ = r; \
	*vptr++ = g; \
	*vptr++ = b; \
	*vptr++ = a;
	
	bglUseShader(plainQuadShader);
	
	bglOutputBuffers(MainScreenTex, OBJDepthBuffer);
	
	bglEnableStencilTest(true);
	bglStencilFunc(GPU_ALWAYS, 0x00, 0xFF, 0xFF);
	bglStencilOp(GPU_AND_NOT, GPU_AND_NOT, GPU_AND_NOT);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(false);
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglColorDepthMask(GPU_WRITE_ALL);
	
	bglUniformMatrix(0x20, snesProjMatrix);
	
	bglEnableTextures(0);
	
	bglTexEnv(0, 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0), 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	bglDummyTexEnv(1);
	bglDummyTexEnv(2);
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 3);	// vertex
	bglAttribType(1, GPU_UNSIGNED_BYTE, 4);	// color
	bglAttribBuffer(vptr);
	
	int nvtx = 0;
	int ystart = 0;
	PPU_ModeSection* s = &PPU.ModeSections[0];
	for (;;)
	{
		u8 alpha = (s->ColorMath2 & 0x20) ? 0xFF:0x80;
		ADDVERTEX(0, ystart, 0x80,      	r, g, b, alpha);
		ADDVERTEX(256, ystart, 0x80,    	r, g, b, alpha);
		ADDVERTEX(256, s->EndOffset, 0x80,  r, g, b, alpha);
		ADDVERTEX(0, ystart, 0x80,      	r, g, b, alpha);
		ADDVERTEX(256, s->EndOffset, 0x80,  r, g, b, alpha);
		ADDVERTEX(0, s->EndOffset, 0x80,    r, g, b, alpha);
		nvtx += 6;
		
		if (s->EndOffset >= 240) break;
		ystart = s->EndOffset;
		s++;
	}
	
	bglDrawArrays(GPU_TRIANGLES, nvtx);
	
	// clear the OBJ buffer

	bglOutputBuffers(OBJColorBuffer, OBJDepthBuffer);
	
	bglStencilOp(GPU_XOR, GPU_XOR, GPU_XOR);
	bglColorDepthMask(GPU_WRITE_COLOR);
	
	bglAttribBuffer(vptr);
		
	// Z here doesn't matter
	ADDVERTEX(0, 0, 0,      255, 0, 255, 0);
	ADDVERTEX(256, 0, 0,    255, 0, 255, 0);
	ADDVERTEX(256, 256, 0,  255, 0, 255, 0);
	ADDVERTEX(0, 0, 0,      255, 0, 255, 0);
	ADDVERTEX(256, 256, 0,  255, 0, 255, 0);
	ADDVERTEX(0, 256, 0,    255, 0, 255, 0);
	vptr = (u8*)((((u32)vptr) + 0xF) & ~0xF);
	
	bglDrawArrays(GPU_TRIANGLES, 2*3);
	
	vertexPtr = vptr;
	
#undef ADDVERTEX
}

void PPU_ClearSubScreen()
{
	u8* vptr = (u8*)vertexPtr;
	
#define ADDVERTEX(x, y, z, r, g, b, a) \
	*(u16*)vptr = x; vptr += 2; \
	*(u16*)vptr = y; vptr += 2; \
	*(u16*)vptr = z; vptr += 2; \
	*vptr++ = r; \
	*vptr++ = g; \
	*vptr++ = b; \
	*vptr++ = a;
	
	bglUseShader(plainQuadShader);
	
	bglOutputBuffers(SubScreenTex, OBJDepthBuffer);
	
	bglEnableStencilTest(true);
	bglStencilFunc(GPU_ALWAYS, 0x00, 0xFF, 0xFF);
	bglStencilOp(GPU_AND_NOT, GPU_AND_NOT, GPU_AND_NOT);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(false);
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglColorDepthMask(GPU_WRITE_COLOR);
	
	bglUniformMatrix(0x20, snesProjMatrix);
	
	bglEnableTextures(0);
	
	bglTexEnv(0, 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0), 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	bglDummyTexEnv(1);
	bglDummyTexEnv(2);
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 3);	// vertex
	bglAttribType(1, GPU_UNSIGNED_BYTE, 4);	// color
	bglAttribBuffer(vptr);
		
	int nvtx = 0;
	int ystart = 0;
	PPU_SubBackdropSection* s = &PPU.SubBackdropSections[0];
	for (;;)
	{
		u16 col = s->Color;
		u8 r = (col & 0xF800) >> 8; r |= (r >> 5);
		u8 g = (col & 0x07C0) >> 3; g |= (g >> 5);
		u8 b = (col & 0x003E) << 2; b |= (b >> 5);
		
		// 'If "Sub Screen BG/OBJ Enable" is off (2130h.Bit1=0), then the "Div2" isn't forcefully ignored'
		// -> this causes a glitch in Super Puyo Puyo-- it has subscreen disabled but color math enabled on BG1 AND div2 enabled
		u8 alpha = 0xFF;//(s->Div2) ? 0x80:0xFF;
		ADDVERTEX(0, ystart, 0x80,      	r, g, b, alpha);
		ADDVERTEX(256, ystart, 0x80,    	r, g, b, alpha);
		ADDVERTEX(256, s->EndOffset, 0x80,  r, g, b, alpha);
		ADDVERTEX(0, ystart, 0x80,      	r, g, b, alpha);
		ADDVERTEX(256, s->EndOffset, 0x80,  r, g, b, alpha);
		ADDVERTEX(0, s->EndOffset, 0x80,    r, g, b, alpha);
		nvtx += 6;
		
		if (s->EndOffset >= 240) break;
		ystart = s->EndOffset;
		s++;
	}
	
	vptr = (u8*)((((u32)vptr) + 0xF) & ~0xF);
	
	bglDrawArrays(GPU_TRIANGLES, nvtx);
	
	// fully clear the stencil buffer

	bglOutputBuffers(SubScreenTex, OBJDepthBuffer);
	
	bglStencilOp(GPU_XOR, GPU_XOR, GPU_XOR);
	bglColorDepthMask(0);
	
	bglAttribBuffer(vptr);
		
	// Z here doesn't matter
	ADDVERTEX(0, 0, 0,      255, 0, 255, 0);
	ADDVERTEX(256, 0, 0,    255, 0, 255, 0);
	ADDVERTEX(256, 256, 0,  255, 0, 255, 0);
	ADDVERTEX(0, 0, 0,      255, 0, 255, 0);
	ADDVERTEX(256, 256, 0,  255, 0, 255, 0);
	ADDVERTEX(0, 256, 0,    255, 0, 255, 0);
	vptr = (u8*)((((u32)vptr) + 0xF) & ~0xF);
	
	bglDrawArrays(GPU_TRIANGLES, 2*3);
	
	vertexPtr = vptr;
	
#undef ADDVERTEX
}


void PPU_ClearAlpha(u32 snum)
{
	u8* vptr = (u8*)vertexPtr;
	
#define ADDVERTEX(x, y, z, r, g, b, a) \
	*(u16*)vptr = x; vptr += 2; \
	*(u16*)vptr = y; vptr += 2; \
	*(u16*)vptr = z; vptr += 2; \
	*vptr++ = r; \
	*vptr++ = g; \
	*vptr++ = b; \
	*vptr++ = a;
	
	bglUseShader(plainQuadShader);
	
	bglEnableStencilTest(false);
	bglStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(false);
	
	// transform alpha=128 into 0 and alpha=255 into 255
	// color blending mode doesn't matter since we only write alpha anyway
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_REVERSE_SUBTRACT);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE_MINUS_DST_ALPHA, GPU_ONE);
	
	bglScissorMode(GPU_SCISSOR_DISABLE);
	
	bglColorDepthMask(GPU_WRITE_ALPHA);
	
	bglUniformMatrix(0x20, snesProjMatrix);
	
	bglEnableTextures(0);
	
	bglTexEnv(0, 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0), 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	bglDummyTexEnv(1);
	bglDummyTexEnv(2);
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 3);	// vertex
	bglAttribType(1, GPU_UNSIGNED_BYTE, 4);	// color
	bglAttribBuffer(vptr);
	
	ADDVERTEX(0, 0, 0,      255, 0, 255, 255);
	ADDVERTEX(256, 0, 0,    255, 0, 255, 255);
	ADDVERTEX(256, 256, 0,  255, 0, 255, 255);
	ADDVERTEX(0, 0, 0,      255, 0, 255, 255);
	ADDVERTEX(256, 256, 0,  255, 0, 255, 255);
	ADDVERTEX(0, 256, 0,    255, 0, 255, 255);
	vptr = (u8*)((((u32)vptr) + 0xF) & ~0xF);
	
	bglDrawArrays(GPU_TRIANGLES, 2*3);

	vertexPtr = vptr;
	
#undef ADDVERTEX
}


void PPU_HardRenderBG_8x8(u32 setalpha, PPU_Background* bg, int type, u32 prio, int ystart, int yend)
{
	u16* tilemap;
	int tileaddrshift = ((int[]){4, 5, 6})[type];
	u32 xoff, yoff;
	u16 curtile;
	int x, y;
	u32 idx;
	int systart = 0, syend;
	int ntiles = 0;
	u16* vptr = (u16*)vertexPtr;
	
#define ADDVERTEX(x, y, coord) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = coord;
	
	PPU_BGSection* s = &bg->Sections[0];
	for (;;)
	{
		syend = s->EndOffset;
		
		if (syend <= ystart)
		{
			systart = syend;
			s++;
			continue;
		}

		if (systart < ystart) systart = ystart;
		if (syend > yend) syend = yend;
		
		yoff = s->YScroll + systart;
		ntiles = 0;
		
		for (y = systart - (yoff&7); y < syend; y += 8, yoff += 8)
		{
			tilemap = PPU.VRAM + s->TilemapOffset + ((yoff & 0xF8) << 3);
			if (yoff & 0x100)
			{
				if (bg->Size & 0x2)
					tilemap += (bg->Size & 0x1) ? 2048 : 1024;
			}
			
			xoff = s->XScroll;
			x = -(xoff & 7);
		
			for (; x < 256; x += 8, xoff += 8)
			{
				idx = (xoff & 0xF8) >> 3;
				if (xoff & 0x100)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}

				curtile = tilemap[idx];
				if ((curtile ^ prio) & 0x2000)
					continue;

				// render the tile
				
				u32 addr = s->TilesetOffset + ((curtile & 0x03FF) << tileaddrshift);
				u32 palid = (curtile & 0x1C00) >> 10;
				
				u32 coord = PPU_StoreTileInCache(type, palid, addr);
				
				switch (curtile & 0xC000)
				{
					case 0x0000:
						ADDVERTEX(x,   y,     coord);
						ADDVERTEX(x+8, y,     coord+0x0001);
						ADDVERTEX(x+8, y+8,   coord+0x0101);
						ADDVERTEX(x,   y,     coord);
						ADDVERTEX(x+8, y+8,   coord+0x0101);
						ADDVERTEX(x,   y+8,   coord+0x0100);
						break;
						
					case 0x4000: // hflip
						ADDVERTEX(x,   y,     coord+0x0001);
						ADDVERTEX(x+8, y,     coord);
						ADDVERTEX(x+8, y+8,   coord+0x0100);
						ADDVERTEX(x,   y,     coord+0x0001);
						ADDVERTEX(x+8, y+8,   coord+0x0100);
						ADDVERTEX(x,   y+8,   coord+0x0101);
						break;
						
					case 0x8000: // vflip
						ADDVERTEX(x,   y,     coord+0x0100);
						ADDVERTEX(x+8, y,     coord+0x0101);
						ADDVERTEX(x+8, y+8,   coord+0x0001);
						ADDVERTEX(x,   y,     coord+0x0100);
						ADDVERTEX(x+8, y+8,   coord+0x0001);
						ADDVERTEX(x,   y+8,   coord);
						break;
						
					case 0xC000: // hflip+vflip
						ADDVERTEX(x,   y,     coord+0x0101);
						ADDVERTEX(x+8, y,     coord+0x0100);
						ADDVERTEX(x+8, y+8,   coord);
						ADDVERTEX(x,   y,     coord+0x0101);
						ADDVERTEX(x+8, y+8,   coord);
						ADDVERTEX(x,   y+8,   coord+0x0001);
						break;
				}
				
				ntiles++;
			}
		}
		
		if (ntiles)
		{
			PPU_StartBG();
			
			bglScissor(0, systart, 256, syend);
			
			// set alpha to 128 if we need to disable color math in this BG section
			bglTexEnv(0, 
				GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
				GPU_TEVSOURCES(GPU_TEXTURE0, GPU_CONSTANT, 0),
				GPU_TEVOPERANDS(0,0,0), 
				GPU_TEVOPERANDS(0,0,0), 
				GPU_REPLACE, setalpha ? GPU_REPLACE:GPU_MODULATE, 
				setalpha ? 0xFFFFFFFF:0x80FFFFFF);
			
			bglAttribBuffer(vertexPtr);
			
			vptr = (u16*)((((u32)vptr) + 0xF) & ~0xF);
			vertexPtr = vptr;
			
			bglDrawArrays(GPU_TRIANGLES, ntiles*2*3);
		}
		
		if (syend >= yend) break;
		systart = syend;
		s++;
	}
	
#undef ADDVERTEX
}

void PPU_HardRenderBG_16x16(u32 setalpha, PPU_Background* bg, int type, u32 prio, int ystart, int yend)
{
	u16* tilemap;
	int tileaddrshift = ((int[]){4, 5, 6})[type];
	u32 xoff, yoff;
	u16 curtile;
	int x, y;
	u32 idx;
	int systart = 0, syend;
	int ntiles = 0;
	u16* vptr = (u16*)vertexPtr;
	
#define ADDVERTEX(x, y, coord) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = coord;
	
	PPU_BGSection* s = &bg->Sections[0];
	for (;;)
	{
		syend = s->EndOffset;
		
		if (syend <= ystart)
		{
			systart = syend;
			s++;
			continue;
		}

		if (systart < ystart) systart = ystart;
		if (syend > yend) syend = yend;
		
		yoff = s->YScroll + systart;
		ntiles = 0;
		
		for (y = systart - (yoff&15); y < syend; y += 16, yoff += 16)
		{
			tilemap = PPU.VRAM + s->TilemapOffset + ((yoff & 0x1F0) << 2);
			if (yoff & 0x200)
			{
				if (bg->Size & 0x2)
					tilemap += (bg->Size & 0x1) ? 2048 : 1024;
			}
			
			xoff = s->XScroll;
			x = -(xoff & 15);
		
			for (; x < 256; x += 16, xoff += 16)
			{
				idx = (xoff & 0x1F0) >> 4;
				if (xoff & 0x200)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}

				curtile = tilemap[idx];
				if ((curtile ^ prio) & 0x2000)
					continue;

				// render the tile
				
				u32 addr = s->TilesetOffset + ((curtile & 0x03FF) << tileaddrshift);
				u32 palid = (curtile & 0x1C00) >> 10;
				
				u32 coord0 = PPU_StoreTileInCache(type, palid, addr+(0x00<<tileaddrshift));
				u32 coord1 = PPU_StoreTileInCache(type, palid, addr+(0x01<<tileaddrshift));
				u32 coord2 = PPU_StoreTileInCache(type, palid, addr+(0x10<<tileaddrshift));
				u32 coord3 = PPU_StoreTileInCache(type, palid, addr+(0x11<<tileaddrshift));
				
#define DO_SUBTILE(sx, sy, coord, t0, t1, t2, t3) \
					{ \
						ADDVERTEX(x+sx,   y+sy,     coord+t0); \
						ADDVERTEX(x+sx+8, y+sy,     coord+t1); \
						ADDVERTEX(x+sx+8, y+sy+8,   coord+t3); \
						ADDVERTEX(x+sx,   y+sy,     coord+t0); \
						ADDVERTEX(x+sx+8, y+sy+8,   coord+t3); \
						ADDVERTEX(x+sx,   y+sy+8,   coord+t2); \
						ntiles++; \
					}
				
				switch (curtile & 0xC000)
				{
					case 0x0000:
						if ((y - systart) > -8)
						{
							if (x > -8)  DO_SUBTILE(0, 0,  coord0, 0x0000, 0x0001, 0x0100, 0x0101);
							if (x < 248) DO_SUBTILE(8, 0,  coord1, 0x0000, 0x0001, 0x0100, 0x0101);
						}
						if (y < (syend - 8))
						{
							if (x > -8)  DO_SUBTILE(0, 8,  coord2, 0x0000, 0x0001, 0x0100, 0x0101);
							if (x < 248) DO_SUBTILE(8, 8,  coord3, 0x0000, 0x0001, 0x0100, 0x0101);
						}
						break;
						
					case 0x4000: // hflip
						if ((y - systart) > -8)
						{
							if (x > -8)  DO_SUBTILE(0, 0,  coord1, 0x0001, 0x0000, 0x0101, 0x0100);
							if (x < 248) DO_SUBTILE(8, 0,  coord0, 0x0001, 0x0000, 0x0101, 0x0100);
						}
						if (y < (syend - 8))
						{
							if (x > -8)  DO_SUBTILE(0, 8,  coord3, 0x0001, 0x0000, 0x0101, 0x0100);
							if (x < 248) DO_SUBTILE(8, 8,  coord2, 0x0001, 0x0000, 0x0101, 0x0100);
						}
						break;
						
					case 0x8000: // vflip
						if ((y - systart) > -8)
						{
							if (x > -8)  DO_SUBTILE(0, 0,  coord2, 0x0100, 0x0101, 0x0000, 0x0001);
							if (x < 248) DO_SUBTILE(8, 0,  coord3, 0x0100, 0x0101, 0x0000, 0x0001);
						}
						if (y < (syend - 8))
						{
							if (x > -8)  DO_SUBTILE(0, 8,  coord0, 0x0100, 0x0101, 0x0000, 0x0001);
							if (x < 248) DO_SUBTILE(8, 8,  coord1, 0x0100, 0x0101, 0x0000, 0x0001);
						}
						break;
						
					case 0xC000: // hflip+vflip
						if ((y - systart) > -8)
						{
							if (x > -8)  DO_SUBTILE(0, 0,  coord3, 0x0101, 0x0100, 0x0001, 0x0000);
							if (x < 248) DO_SUBTILE(8, 0,  coord2, 0x0101, 0x0100, 0x0001, 0x0000);
						}
						if (y < (syend - 8))
						{
							if (x > -8)  DO_SUBTILE(0, 8,  coord1, 0x0101, 0x0100, 0x0001, 0x0000);
							if (x < 248) DO_SUBTILE(8, 8,  coord0, 0x0101, 0x0100, 0x0001, 0x0000);
						}
						break;
				}
				
#undef DO_SUBTILE
			}
		}
		
		if (ntiles)
		{
			PPU_StartBG();
			
			bglScissor(0, systart, 256, syend);
			
			// set alpha to 128 if we need to disable color math in this BG section
			bglTexEnv(0, 
				GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
				GPU_TEVSOURCES(GPU_TEXTURE0, GPU_CONSTANT, 0),
				GPU_TEVOPERANDS(0,0,0), 
				GPU_TEVOPERANDS(0,0,0), 
				GPU_REPLACE, setalpha ? GPU_REPLACE:GPU_MODULATE, 
				setalpha ? 0xFFFFFFFF:0x80FFFFFF);
			
			bglAttribBuffer(vertexPtr);
			
			vptr = (u16*)((((u32)vptr) + 0xF) & ~0xF);
			vertexPtr = vptr;
			
			bglDrawArrays(GPU_TRIANGLES, ntiles*2*3);
		}
		
		if (syend >= yend) break;
		systart = syend;
		s++;
	}
	
#undef ADDVERTEX
}


#if 0
void PPU_HardRenderBG_Mode7(u32 setalpha, int ystart, int yend)
{
	s32 x = (PPU.M7A * (PPU.M7XScroll-PPU.M7RefX)) + (PPU.M7B * (line+PPU.M7YScroll-PPU.M7RefY)) + (PPU.M7RefX << 8);
	s32 y = (PPU.M7C * (PPU.M7XScroll-PPU.M7RefX)) + (PPU.M7D * (line+PPU.M7YScroll-PPU.M7RefY)) + (PPU.M7RefY << 8);
	int i = 0;
	u32 tileidx;
	u8 colorval;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		u16 hidden = window ? (PPU.BG[0].WindowCombine & (1 << (s->WindowMask ^ PPU.BG[0].WindowMask))) : 0;
		
		if (hidden)
		{
			int remaining = s->EndOffset - i;
			i += remaining;
			x += remaining*PPU.M7A;
			y += remaining*PPU.M7C;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			int end = s->EndOffset;
			
			while (i < end)
			{
				// TODO screen h/v flip
				
				if ((x|y) & 0xFFFC0000)
				{
					// wraparound
					if ((PPU.M7Sel & 0xC0) == 0x80)
					{
						// skip pixel (transparent)
						i++;
						x += PPU.M7A;
						y += PPU.M7C;
						continue;
					}
					else if ((PPU.M7Sel & 0xC0) == 0xC0)
					{
						// use tile 0
						tileidx = 0;
					}
					else
					{
						// ignore wraparound
						tileidx = ((x & 0x3F800) >> 10) + ((y & 0x3F800) >> 3);
						tileidx = PPU.VRAM[tileidx] << 7;
					}
				}
				else
				{
					tileidx = ((x & 0x3F800) >> 10) + ((y & 0x3F800) >> 3);
					tileidx = PPU.VRAM[tileidx] << 7;
				}
				
				tileidx += ((x & 0x700) >> 7) + ((y & 0x700) >> 4) + 1;
				colorval = PPU.VRAM[tileidx];
				
				if (colorval)
					buffer[i] = pal[colorval] | finalalpha;
				
				i++;
				x += PPU.M7A;
				y += PPU.M7C;
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}
#endif


int PPU_HardRenderOBJ(u8* oam, u32 oamextra, int y, int height, int ystart, int yend)
{
	s32 xoff;
	u16 attrib;
	u32 idx;
	s32 x;
	s32 width = (s32)PPU.OBJWidth[(oamextra & 0x2) >> 1];
	u32 palid, prio;
	int ntiles = 0;
	u16* vptr = (u16*)vertexPtr;
	
#define ADDVERTEX(x, y, z, coord) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = z; \
	*vptr++ = coord;
	
	xoff = oam[0];
	if (oamextra & 0x1) // xpos bit8, sign bit
	{
		xoff = 0x100 - xoff;
		if (xoff >= width) return 0;
		x = -xoff;
	}
	else
		x = xoff;
		
	attrib = *(u16*)&oam[2];
	
	idx = (attrib & 0x01FF) << 5;
	
	if (attrib & 0x4000)
		idx += ((width-1) & 0x38) << 2;
	if (attrib & 0x8000)
		idx += ((height-1) & 0x38) << 6;
		
	width += x;
	if (width > 256) width = 256;
	height += y;
	if (height > yend) height = yend;
	ystart -= 8;
	
	palid = 8 + ((oam[3] & 0x0E) >> 1);
	
	prio = (oam[3] & 0x30);
	if (palid < 12) prio += 0x40;
	
	for (; y < height; y += 8)
	{
		if (y <= ystart)
		{
			idx += (attrib & 0x8000) ? -512:512;
			continue;
		}
		
		u32 firstidx = idx;
		s32 firstx = x;
		
		for (; x < width; x += 8)
		{
			// skip offscreen tiles
			if (x <= -8)
			{
				idx += (attrib & 0x4000) ? -32:32;
				continue;
			}
			
			u32 addr = PPU.OBJTilesetAddr + idx;
			u32 coord = PPU_StoreTileInCache(TILE_4BPP, palid, addr);
			
			//if (x <= -8 || x > 255 || y <= -8 || y > 223)
			//	bprintf("OBJ tile %d/%d %04X\n", x, y, coord);
			
			switch (attrib & 0xC000)
			{
				case 0x0000:
					ADDVERTEX(x,   y,     prio, coord);
					ADDVERTEX(x+8, y,     prio, coord+0x0001);
					ADDVERTEX(x+8, y+8,   prio, coord+0x0101);
					ADDVERTEX(x,   y,     prio, coord);
					ADDVERTEX(x+8, y+8,   prio, coord+0x0101);
					ADDVERTEX(x,   y+8,   prio, coord+0x0100);
					break;
					
				case 0x4000: // hflip
					ADDVERTEX(x,   y,     prio, coord+0x0001);
					ADDVERTEX(x+8, y,     prio, coord);
					ADDVERTEX(x+8, y+8,   prio, coord+0x0100);
					ADDVERTEX(x,   y,     prio, coord+0x0001);
					ADDVERTEX(x+8, y+8,   prio, coord+0x0100);
					ADDVERTEX(x,   y+8,   prio, coord+0x0101);
					break;
					
				case 0x8000: // vflip
					ADDVERTEX(x,   y,     prio, coord+0x0100);
					ADDVERTEX(x+8, y,     prio, coord+0x0101);
					ADDVERTEX(x+8, y+8,   prio, coord+0x0001);
					ADDVERTEX(x,   y,     prio, coord+0x0100);
					ADDVERTEX(x+8, y+8,   prio, coord+0x0001);
					ADDVERTEX(x,   y+8,   prio, coord);
					break;
					
				case 0xC000: // hflip+vflip
					ADDVERTEX(x,   y,     prio, coord+0x0101);
					ADDVERTEX(x+8, y,     prio, coord+0x0100);
					ADDVERTEX(x+8, y+8,   prio, coord);
					ADDVERTEX(x,   y,     prio, coord+0x0101);
					ADDVERTEX(x+8, y+8,   prio, coord);
					ADDVERTEX(x,   y+8,   prio, coord+0x0001);
					break;
			}
			
			ntiles++;
			
			idx += (attrib & 0x4000) ? -32:32;
		}
		
		idx = firstidx + ((attrib & 0x8000) ? -512:512);
		x = firstx;
	}
	
#undef ADDVERTEX
	
	vertexPtr = vptr;
	return ntiles;
}

void PPU_HardRenderOBJs()
{
	int i = PPU.FirstOBJ;
	i--;
	if (i < 0) i = 127;
	int last = i;
	int ntiles = 0;
	int ystart = 0, yend = 224;
	void* vstart = vertexPtr;

	do
	{
		u8* oam = &PPU.OAM[i << 2];
		u8 oamextra = PPU.OAM[0x200 + (i >> 2)] >> ((i & 0x03) << 1);
		s32 oy = (s32)oam[1] + 1;
		s32 oh = (s32)PPU.OBJHeight[(oamextra & 0x2) >> 1];
		
		if ((oy+oh) > ystart && oy < yend)
		{
			ntiles += PPU_HardRenderOBJ(oam, oamextra, oy, oh, ystart, yend);
		}
		else if (oy >= 192)
		{
			oy -= 0x100;
			if ((oy+oh) > 1 && (oy+oh) > ystart)
			{
				ntiles += PPU_HardRenderOBJ(oam, oamextra, oy, oh, ystart, yend);
			}
		}

		i--;
		if (i < 0) i = 127;
	}
	while (i != last);
	if (!ntiles) return;
	
	
	bglOutputBuffers(OBJColorBuffer, OBJDepthBuffer);
	
	bglScissorMode(GPU_SCISSOR_NORMAL);
		
	bglEnableStencilTest(false);
	bglStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(true);
	bglAlphaFunc(GPU_GREATER, 0);
	
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglColorDepthMask(GPU_WRITE_ALL);
	
	bglUniformMatrix(0x20, snesProjMatrix);
	SET_UNIFORM(0x24, 1.0f/128.0f, 1.0f/128.0f, 1.0f, 1.0f);
	SET_UNIFORM(0x25, 1.0f, 0.0f, 0.0f, 0.0f);
	
	bglEnableTextures(GPU_TEXUNIT0);
	
	bglTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	bglDummyTexEnv(1);
	bglDummyTexEnv(2);
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
		
	bglTexImage(GPU_TEXUNIT0, PPU_TileCache,1024,1024,0,GPU_RGBA5551);
	
	vertexPtr = (u16*)((((u32)vertexPtr) + 0xF) & ~0xF);
	
	bglScissor(0, ystart, 256, yend);

	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 3);	// vertex
	bglAttribType(1, GPU_UNSIGNED_BYTE, 2);	// texcoord
	bglAttribBuffer(vstart);
	
	bglDrawArrays(GPU_TRIANGLES, ntiles*2*3);
}

void PPU_HardRenderOBJLayer(u32 setalpha, u32 prio, int ystart, int yend)
{
	u16* vptr = (u16*)vertexPtr;
	
#define ADDVERTEX(x, y, z, s, t) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = z; \
	*vptr++ = s; \
	*vptr++ = t;
	
	doingBG = 0;
	
	/*bglEnableStencilTest(true);
	bglStencilFunc(GPU_ALWAYS, 0x00, 0xFF, 0x02);*/
	bglEnableStencilTest(false);
	bglStencilOp(GPU_KEEP, GPU_KEEP, GPU_AND_NOT);
	
	bglEnableDepthTest(true);
	bglDepthFunc(GPU_EQUAL);
	bglEnableAlphaTest(true);
	bglAlphaFunc(GPU_GREATER, 0);
	
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglScissorMode(GPU_SCISSOR_DISABLE);
	
	bglColorDepthMask(GPU_WRITE_COLOR);
	
	SET_UNIFORM(0x24, 1.0f/256.0f, 1.0f/256.0f, 1.0f, 1.0f);
	SET_UNIFORM(0x25, 1.0f, 0.0f, 0.0f, 0.0f);
	
	bglEnableTextures(GPU_TEXUNIT0);
	
	// set alpha to 128 if we need to disable color math in this section
	bglTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE0, GPU_CONSTANT, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, (setalpha&0x10) ? GPU_REPLACE:GPU_MODULATE, 
		(setalpha&0x10) ? 0xFFFFFFFF:0x80FFFFFF);
	bglDummyTexEnv(1);
	bglDummyTexEnv(2);
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
		
	bglTexImage(GPU_TEXUNIT0, OBJColorBuffer,256,256,0,GPU_RGBA8);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 3);	// vertex
	bglAttribType(1, GPU_SHORT, 2);	// texcoord
	bglAttribBuffer(vptr);
		
	ADDVERTEX(0, ystart,   prio,  0, ystart);
	ADDVERTEX(256, ystart, prio,  256, ystart);
	ADDVERTEX(256, yend,   prio,  256, yend);
	ADDVERTEX(0, ystart,   prio,  0, ystart);
	ADDVERTEX(256, yend,   prio,  256, yend);
	ADDVERTEX(0, yend,     prio,  0, yend);
	vptr = (u16*)((((u32)vptr) + 0xF) & ~0xF);
	
	bglDrawArrays(GPU_TRIANGLES, 2*3);
	
	// sprites with no color math
	
	bglTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE0, GPU_CONSTANT, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, (setalpha&0x80) ? GPU_REPLACE:GPU_MODULATE, 
		(setalpha&0x80) ? 0xFFFFFFFF:0x80FFFFFF);
	bglAttribBuffer(vptr);
	
	prio += 0x40;
	ADDVERTEX(0, ystart,   prio,  0, ystart);
	ADDVERTEX(256, ystart, prio,  256, ystart);
	ADDVERTEX(256, yend,   prio,  256, yend);
	ADDVERTEX(0, ystart,   prio,  0, ystart);
	ADDVERTEX(256, yend,   prio,  256, yend);
	ADDVERTEX(0, yend,     prio,  0, yend);
	vptr = (u16*)((((u32)vptr) + 0xF) & ~0xF);
	vertexPtr = vptr;
	
	bglDrawArrays(GPU_TRIANGLES, 2*3);
	
#undef ADDVERTEX
}




void PPU_RenderScanline_Hard(u32 line)
{
	int i;
	
	if (!line)
	{
		// initialize stuff upon line 0
		
		//memset(PPU.NumPaletteChanges, 0, 240);
		
		PPU.CurModeSection = &PPU.ModeSections[0];
		PPU.CurModeSection->Mode = PPU.Mode;
		PPU.CurModeSection->MainScreen = PPU.MainScreen;
		PPU.CurModeSection->SubScreen = PPU.SubScreen;
		PPU.CurModeSection->ColorMath1 = PPU.ColorMath1;
		PPU.CurModeSection->ColorMath2 = PPU.ColorMath2;
		PPU.ModeDirty = 0;
		
		for (i = 0; i < 4; i++)
		{
			PPU_Background* bg = &PPU.BG[i];
			
			bg->CurSection = &bg->Sections[0];
			bg->CurSection->ScrollParams = bg->ScrollParams;
			bg->CurSection->GraphicsParams = bg->GraphicsParams;
			
			bg->LastScrollParams = bg->ScrollParams;
			bg->LastGraphicsParams = bg->GraphicsParams;
		}
		
		PPU.CurColorEffect = &PPU.ColorEffectSections[0];
		PPU.CurColorEffect->ColorMath = (PPU.ColorMath2 & 0x80);
		PPU.CurColorEffect->Brightness = PPU.CurBrightness;
		PPU.ColorEffectDirty = 0;
		
		PPU.CurSubBackdrop = &PPU.SubBackdropSections[0];
		PPU.CurSubBackdrop->Color = PPU.SubBackdrop;
		PPU.CurSubBackdrop->Div2 = (!(PPU.ColorMath1 & 0x02)) && (PPU.ColorMath2 & 0x40);
		PPU.SubBackdropDirty = 0;
	}
	else
	{
		if (PPU.ModeDirty)
		{
			PPU.CurModeSection->EndOffset = line;
			PPU.CurModeSection++;
			
			PPU.CurModeSection->Mode = PPU.Mode;
			PPU.CurModeSection->MainScreen = PPU.MainScreen;
			PPU.CurModeSection->SubScreen = PPU.SubScreen;
			PPU.CurModeSection->ColorMath1 = PPU.ColorMath1;
			PPU.CurModeSection->ColorMath2 = PPU.ColorMath2;
			PPU.ModeDirty = 0;
		}
		
		for (i = 0; i < 4; i++)
		{
			PPU_Background* bg = &PPU.BG[i];
			
			if (bg->ScrollParams == bg->LastScrollParams && bg->GraphicsParams == bg->LastGraphicsParams)
				continue;
				
			bg->CurSection->EndOffset = line;
			bg->CurSection++;
			
			bg->CurSection->ScrollParams = bg->ScrollParams;
			bg->CurSection->GraphicsParams = bg->GraphicsParams;
			
			bg->LastScrollParams = bg->ScrollParams;
			bg->LastGraphicsParams = bg->GraphicsParams;
		}
		
		if (PPU.ColorEffectDirty)
		{
			PPU.CurColorEffect->EndOffset = line;
			PPU.CurColorEffect++;
			
			PPU.CurColorEffect->ColorMath = (PPU.ColorMath2 & 0x80);
			PPU.CurColorEffect->Brightness = PPU.CurBrightness;
			PPU.ColorEffectDirty = 0;
		}
		
		if (PPU.SubBackdropDirty)
		{
			PPU.CurSubBackdrop->EndOffset = line;
			PPU.CurSubBackdrop++;
			
			PPU.CurSubBackdrop->Color = PPU.SubBackdrop;
			PPU.CurSubBackdrop->Div2 = (!(PPU.ColorMath1 & 0x02)) && (PPU.ColorMath2 & 0x40);
			PPU.SubBackdropDirty = 0;
		}
	}
}



#define RENDERBG(num, type, prio) \
	{ \
		if (mode & (0x10<<num)) \
			PPU_HardRenderBG_16x16(colormath&(1<<num), &PPU.BG[num], type, prio?0x2000:0, ystart, yend); \
		else \
			PPU_HardRenderBG_8x8(colormath&(1<<num), &PPU.BG[num], type, prio?0x2000:0, ystart, yend); \
	}

void PPU_HardRender_Mode0(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x08) RENDERBG(3, TILE_2BPP, 0);
	if (screen & 0x04) RENDERBG(2, TILE_2BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x08) RENDERBG(3, TILE_2BPP, 1);
	if (screen & 0x04) RENDERBG(2, TILE_2BPP, 1);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 0);
	if (screen & 0x01) RENDERBG(0, TILE_2BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 1);
	if (screen & 0x01) RENDERBG(0, TILE_2BPP, 1);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode1(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x04) RENDERBG(2, TILE_2BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x04) 
	{
		if (!(mode & 0x08))
			RENDERBG(2, TILE_2BPP, 1);
	}
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0);
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 1);
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 1);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
	
	if (screen & 0x04) 
	{
		if (mode & 0x08)
			RENDERBG(2, TILE_2BPP, 1);
	}
}

void PPU_HardRender_Mode2(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 1);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 1);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode3(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 1);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 1);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode4(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 1);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 1);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender(u32 snum)
{
	PPU_ModeSection* s = &PPU.ModeSections[0];
	int ystart = 0;
	
	for (;;)
	{
		u32 screen, colormath;
		if (!snum)
		{
			screen = s->MainScreen;
			colormath = s->ColorMath2 & 0x1F;
		}
		else
		{
			if (!(s->ColorMath1 & 0x02))
			{
				if (s->EndOffset >= 240) break;
				ystart = s->EndOffset;
				s++;
				continue;
			}
			
			screen = s->SubScreen;
			colormath = (s->ColorMath2 & 0x40) ? 0x00:0xFF;
		}
		
		/*if (PPU.NumPaletteChanges[ystart])
		{
			bprintf("[%03d] %d changes\n", ystart, PPU.NumPaletteChanges[ystart]);
			PPU_ApplyPaletteChanges(PPU.NumPaletteChanges[ystart], PPU.PaletteChanges[ystart]);
		}*/
		
		switch (s->Mode & 0x07)
		{
			case 0:
				PPU_HardRender_Mode0(ystart, s->EndOffset, screen, s->Mode, colormath);
				break;
				
			case 1:
				PPU_HardRender_Mode1(ystart, s->EndOffset, screen, s->Mode, colormath);
				break;
				
			case 2:
				PPU_HardRender_Mode2(ystart, s->EndOffset, screen, s->Mode, colormath);
				break;
				
			case 3:
				PPU_HardRender_Mode3(ystart, s->EndOffset, screen, s->Mode, colormath);
				break;
				
			case 4:
				PPU_HardRender_Mode4(ystart, s->EndOffset, screen, s->Mode, colormath);
				break;
		}
		
		if (s->EndOffset >= 240) break;
		ystart = s->EndOffset;
		s++;
	}
}


void PPU_VBlank_Hard()
{
	int i;
	
	PPU.CurModeSection->EndOffset = 240;
	
	for (i = 0; i < 4; i++)
	{
		PPU_Background* bg = &PPU.BG[i];
		bg->CurSection->EndOffset = 240;
	}
	
	PPU.CurColorEffect->EndOffset = 240;
	PPU.CurSubBackdrop->EndOffset = 240;
	
	
	vertexPtr = vertexBuf;
	
	bglViewport(0, 0, 256, 256);
	
	memcpy(TempPalette, PPU.Palette, 512);
	PPU_ClearMainScreen();
	
	bglUseShader(hardRenderShader);
	
	// OBJ LAYER
	
	PPU_HardRenderOBJs();
	
	// MAIN SCREEN
	
	doingBG = 0;
	bglOutputBuffers(MainScreenTex, OBJDepthBuffer);
	PPU_HardRender(0);
	PPU_ClearAlpha(0);
	
	// SUB SCREEN
	
	memcpy(TempPalette, PPU.Palette, 512);
	PPU_ClearSubScreen();
	
	bglUseShader(hardRenderShader);
	
	doingBG = 0;
	bglOutputBuffers(SubScreenTex, OBJDepthBuffer);
	PPU_HardRender(1);
	PPU_ClearAlpha(1);
	
	// reuse the color math system used by the soft renderer
	bglScissorMode(GPU_SCISSOR_DISABLE);
	PPU_BlendScreens(GPU_RGBA8);
	
	u32 taken = ((u32)vertexPtr - (u32)vertexBuf);
	GSPGPU_FlushDataCache(NULL, vertexBuf, taken);
	if (taken > 0x80000)
		bprintf("OVERFLOW %05X/40000 (%d%%)\n", taken, (taken*100)/0x80000);
		
	
	GSPGPU_FlushDataCache(NULL, PPU_TileCache, 1024*1024*sizeof(u16));
}

