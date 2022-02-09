/*
    Copyright 2014-2022 Arisotura

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

#include <limits.h>

#include "blargGL.h"
#include "mem.h"
#include "snes.h"
#include "ppu.h"

#include "config.h"



#define SET_UNIFORM(t, n, v0, v1, v2, v3) \
	{ \
		float blarg[4] = {(v0), (v1), (v2), (v3)}; \
		bglUniform(t, n, blarg); \
	}

u8 CurForcedBlank = 0;


extern void* vertexBuf;
extern void* vertexPtr;
void* vertexPtrStart;
void SwapVertexBuf();


extern shaderProgram_s hardRenderShaderP;
extern shaderProgram_s hard7RenderShaderP;
extern shaderProgram_s hardRenderOBJShaderP;
extern shaderProgram_s plainQuadShaderP;
extern shaderProgram_s windowMaskShaderP;

extern float snesM7Matrix[16];
extern float snesM7Offset[4];

extern u32* gpuOut;
extern u32* gpuDOut;
extern u32* SNESFrame;
extern u16* MainScreenTex;
extern u16* SubScreenTex;

u32* DepthBuffer;

u32* OBJColorBuffer;
u32* OBJPrioBuffer;

u16* Mode7ColorBuffer;

u32 YOffset256[256];

u8 FirstScreenSection;
u8 ScreenYStart, ScreenYEnd;

int doingBG = 0;
int scissorY = 0; // gross hack

u32 tile0idx;
u32 coord0;


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
// + mode7 scroll/matrix/etc
// + color math layer sel

// * OBJ tileset address

// + window registers

// + sub backdrop color


#define TILE_2BPP		0
#define TILE_4BPP		1
#define TILE_8BPP		2
#define TILE_Mode7		3
#define TILE_Mode7_1	4
#define TILE_Mode7_2	5


u16* PPU_TileCache;
u16* PPU_TileCacheRAM;
u32 PPU_TileCacheIndex;

#define TC_CHUNK_SHIFT 12   // divide the cache in 4K chunks
#define TC_NUM_CHUNKS (0x200000 >> TC_CHUNK_SHIFT)
#define TC_CHUNK_SIZE (1 << TC_CHUNK_SHIFT)
u8 PPU_TileChunkDirty[TC_NUM_CHUNKS];

u16 PPU_TileCacheList[0x20000];
u32 PPU_TileCacheReverseList[16384];

u32 PPU_TileVRAMUpdate[0x20000];
u32 PPU_TilePalHash[0x20000];

u8 PPU_LastPalUpdate[64];
u16 PPU_LastPalUpdate128;
u16 PPU_LastPalUpdate256;

u32 PPU_PalHash4[32];  // first half of palette
u32 PPU_PalHash16[16]; // entire palette, including OBJ (second half)
u32 PPU_PalHash128;
u32 PPU_PalHash256;


u32 SuperFastHash(const u8* data, u32 len);
u32 SuperFastPalHash(const u8* data, u32 len);

void PPU_HardRenderSection(u32 endline);


void PPU_Init_Hard()
{
	int i;
	
	// main/sub screen buffers, RGBA8
	MainScreenTex = (u16*)VRAM_Alloc(256*512*4);
	SubScreenTex = (u16*)&((u32*)MainScreenTex)[256*256];
	
	DepthBuffer = (u32*)VRAM_Alloc(256*512*4);
	
	OBJColorBuffer = (u32*)VRAM_Alloc(256*256*4);
	OBJPrioBuffer = (u32*)VRAM_Alloc(256*256*4);
	
	Mode7ColorBuffer = (u16*)linearAlloc(256*512*2);
	
	PPU_TileCache = (u16*)VRAM_Alloc(1024*1024*sizeof(u16));
	PPU_TileCacheRAM = (u16*)linearAlloc(1024*1024*sizeof(u16));
	PPU_TileCacheIndex = 0;
	
	for (i = 0; i < TC_NUM_CHUNKS; i++)
		PPU_TileChunkDirty[i] = 0;
	
	for (i = 0; i < 0x10000; i++)
	{
		PPU_TileCacheList[i] = 0x8000;
		PPU_TileVRAMUpdate[i] = 0;
		PPU_TilePalHash[i] = 0;
	}
	
	for (i = 0; i < 16384; i++)
		PPU_TileCacheReverseList[i] = 0x80000000;
		
	for (i = 0; i < 1024*1024; i++)
		PPU_TileCacheRAM[i] = 0xF800;
	
	for (i = 0; i < 64; i++)
		PPU_LastPalUpdate[i] = 0;
	PPU_LastPalUpdate128 = 0;
	PPU_LastPalUpdate256 = 0;
	
	for (i = 0; i < 32; i++) PPU_PalHash4[i] = 0;
	for (i = 0; i < 16; i++) PPU_PalHash16[i] = 0;
	PPU_PalHash128 = 0;
	PPU_PalHash256 = 0;
		
	for (i = 0; i < 256; i++)
	{
		u32 y = (i & 0x1) << 1;
		y    |= (i & 0x2) << 2;
		y    |= (i & 0x4) << 3;
		y    |= (i & ~0x7) << 8;
		
		YOffset256[i] = y;
	}
}

void PPU_DeInit_Hard()
{
	VRAM_Free(PPU_TileCache);
	linearFree(PPU_TileCacheRAM);
	
	VRAM_Free(MainScreenTex);
	VRAM_Free(DepthBuffer);
	VRAM_Free(OBJColorBuffer);
	VRAM_Free(OBJPrioBuffer);
	linearFree(Mode7ColorBuffer);
}


// tile decoding
// note: tiles are directly converted to PICA200 tiles (zcurve)

u32 PPU_DecodeTile_2bpp(u16* vram, u16* pal, u32* dst)
{
	int i;
	u8 p1, p2, p3, p4;
	u32 col1, col2;
	u32 nonzero = 0;
	
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
		
		nonzero |= line1 | line2 | line3 | line4;
		
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
	return nonzero;
}

u32 PPU_DecodeTile_4bpp(u16* vram, u16* pal, u32* dst)
{
	int i;
	u8 p1, p2, p3, p4;
	u32 col1, col2;
	u32 nonzero = 0;
	
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
		
		nonzero |= line1 | line2 | line3 | line4;
		
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
	return nonzero;
}

u32 PPU_DecodeTile_8bpp(u16* vram, u16* pal, u32* dst)
{
	int i;
	u8 p1, p2, p3, p4;
	u32 col1, col2;
	u32 nonzero = 0;
	
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
		
		nonzero |= line11 | line21 | line31 | line41;
		nonzero |= line12 | line22 | line32 | line42;
		
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
	return nonzero;
}

u32 PPU_DecodeTile_8bpp_m7(u16* vram, u16* pal, u32* dst)
{
	int i;
	u8 p1, p2, p3, p4;
	u32 col1, col2;
	u32 nonzero = 0;
	
	u16 oldcolor0 = pal[0];
	pal[0] = 0;
	
#define DO_MINIBLOCK(block) \
	p1 = block & 0xFF; p2 = (block >> 8) & 0xFF; \
	p3 = (block >> 16) & 0xFF; p4 = (block >> 24) & 0xFF; \
	col1 = pal[p1] | (pal[p2] << 16); \
	col2 = pal[p3] | (pal[p4] << 16); \
	*dst++ = col1; \
	*dst++ = col2; 
	
	for (i = 16; i >= 0; i -= 16)
	{
		u32 b1 = vram[i+12] | (vram[i+8 ] << 16);
		u32 b2 = vram[i+13] | (vram[i+9 ] << 16);
		u32 b3 = vram[i+4 ] | (vram[i+0 ] << 16);
		u32 b4 = vram[i+5 ] | (vram[i+1 ] << 16);
		u32 b5 = vram[i+14] | (vram[i+10] << 16);
		u32 b6 = vram[i+15] | (vram[i+11] << 16);
		u32 b7 = vram[i+6 ] | (vram[i+2 ] << 16);
		u32 b8 = vram[i+7 ] | (vram[i+3 ] << 16);
		
		nonzero |= b1 | b2 | b3 | b4;
		nonzero |= b5 | b6 | b7 | b8;
		
		DO_MINIBLOCK(b1);
		DO_MINIBLOCK(b2);
		DO_MINIBLOCK(b3);
		DO_MINIBLOCK(b4);
		DO_MINIBLOCK(b5);
		DO_MINIBLOCK(b6);
		DO_MINIBLOCK(b7);
		DO_MINIBLOCK(b8);
	}
	
	pal[0] = oldcolor0;
	
#undef DO_MINIBLOCK
	return nonzero;
}

u32 PPU_DecodeTile_8bpp_m7e(u16* vram, u16* pal, u32* dst, u32 prio)
{
	int i, j, k;
	u8 p1, p2, p3, p4;
	u32 col1, col2;
	u32 nonzero = 0;
	u32 block[8], tmp, tmp1;
	u16 oldcolor0 = pal[0];
	pal[0] = 0;

	if(!prio)
		prio = 0x80808080;
	else
		prio = 0x00000000;
	
#define DO_MINIBLOCK(block) \
	p1 = block & 0xFF; p2 = (block >> 8) & 0xFF; \
	p3 = (block >> 16) & 0xFF; p4 = (block >> 24) & 0xFF; \
	col1 = pal[p1] | (pal[p2] << 16); \
	col2 = pal[p3] | (pal[p4] << 16); \
	*dst++ = col1; \
	*dst++ = col2; 
	
	for (i = 16; i >= 0; i -= 16)
	{
		block[0] = vram[i+12] | (vram[i+8 ] << 16);
		block[1] = vram[i+13] | (vram[i+9 ] << 16);
		block[2] = vram[i+4 ] | (vram[i+0 ] << 16);
		block[3] = vram[i+5 ] | (vram[i+1 ] << 16);
		block[4] = vram[i+14] | (vram[i+10] << 16);
		block[5] = vram[i+15] | (vram[i+11] << 16);
		block[6] = vram[i+6 ] | (vram[i+2 ] << 16);
		block[7] = vram[i+7 ] | (vram[i+3 ] << 16);

		for(j = 0; j < 8; j++)
		{
			tmp = 0;
			tmp1 = block[j] ^ prio;
			if(tmp1 & 0x80000000)
				tmp |= 0xFF000000;
			if(tmp1 & 0x00800000)
				tmp |= 0x00FF0000;
			if(tmp1 & 0x00008000)
				tmp |= 0x0000FF00;
			if(tmp1 & 0x00000080)
				tmp |= 0x000000FF;
			tmp = block[j] & tmp;
			
			nonzero |= tmp;
			DO_MINIBLOCK(tmp);
		}
	}
	
	pal[0] = oldcolor0;
	
#undef DO_MINIBLOCK
	return nonzero;
}

u32 PPU_StoreTileInCache(u32 type, u32 palid, u32 addr)
{
	u32 key;
	u32 palhash = 0;
	u32 m7upper = 0;
	u32 vramdirty = 0;
	u32 nonzero = 0;
	u32 isnew = 0;
	u32 tempbuf[32];
	
	switch (type)
	{
		case TILE_2BPP: 
			palhash = PPU_PalHash4[palid];
			vramdirty = PPU.VRAMUpdateCount[addr >> 4];
			break;
			
		case TILE_4BPP: 
			palhash = PPU_PalHash16[palid];
			vramdirty = *(u16*)&PPU.VRAMUpdateCount[addr >> 4];
			break;
			
		case TILE_8BPP: 
			palhash = PPU_PalHash256;
			vramdirty = *(u32*)&PPU.VRAMUpdateCount[addr >> 4];
			break;

		case TILE_Mode7:
			palhash = PPU_PalHash256;
			vramdirty = PPU.VRAM7UpdateCount[addr >> 4];
			break;

		case TILE_Mode7_2:
			m7upper = 1;
		case TILE_Mode7_1:
			palhash = PPU_PalHash128;
			vramdirty = PPU.VRAM7UpdateCount[addr >> 4];
			break;

		default:
			bprintf("unknown tile type %d\n", type);
			return 0xFFFF;
	}
	
	key = (addr >> 4) | (palid << 12) | (m7upper << 16);
	
	u16 coord = PPU_TileCacheList[key];
	u32 tileidx;
	
	if (coord != 0x8000) // tile already exists
	{
		// if the VRAM hasn't been modified in the meantime, just return the old tile
		if (vramdirty == PPU_TileVRAMUpdate[key] && palhash == PPU_TilePalHash[key])
			return coord;
		
		if (coord == 0xC000)
		{
			tileidx = PPU_TileCacheIndex;
			coord = (tileidx & 0x7F) | (0x7F00 - ((tileidx & 0x3F80) << 1));
			isnew = 1;
		}
		else
			tileidx = (coord & 0x7F) | ((0x7F00 - (coord & 0x7F00)) >> 1);
	}
	else
	{
		tileidx = PPU_TileCacheIndex;
		
		coord = (tileidx & 0x7F) | (0x7F00 - ((tileidx & 0x3F80) << 1));
		isnew = 1;
	}
	
	switch (type)
	{
		case TILE_2BPP: nonzero = PPU_DecodeTile_2bpp((u16*)&PPU.VRAM[addr], &PPU.HardPalette[palid << 2], tempbuf); break;
		case TILE_4BPP: nonzero = PPU_DecodeTile_4bpp((u16*)&PPU.VRAM[addr], &PPU.HardPalette[palid << 4], tempbuf); break;
		
		case TILE_8BPP: 
			// TODO: direct color!
			nonzero = PPU_DecodeTile_8bpp((u16*)&PPU.VRAM[addr], &PPU.HardPalette[0], tempbuf); 
			break;

		case TILE_Mode7: nonzero = PPU_DecodeTile_8bpp_m7((u16*)&PPU.VRAM7[addr << 2], &PPU.HardPalette[0], tempbuf);	break;
		case TILE_Mode7_1: nonzero = PPU_DecodeTile_8bpp_m7e((u16*)&PPU.VRAM7[addr << 2], &PPU.PaletteEx1[0], tempbuf, 0); break;
		case TILE_Mode7_2: nonzero = PPU_DecodeTile_8bpp_m7e((u16*)&PPU.VRAM7[addr << 2], &PPU.PaletteEx2[0], tempbuf, 1); break;
	}
	
	PPU_TileVRAMUpdate[key] = vramdirty;
	PPU_TilePalHash[key] = palhash;
	
	if (!nonzero) // tile is empty - mark it as such
	{
		coord = 0xC000;
		PPU_TileCacheList[key] = coord;
		
		if (!isnew)
		{
			// free previous tile if need be
			u32 oldkey = PPU_TileCacheReverseList[tileidx];
			PPU_TileCacheReverseList[tileidx] = 0x80000000;
			if (oldkey != 0x80000000)
				PPU_TileCacheList[oldkey] = 0x8000;
		}
	}
	else
	{
		if (isnew)
		{
			PPU_TileCacheIndex++;
			PPU_TileCacheIndex &= ~0x3FC000; // prevent overflow
			
			PPU_TileCacheList[key] = coord;
		}
		
		memcpy(&PPU_TileCacheRAM[tileidx * 64], tempbuf, 64*2);
		PPU_TileChunkDirty[tileidx >> (TC_CHUNK_SHIFT-7)] = 1;
		
		// invalidate previous tile if need be
		u32 oldkey = PPU_TileCacheReverseList[tileidx];
		PPU_TileCacheReverseList[tileidx] = key;
		if (oldkey != key && oldkey != 0x80000000)
			PPU_TileCacheList[oldkey] = 0x8000;
	}
	
	return coord;
}


void PPU_StartBG(u32 hi)
{
	if (doingBG) return;
	doingBG = 1;
	
	//bglEnableStencilTest(true);
	//bglStencilFunc(GPU_ALWAYS, 0x00, 0xFF, 0x02);
	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(true);
	bglAlphaFunc(GPU_GREATER, 0);
	
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglScissorMode(GPU_SCISSOR_NORMAL);
	
	bglColorDepthMask(GPU_WRITE_COLOR);
	
	SET_UNIFORM(GPU_VERTEX_SHADER, 4, 1.0f/128.0f, 1.0f/128.0f, 1.0f, 1.0f);
	
	bglEnableTextures(GPU_TEXUNIT0);
	
	bglDummyTexEnv(1);
	bglDummyTexEnv(2);
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
		
	bglTexImage(GPU_TEXUNIT0, PPU_TileCache,1024,1024,(hi ? 0x6: 0),GPU_RGBA5551);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 2);	// vertex
	bglAttribType(1, GPU_UNSIGNED_BYTE, 2);	// texcoord
}



void PPU_ClearScreens()
{
	u8* vptr = (u8*)vertexPtr;
	int nvtx;
	
#define ADDVERTEX(x, y, z, r, g, b, a) \
	*(u16*)vptr = x; vptr += 2; \
	*(u16*)vptr = y; vptr += 2; \
	*(u16*)vptr = z; vptr += 2; \
	*vptr++ = r; \
	*vptr++ = g; \
	*vptr++ = b; \
	*vptr++ = a;
	

	bglUseShader(&plainQuadShaderP);
	bglOutputBuffers(MainScreenTex, NULL, 256, 512);
	
	bglViewport(0, 0, 256, 512);
	bglScissorMode(GPU_SCISSOR_DISABLE);
	
	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(false);
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglColorDepthMask(GPU_WRITE_COLOR);
	
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
	
	nvtx = 0;
	{
		int ystart = ScreenYStart;
		PPU_MainBackdropSection* s = &PPU.MainBackdropSections[0];
		for (;;)
		{
			u16 col = s->Color;
			u8 r = (col & 0xF800) >> 8; r |= (r >> 5);
			u8 g = (col & 0x07C0) >> 3; g |= (g >> 5);
			u8 b = (col & 0x003E) << 2; b |= (b >> 5);
			u8 alpha = (s->ColorMath2 & 0x20) ? 0xFF:0x80;
			ADDVERTEX(0, ystart+256, 0,      	  r, g, b, alpha);
			ADDVERTEX(256, s->EndOffset+256, 0,  r, g, b, alpha);
			nvtx += 2;
			
			if (s->EndOffset >= ScreenYEnd) break;
			ystart = s->EndOffset;
			s++;
		}
	}
	{
		int ystart = ScreenYStart;
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
			ADDVERTEX(0, ystart, 0,      	 r, g, b, alpha);
			ADDVERTEX(256, s->EndOffset, 0, r, g, b, alpha);
			nvtx += 2;
			
			if (s->EndOffset >= ScreenYEnd) break;
			ystart = s->EndOffset;
			s++;
		}
	}

	vptr = (u8*)((((u32)vptr) + 0x1F) & ~0x1F);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, nvtx);
	
	// clear the OBJ buffer
	if (FirstScreenSection)
	{
		bglOutputBuffers(OBJColorBuffer, OBJPrioBuffer, 256, 256);
		bglViewport(0, 0, 256, 256);
		
		bglColorDepthMask(GPU_WRITE_ALL);
		
		bglAttribBuffer(vptr);
			
		ADDVERTEX(0, 0, 0,      255, 0, 255, 0);
		ADDVERTEX(256, 512, 0,  255, 0, 255, 0);
		vptr = (u8*)((((u32)vptr) + 0x1F) & ~0x1F);
		
		bglDrawArrays(GPU_GEOMETRY_PRIM, 2);
	}
	
	vertexPtr = vptr;
	
#undef ADDVERTEX
}


void PPU_DrawWindowMask()
{
	// a bit of trickery is used here to easily fill the stencil buffer
	//
	// color buffer:  RRGGBBAA  (8-bit RGBA)
	// depth buffer:  SSDDDDDD  (8-bit stencil, 24-bit depth)
	//
	// thus we can use the depth buffer as a color buffer and write red
	
	u8* vptr = (u8*)vertexPtr;
	
#define ADDVERTEX(x, y, a) \
	*(u16*)vptr = x; vptr += 2; \
	*(u16*)vptr = y; vptr += 2; \
	*vptr++ = a; vptr++;

	bglUseShader(&windowMaskShaderP);

	
	bglOutputBuffers(DepthBuffer, NULL, 256, 512);
	bglViewport(0, 0, 256, 512);
	bglScissorMode(GPU_SCISSOR_DISABLE);
	
	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(false);
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglColorDepthMask(GPU_WRITE_RED);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 2);	// vertex
	bglAttribType(1, GPU_UNSIGNED_BYTE, 2);	// color
	bglAttribBuffer(vptr);
		
	int nvtx = 0;
	int ystart = ScreenYStart;
	PPU_WindowSection* s = &PPU.WindowSections[0];
	for (;;)
	{
		int xstart = 0;
		PPU_WindowSegment* ws = &s->Window[0];
		for (;;)
		{
			if (xstart < ws->EndOffset)
			{
				u8 alpha = ws->FinalMaskMain;
				ADDVERTEX(xstart, ystart+256,       	    alpha);
				ADDVERTEX(ws->EndOffset, s->EndOffset+256,  alpha);
				alpha = ws->FinalMaskSub;
				ADDVERTEX(xstart, ystart,       	    alpha);
				ADDVERTEX(ws->EndOffset, s->EndOffset,  alpha);
				nvtx += 4;
			}
			
			if (ws->EndOffset >= 256) break;
			xstart = ws->EndOffset;
			ws++;
		}
		
		if (s->EndOffset >= ScreenYEnd) break;
		ystart = s->EndOffset;
		s++;
	}
	
	vptr = (u8*)((((u32)vptr) + 0x1F) & ~0x1F);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, nvtx);
	
	vertexPtr = vptr;
	
#undef ADDVERTEX
}


void PPU_ClearAlpha()
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


	bglUseShader(&plainQuadShaderP);
	bglViewport(0, 0, 256, 512);
	
	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(false);
	
	// transform alpha=128 into 0 and alpha=255 into 255
	// color blending mode doesn't matter since we only write alpha anyway
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_REVERSE_SUBTRACT);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE_MINUS_DST_ALPHA, GPU_ONE);
	
	bglScissorMode(GPU_SCISSOR_DISABLE);
	
	bglColorDepthMask(GPU_WRITE_ALPHA);
	
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
	ADDVERTEX(256, 512, 0,  255, 0, 255, 255);
	vptr = (u8*)((((u32)vptr) + 0x1F) & ~0x1F);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, 2);
	
	// clear alpha wherever the color math window applies
	bglEnableStencilTest(true);
	bglStencilFunc(GPU_EQUAL, 0x20, 0x20, 0xFF);
	
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ZERO, GPU_ZERO);
	
	bglAttribBuffer(vptr);
	
	ADDVERTEX(0, 256, 0,      255, 0, 255, 255);
	ADDVERTEX(256, 512, 0,  255, 0, 255, 255);
	vptr = (u8*)((((u32)vptr) + 0x1F) & ~0x1F);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, 2);

	vertexPtr = vptr;
	
#undef ADDVERTEX
}


void PPU_HardRenderBG_8x8(u32 setalpha, u32 num, int type, u32 prio, int ystart, int yend, u32 opt, u32 hi)
{
	PPU_Background* bg = &PPU.BG[num];
	PPU_Background* obg = &PPU.BG[2];
	u16* tilemap;
	u16* tilemapx;
	u16* tilemapy;
	int tileaddrshift = ((int[]){4, 5, 6})[type];
	u32 xoff, yoff, oxoff, oyoff;
	u16 curtile;
	int x, y, ox, oy, yf;
	u32 idx;
	int systart = 1, syend, syend1, oyend;
	int ntiles = 0;
	u16* vptr = (u16*)vertexPtr;
	u32 validBit = (num + 1) * 0x2000;
	int xmax = 256, xsize = 8, yincr = 8, ysize = 8, yshift = 0;

	if(hi)
	{
		xmax = 512;
		xsize = 4;
		if(PPU.Interlace)
		{
			yincr = 4;
			ysize = 4;
			yshift = 1;
		}
	}
	
#define ADDVERTEX(x, y, coord) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = coord;
	
	PPU_BGSection* s = &bg->Sections[0];
	PPU_BGSection* o;
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
		
		yoff = (s->YScroll + systart) >> yshift;
		ntiles = 0;

		if(opt)
		{
			o = &obg->Sections[0];
			oyend = o->EndOffset;
			while(systart >= oyend)
			{
				o++;
				oyend = o->EndOffset;
			}
			oxoff = o->XScroll & 0xF8;
			oyoff = o->YScroll >> yshift;
			tilemapx = PPU.VRAM + o->TilemapOffset + ((oyoff & 0xF8) << 3);
			tilemapy = PPU.VRAM + o->TilemapOffset + (((oyoff + 8) & 0xF8) << 3);
			if (oyoff & 0x100) if (o->Size & 0x2) tilemapx += (o->Size & 0x1) ? 2048 : 1024;
			if ((oyoff+8) & 0x100) if (o->Size & 0x2) tilemapy += (o->Size & 0x1) ? 2048 : 1024;
			syend1 = syend + (hi && PPU.Interlace ? 3 : 7);
			y = systart;
			oy = y - (yoff & 7);
		}
		else
		{
			y = systart - (yoff & 7);
			oy = y;
			syend1 = syend;
		}
		
		for (; y < syend1; y += yincr, oy += yincr, yoff += 8)
		{
			if(oy >= syend)
			{
				yoff -= (y - syend + 1) << yshift;
				y = syend - 1;
			}

			tilemap = PPU.VRAM + s->TilemapOffset + ((yoff & 0xF8) << 3);
			if (yoff & 0x100)
			{
				if (s->Size & 0x2)
					tilemap += (s->Size & 0x1) ? 2048 : 1024;
			}
			
			xoff = s->XScroll;
			ox = xoff & 7;
			for (x = -ox; x < xmax; x += 8, xoff += 8, ox += 8)
			{
				if(ox < 8 || !opt)
				{
					idx = (xoff & 0xF8) >> 3;
					if (xoff & 0x100)
					{
						if (s->Size & 0x1)
							idx += 1024;
					}
					yf = oy;
				}
				else
				{
					u32 hofs = xoff;
					int vofs = yoff;
					idx = (ox - 8 + oxoff) >> 3;
					if ((ox - 8 + oxoff) & 0x100) if (o->Size & 0x1) idx += 1024;
					u16 hval = tilemapx[idx], vval;
					if(opt==4)
					{
						if(hval & 0x8000)
						{
							vval = hval;
							hval = 0;
						}
						else
							vval = 0;
					}
					else
						vval = tilemapy[idx];
					if (hval & validBit) hofs = ox + (hval & 0x1F8) - (hval & 0x200);
					if (vval & validBit) vofs = y + (vval & 0x1FF) - (vval & 0x200);
					tilemap = PPU.VRAM + s->TilemapOffset + ((vofs & 0xF8) << 3);
					if(vofs & 0x100) if(s->Size & 0x2) tilemap += (s->Size & 0x1) ? 2048 : 1024;
					idx = (hofs & 0xF8) >> 3;
					if (hofs & 0x100) if (s->Size & 0x1) idx += 1024;
					yf = y - (vofs & 7);
				}

				curtile = tilemap[idx];
				

				if ((curtile ^ prio) & 0x2000)
					continue;

				// render the tile
				
				u32 addr = s->TilesetOffset + ((curtile & 0x03FF) << tileaddrshift);
				u32 palid = (curtile & 0x1C00) >> 10;
				
				u32 coord0 = PPU_StoreTileInCache(type, palid, addr);
#define DO_SUBTILE(sx, sy, coord, t0, t3) \
						if (coord != 0xC000) \
						{ \
							ADDVERTEX(x+sx,       yf+sy,       coord+t0); \
							ADDVERTEX(x+sx+xsize, yf+sy+ysize, coord+t3); \
							ntiles++; \
						}				
				if(hi)
				{
					u32 coord1 = PPU_StoreTileInCache(type, palid, addr+(0x01<<tileaddrshift));
					switch (curtile & 0xC000)
					{
						case 0x0000:
							if (x > -4)  DO_SUBTILE(0, 0,  coord0, 0x0000, 0x0101);
							if (x < 508) DO_SUBTILE(4, 0,  coord1, 0x0000, 0x0101);
							break;
						case 0x4000: // hflip
							if (x > -4)  DO_SUBTILE(0, 0,  coord1, 0x0001, 0x0100);
							if (x < 508) DO_SUBTILE(4, 0,  coord0, 0x0001, 0x0100);
							break;
						
						case 0x8000: // vflip
							if (x > -4)  DO_SUBTILE(0, 0,  coord0, 0x0100, 0x0001);
							if (x < 508) DO_SUBTILE(4, 0,  coord1, 0x0100, 0x0001);
							break;
						
						case 0xC000: // hflip+vflip
							if (x > -4)  DO_SUBTILE(0, 0,  coord1, 0x0101, 0x0000);
							if (x < 508) DO_SUBTILE(4, 0,  coord0, 0x0101, 0x0000);
							break;
					}
				}
				else
				{
					if (coord0 == 0xC000) continue;
					switch (curtile & 0xC000)
					{
						case 0x0000:
							DO_SUBTILE(0, 0,  coord0, 0x0000, 0x0101);
							break;
						case 0x4000:
							DO_SUBTILE(0, 0,  coord0, 0x0001, 0x0100);
							break;
						case 0x8000:
							DO_SUBTILE(0, 0,  coord0, 0x0100, 0x0001);
							break;
						case 0xC000:
							DO_SUBTILE(0, 0,  coord0, 0x0101, 0x0000);
							break;
					}
				}
#undef DO_SUBTILE
			}
		}
				
		if (ntiles)
		{
			PPU_StartBG(hi);
			
			bglScissor(0, scissorY+systart, 256, scissorY+syend);
			
			bglEnableStencilTest(true);
			bglStencilFunc(GPU_EQUAL, 0x00, 1<<num, 0xFF);
			
			// set alpha to 128 if we need to disable color math in this BG section
			bglTexEnv(0, 
				GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
				GPU_TEVSOURCES(GPU_TEXTURE0, GPU_CONSTANT, 0),
				GPU_TEVOPERANDS(0,0,0), 
				GPU_TEVOPERANDS(0,0,0), 
				GPU_REPLACE, setalpha ? GPU_REPLACE:GPU_MODULATE, 
				setalpha ? 0xFFFFFFFF:0x80FFFFFF);
			
			bglAttribBuffer(vertexPtr);
			
			vptr = (u16*)((((u32)vptr) + 0x1F) & ~0x1F);
			vertexPtr = vptr;
			
			bglDrawArrays(GPU_GEOMETRY_PRIM, ntiles*2);
		}
		
		if (syend >= yend) break;
		systart = syend;
		s++;
	}
	
#undef ADDVERTEX
}

void PPU_HardRenderBG_16x16(u32 setalpha, u32 num, int type, u32 prio, int ystart, int yend, u32 hi)
{
	PPU_Background* bg = &PPU.BG[num];
	u16* tilemap;
	int tileaddrshift = ((int[]){4, 5, 6})[type];
	u32 xoff, yoff;
	u16 curtile;
	int x, y;
	u32 idx;
	int systart = 1, syend;
	int ntiles = 0;
	u16* vptr = (u16*)vertexPtr;
	int xincr = 16, xsize = 8, yincr = 16, ysize = 8, yshift = 0;

	if(hi)
	{
		xincr = 8;
		xsize = 4;
		if(PPU.Interlace)
		{
			yincr = 8;
			ysize = 4;
			yshift = 1;
		}
	}
	
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
		
		yoff = (s->YScroll + systart) >> yshift;
		ntiles = 0;
		
		for (y = systart - (yoff&15); y < syend; y += yincr, yoff += 16)
		{
			tilemap = PPU.VRAM + s->TilemapOffset + ((yoff & 0x1F0) << 2);
			if (yoff & 0x200)
			{
				if (s->Size & 0x2)
					tilemap += (s->Size & 0x1) ? 2048 : 1024;
			}
			
			xoff = s->XScroll;
			x = -(xoff & 15);
		
			for (; x < 256; x += xincr, xoff += 16)
			{
				idx = (xoff & 0x1F0) >> 4;
				if (xoff & 0x200)
				{
					if (s->Size & 0x1)
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
				
#define DO_SUBTILE(sx, sy, coord, t0, t3) \
					if (coord != 0xC000) \
					{ \
						ADDVERTEX(x+sx,   y+sy,     coord+t0); \
						ADDVERTEX(x+sx+xsize, y+sy+ysize,   coord+t3); \
						ntiles++; \
					}
				
				switch (curtile & 0xC000)
				{
					case 0x0000:
						if ((y - systart) > -ysize)
						{
							if (x > -xsize)      DO_SUBTILE(0, 0,  coord0, 0x0000, 0x0101);
							if (x < 256 - xsize) DO_SUBTILE(xsize, 0,  coord1, 0x0000, 0x0101);
						}
						if (y < (syend - ysize))
						{
							if (x > -xsize)      DO_SUBTILE(0, ysize,  coord2, 0x0000, 0x0101);
							if (x < 256 - xsize) DO_SUBTILE(xsize, ysize,  coord3, 0x0000, 0x0101);
						}
						break;
						
					case 0x4000: // hflip
						if ((y - systart) > -ysize)
						{
							if (x > -xsize)      DO_SUBTILE(0, 0,  coord1, 0x0001, 0x0100);
							if (x < 256 - xsize) DO_SUBTILE(xsize, 0,  coord0, 0x0001, 0x0100);
						}
						if (y < (syend - ysize))
						{
							if (x > -xsize)      DO_SUBTILE(0, ysize,  coord3, 0x0001, 0x0100);
							if (x < 256 - xsize) DO_SUBTILE(xsize, ysize,  coord2, 0x0001, 0x0100);
						}
						break;
						
					case 0x8000: // vflip
						if ((y - systart) > -ysize)
						{
							if (x > -xsize)      DO_SUBTILE(0, 0,  coord2, 0x0100, 0x0001);
							if (x < 256 - xsize) DO_SUBTILE(xsize, 0,  coord3, 0x0100, 0x0001);
						}
						if (y < (syend - ysize))
						{
							if (x > -xsize)      DO_SUBTILE(0, ysize,  coord0, 0x0100, 0x0001);
							if (x < 256 - xsize) DO_SUBTILE(xsize, ysize,  coord1, 0x0100, 0x0001);
						}
						break;
						
					case 0xC000: // hflip+vflip
						if ((y - systart) > -ysize)
						{
							if (x > -xsize)      DO_SUBTILE(0, 0,  coord3, 0x0101, 0x0000);
							if (x < 256 - xsize) DO_SUBTILE(xsize, 0,  coord2, 0x0101, 0x0000);
						}
						if (y < (syend - ysize))
						{
							if (x > -xsize)      DO_SUBTILE(0, ysize,  coord1, 0x0101, 0x0000);
							if (x < 256 - xsize) DO_SUBTILE(xsize, ysize,  coord0, 0x0101, 0x0000);
						}
						break;
				}
				
#undef DO_SUBTILE
			}
		}
		
		if (ntiles)
		{
			PPU_StartBG(hi);
			
			bglScissor(0, scissorY+systart, 256, scissorY+syend);
			
			bglEnableStencilTest(true);
			bglStencilFunc(GPU_EQUAL, 0x00, 1<<num, 0xFF);
			
			// set alpha to 128 if we need to disable color math in this BG section
			bglTexEnv(0, 
				GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
				GPU_TEVSOURCES(GPU_TEXTURE0, GPU_CONSTANT, 0),
				GPU_TEVOPERANDS(0,0,0), 
				GPU_TEVOPERANDS(0,0,0), 
				GPU_REPLACE, setalpha ? GPU_REPLACE:GPU_MODULATE, 
				setalpha ? 0xFFFFFFFF:0x80FFFFFF);
			
			bglAttribBuffer(vertexPtr);
			
			vptr = (u16*)((((u32)vptr) + 0x1F) & ~0x1F);
			vertexPtr = vptr;
			
			bglDrawArrays(GPU_GEOMETRY_PRIM, ntiles*2);
		}
		
		if (syend >= yend) break;
		systart = syend;
		s++;
	}
	
#undef ADDVERTEX
}

int PPU_HardRenderBG_Mode7TileRow(int left, int right, int cy, u16* vptr, int type, int style)
{
#define ADDVERTEX(x, y, coord) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = coord;

	u32 tileidx;
	int ntiles = 0;
	int yy = cy << 3;
	int yyy = (yy & 0x3F8) << 4;

#define DO_SUBTILE(sx, sy, t) \
	{ \
		ADDVERTEX(sx,   sy,   t); \
		ntiles++; \
	}

	if(type == 2)
	{
		if(cy < 0 || cy > 127)
			return 0;
		if(left < 0) left = 0;
		if(right > 127) right = 127;
		int xx = left << 3;
		for(; left <= right; left++, xx += 8)
		{
			tileidx = PPU.VRAM[(left + yyy) << 1] << 4;
			u32 coord = PPU_StoreTileInCache(style, 0, tileidx);
			if (coord == 0xC000) continue;
			DO_SUBTILE(xx, yy, coord);
		}
	}
	else if(type == 3)
	{
		if(cy < 0 || cy > 127)
		{
			left <<= 3; right <<= 3;
			for(; left <= right; left += 8)
				DO_SUBTILE(left, yy, coord0);
		}
		else
		{
			if(left < 0)
			{
				int xx = left << 3;
				int xxr = (right < 0 ? right << 3: 0);
				for(; xx <= xxr; xx += 8)
					DO_SUBTILE(xx, yy, coord0);
				left = 0;
			}
			if(right > 127)
			{
				int xx = (left > 127 ? left << 3 : 0);
				int xxr = right << 3;
				for(; xx <= xxr; xx += 8)
					DO_SUBTILE(xx, yy, coord0);
				right = 127;
			}
			int xx = left << 3;
			for(; left <= right; left++, xx += 8)
			{
				tileidx = PPU.VRAM[(left + yyy) << 1] << 4;
				u32 coord = PPU_StoreTileInCache(style, 0, tileidx);
				if (coord == 0xC000) continue;
				DO_SUBTILE(xx, yy, coord);
			}
		}
	}
	else
	{
		int xx = left << 3;
		for(; left <= right; left++, xx += 8)
		{
			tileidx = PPU.VRAM[((left & 0x7F) + yyy) << 1] << 4;
			u32 coord = PPU_StoreTileInCache(style, 0, tileidx);
			if (coord == 0xC000) continue;
			DO_SUBTILE(xx, yy, coord);
		}
	}

#undef DO_SUBTILE
#undef ADDVERTEX

	return ntiles;
}


void PPU_HardRenderBG_ProcessMode7(int ystart, int yend)
{
	// General variables for both modes
	int systart = 1, syend;
	s32 x, y, lx, ly;
	s16 A, B, C, D;
	int i, j;
	u32 tileidx, transp, tile0;
	u32 hflip, vflip;
	u8 colorval;
	u16 *buffer;
	const u32 xincr[8] = {1, 3, 1, 11, 1, 3, 1, 43};

	u8 wasSW = 0;


	u16 oldcolor0 = PPU.HardPalette[0];
	PPU.HardPalette[0] = 0;

	PPU_Mode7Section* s = &PPU.Mode7Sections[0];

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

		if(s->doHW)
		{
			// Calculate vertices for Hardware Mode

			// Need to set the last software section to be indicated as the end
			if(wasSW)
			{
				(s-1)->endSW = 1;
				wasSW = 0;
			}
			
			PPU_Vertex vert[8], vertT[4];
			int M7Top, vi, length = syend - systart;
			int tileType;

			hflip = s->Sel & 0x1;
			vflip = s->Sel & 0x2 ? 0xFF : 0x0;
			tileType = s->Sel >> 6;

			vi = (vflip ? (systart ^ 0xFF) - length : systart);


			// Grab the 4 points that make up the parallelogram
			vertT[0].x = (s->A * (s->XScroll-s->RefX)) + (s->B * (vi+s->YScroll-s->RefY)) + (s->RefX << 8);
			vertT[0].y = (s->C * (s->XScroll-s->RefX)) + (s->D * (vi+s->YScroll-s->RefY)) + (s->RefY << 8);
			vertT[1].x = vertT[0].x + (length * s->B);	vertT[1].y = vertT[0].y + (length * s->D);
			vertT[2].x = vertT[1].x + (256 * s->A);		vertT[2].y = vertT[1].y + (256 * s->C);
			vertT[3].x = vertT[0].x + (256 * s->A);		vertT[3].y = vertT[0].y + (256 * s->C);

			// get the slopes
			for(j = 0; j < 4; j++)
			{
				int xdiff = vertT[(j+1) & 0x3].x - vertT[j].x, ydiff = vertT[(j+1) & 0x3].y - vertT[j].y;
				if(ydiff)
					vertT[j].slope = (xdiff << 11) / ydiff;
				else 
					vertT[j].slope = (xdiff < 0 ? INT_MIN : INT_MAX);
			}

			// Find top-most, rotate them around so top-most is first in array, and then form two sets of 4 points that go from top-most to bottom-most
			M7Top = 0;
			for (j = 1; j < 4; j++)
				if (vertT[j].y < vertT[M7Top].y)
					M7Top = j;
			vert[0] = vert[4] = vertT[M7Top];
			vert[1] = vert[2] = vertT[(M7Top + 1) & 0x3];
			vert[3] = vert[7] = vertT[(M7Top + 2) & 0x3];
			vert[5] = vert[6] = vertT[(M7Top + 3) & 0x3];
			vert[4].slope = vert[5].slope;
			vert[6].slope = vert[7].slope;
		
			// Based on Y positioning of middle segments, calculate to correct X/Y positions, and assign slopes if necessary
			if(vert[1].y <= vert[5].y)
			{
				vert[2].y = vert[6].y;			vert[2].x = vert[1].x + ((vert[1].slope * (vert[2].y - vert[1].y)) >> 11);
				vert[5].y = vert[1].y;			vert[5].x = vert[4].x + ((vert[5].slope * (vert[5].y - vert[4].y)) >> 11);
			}
			else
			{
				vert[1].y = vert[5].y;			vert[1].x = vert[0].x + ((vert[0].slope * (vert[1].y - vert[0].y)) >> 11);
				vert[6].y = vert[2].y;			vert[6].x = vert[5].x + ((vert[6].slope * (vert[6].y - vert[5].y)) >> 11);
				vert[1].slope = vert[0].slope;	vert[5].slope = vert[6].slope;
			}

			// Possible ABCD values may make the order CW, so make sure it's CCW
			if(vert[1].x > vert[5].x)
			{
				vertT[0] = vert[1]; vert[1] = vert[5]; vert[5] = vertT[0];
				vertT[0] = vert[2]; vert[2] = vert[6]; vert[6] = vertT[0];
			}

			// If layer is type 2 (in that there is no wrapping), then we truncate the points to within vertical limits of the layer
			if(tileType == 2)
			{
				vert[3].slope = vert[2].slope;
				vert[7].slope = vert[6].slope;
				for(j = 0; j < 8; j++)
				{
					if(vert[j].y < 0)
					{
						vert[j].x -= ((vert[j].y * vert[j].slope) >> 11);
						vert[j].y = 0;
					}
					else if(vert[j].y > 262144)
					{
						vert[j].x -= (((vert[j].y - 262144) * vert[j].slope) >> 11);
						vert[j].y = 262144;
					}
				}
			}

			s->hflip = hflip;
			s->vflip = vflip;
			s->tileType = tileType;
			for(j = 0; j < 8; j++)
				s->vert[j] = vert[j];			
		}
		else
		{
			s->endSW = 0;
			wasSW = 1;

			// Process scanlines for Software Mode
			
			hflip = s->Sel & 0x1 ? 0xFF : 0x0;
			vflip = s->Sel & 0x2 ? 0xFF : 0x0;
			A = s->A * (hflip ? -1 : 1); B = s->B * (vflip ? -1 : 1); C = s->C * (hflip ? -1 : 1); D = s->D * (vflip ? -1 : 1);
			lx = (s->A * (hflip+s->XScroll-s->RefX)) + (s->B * ((systart^vflip)+s->YScroll-s->RefY)) + (s->RefX << 8);
			ly = (s->C * (hflip+s->XScroll-s->RefX)) + (s->D * ((systart^vflip)+s->YScroll-s->RefY)) + (s->RefY << 8);
			transp = (s->Sel & 0xC0) == 0x80; tile0 = (s->Sel & 0xC0) == 0xC0;

			for (j = systart; j < syend; j++)
			{
				x = lx; y = ly;
				buffer = &Mode7ColorBuffer[YOffset256[j]];
				if(PPU.M7ExtBG)
				{
					//Multi-layer processing
					for (i = 0; i < 256; i++)
					{
						if ((x|y) & 0xFFFC0000)
						{
							// wraparound
							if (transp)
							{
								// transparent
								*buffer = 0;
								buffer[65536] = 0;
								buffer += xincr[i&7];
						
								x += A;
								y += C;
								continue;
							}
							else if (tile0)
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
				
						*buffer = PPU.HardPalette[colorval];
						buffer[65536] = PPU.PaletteEx2[colorval];
						buffer += xincr[i&7];
				
						x += A;
						y += C;
					}
				}
				else
				{
					// Single layer processing

					for (i = 0; i < 256; i++)
					{
						if ((x|y) & 0xFFFC0000)
						{
							if (transp)
							{
								*buffer = 0;
								buffer += xincr[i&7];
								x += A;
								y += C;
								continue;
							}
							else if (tile0)
								tileidx = 0;
							else
							{
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
						*buffer = PPU.HardPalette[colorval];
						buffer += xincr[i&7];
						x += A;
						y += C;
					}
				}
				lx += B;
				ly += D;
			}
			
		}

		if (syend >= yend) break;
		systart = syend;
		s++;
	}
	s->endSW = 1;

	PPU.HardPalette[0] = oldcolor0;

}


void PPU_HardRenderBG_Mode7(u32 setalpha, int ystart, int yend, u32 prio)
{
	int systart = 1, syend;
	int ntiles = 0, tilecnt = 0;
	int i, j;
	int curHW = 0;
	int style;
	u16* vptr = (u16*)vertexPtr;
	

	u16 oldcolor0 = PPU.HardPalette[0];
	PPU.HardPalette[0] = 0;

	style = (PPU.M7ExtBG ? (prio ? TILE_Mode7_2 : TILE_Mode7_1) : TILE_Mode7);

	PPU_Mode7Section* s = &PPU.Mode7Sections[0];
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
		
		if(s->doHW)
		{
			PPU_Vertex vert[8];
			for(i = 0; i < 8; i++)
				vert[i] = s->vert[i];
			
			ntiles = 0;

			//Travel through each side together from point to point, obtaining the left-most and right-most areas for each tile row,
			// then add them to the buffer

			int lx = vert[0].x, rx = vert[4].x;
			int flx = lx, frx = rx;
			int cury = vert[0].y >> 11;
			for(i = 0; i < 3; i++)
			{
				int ydiff = vert[i + 1].y - vert[i].y;
				int yoff = (0x800 - (vert[i].y & 0x7FF));
				ydiff -= yoff + (vert[i + 1].y & 0x7FF);
				// If from point to point doesn't cross tile row vertical edges, then we're still in the same tile row, so nothing needs to be added to the buffer yet
				if(ydiff < 0)
				{
					if(flx > vert[i + 1].x) flx = vert[i + 1].x;
					if(frx < vert[i + 5].x) frx = vert[i + 5].x;
				}
				else
				{
					// Grab the left-most and right-most tiles from the tile row for buffer input, and then assign vertical position to be right on the edge of the next tile row
					int lslope = vert[i].slope, rslope = vert[i + 4].slope;
					lx += (yoff * lslope) >> 11;
					rx += (yoff * rslope) >> 11;
					if(flx > lx) flx = lx;
					if(frx < rx) frx = rx;
					tilecnt = PPU_HardRenderBG_Mode7TileRow(flx >> 11, frx >> 11, cury, vptr, s->tileType, style);
					vptr += tilecnt * 3;
					ntiles += tilecnt;
					flx = lx;
					frx = rx;

					// Depending on if the slope is negative or positive, we may increment the x-position so we're guarenteed the left-most or right-most for following tile rows
					if(lslope < 0) flx += lslope;
					if(rslope > 0) frx += rslope;
					cury++;

					// If the length is greater or equal to 1 tile row, then we obtain them without excessive calculations
					int rcount = ydiff >> 11;
					for(j = 0; j < rcount; j++, flx += lslope, frx += rslope, cury++)
					{
						tilecnt = PPU_HardRenderBG_Mode7TileRow(flx >> 11, frx >> 11, cury, vptr, s->tileType, style);
						vptr += tilecnt * 3;
						ntiles += tilecnt;
					}
					if(lslope < 0) flx = vert[i + 1].x;
					if(rslope > 0) frx = vert[i + 5].x;
				}

				lx = vert[i + 1].x;
				rx = vert[i + 5].x;
			}

			// Finish off the last tile row

			if(flx > lx) flx = lx;
			if(frx < rx) frx = rx;
			tilecnt = PPU_HardRenderBG_Mode7TileRow(flx >> 11, frx >> 11, cury, vptr, s->tileType, style);
			vptr += tilecnt * 3;
			ntiles += tilecnt;
		
			if (ntiles)
			{
				// Do we need to set the system to hardware-Mode7-rendering?
				if(curHW < 1)
				{
					doingBG = 0;
					bglUseShader(&hard7RenderShaderP);
					bglFaceCulling(GPU_CULL_NONE);
					PPU_StartBG(0);
					curHW = 1;
				}
								

				// Unlike software rendering of Mode 7, we need to grab the inverse of the 2x2 matrix for proper orientation and positioning
				float A = (float)s->A / 256.0f;
				float B = (float)s->B / 256.0f;
				float C = (float)s->C / 256.0f;
				float D = (float)s->D / 256.0f;
				int flipX = s->RefX, flipY = s->RefY;

				if(s->hflip)
				{
					A = -A;
					C = -C;
					s->XScroll = -s->XScroll;
					flipX = 255 - flipX;
				}
				if(s->vflip)
				{
					B = -B;
					D = -D;
					s->YScroll = -s->YScroll;
					flipY = 255 - flipY;
				}

						
				float det = (A * D) - (B * C);
				if(det == 0.0f)
				{
					// Not correct, as the actual values would be "infinity" when dividing by 0, so max float values?
					snesM7Matrix[0] = 0.0; snesM7Matrix[1] = 0.0; snesM7Matrix[4] = 0.0; snesM7Matrix[5] = 0.0;
				}
				else
				{
					det = 1.0f / det;
					snesM7Matrix[0] = D * det;
					snesM7Matrix[1] = -B * det;
					snesM7Matrix[4] = -C * det;
					snesM7Matrix[5] = A * det;
				}
			
				snesM7Matrix[3] = (snesM7Matrix[0] * -s->RefX) + (snesM7Matrix[1] * -s->RefY) + flipX - s->XScroll;
				snesM7Matrix[7] = (snesM7Matrix[4] * -s->RefX) + (snesM7Matrix[5] * -s->RefY) + flipY - s->YScroll;
			

				bglUniformMatrix(GPU_VERTEX_SHADER, 0, snesM7Matrix);
				SET_UNIFORM(GPU_GEOMETRY_SHADER, 14, snesM7Matrix[0] * 0.0628f, snesM7Matrix[4] * 0.0628f, 0.0f, 0.0f);
				SET_UNIFORM(GPU_GEOMETRY_SHADER, 15, snesM7Matrix[1] * 0.0628f, snesM7Matrix[5] * 0.0628f, 0.0f, 0.0f);
				
				bglScissor(0, scissorY+systart, 256, scissorY+syend);
			
				bglEnableStencilTest(true);
				bglStencilFunc(GPU_EQUAL, 0x00, 0x01, 0xFF);
			
				// set alpha to 128 if we need to disable color math in this BG section
				bglTexEnv(0, 
					GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
					GPU_TEVSOURCES(GPU_TEXTURE0, GPU_CONSTANT, 0),
					GPU_TEVOPERANDS(0,0,0), 
					GPU_TEVOPERANDS(0,0,0), 
					GPU_REPLACE, setalpha ? GPU_REPLACE:GPU_MODULATE, 
					setalpha ? 0xFFFFFFFF:0x80FFFFFF);
			
				bglAttribBuffer(vertexPtr);
			
				vptr = (u16*)((((u32)vptr) + 0x1F) & ~0x1F);
				vertexPtr = vptr;
			
				bglDrawArrays(GPU_GEOMETRY_PRIM, ntiles);
			}
		}
		else
		{
			// This grabs the entire continuous software-rendered section, so it prevents needing to assign system settings each time and render scanline-by-scanline.
			while(!(s->endSW))
			{
				s++;
				syend = s->EndOffset;
				if (syend > yend) syend = yend;
			}

			// Do we need to set the system to regular hardware-rendering?
			if(curHW)
			{
				doingBG = 0;
				bglUseShader(&hardRenderShaderP);
				bglFaceCulling(GPU_CULL_BACK_CCW);
				curHW = 0;
			}


#define ADDVERTEX(x, y, s, t) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = s; \
	*vptr++ = t;

			bglEnableStencilTest(true);
			bglStencilFunc(GPU_EQUAL, 0x00, 0x01, 0xFF);
			bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
			bglEnableDepthTest(false);
			bglEnableAlphaTest(true);
			bglAlphaFunc(GPU_GREATER, 0);
	
			bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
			bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
			bglScissorMode(GPU_SCISSOR_DISABLE);
	
			bglColorDepthMask(GPU_WRITE_COLOR);
	
			SET_UNIFORM(GPU_VERTEX_SHADER, 4, 1.0f/256.0f, 1.0f/256.0f, 1.0f, 1.0f);
	
			bglEnableTextures(GPU_TEXUNIT0);
	
		// set alpha to 128 if we need to disable color math in this section
			bglTexEnv(0, 
				GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
				GPU_TEVSOURCES(GPU_TEXTURE0, GPU_CONSTANT, 0),
				GPU_TEVOPERANDS(0,0,0), 
				GPU_TEVOPERANDS(0,0,0), 
				GPU_REPLACE, setalpha ? GPU_REPLACE:GPU_MODULATE, 
				setalpha ? 0xFFFFFFFF:0x80FFFFFF);
			bglDummyTexEnv(1);
			bglDummyTexEnv(2);
			bglDummyTexEnv(3);
			bglDummyTexEnv(4);
			bglDummyTexEnv(5);

			bglTexImage(GPU_TEXUNIT0, Mode7ColorBuffer + (prio ? 65536 : 0),256,256,0,GPU_RGBA5551);
				
			bglNumAttribs(2);
			bglAttribType(0, GPU_SHORT, 2);	// vertex
			bglAttribType(1, GPU_SHORT, 2);	// texcoord
			bglAttribBuffer(vertexPtr);

			ADDVERTEX(0, systart,     0, 256-systart);
			ADDVERTEX(256, syend,     256, 256-syend);

			vptr = (u16*)((((u32)vptr) + 0x1F) & ~0x1F);
			vertexPtr = vptr;

			bglDrawArrays(GPU_GEOMETRY_PRIM, 2);

#undef ADDVERTEX
		}

		if (syend >= yend) break;
		systart = syend;
		s++;
	}
	
	PPU.HardPalette[0] = oldcolor0;

	// As a precaution, we'll revert back to normal hardware-rendering should the need be
	if(curHW)
	{
		bglUseShader(&hardRenderShaderP);
		bglFaceCulling(GPU_CULL_BACK_CCW);
	}
}



int PPU_HardRenderOBJ(u8* oam, u32 oamextra, int y, int width, int height, int ystart, int yend, u32 tileaddr, u32 tilegap)
{
	s32 xoff;
	u16 attrib;
	u32 idx;
	s32 x;
	u32 palid, prio, cmath;
	int yincr = 8;
	int ntiles = 0;
	u16* vptr = (u16*)vertexPtr;
	
#define ADDVERTEX(x, y, z, coord, pr) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = z; \
	*vptr++ = coord; \
	*vptr++ = pr;
	
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
	if (attrib & 0x100)
		idx += tilegap;
	
	if (attrib & 0x4000)
		idx += ((width-1) & 0x38) << 2;
	if (attrib & 0x8000)
		idx += ((height-1) & 0x38) << 6;
		
	width += x;
	if (width > 256) width = 256;
	if(PPU.OBJVDir)
	{
		height >>= 1;
		yincr = 4;
	}
	height += y;
	if (height > yend) height = yend;
	ystart -= 8;
	
	palid = 8 + ((oam[3] & 0x0E) >> 1);
	
	prio = (oam[3] & 0x30);
	u16 priotbl[4] = {0x0000, 0x0001, 0x0100, 0x0101};
	prio = priotbl[prio >> 4];
	
	cmath = (palid < 12) ? 0x80 : 0xFF;
	
	for (; y < height; y += yincr)
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
			
			u32 addr = tileaddr + idx;
			u32 coord = PPU_StoreTileInCache(TILE_4BPP, palid, addr);
			if (coord == 0xC000)
			{
				idx += (attrib & 0x4000) ? -32:32;
				continue;
			}
			
			//if (x <= -8 || x > 255 || y <= -8 || y > 223)
			//	bprintf("OBJ tile %d/%d %04X\n", x, y, coord);
			
			switch (attrib & 0xC000)
			{
				case 0x0000:
					ADDVERTEX(x,   y,     cmath, coord, prio);
					ADDVERTEX(x+8, y+yincr,   cmath, coord+0x0101, prio);
					break;
					
				case 0x4000: // hflip
					ADDVERTEX(x,   y,     cmath, coord+0x0001, prio);
					ADDVERTEX(x+8, y+yincr,   cmath, coord+0x0100, prio);
					break;
					
				case 0x8000: // vflip
					ADDVERTEX(x,   y,     cmath, coord+0x0100, prio);
					ADDVERTEX(x+8, y+yincr,   cmath, coord+0x0001, prio);
					break;
					
				case 0xC000: // hflip+vflip
					ADDVERTEX(x,   y,     cmath, coord+0x0101, prio);
					ADDVERTEX(x+8, y+yincr,   cmath, coord, prio);
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
	int ystart = ScreenYStart, yend;
	u8* cur_oam = PPU.HardOAM;
	
	
	bglOutputBuffers(OBJColorBuffer, OBJPrioBuffer, 256, 256);
			
	bglScissorMode(GPU_SCISSOR_NORMAL);
		
	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(true);
	bglAlphaFunc(GPU_GREATER, 0);
	
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglColorDepthMask(GPU_WRITE_ALL);
	
	SET_UNIFORM(GPU_VERTEX_SHADER, 4, 1.0f/128.0f, 1.0f/128.0f, 1.0f, 1.0f);
	//SET_UNIFORM(GPU_VERTEX_SHADER, 5, 1.0f, 0.0f, 0.0f, 0.0f);
	
	bglEnableTextures(GPU_TEXUNIT0);
	
	bglTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_MODULATE, 
		0xFFFFFFFF);
	bglDummyTexEnv(1);
	bglDummyTexEnv(2);
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
		
	bglTexImage(GPU_TEXUNIT0, PPU_TileCache,1024,1024,0,GPU_RGBA5551);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 3);	// vertex
	bglAttribType(1, GPU_UNSIGNED_BYTE, 4);	// texcoord/prio
	

	PPU_OBJSection* s = &PPU.OBJSections[0];
	for (;;)
	{
		void* vstart = vertexPtr;
		int ntiles = 0;
		
		yend = s->EndOffset;

		i = last;
		do
		{
			u8* oam = &cur_oam[i << 2];
			u8 oamextra = cur_oam[0x200 + (i >> 2)] >> ((i & 0x03) << 1);
			s32 oy = (s32)oam[1] + 1;
			
			s32 ow = (s32)s->OBJWidth[(oamextra & 0x2) >> 1];
			s32 oh = (s32)s->OBJHeight[(oamextra & 0x2) >> 1];

			if ((oy+oh) > ystart && oy < yend)
			{
				ntiles += PPU_HardRenderOBJ(oam, oamextra, oy, ow, oh, ystart, yend, s->OBJTilesetAddr, s->OBJGap);
			}
			if (oy >= 192)
			{
				oy -= 0x100;
				if ((oy+oh) > ystart)
				{
					ntiles += PPU_HardRenderOBJ(oam, oamextra, oy, ow, oh, ystart, yend, s->OBJTilesetAddr, s->OBJGap);
				}
			}

			i--;
			if (i < 0) i = 127;
		}
		while (i != last);
		
		if (ntiles)
		{
			vertexPtr = (u16*)((((u32)vertexPtr) + 0xF) & ~0xF);
			bglScissor(0, ystart, 256, yend);
			bglAttribBuffer(vstart);
			
			bglDrawArrays(GPU_GEOMETRY_PRIM, ntiles*2);
		}
		
		if (yend >= ScreenYEnd)
		{
			break;
		}
		else
		{
			ystart = yend;
			s++;
		}
	}
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
	
	bglEnableStencilTest(true);
	bglStencilFunc(GPU_EQUAL, 0x00, 0x10, 0xFF);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(true);
	bglAlphaFunc(GPU_GREATER, 0);
	
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglScissorMode(GPU_SCISSOR_DISABLE);
	
	bglColorDepthMask(GPU_WRITE_COLOR);
	
	SET_UNIFORM(GPU_VERTEX_SHADER, 4, 1.0f/256.0f, 1.0f/256.0f, 1.0f, 1.0f);
	//SET_UNIFORM(GPU_VERTEX_SHADER, 5, 1.0f, 0.0f, 0.0f, 0.0f);
	
	bglEnableTextures(GPU_TEXUNIT0 | GPU_TEXUNIT1);
	
	// force alpha to 0 if the priority value isn't what we want
	bglTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE1, GPU_TEXTURE1, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS((prio&0x20)?GPU_TEVOP_A_SRC_B:GPU_TEVOP_A_ONE_MINUS_SRC_B, 
		                (prio&0x10)?GPU_TEVOP_A_SRC_G:GPU_TEVOP_A_ONE_MINUS_SRC_G, 
						0), 
		GPU_REPLACE, GPU_MODULATE, 
		0xFFFFFFFF);
	bglTexEnv(1, 
		GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0), 
		GPU_TEVSOURCES(GPU_PREVIOUS, GPU_TEXTURE0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_MODULATE, 
		0xFFFFFFFF);
	// set alpha according to color-math rules
	if (setalpha & 0x80) 
	{
		// subscreen, disable div2
		// 0 -> 0, anything else -> 255
		bglTexEnv(2, 
			GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0), 
			GPU_TEVSOURCES(GPU_PREVIOUS, GPU_PREVIOUS, 0),
			GPU_TEVOPERANDS(0,0,0), 
			GPU_TEVOPERANDS(0,0,0), 
			GPU_REPLACE, GPU_ADD, 
			0xFFFFFFFF);
	}
	else if (setalpha & 0x10)
	{
		// mainscreen, enable color math for sprites
		// keep alpha as-is
		bglTexEnv(2, 
			GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0), 
			GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0),
			GPU_TEVOPERANDS(0,0,0), 
			GPU_TEVOPERANDS(0,0,0), 
			GPU_REPLACE, GPU_REPLACE, 
			0x80FFFFFF);
	}
	else
	{
		// 1. mainscreen, disable color math for sprites
		// 2. subscreen, enable div2
		// 0 -> 0, anything else -> 128
		bglTexEnv(2, 
			GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0), 
			GPU_TEVSOURCES(GPU_PREVIOUS, GPU_CONSTANT, 0),
			GPU_TEVOPERANDS(0,0,0), 
			GPU_TEVOPERANDS(0,0,0), 
			GPU_REPLACE, GPU_MODULATE, 
			0x80FFFFFF);
	}
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
		
	bglTexImage(GPU_TEXUNIT0, OBJColorBuffer,256,256,0,GPU_RGBA8);
	bglTexImage(GPU_TEXUNIT1, OBJPrioBuffer,256,256,0,GPU_RGBA8);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 3);	// vertex
	bglAttribType(1, GPU_SHORT, 2);	// texcoord
	bglAttribBuffer(vptr);
		
	ADDVERTEX(0, ystart,   prio,  0, ystart);
	ADDVERTEX(256, yend,   prio,  256, yend);
	vptr = (u16*)((((u32)vptr) + 0x1F) & ~0x1F);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, 2);
	
	vertexPtr = vptr;
	
#undef ADDVERTEX
}




void PPU_ComputeWindows_Hard(PPU_WindowSegment* s)
{
	PPU_ComputeWindows(s);
	
	// compute final window masks
	for (;;)
	{
		u16 allenable = PPU.MainScreen|PPU.SubScreen;
		allenable &= (allenable >> 8);
		u8 finalmask = 0;
		
		if (allenable & 0x01)
			finalmask |= ((PPU.BG[0].WindowCombine & (1 << (s->WindowMask ^ PPU.BG[0].WindowMask))) ? 0x01 : 0);
		if (allenable & 0x02)
			finalmask |= ((PPU.BG[1].WindowCombine & (1 << (s->WindowMask ^ PPU.BG[1].WindowMask))) ? 0x02 : 0);
		if (allenable & 0x04)
			finalmask |= ((PPU.BG[2].WindowCombine & (1 << (s->WindowMask ^ PPU.BG[2].WindowMask))) ? 0x04 : 0);
		if (allenable & 0x08)
			finalmask |= ((PPU.BG[3].WindowCombine & (1 << (s->WindowMask ^ PPU.BG[3].WindowMask))) ? 0x08 : 0);
		
		if (allenable & 0x10)
			finalmask |= ((PPU.OBJWindowCombine & (1 << (s->WindowMask ^ PPU.OBJWindowMask))) ? 0x10 : 0);
		
		if (s->ColorMath & PPU.ColorMath1)
			finalmask |= 0x20;
			
		s->FinalMaskMain = finalmask & (PPU.MainWindowEnable|0x20);
		s->FinalMaskSub = finalmask & (PPU.SubWindowEnable|0x20);
		
		if (s->EndOffset >= 256) break;
		s++;
	}
}

void PPU_StartHardSection(u32 line)
{
	int i;
	
	ScreenYStart = line;
	
	PPU.CurModeSection = &PPU.ModeSections[0];
	PPU.CurModeSection->Mode = PPU.Mode;
	PPU.CurModeSection->MainScreen = PPU.MainScreen;
	PPU.CurModeSection->SubScreen = PPU.SubScreen;
	PPU.CurModeSection->ColorMath1 = PPU.ColorMath1;
	PPU.CurModeSection->ColorMath2 = PPU.ColorMath2;
	PPU.ModeDirty = 0;

	PPU.CurOBJSection = &PPU.OBJSections[0];
	PPU.CurOBJSection->OBJWidth = PPU.OBJWidth;
	PPU.CurOBJSection->OBJHeight = PPU.OBJHeight;
	PPU.CurOBJSection->OBJTilesetAddr = PPU.OBJTilesetAddr;
	PPU.CurOBJSection->OBJGap = PPU.OBJGap;
	PPU.OBJDirty = 0;
	
	for (i = 0; i < 4; i++)
	{
		PPU_Background* bg = &PPU.BG[i];
		
		bg->CurSection = &bg->Sections[0];
		bg->CurSection->ScrollParams = bg->ScrollParams;
		bg->CurSection->GraphicsParams = bg->GraphicsParams;
		bg->CurSection->Size = bg->Size;
		
		bg->LastScrollParams = bg->ScrollParams;
		bg->LastGraphicsParams = bg->GraphicsParams;
		bg->LastSize = bg->Size;
	}

	PPU.CurMode7Section = &PPU.Mode7Sections[0];
	PPU.CurMode7Section->StartOffset = line;
	PPU.CurMode7Section->Sel = PPU.M7Sel;
	PPU.CurMode7Section->AffineParams1 = PPU.M7AffineParams1;
	PPU.CurMode7Section->AffineParams2 = PPU.M7AffineParams2;
	PPU.CurMode7Section->RefParams = PPU.M7RefParams;
	PPU.CurMode7Section->ScrollParams = PPU.M7ScrollParams;
	PPU.Mode7Dirty = 0;
	
	PPU.CurWindowSection = &PPU.WindowSections[0];
	PPU_ComputeWindows_Hard(&PPU.CurWindowSection->Window[0]);
	PPU.WindowDirty = 0;
	
	PPU.CurMainBackdrop = &PPU.MainBackdropSections[0];
	PPU.CurMainBackdrop->Color = PPU.Palette[0];
	PPU.CurMainBackdrop->ColorMath2 = PPU.ColorMath2;
	PPU.MainBackdropDirty = 0;

	PPU.CurSubBackdrop = &PPU.SubBackdropSections[0];
	PPU.CurSubBackdrop->Color = PPU.SubBackdrop;
	PPU.CurSubBackdrop->Div2 = (!(PPU.ColorMath1 & 0x02)) && (PPU.ColorMath2 & 0x40);
	PPU.SubBackdropDirty = 0;
}

void PPU_FinishHardSection(u32 line)
{
	int i;
	
	ScreenYEnd = line;
	
	PPU.CurModeSection->EndOffset = line;
	
	PPU.CurOBJSection->EndOffset = line;

	for (i = 0; i < 4; i++)
	{
		PPU_Background* bg = &PPU.BG[i];
		bg->CurSection->EndOffset = line;
	}
	
	PPU_Mode7Section *cur = PPU.CurMode7Section;
	cur->EndOffset = line;
	if((PPU.Mode & 0x07) == 7)
	{
		if(Config.HardwareMode7 > 0)
		{
			int numLines = cur->EndOffset - cur->StartOffset;
			int sizeComp = (numLines == 1 ? 0x30000 : (numLines >= 16 ? 0x100000 : 0x80000));
			coord0 = PPU_StoreTileInCache(TILE_Mode7, 0, ((u32)PPU.VRAM[0]) << 7);
			if((cur->Sel >> 6) == 2)
				cur->doHW = 1;
			else if(((cur->Sel >> 6) == 3) && (coord0 == 0xC000))
			{
				cur->Sel &= 0xBF;
				cur->doHW = 1;
			}
			else if(((((int)cur->A * cur->A) + ((int)cur->C * cur->C)) <= sizeComp) && ((((int)cur->B * cur->B) + ((int)cur->D * cur->D)) <= sizeComp))
				cur->doHW = 1;
			else
				cur->doHW = 0;
		}
		else
			cur->doHW = 0;
	}
	
	PPU.CurWindowSection->EndOffset = line;
	
	PPU.CurMainBackdrop->EndOffset = line;
	PPU.CurSubBackdrop->EndOffset = line;
}

void PPU_RenderScanline_Hard(u32 line)
{
	int i;
	
	if (!line)
	{
		FirstScreenSection = 1;
		
		CurForcedBlank = PPU.ForcedBlank;
		
		// initialize stuff upon line 0
		PPU_StartHardSection(1);
		
		PPU.CurColorEffect = &PPU.ColorEffectSections[0];
		PPU.CurColorEffect->ColorMath = (PPU.ColorMath2 & 0x80);
		PPU.CurColorEffect->Brightness = PPU.CurBrightness;
		PPU.ColorEffectDirty = 0;
	}
	else
	{
		if (PPU.ForcedBlank)
		{
			if (!CurForcedBlank)
			{
				PPU_HardRenderSection(line);
			}
		}
		else if (CurForcedBlank)
		{
			PPU_StartHardSection(line);
		}
		else
		{
			if (PPU.ModeDirty)
			{
				if((PPU.CurModeSection->Mode != PPU.Mode) ||
				   (PPU.CurModeSection->MainScreen != PPU.MainScreen) ||
				   (PPU.CurModeSection->SubScreen != PPU.SubScreen) ||
				   (PPU.CurModeSection->ColorMath1 != PPU.ColorMath1) ||
				   (PPU.CurModeSection->ColorMath2 != PPU.ColorMath2))
				{
					PPU.CurModeSection->EndOffset = line;
					PPU.CurModeSection++;
				
					PPU.CurModeSection->Mode = PPU.Mode;
					PPU.CurModeSection->MainScreen = PPU.MainScreen;
					PPU.CurModeSection->SubScreen = PPU.SubScreen;
					PPU.CurModeSection->ColorMath1 = PPU.ColorMath1;
					PPU.CurModeSection->ColorMath2 = PPU.ColorMath2;
					PPU.MainBackdropDirty = 1;
				}		
			}

			if (PPU.OBJDirty)
			{
				PPU.CurOBJSection->EndOffset = line;
				PPU.CurOBJSection++;

				PPU.CurOBJSection->OBJWidth = PPU.OBJWidth;
				PPU.CurOBJSection->OBJHeight = PPU.OBJHeight;
				PPU.CurOBJSection->OBJTilesetAddr = PPU.OBJTilesetAddr;
				PPU.CurOBJSection->OBJGap = PPU.OBJGap;
				
				PPU.OBJDirty = 0;
			}
			

			u32 optChange = 0;
			for (i = 3; i >= 0; i--)
			{
				PPU_Background* bg = &PPU.BG[i];
				
				if(!PPU.ModeDirty)
				{
					if(!optChange)
					{
						if(i==2 && (PPU.CurModeSection->Mode & 0x7) > 0 && !(PPU.CurModeSection->Mode & 0x1))
						{
							if(bg->ScrollParams == bg->LastScrollParams && bg->GraphicsParams == bg->LastGraphicsParams && bg->Size == bg->LastSize)
								continue;
							else
								optChange = 1;
						}
						else
							if (bg->ScrollParams == bg->LastScrollParams && bg->GraphicsParams == bg->LastGraphicsParams && bg->Size == bg->LastSize)
								continue;
					}
				}
		
				bg->CurSection->EndOffset = line;
				bg->CurSection++;
				
				bg->CurSection->ScrollParams = bg->ScrollParams;
				bg->CurSection->GraphicsParams = bg->GraphicsParams;
				bg->CurSection->Size = bg->Size;
				
				bg->LastScrollParams = bg->ScrollParams;
				bg->LastGraphicsParams = bg->GraphicsParams;
				bg->LastSize = bg->Size;
			}

			PPU.ModeDirty = 0;
			
			if (PPU.Mode7Dirty && (PPU.Mode & 0x07) == 7)
			{
				PPU_Mode7Section *cur = PPU.CurMode7Section;
				if((cur->Sel != PPU.M7Sel) ||
				   (cur->AffineParams1 != PPU.M7AffineParams1) ||
				   (cur->AffineParams2 != PPU.M7AffineParams2) ||
				   (cur->RefParams != PPU.M7RefParams) ||
				   (cur->ScrollParams != PPU.M7ScrollParams))
				{
					cur->EndOffset = line;

					{
						if(Config.HardwareMode7 > 0)
						{
							int numLines = cur->EndOffset - cur->StartOffset;
							int sizeComp = (numLines == 1 ? 0x30000 : (numLines >= 16 ? 0x100000 : 0x80000));
							coord0 = PPU_StoreTileInCache(TILE_Mode7, 0, ((u32)PPU.VRAM[0]) << 7);
							if((cur->Sel >> 6) == 2)
								cur->doHW = 1;
							else if(((cur->Sel >> 6) == 3) && (coord0 == 0xC000))
							{
								cur->Sel &= 0xBF;
								cur->doHW = 1;
							}
							else if(((((int)cur->A * cur->A) + ((int)cur->C * cur->C)) <= sizeComp) && ((((int)cur->B * cur->B) + ((int)cur->D * cur->D)) <= sizeComp))
								cur->doHW = 1;
							else
								cur->doHW = 0;
						}
						else
							cur->doHW = 0;
					}

					cur = ++PPU.CurMode7Section;
					
					cur->StartOffset = line;
					cur->Sel = PPU.M7Sel;
					cur->AffineParams1 = PPU.M7AffineParams1;
					cur->AffineParams2 = PPU.M7AffineParams2;
					cur->RefParams = PPU.M7RefParams;
					cur->ScrollParams = PPU.M7ScrollParams;
				}
				PPU.Mode7Dirty = 0;
			}
			
			if (PPU.WindowDirty)
			{
				PPU.CurWindowSection->EndOffset = line;
				PPU.CurWindowSection++;
				
				PPU_ComputeWindows_Hard(&PPU.CurWindowSection->Window[0]);
				PPU.WindowDirty = 0;
			}

			if(PPU.MainBackdropDirty)
			{
				PPU.CurMainBackdrop->EndOffset = line;
				PPU.CurMainBackdrop++;
				
				PPU.CurMainBackdrop->Color = PPU.Palette[0];
				PPU.CurMainBackdrop->ColorMath2 = PPU.ColorMath2;
				PPU.MainBackdropDirty = 0;
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
		
		CurForcedBlank = PPU.ForcedBlank;
		
		if (PPU.ColorEffectDirty)
		{
			PPU.CurColorEffect->EndOffset = line;
			PPU.CurColorEffect++;
			
			PPU.CurColorEffect->ColorMath = (PPU.ColorMath2 & 0x80);
			PPU.CurColorEffect->Brightness = PPU.CurBrightness;
			PPU.ColorEffectDirty = 0;
		}
	}
}



#define RENDERBG(num, type, prio, opt, hi) \
	{ \
		if ((mode & (0x10<<num)) && (mode!=6)) \
			PPU_HardRenderBG_16x16(colormath&(1<<num), num, type, prio?0x2000:0, ystart, yend, hi); \
		else \
			PPU_HardRenderBG_8x8(colormath&(1<<num), num, type, prio?0x2000:0, ystart, yend, opt, hi); \
	}


void PPU_HardRender_Mode0(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x08) RENDERBG(3, TILE_2BPP, 0, 0, 0);
	if (screen & 0x04) RENDERBG(2, TILE_2BPP, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x08) RENDERBG(3, TILE_2BPP, 1, 0, 0);
	if (screen & 0x04) RENDERBG(2, TILE_2BPP, 1, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 0, 0, 0);
	if (screen & 0x01) RENDERBG(0, TILE_2BPP, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 1, 0, 0);
	if (screen & 0x01) RENDERBG(0, TILE_2BPP, 1, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode1(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x04) RENDERBG(2, TILE_2BPP, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x04) 
	{
		if (!(mode & 0x08))
			RENDERBG(2, TILE_2BPP, 1, 0, 0);
	}
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0, 0, 0);
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 1, 0, 0);
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 1, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
	
	if (screen & 0x04) 
	{
		if (mode & 0x08)
			RENDERBG(2, TILE_2BPP, 1, 0, 0);
	}
}

void PPU_HardRender_Mode2(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0, 2, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 2, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 1, 2, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 1, 2, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode3(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 1, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 1, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode4(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 0, 4, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 0, 4, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 1, 4, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 1, 4, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode5(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 0, 0, 1);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);

	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 0, 1);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);

	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 1, 0, 1);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);

	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 1, 0, 1);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);

}

void PPU_HardRender_Mode6(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);

	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 6, 1);

	if (screen & 0x10)
	{
		PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
		PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	}

	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 6, 1, 1);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode7(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	PPU_HardRenderBG_ProcessMode7(ystart, yend);

	if(PPU.M7ExtBG)
		if(screen & 0x02)
			PPU_HardRenderBG_Mode7(colormath&0x02, ystart, yend, 0);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);

	if (!PPU.M7ExtBG)
		if(screen & 0x01)
			PPU_HardRenderBG_Mode7(colormath&0x01, ystart, yend, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);

	if(PPU.M7ExtBG)
		if(screen & 0x02) 
			PPU_HardRenderBG_Mode7(colormath&0x02, ystart, yend, 1);

	if (screen & 0x10)
	{
		PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
		PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
	}
}

void PPU_HardRender(u32 snum)
{
	PPU_ModeSection* s = &PPU.ModeSections[0];
	int ystart = ScreenYStart;
	
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
				if (s->EndOffset >= ScreenYEnd) break;
				ystart = s->EndOffset;
				s++;
				continue;
			}
			
			screen = s->SubScreen;
			colormath = (s->ColorMath2 & 0x40) ? 0x00:0xFF;
		}
		
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

			case 5:
				PPU_HardRender_Mode5(ystart, s->EndOffset, screen, s->Mode, colormath);
				break;

			case 6:
				PPU_HardRender_Mode6(ystart, s->EndOffset, screen, s->Mode, colormath);
				break;

			case 7:
				PPU_HardRender_Mode7(ystart, s->EndOffset, screen, s->Mode, colormath);
				break;
		}
		
		if (s->EndOffset >= ScreenYEnd) break;
		ystart = s->EndOffset;
		s++;
	}
}


void PPU_HardRenderSection(u32 endline)
{
	PPU_FinishHardSection(endline);
	
	if (CurForcedBlank) return;
	
	if (FirstScreenSection)
	{
		SwapVertexBuf();
		vertexPtrStart = vertexPtr;
	}
	
	if (PPU.PaletteUpdateCount256 != PPU_LastPalUpdate256)
	{
		PPU_LastPalUpdate256 = PPU.PaletteUpdateCount256;
		PPU_PalHash256 = SuperFastPalHash((u8*)&PPU.HardPalette[0], 512);
		
		if (PPU.PaletteUpdateCount128 != PPU_LastPalUpdate128)
		{
			PPU_LastPalUpdate128 = PPU.PaletteUpdateCount128;
			PPU_PalHash128 = SuperFastPalHash((u8*)&PPU.HardPalette[0], 256);
			
			for (u32 i = 0; i < 32; i++)
			{
				if (PPU.PaletteUpdateCount[i] != PPU_LastPalUpdate[i])
					PPU_PalHash4[i] = SuperFastPalHash((u8*)&PPU.HardPalette[i << 2], 8);
			}
			
			for (u32 i = 0; i < 16; i++)
			{
				if (*(u32*)&PPU.PaletteUpdateCount[i << 2] != *(u32*)&PPU_LastPalUpdate[i << 2])
					PPU_PalHash16[i] = SuperFastPalHash((u8*)&PPU.HardPalette[i << 4], 32);
				
				*(u32*)&PPU_LastPalUpdate[i << 2] = *(u32*)&PPU.PaletteUpdateCount[i << 2];
			}
		}
	}
	
	PPU_ClearScreens();

	PPU_DrawWindowMask();

	// OBJ LAYER
	
	bglViewport(0, 0, 256, 256);
	bglUseShader(&hardRenderOBJShaderP);
	PPU_HardRenderOBJs();
	
	// MAIN SCREEN

	doingBG = 0;
	scissorY = 256;
	bglUseShader(&hardRenderShaderP);
	bglOutputBuffers(MainScreenTex, DepthBuffer, 256, 512);
	bglViewport(0, 256, 256, 256);
	PPU_HardRender(0);
	
	// SUB SCREEN

	doingBG = 0;
	scissorY = 0;
	bglViewport(0, 0, 256, 256);
	PPU_HardRender(1); 
	
	FirstScreenSection = 0;
}


void PPU_VBlank_Hard(int endLine)
{
	PPU.CurColorEffect->EndOffset = endLine;
	
	PPU_HardRenderSection(endLine);
	
	// just in case nothing was rendered
	if (FirstScreenSection)
	{
		SwapVertexBuf();
		vertexPtrStart = vertexPtr;
	}
	
	PPU_ClearAlpha();

	// reuse the color math system used by the soft renderer
	bglEnableStencilTest(false);
	bglScissorMode(GPU_SCISSOR_DISABLE);
	PPU_BlendScreens(GPU_RGBA8);

	u32 taken = ((u32)vertexPtr - (u32)vertexPtrStart);
	GSPGPU_FlushDataCache(vertexPtrStart, taken);
	if (taken > 0x200000)
		bprintf("OVERFLOW %06X/200000 (%d%%)\n", taken, (taken*100)/0x200000);
	
	u32 rgnstart = 0;
	u32 rgnsize = 0;
	for (u32 i = 0; i < TC_NUM_CHUNKS; i++)
	{
		if (!PPU_TileChunkDirty[i])
		{
			rgnsize = 0;
			continue;
		}
		
		PPU_TileChunkDirty[i] = 0;
		
		if (!rgnsize) rgnstart = i << TC_CHUNK_SHIFT;
		rgnsize += TC_CHUNK_SIZE;
		
		if ((i == (TC_NUM_CHUNKS-1)) || (!PPU_TileChunkDirty[i+1]))
		{
			GSPGPU_FlushDataCache(&PPU_TileCacheRAM[rgnstart>>1], rgnsize);
			GX_RequestDma((u32*)&PPU_TileCacheRAM[rgnstart>>1], (u32*)&PPU_TileCache[rgnstart>>1], rgnsize);
		}
	}
	
	GSPGPU_FlushDataCache(Mode7ColorBuffer, 256*512*sizeof(u16));
}

