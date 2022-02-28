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


extern shaderProgram_s hardRenderShaderP;
extern shaderProgram_s hard7RenderShaderP;
extern shaderProgram_s hardRenderOBJShaderP;
extern shaderProgram_s plainQuadShaderP;
extern shaderProgram_s windowMaskShaderP;

extern u32* SNESFrame;
extern u16* MainScreenTex;
extern u16* SubScreenTex;

u32* DepthBuffer;

u32* OBJColorBuffer;
u32* OBJPrioBuffer;

u32 YOffset256[256];

u8 FirstScreenSection;
u8 ScreenYStart, ScreenYEnd;

int doingBG = 0;
int scissorY = 0; // gross hack

u8 OBJLayerUsed[4];


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


u16* PPU_M7Cache;
u16* PPU_M7Layer;
u16* PPU_M7LayerExt;
u16* PPU_M7VertBuf;
u32 PPU_M7SecFlg[4];
u32 PPU_M7TileFlg[256];
u16 PPU_M7TileCnt[256*4];
u16 PPU_M7PalUpdate;

typedef struct
{
	float A;
	float B;
} M7Ratios;

bool curM7 = false, lastM7 = false;
u32 updM7Sec = 0;


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
u16 PPU_LastPalUpdate256;

u32 PPU_PalHash4[32];  // first half of palette
u32 PPU_PalHash16[16]; // entire palette, including OBJ (second half)
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
	
	PPU_M7Cache = (u16*)linearAlloc(128*256*sizeof(u16));
	PPU_M7Layer = (u16*)VRAM_Alloc(1024*1024*sizeof(u16));
	PPU_M7LayerExt = (u16*)linearAlloc(1024*1024*sizeof(u16));
	PPU_M7VertBuf = (u16*)linearAlloc(128*128*6*sizeof(u16));

	for(i = 0; i < 128*128; i++)
	{
		PPU_M7VertBuf[i * 6] = (i & 0x3F) << 3;
		PPU_M7VertBuf[(i * 6) + 1] = (i & 0xFC0) >> 3;
		PPU_M7VertBuf[(i * 6) + 3] = ((i & 0x3F) << 3) + 8;
		PPU_M7VertBuf[(i * 6) + 4] = ((i & 0xFC0) >> 3) + 8;
	}
	
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
	PPU_LastPalUpdate256 = 0;
	
	for (i = 0; i < 32; i++) PPU_PalHash4[i] = 0;
	for (i = 0; i < 16; i++) PPU_PalHash16[i] = 0;
	PPU_PalHash256 = 0;
		
	for (i = 0; i < 256; i++)
	{
		u32 y = (i & 0x1) << 1;
		y    |= (i & 0x2) << 2;
		y    |= (i & 0x4) << 3;
		y    |= (i & ~0x7) << 8;
		
		YOffset256[i] = y;
	}
	
	PPU_ConvertVRAMAll();
}

void PPU_DeInit_Hard()
{
	linearFree(PPU_M7VertBuf);
	linearFree(PPU_M7LayerExt);
	VRAM_Free(PPU_M7Layer);
	linearFree(PPU_M7Cache);
	
	VRAM_Free(PPU_TileCache);
	linearFree(PPU_TileCacheRAM);
	
	VRAM_Free(MainScreenTex);
	VRAM_Free(DepthBuffer);
	VRAM_Free(OBJColorBuffer);
	VRAM_Free(OBJPrioBuffer);
}

void PPU_Reset_Hard()
{
	curM7 = lastM7 = false;
	updM7Sec = 0;

	PPU_ConvertVRAMAll();
}


// tile decoding
// note: tiles are directly converted to PICA200 tiles (zcurve)

inline int bitcount(u32 val)
{
	val = val - ((val >> 1) & 0x55555555);                    // reuse input as temporary
	val = (val & 0x33333333) + ((val >> 2) & 0x33333333);     // temp
	return (((val + (val >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24; // count
}

void PPU_ConvertVRAM8(u32 addr, u8 val)
{
	const u32 xincr[4] = {2, 6, 2, 22};
	const u32 xpos[8] = {0, 1, 4, 5, 16, 17, 20, 21};
	const u32 ystart[8] = {42, 40, 34, 32, 10, 8, 2, 0};
	int i, yStart = (addr & 0xF) >> 1;
	u32 group[4];

	PPU.TileEmpty[addr >> 4] += (bitcount(val) - bitcount(PPU.VRAM[addr]));

	group[0] = ((val & 0x40) << 2) | ((val & 0x80) >> 7);
	group[1] = ((val & 0x10) << 4) | ((val & 0x20) >> 5);
	group[2] = ((val & 0x04) << 6) | ((val & 0x08) >> 3);
	group[3] = ((val & 0x01) << 8) | ((val & 0x02) >> 1);
	
	u16* base = (u16*)&PPU.TileBitmap[ystart[yStart]];
	u16 *bit2 = &base[((addr & ~0x0F) << 1) + 0x00000];
	u16 *bit4 = &base[ (addr & ~0x1F)       + 0x20000];
	u16 *bit8 = &base[((addr & ~0x3F) >> 1) + 0x30000];
	
	u16 shift2 = (addr & 0x1), shift4 = shift2 + ((addr & 0x10) >> 3), shift8 = shift4 + ((addr & 0x20) >> 3);
	u16 mask2 = ~(0x101 << shift2), mask4 = ~(0x101 << shift4), mask8 = ~(0x101 << shift8);
	
	for (i = 0; i < 4; i++)
	{
		*bit2 = (*bit2 & mask2) | (group[i] << shift2);
		*bit4 = (*bit4 & mask4) | (group[i] << shift4);
		*bit8 = (*bit8 & mask8) | (group[i] << shift8);
		bit2 += xincr[i]; bit4 += xincr[i]; bit8 += xincr[i];
	}
	
	if (addr < 0x8000)
	{
		if (addr & 0x1)
		{
			PPU.TileBitmap[0x70000 + ((addr >> 7) << 6) + ystart[(addr & 0x7F) >> 4] + xpos[yStart]] = val;
			PPU_M7TileFlg[addr >> 7] = 1;
		}
		else
		{
			u32 addrSec = ((addr & 0x80) >> 7) | ((addr & 0x4000) >> 13);
			u32 addrVert = ((((addr & 0x7E) >> 1) | ((addr & 0x3F00) >> 2) | (addrSec << 12)) * 6);
			u32 vertVal = (val & 0xF) | ((0xF0 - (val & 0xF0)) << 4);
			PPU_M7VertBuf[addrVert + 2] = vertVal;
			PPU_M7VertBuf[addrVert + 5] = vertVal + 0x0101;
			PPU_M7TileCnt[PPU.VRAM[addr] | (addrSec << 8)]--;
			PPU_M7TileCnt[val + (addrSec << 8)]++;
			PPU_M7SecFlg[addrSec] = 1;
		}
	}
	
	PPU.VRAMUpdateCount[addr >> 4]++;
}

void PPU_ConvertVRAM16(u32 addr, u16 val)
{
	const u32 xincr[4] = {2, 6, 2, 22};
	const u32 xpos[8] = {0, 1, 4, 5, 16, 17, 20, 21};
	const u32 ystart[8] = {42, 40, 34, 32, 10, 8, 2, 0};
	int i, yStart = (addr & 0xF) >> 1;
	u32 group[4];

	PPU.TileEmpty[addr >> 4] += (bitcount(val) - bitcount(*(u16*)&PPU.VRAM[addr]));

	group[0] = ((val & 0x0040) << 2) | ((val & 0x0080) >> 7) | ((val & 0x4000) >> 5) | ((val & 0x8000) >> 14);
	group[1] = ((val & 0x0010) << 4) | ((val & 0x0020) >> 5) | ((val & 0x1000) >> 3) | ((val & 0x2000) >> 12);
	group[2] = ((val & 0x0004) << 6) | ((val & 0x0008) >> 3) | ((val & 0x0400) >> 1) | ((val & 0x0800) >> 10);
	group[3] = ((val & 0x0001) << 8) | ((val & 0x0002) >> 1) | ((val & 0x0100) << 1) | ((val & 0x0200) >> 8);
	
	u16* base = (u16*)&PPU.TileBitmap[ystart[yStart]];
	u16 *bit2 = &base[((addr & ~0x0F) << 1) + 0x00000];
	u16 *bit4 = &base[ (addr & ~0x1F)       + 0x20000];
	u16 *bit8 = &base[((addr & ~0x3F) >> 1) + 0x30000];

	u16 shift4 = ((addr & 0x10) >> 3), shift8 = shift4 + ((addr & 0x20) >> 3);
	u16 mask4 = ~(0x303 << shift4), mask8 = ~(0x303 << shift8);
	
	for (i = 0; i < 4; i++)
	{
		*bit2 = group[i];
		*bit4 = (*bit4 & mask4) | (group[i] << shift4);
		*bit8 = (*bit8 & mask8) | (group[i] << shift8);
		bit2 += xincr[i]; bit4 += xincr[i]; bit8 += xincr[i]; 
	}
	
	
	if (addr < 0x8000)
	{
		u32 addrSec = ((addr & 0x80) >> 7) | ((addr & 0x4000) >> 13);
		u32 addrVert = ((((addr & 0x7E) >> 1) | ((addr & 0x3F00) >> 2) | (addrSec << 12)) * 6);
		u32 vertVal = (val & 0xF) | ((0xF0 - (val & 0xF0)) << 4);
		PPU_M7VertBuf[addrVert + 2] = vertVal;
		PPU_M7VertBuf[addrVert + 5] = vertVal + 0x0101;
		PPU_M7TileCnt[PPU.VRAM[addr] | (addrSec << 8)]--;
		PPU_M7TileCnt[(val & 0xFF) + (addrSec << 8)]++;
		PPU_M7SecFlg[addrSec] = 1;
		
		PPU.TileBitmap[((addr >> 7) << 6) + ystart[(addr & 0x7F) >> 4] + xpos[yStart] + 0x70000] = (val >> 8);
		PPU_M7TileFlg[addr >> 7] = 1;
	}
	
	PPU.VRAMUpdateCount[addr >> 4]++;
}

void PPU_ConvertVRAMAll()
{
	int i;
	u16 *ptr = (u16*)&PPU.VRAM[0];
	memset(PPU.TileEmpty, 0, 0x1000);
	for (i = 0; i < 0x10000; i += 2)
	{
		PPU.TileEmpty[i >> 4] += bitcount(*ptr);
		PPU_ConvertVRAM16(i, *ptr++);
	}
	PPU_M7PalUpdate = PPU.PaletteUpdateCount256;
}

void PPU_DecodeTile(u8* src, u16* pal, u16* dst)
{
	int i;
	u16 oldcolor0 = pal[0];
	u32* dst32 = (u32*)dst;
	pal[0] = 0;
	for (i = 0; i < 64; i += 2)
		*dst32++ = pal[src[i]] | (pal[src[i+1]] << 16);
	pal[0] = oldcolor0;
}

void PPU_DecodeTileExt(u8* src, u16* pal, u16* dst)
{
	int i;
	u16 oldcolor0 = pal[0];
	pal[0] = 0;
	for (i = 0; i < 64; i++)
	{
		if (src[i] & 0x80)
			*dst++ = pal[src[i] & 0x7F];
		else
			*dst++ = 0;
	}
	pal[0] = oldcolor0;
}

u32 PPU_StoreTileInCache(u32 type, u32 palid, u32 addr)
{
	u32 key;
	u32 palhash = 0;
	u32 vramdirty = 0;
	u32 nonzero = 0;
	u32 isnew = 0;
	u8 *bitmap;
	u16 *pal;
	
	switch (type)
	{
		case TILE_2BPP: 
			palhash = PPU_PalHash4[palid];
			vramdirty = PPU.VRAMUpdateCount[addr >> 4];
			bitmap = &PPU.TileBitmap[0x00000 + ((addr >> 4) << 6)];
			pal = &PPU.HardPalette[palid << 2];
			nonzero = PPU.TileEmpty[addr >> 4];
			break;
			
		case TILE_4BPP: 
			palhash = PPU_PalHash16[palid];
			vramdirty = *(u16*)&PPU.VRAMUpdateCount[addr >> 4];
			bitmap = &PPU.TileBitmap[0x40000 + ((addr >> 5) << 6)];
			pal = &PPU.HardPalette[palid << 4];
			nonzero = *((u16*)&PPU.TileEmpty[addr >> 4]);
			break;
			
		case TILE_8BPP: 
			palhash = PPU_PalHash256;
			vramdirty = *(u32*)&PPU.VRAMUpdateCount[addr >> 4];
			bitmap = &PPU.TileBitmap[0x60000 + ((addr >> 6) << 6)];
			pal = &PPU.HardPalette[0];
			nonzero = *((u32*)&PPU.TileEmpty[addr >> 4]);
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
		
		PPU_DecodeTile(bitmap, pal, &PPU_TileCacheRAM[tileidx * 64]);
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
	
	SET_UNIFORM(GPU_VERTEX_SHADER, 4, 1.0f/128.0f, 1.0f/128.0f, 2.0f/256.0f, 1.0f);
	
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
	bglOutputBuffers(MainScreenTex, NULL, GPU_RGBA8, 256, 512);
	
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
		bglOutputBuffers(OBJColorBuffer, OBJPrioBuffer, GPU_RGBA8, 256, 256);
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

	
	bglOutputBuffers(DepthBuffer, NULL, GPU_RGBA8, 256, 512);
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


void PPU_HardRenderBG_8x8(u32 setalpha, u32 num, int type, u32 palbase, u32 prio, int ystart, int yend, u32 opt, u32 hi)
{
	PPU_Background* bg = &PPU.BG[num];
	PPU_Background* obg = &PPU.BG[2];
	u16* tilemap;
	u16* tilemapx = NULL;
	u16* tilemapy = NULL;
	int tileaddrshift = ((int[]){4, 5, 6})[type];
	u32 xoff, yoff, oxoff = 0, oyoff;
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
	PPU_BGSection* o = NULL;
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
			tilemapx = (u16*)(PPU.VRAM + o->TilemapOffset + ((oyoff & 0xF8) << 3));
			tilemapy = (u16*)(PPU.VRAM + o->TilemapOffset + (((oyoff + 8) & 0xF8) << 3));
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

			tilemap = (u16*)(PPU.VRAM + s->TilemapOffset + ((yoff & 0xF8) << 3));
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
					tilemap = (u16*)(PPU.VRAM + s->TilemapOffset + ((vofs & 0xF8) << 3));
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
				u32 palid = ((curtile & 0x1C00) >> 10) + palbase;
				
				u32 coord0 = PPU_StoreTileInCache(type, palid, addr);
#define DO_SUBTILE(sx, sy, coord, t0, t3) \
						if (coord != 0xC000) \
						{ \
							ADDVERTEX(x+sx,       yf+sy,       coord+t0); \
							ADDVERTEX(x+sx+xsize, yf+sy+ysize, coord+t3); \
							ntiles++; \
						}				
				if (hi)
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

void PPU_HardRenderBG_16x16(u32 setalpha, u32 num, int type, u32 palbase, u32 prio, int ystart, int yend, u32 hi)
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
			tilemap = (u16*)(PPU.VRAM + s->TilemapOffset + ((yoff & 0x1F0) << 2));
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
				u32 palid = ((curtile & 0x1C00) >> 10) + palbase;
				
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


// These 2 functions serve the purpose of line clipping, and are based on the Cohen-Sutherland algorithm
u32 ComputeOutCode(float x, float y)
{
	u32 code = 0;
	if(x < 0.0f)
		code |= 1;
	else if(x > 1024.0f)
		code |= 2;
	if(y < 0.0f)
		code |= 4;
	else if(y > 1024.0f)
		code |= 8;

	return code;
}

M7Ratios FindIntersectPoints(float x0, float y0, float x1, float y1)
{
	M7Ratios points = {0.0f, 1.0f};
	float x2 = x0, y2 = y0, x3 = x1, y3 = y1;

	u32 outcode0 = ComputeOutCode(x2, y2);
	u32 outcode1 = ComputeOutCode(x3, y3);

	if(!(outcode0 | outcode1))
	{
		return points;
	}
	else if(outcode0 & outcode1)
	{
		points.A = 1.0f;
		points.B = 0.0f;
		return points;
	}

	bool accept = false;


	while(true)
	{
		float x, y;

		u32 outcodeOut = outcode0 ? outcode0 : outcode1;

		// (0,0)-(1024,1024) is the min/max of the Mode 7 layer, so we adjust by that, since all we want is the inside
		if(outcodeOut & 8)
		{
			x = x2 + (x3 - x2) * (1024.0f - y2) / (y3 - y2);
			y = 1024.0f;
		}
		else if(outcodeOut & 4)
		{
			x = x2 + (x3 - x2) * (0.0f - y2) / (y3 - y2);
			y = 0.0f;
		}
		else if(outcodeOut & 2)
		{
			y = y2 + (y3 - y2) * (1024.0f - x2) / (x3 - x2);
			x = 1024.0f;
		}
		else
		{
			y = y2 + (y3 - y2) * (0.0f - x2) / (x3 - x2);
			x = 0.0f;
		}

		if(outcodeOut == outcode0)
		{
			x2 = x;
			y2 = y;
			outcode0 = ComputeOutCode(x2, y2);
		}
		else
		{
			x3 = x;
			y3 = y;
			outcode1 = ComputeOutCode(x3, y3);
		}

		if(!(outcode0 | outcode1))
		{
			accept = true;
			break;
		}
		else if(outcode0 & outcode1)
			break;		
	}

	if(accept)
	{
		if((x1 - x0) != 0.0f)
		{
			points.A = (x2 - x0) / (x1 - x0);
			points.B = (x3 - x0) / (x1 - x0);
		}
		else
		{
			points.A = (y2 - y0) / (y1 - y0);
			points.B = (y3 - y0) / (y1 - y0);
		}
	}
	else
	{
		points.A = 1.0f;
		points.B = 0.0f;
	}
	
	return points;
}

float snesProjMatrix[16] = 
{
	2.0f/256.0f, 0, 0, -1,
	0, 2.0f/256.0f, 0, -1,
	0, 0, 1.0f/128.0f, -1,
	0, 0, 0, 1
};

void PPU_HardRenderBG_Mode7(u32 setalpha, u32 num, int ystart, int yend, u32 prio)
{
	int systart = 1, syend;
	float sx, sy, ex, ey;
	float A, B, C, D;
	int i;
	u32 hflip, vflip, mode = 0;
	u32 filter = Config.HardwareMode7Filter ? 0x6 : 0x0;

	int nlines = 0;
	float* vptr = (float*)vertexPtr;


#define ADDVERTEX(x, y, s, t) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = s; \
	*vptr++ = t;
	

	PPU_Mode7Section* s = &PPU.Mode7Sections[0];
	PPU_Mode7Section* found = NULL;
	for(;;)
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

		if(prio)
		{
			found = s;
			break;
		}
		else
		{
			if(!found)
			{
				found = s;
				found->vertexStart = (float*)vertexPtr;
				found->vertexLen = 0;
			}
		}

		hflip = s->Sel & 0x1 ? 0xFF : 0x0;
		vflip = s->Sel & 0x2 ? 0xFF : 0x0;
		mode = s->Sel >> 6;

		A = (float)(hflip ? -s->A : s->A); B = (float)(vflip ? -s->B : s->B) / 256.0f; C = (float)(hflip ? -s->C : s->C); D = (float)(vflip ? -s->D : s->D) / 256.0f;

		// Going straight to float caused glitches, so casted to signed int, then to float
		sx = (float)((s32)((s->A * (hflip+s->XScroll-s->RefX)) + (s->B * ((systart^vflip)+s->YScroll-s->RefY)) + (s->RefX << 8))) / 256.0f;
		sy = (float)((s32)((s->C * (hflip+s->XScroll-s->RefX)) + (s->D * ((systart^vflip)+s->YScroll-s->RefY)) + (s->RefY << 8))) / 256.0f;

		for (i = systart; i < syend; i++)
		{
			ex = sx + A;
			ey = sy + C;
			ADDVERTEX(0.0f, i, sx, sy);
			if(mode < 3)
			{
				ADDVERTEX(0.0f, 1.0f, ex, ey);
			}
			else
			{
				M7Ratios points = FindIntersectPoints(sx, sy, ex, ey);
				ADDVERTEX(points.A, points.B, ex, ey);
			}
			sx += B;
			sy += D;
		}

		nlines += (syend - systart);
		
		if (syend >= yend) break;
		systart = syend;
		s++;
	}

	if(!prio)
	{
		found->vertexLen = nlines;
		vptr = (float*)((((u32)vptr) + 0x1F) & ~0x1F);
		vertexPtr = vptr;
	}

	if(found->vertexLen)
	{
		doingBG = 0;

		bglUseShader(&hard7RenderShaderP);

		//bglOutputBufferAccess(0, 1, 1, 0);	// Is writing to color buffer, but does need to read from the stencil

		bglEnableStencilTest(true);
		bglStencilFunc(GPU_EQUAL, 0x00, 1 << num, 0xFF);
		bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
		bglEnableDepthTest(false);
		bglEnableAlphaTest(true);
		bglAlphaFunc(GPU_GREATER, 0);
	
		bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
		bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
		bglScissorMode(GPU_SCISSOR_DISABLE);
			
		bglColorDepthMask(GPU_WRITE_COLOR);

		bglUniformMatrix(GPU_GEOMETRY_SHADER, 0, snesProjMatrix);
	
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
				
		bglNumAttribs(2);
		bglAttribType(0, GPU_FLOAT, 2);	// vertex
		bglAttribType(1, GPU_FLOAT, 2);	// texcoord

		SET_UNIFORM(GPU_VERTEX_SHADER, 4, 1.0f/1024.0f, 1.0f/1024.0f, 1.0f, 1.0f);
		SET_UNIFORM(GPU_GEOMETRY_SHADER, 5, 0.0, 0.0f, 0.0f, 0.0f);
		bglTexImage(GPU_TEXUNIT0, (prio ? PPU_M7LayerExt : PPU_M7Layer), 1024, 1024, ((found->Sel >> 6) != 2 ? 0x2200 : 0x1100) | filter, GPU_RGBA5551);
		bglAttribBuffer(found->vertexStart);
		bglDrawArrays(GPU_GEOMETRY_PRIM, found->vertexLen*2);
		
		if(mode == 3)
		{
			// Prep for Tile0 enclosure
			SET_UNIFORM(GPU_VERTEX_SHADER, 4, 1.0f/8.0f, 1.0f/8.0f, 1.0f, 1.0f);
			SET_UNIFORM(GPU_GEOMETRY_SHADER, 5, 1.0, 0.0f, 0.0f, 0.0f);
			bglTexImage(GPU_TEXUNIT0, PPU_M7Cache + (prio ? 128*128 : 0), 8, 8, 0x2200 | filter, GPU_RGBA5551);
			bglAttribBuffer(found->vertexStart);
			bglDrawArrays(GPU_GEOMETRY_PRIM, found->vertexLen*2);
		}
	}
	
#undef ADDVERTEX
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
	
	if (ntiles) OBJLayerUsed[(oam[3] >> 4) & 0x3] = 1;
	
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
	
	*(u32*)&OBJLayerUsed[0] = 0;
	
	bglOutputBuffers(OBJColorBuffer, OBJPrioBuffer, GPU_RGBA8, 256, 256);
			
	bglScissorMode(GPU_SCISSOR_NORMAL);
		
	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(true);
	bglAlphaFunc(GPU_GREATER, 0);
	
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglColorDepthMask(GPU_WRITE_ALL);
	
	SET_UNIFORM(GPU_VERTEX_SHADER, 4, 1.0f/128.0f, 1.0f/128.0f, 2.0f/256.0f, 1.0f);
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
	
	if (!OBJLayerUsed[prio >> 4])
		return;
	
#define ADDVERTEX(x, y, z, s, t) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = z; \
	*vptr++ = s; \
	*vptr++ = t;
	
	doingBG = 0;
	
	bglUseShader(&hardRenderShaderP);
	
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
	
	SET_UNIFORM(GPU_VERTEX_SHADER, 4, 1.0f/256.0f, 1.0f/256.0f, 2.0f/256.0f, 1.0f);
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


void PPU_UpdateMode7()
{
	int i, j;

	if((PPU_M7PalUpdate != PPU.PaletteUpdateCount256) || !lastM7)
	{
		//Update all tiles, then start updating all layer sections
		for(i = 0; i < 256; i++)
			PPU_DecodeTile(&PPU.TileBitmap[0x70000 + (i * 64)], &PPU.HardPalette[0], &PPU_M7Cache[i * 64]);
		if(PPU.M7ExtBG)
		{
			for(i = 0; i < 256; i++)
				PPU_DecodeTileExt(&PPU.TileBitmap[0x70000 + (i * 64)], &PPU.HardPalette[0], &PPU_M7Cache[(i * 64) + 128*128]);
		}
		for(i = 0; i < 4; i++)
			PPU_M7SecFlg[i] = 1;
		PPU_M7PalUpdate = PPU.PaletteUpdateCount256;
	}
	else
	{
		//Examine each tile, update as necessary, then start updating all necessary layer sections
		for(i = 0; i < 256; i++)
		{
			if(PPU_M7TileFlg[i])
			{
				PPU_DecodeTile(&PPU.TileBitmap[0x70000 + (i << 6)], &PPU.HardPalette[0], &PPU_M7Cache[i << 6]);
				if(PPU.M7ExtBG)
					PPU_DecodeTileExt(&PPU.TileBitmap[0x70000 + (i << 6)], &PPU.HardPalette[0], &PPU_M7Cache[(i << 6) + 128*128]);
				for(j = 0; j < 4; j++)
				{
					if(PPU_M7TileCnt[i + (j << 8)])
						PPU_M7SecFlg[j] = 1;
				}
				PPU_M7TileFlg[i] = 0;
			}
		}			
	}

	GSPGPU_FlushDataCache(PPU_M7Cache, 128*256*sizeof(u16));

	// Find a section, starting with the current one, to update
	u32 updCnt = (PPU.M7ExtBG ? 0x1 : 0x2);
	bool alreadySet = false;

	for(i = 0; i < 4; i++)
	{
		if(PPU_M7SecFlg[(updM7Sec + i) & 0x3])
		{
			u32 curM7Sec = (updM7Sec + i) & 0x3;

			if(!alreadySet)
			{
				doingBG = 0;

				
				bglUseShader(&hardRenderShaderP);

				//bglOutputBufferAccess(0, 1, 0, 0);

				bglEnableStencilTest(false);
				bglEnableDepthTest(false);
				bglDepthFunc(GPU_NEVER);
				bglEnableAlphaTest(false);
				bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
				bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
			
				bglScissorMode(GPU_SCISSOR_DISABLE);

				bglColorDepthMask(GPU_WRITE_COLOR);

				//bglUniformMatrix(GPU_VERTEX_SHADER, hardRenderUniforms[0], mode7ProjMatrix);
				SET_UNIFORM(GPU_VERTEX_SHADER, 4, 1.0f/16.0f, 1.0f/16.0f, 2.0f/512.0f, 1.0f);

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

				bglNumAttribs(2);
				bglAttribType(0, GPU_SHORT, 2);	// vertex
				bglAttribType(1, GPU_UNSIGNED_BYTE, 2);	// texcoord
				
				alreadySet = true;
			}
			GSPGPU_FlushDataCache(&PPU_M7VertBuf[curM7Sec * 0x6000], 0x6000 * 2);
			bglOutputBuffers(PPU_M7Layer, NULL, GPU_RGBA5551, 1024, 1024);
			bglViewport((curM7Sec & 0x1) * 512, (curM7Sec >> 1) * 512, 512, 512);
			bglTexImage(GPU_TEXUNIT0, PPU_M7Cache, 128, 128, 0, GPU_RGBA5551);
			bglAttribBuffer(&PPU_M7VertBuf[curM7Sec * 0x6000]);
			bglDrawArrays(GPU_GEOMETRY_PRIM, 0x1000 * 2);

			if(PPU.M7ExtBG)
			{
				bglOutputBuffers(PPU_M7LayerExt, NULL, GPU_RGBA5551, 1024, 1024);
				bglTexImage(GPU_TEXUNIT0, &PPU_M7Cache[128*128], 128, 128, 0, GPU_RGBA5551);
				bglDrawArrays(GPU_GEOMETRY_PRIM, 0x1000 * 2);
				updM7Sec = (curM7Sec + 1) & 0x3;
				break;
			}

			updCnt--;
			if(!updCnt)
			{
				updM7Sec = (curM7Sec + 1) & 0x3;
				break;
			}
		}
	}
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
	
	curM7 = ((PPU.Mode & 0x7) == 7);

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
	PPU.CurMode7Section->vertexStart = NULL;
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
		/*curM7 = true;
		if (PPU.Mode7Dirty)
		{
			PPU_Mode7Section *cur = PPU.CurMode7Section;
			if ((cur->Sel != PPU.M7Sel) ||
			   (cur->AffineParams1 != PPU.M7AffineParams1) ||
			   (cur->AffineParams2 != PPU.M7AffineParams2) ||
			   (cur->RefParams != PPU.M7RefParams) ||
			   (cur->ScrollParams != PPU.M7ScrollParams))
			{
				cur->EndOffset = line;
				cur = ++PPU.CurMode7Section;
			
				cur->vertexStart = NULL;
				cur->Sel = PPU.M7Sel;
				cur->AffineParams1 = PPU.M7AffineParams1;
				cur->AffineParams2 = PPU.M7AffineParams2;
				cur->RefParams = PPU.M7RefParams;
				cur->ScrollParams = PPU.M7ScrollParams;
			}
			PPU.Mode7Dirty = 0;
		}*/
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
			
			if ((PPU.Mode & 0x07) == 7)
			{
				curM7 = true;
				if (PPU.Mode7Dirty)
				{
					PPU_Mode7Section *cur = PPU.CurMode7Section;
					if ((cur->Sel != PPU.M7Sel) ||
					    (cur->AffineParams1 != PPU.M7AffineParams1) ||
					    (cur->AffineParams2 != PPU.M7AffineParams2) ||
					    (cur->RefParams != PPU.M7RefParams) ||
					    (cur->ScrollParams != PPU.M7ScrollParams))
					{
						cur->EndOffset = line;
						cur = ++PPU.CurMode7Section;
					
						cur->vertexStart = NULL;
						cur->Sel = PPU.M7Sel;
						cur->AffineParams1 = PPU.M7AffineParams1;
						cur->AffineParams2 = PPU.M7AffineParams2;
						cur->RefParams = PPU.M7RefParams;
						cur->ScrollParams = PPU.M7ScrollParams;
					}
					PPU.Mode7Dirty = 0;
				}
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



#define RENDERBG(num, type, palbase, prio, opt, hi) \
	{ \
		if ((mode & (0x10<<num)) && (mode!=6)) \
			PPU_HardRenderBG_16x16(colormath&(1<<num), num, type, palbase, prio?0x2000:0, ystart, yend, hi); \
		else \
			PPU_HardRenderBG_8x8(colormath&(1<<num), num, type, palbase, prio?0x2000:0, ystart, yend, opt, hi); \
	}


void PPU_HardRender_Mode0(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x08) RENDERBG(3, TILE_2BPP, 24, 0, 0, 0);
	if (screen & 0x04) RENDERBG(2, TILE_2BPP, 16, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x08) RENDERBG(3, TILE_2BPP, 24, 1, 0, 0);
	if (screen & 0x04) RENDERBG(2, TILE_2BPP, 16, 1, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 8, 0, 0, 0);
	if (screen & 0x01) RENDERBG(0, TILE_2BPP, 0, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 8, 1, 0, 0);
	if (screen & 0x01) RENDERBG(0, TILE_2BPP, 0, 1, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode1(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x04) RENDERBG(2, TILE_2BPP, 0, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x04) 
	{
		if (!(mode & 0x08))
			RENDERBG(2, TILE_2BPP, 0, 1, 0, 0);
	}
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0, 0, 0, 0);
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0, 1, 0, 0);
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 1, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
	
	if (screen & 0x04) 
	{
		if (mode & 0x08)
			RENDERBG(2, TILE_2BPP, 0, 1, 0, 0);
	}
}

void PPU_HardRender_Mode2(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0, 0, 2, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 0, 2, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0, 1, 2, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 1, 2, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode3(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 0, 0, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0, 1, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 0, 1, 0, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode4(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 0, 0, 4, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 0, 0, 4, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 0, 1, 4, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	
	if (screen & 0x01) RENDERBG(0, TILE_8BPP, 0, 1, 4, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode5(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 0, 0, 0, 1);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);

	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 0, 0, 1);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);

	if (screen & 0x02) RENDERBG(1, TILE_2BPP, 0, 1, 0, 1);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);

	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 1, 0, 1);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);

}

void PPU_HardRender_Mode6(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);

	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 0, 6, 1);

	if (screen & 0x10)
	{
		PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
		PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
	}

	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 1, 6, 1);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode7(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if(PPU.M7ExtBG)
		if(screen & 0x02)
			PPU_HardRenderBG_Mode7(colormath&0x02, 1, ystart, yend, 0);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);

	if (!PPU.M7ExtBG)
		if(screen & 0x01)
			PPU_HardRenderBG_Mode7(colormath&0x01, 0, ystart, yend, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);

	if(PPU.M7ExtBG)
		if(screen & 0x02) 
			PPU_HardRenderBG_Mode7(colormath&0x02, 1, ystart, yend, 1);

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
		vertexPtrStart = vertexPtr;
	}
	
	if (PPU.PaletteUpdateCount256 != PPU_LastPalUpdate256)
	{
		PPU_LastPalUpdate256 = PPU.PaletteUpdateCount256;
		PPU_PalHash256 = SuperFastPalHash((u8*)&PPU.HardPalette[0], 512);
		
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
	
	if (curM7)
		PPU_UpdateMode7();
	lastM7 = curM7;
	
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
	bglOutputBuffers(MainScreenTex, DepthBuffer, GPU_RGBA8, 256, 512);
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
}

