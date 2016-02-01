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

#include <limits.h>

#include "blargGL.h"
#include "mem.h"
#include "snes.h"
#include "ppu.h"
#include "ui_console.h"
#include "config.h"


#define SET_UNIFORM(t, n, v0, v1, v2, v3) \
	{ \
		float blarg[4] = {(v0), (v1), (v2), (v3)}; \
		bglUniform(t, n, blarg); \
	}

u8 curForcedBlank = 0;


extern void* vertexBuf;
extern void* vertexPtr;


extern shaderProgram_s hardRenderShaderP;
extern shaderProgram_s hard7RenderShaderP;
extern shaderProgram_s plainQuadShaderP;
extern shaderProgram_s windowMaskShaderP;

extern u8 finalUniforms[1];
extern u8 softRenderUniforms[1];
extern u8 hardRenderUniforms[2];
extern u8 hard7RenderUniforms[3];
extern u8 plainQuadUniforms[1];
extern u8 windowMaskUniforms[1];

extern float snesProjMatrix[16];
extern float mode7ProjMatrix[16];

extern u32* gpuOut;
extern u32* SNESFrame;
extern u16* MainScreenTex;
extern u16* SubScreenTex;

u32* OBJColorBuffer;
u32* OBJDepthBuffer;


u32 YOffset256[256];

//u16 TempPalette[256];
#define TempPalette PPU.Palette

int doingBG = 0;

u32 objUseLayer[8] = {0};

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

u16* PPU_LayerGroup;

typedef struct
{
	float A;
	float B;
} M7Ratios;

bool curM7 = false, lastM7 = false;
u32 updM7Sec = 0;

u16* PPU_TileCache;
u32 PPU_TileCacheIndex;

u16 PPU_TileCacheList[0x20000];
u32 PPU_TileCacheReverseList[16384];

u32 PPU_TileVRAMUpdate[0x20000];
u32 PPU_TilePalUpdate[0x20000];


void PPU_Init_Hard()
{
	int i;
	
	// main/sub screen buffers, RGBA8
	MainScreenTex = (u16*)vramAlloc(256*512*4);
	SubScreenTex = (u16*)&((u32*)MainScreenTex)[256*256];
	
	OBJColorBuffer = (u32*)vramAlloc(256*256*2);
	OBJDepthBuffer = (u32*)vramAlloc(256*256*4);
	
	PPU_TileCache = (u16*)linearAlloc(1024*1024*sizeof(u16));
	PPU_TileCacheIndex = 0;

	PPU_M7Cache = (u16*)linearAlloc(128*256*sizeof(u16));
	PPU_M7Layer = (u16*)vramAlloc(1024*1024*sizeof(u16));
	PPU_M7LayerExt = (u16*)linearAlloc(1024*1024*sizeof(u16));
	PPU_M7VertBuf = (u16*)linearAlloc(128*128*6*sizeof(u16));

	PPU_LayerGroup = (u16*)vramAlloc(256*256*8*sizeof(u16));

	for(i = 0; i < 128*128; i++)
	{
		PPU_M7VertBuf[i * 6] = (i & 0x3F) << 3;
		PPU_M7VertBuf[(i * 6) + 1] = (i & 0xFC0) >> 3;
		PPU_M7VertBuf[(i * 6) + 3] = ((i & 0x3F) << 3) + 8;
		PPU_M7VertBuf[(i * 6) + 4] = ((i & 0xFC0) >> 3) + 8;
	}
	
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
		
	for (i = 0; i < 256; i++)
	{
		u32 y = (i & 0x1) << 1;
		y    |= (i & 0x2) << 2;
		y    |= (i & 0x4) << 3;
		y    |= (i & ~0x7) << 8;
		
		YOffset256[i] = y;
	}

	PPU_Reset_Hard();

}

void PPU_DeInit_Hard()
{
	vramFree(PPU_LayerGroup);
	linearFree(PPU_M7VertBuf);
	linearFree(PPU_M7LayerExt);
	vramFree(PPU_M7Layer);
	linearFree(PPU_M7Cache);
	linearFree(PPU_TileCache);
	
	vramFree(MainScreenTex);
	vramFree(OBJColorBuffer);
	vramFree(OBJDepthBuffer);
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
	
	for(i = 0; i < 4; i++)
	{
		*bit2 = (*bit2 & mask2) | (group[i] << shift2);
		*bit4 = (*bit4 & mask4) | (group[i] << shift4);
		*bit8 = (*bit8 & mask8) | (group[i] << shift8);
		bit2 += xincr[i]; bit4 += xincr[i]; bit8 += xincr[i];
	}
	
	if(addr < 0x8000)
	{
		if(addr & 0x1)
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
	
	for(i = 0; i < 4; i++)
	{
		*bit2 = group[i];
		*bit4 = (*bit4 & mask4) | (group[i] << shift4);
		*bit8 = (*bit8 & mask8) | (group[i] << shift8);
		bit2 += xincr[i]; bit4 += xincr[i]; bit8 += xincr[i]; 
	}
	
	
	if(addr < 0x8000)
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
	for(i = 0; i < 0x10000; i += 2)
	{
		PPU.TileEmpty[i >> 4] += bitcount(*ptr);
		PPU_ConvertVRAM16(i, *ptr++);
	}
	PPU_M7PalUpdate = PPU.PaletteUpdateCount256;
}

void PPU_DecodeTile(u8* src, u16* pal, u16 *dst)
{
	int i;
	u16 oldcolor0 = pal[0];
	pal[0] = 0;
	for(i = 0; i < 64; i++)
		*dst++ = pal[src[i]] | (pal[src[i+1]] << 16);
	pal[0] = oldcolor0;
}

void PPU_DecodeTileExt(u8* src, u16* pal, u16 *dst)
{
	int i;
	u16 oldcolor0 = pal[0];
	pal[0] = 0;
	for(i = 0; i < 64; i++)
	{
		if(src[i] & 0x80)
			*dst++ = pal[src[i] & 0x7F];
		else
			*dst++ = 0;
	}
	pal[0] = oldcolor0;
}


u32 PPU_StoreTileInCache(u32 type, u32 palid, u32 addr)
{
	u32 key;
	u32 paldirty = 0;
	u32 vramdirty = 0;
	u32 nonzero = 0;
	u32 isnew = 0;
	u8 *bitmap;
	u16 *pal;
	
	switch (type)
	{
		case TILE_2BPP: 
			paldirty = PPU.PaletteUpdateCount[palid];
			vramdirty = PPU.VRAMUpdateCount[addr >> 4];
			bitmap = &PPU.TileBitmap[0x00000 + ((addr >> 4) << 6)];
			pal = &TempPalette[palid << 2];
			nonzero = PPU.TileEmpty[addr >> 4];
			break;
			
		case TILE_4BPP: 
			paldirty = *(u32*)&PPU.PaletteUpdateCount[palid << 2];
			vramdirty = *(u16*)&PPU.VRAMUpdateCount[addr >> 4];
			bitmap = &PPU.TileBitmap[0x40000 + ((addr >> 5) << 6)];
			pal = &TempPalette[palid << 4];
			nonzero = *((u16*)&PPU.TileEmpty[addr >> 4]);
			break;
			
		case TILE_8BPP: 
			paldirty = PPU.PaletteUpdateCount256;
			vramdirty = *(u32*)&PPU.VRAMUpdateCount[addr >> 4];
			bitmap = &PPU.TileBitmap[0x60000 + ((addr >> 6) << 6)];
			pal = &TempPalette[0];
			nonzero = *((u32*)&PPU.TileEmpty[addr >> 4]);
			break;
		
		default:
			bprintf("unknown tile type %lu\n", type);
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
	PPU_TilePalUpdate[key] = paldirty;
	
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
		
		PPU_DecodeTile(bitmap, pal, &PPU_TileCache[tileidx * 64]);
		
		// invalidate previous tile if need be
		u32 oldkey = PPU_TileCacheReverseList[tileidx];
		PPU_TileCacheReverseList[tileidx] = key;
		if (oldkey != key && oldkey != 0x80000000)
			PPU_TileCacheList[oldkey] = 0x8000;
	}
	
	return coord;
}


void PPU_ApplyPaletteChanges(u32 num, PPU_PaletteChange* changes)
{
	u32 i;
	
	for (i = 0; i < num; i++)
	{
		PPU.Palette[changes[i].Address] = changes[i].Color;
	}
}


void PPU_StartBG(u32 hi, u32 mode)
{
	if (doingBG) return;
	doingBG = 1;

	bglUseShader(&hardRenderShaderP);
	
	//bglEnableStencilTest(true);
	//bglStencilFunc(GPU_ALWAYS, 0x00, 0xFF, 0x02);
	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglDepthFunc(GPU_NEVER);
	bglEnableAlphaTest(true);
	bglAlphaFunc(GPU_GREATER, 0);
	
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);

	bglScissorMode(GPU_SCISSOR_NORMAL);
	
	bglColorDepthMask(GPU_WRITE_COLOR);

	bglEnableTextures(GPU_TEXUNIT0);
	
	bglUniformMatrix(GPU_VERTEX_SHADER, hardRenderUniforms[0], snesProjMatrix);
	SET_UNIFORM(GPU_VERTEX_SHADER, hardRenderUniforms[1], 1.0f/128.0f, 1.0f/128.0f, 1.0f, 1.0f);
	bglTexImage(GPU_TEXUNIT0, PPU_TileCache,1024,1024,(hi ? 0x6: 0),GPU_RGBA5551);
		
	bglDummyTexEnv(1);
	bglDummyTexEnv(2);
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
		
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 2);	// vertex
	bglAttribType(1, GPU_UNSIGNED_BYTE, 2);	// texcoord
}



void PPU_ClearMainScreen()
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

	bglOutputBuffers(0x2, 0x3, MainScreenTex, OBJDepthBuffer, 256, 256);

	bglOutputBufferAccess(0, 1, 1, 1);	// Only need to write to color and depth/stencil buffer, but that access setting is not supported. Including depth/stencil read will make it supported
	
	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglDepthFunc(GPU_NEVER);
	bglEnableAlphaTest(false);
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglColorDepthMask(GPU_WRITE_ALL);
	
	bglUniformMatrix(GPU_VERTEX_SHADER, plainQuadUniforms[0], snesProjMatrix);
	
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
	int ystart = 1;
	PPU_MainBackdropSection* s = &PPU.MainBackdropSections[0];
	for (;;)
	{
		u16 col = s->Color;
		u8 r = (col & 0xF800) >> 8; r |= (r >> 5);
		u8 g = (col & 0x07C0) >> 3; g |= (g >> 5);
		u8 b = (col & 0x003E) << 2; b |= (b >> 5);
		u8 alpha = (s->ColorMath2 & 0x20) ? 0xFF:0x80;
		ADDVERTEX(0, ystart, 0x80,      	r, g, b, alpha);
		ADDVERTEX(256, s->EndOffset, 0x80,  r, g, b, alpha);
		nvtx += 2;
		
		if (s->EndOffset >= 240) break;
		ystart = s->EndOffset;
		s++;
	}
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, nvtx);

	vptr = (u8*)((((u32)vptr) + 0x1F) & ~0x1F);
	
	// clear the OBJ buffer

	bglOutputBuffers(0x20000, 0x3, OBJColorBuffer, OBJDepthBuffer, 256, 256);

	bglOutputBufferAccess(0, 1, 0, 0);	// Only need to write to color buffer
	
	bglColorDepthMask(GPU_WRITE_COLOR);
	
	bglAttribBuffer(vptr);
		
	// Z here doesn't matter
	ADDVERTEX(0, 0, 0,      255, 0, 255, 0);
	ADDVERTEX(256, 256, 0,  255, 0, 255, 0);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, 2);

	vptr = (u8*)((((u32)vptr) + 0x1F) & ~0x1F);
	
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

	bglUseShader(&plainQuadShaderP);

	bglOutputBuffers(0x2, 0x3, SubScreenTex, OBJDepthBuffer, 256, 256);

	bglOutputBufferAccess(0, 1, 0, 0);	// Only need to write to color buffer

	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglDepthFunc(GPU_NEVER);
	bglEnableAlphaTest(false);
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglColorDepthMask(GPU_WRITE_COLOR);
	
	bglUniformMatrix(GPU_VERTEX_SHADER, plainQuadUniforms[0], snesProjMatrix);
	
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
	int ystart = 1;
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
		ADDVERTEX(256, s->EndOffset, 0x80,  r, g, b, alpha);
		nvtx += 2;
		
		if (s->EndOffset >= 240) break;
		ystart = s->EndOffset;
		s++;
	}
	
	vptr = (u8*)((((u32)vptr) + 0x1F) & ~0x1F);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, nvtx);
	
	vertexPtr = vptr;
	
#undef ADDVERTEX
}


void PPU_DrawWindowMask(u32 snum)
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
	
	bglOutputBuffers(0x2, 0x3, OBJDepthBuffer, OBJDepthBuffer, 256, 256);
	
	bglOutputBufferAccess(1, 1, 0, 0);	// Only need to write to color buffer, but because the color mask below is not full color (just red), we cannot disable color read

	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglDepthFunc(GPU_NEVER);
	bglEnableAlphaTest(false);
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglColorDepthMask(GPU_WRITE_RED);
	
	bglUniformMatrix(GPU_VERTEX_SHADER, windowMaskUniforms[0], snesProjMatrix);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 2);	// vertex
	bglAttribType(1, GPU_UNSIGNED_BYTE, 2);	// color
	bglAttribBuffer(vptr);
		
	int nvtx = 0;
	int ystart = 1;
	PPU_WindowSection* s = &PPU.WindowSections[0];
	for (;;)
	{
		int xstart = 0;
		PPU_WindowSegment* ws = &s->Window[0];
		for (;;)
		{
			if (xstart < ws->EndOffset)
			{
				u8 alpha = snum ? ws->FinalMaskSub : ws->FinalMaskMain;
				ADDVERTEX(xstart, ystart,       	    alpha);
				ADDVERTEX(ws->EndOffset, s->EndOffset,  alpha);
				nvtx += 2;
			}
			
			if (ws->EndOffset >= 256) break;
			xstart = ws->EndOffset;
			ws++;
		}
		
		if (s->EndOffset >= 240) break;
		ystart = s->EndOffset;
		s++;
	}
	
	vptr = (u8*)((((u32)vptr) + 0x1F) & ~0x1F);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, nvtx);
	
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


	bglUseShader(&plainQuadShaderP);

	bglOutputBufferAccess(1, 1, 0, 0);	// Only need to write to color buffer, but because the color mask below is not full color (just alpha), we cannot disable color read
	
	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglDepthFunc(GPU_NEVER);
	bglEnableAlphaTest(false);
	
	// transform alpha=128 into 0 and alpha=255 into 255
	// color blending mode doesn't matter since we only write alpha anyway
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_REVERSE_SUBTRACT);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE_MINUS_DST_ALPHA, GPU_ONE);
	
	bglScissorMode(GPU_SCISSOR_DISABLE);
	
	bglColorDepthMask(GPU_WRITE_ALPHA);
	
	bglUniformMatrix(GPU_VERTEX_SHADER, plainQuadUniforms[0], snesProjMatrix);
	
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
	ADDVERTEX(256, 256, 0,  255, 0, 255, 255);
	vptr = (u8*)((((u32)vptr) + 0x1F) & ~0x1F);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, 2);
	
	// clear alpha wherever the color math window applies
	if (!snum)
	{
		bglOutputBufferAccess(1, 1, 1, 0);	// Setting is as above, but we're including stencil reading, so we add that.
		bglEnableStencilTest(true);
		bglStencilFunc(GPU_EQUAL, 0x20, 0x20, 0xFF);
		
		bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
		bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ZERO, GPU_ZERO);
		
		bglDrawArrays(GPU_GEOMETRY_PRIM, 2);
	}

	vertexPtr = vptr;
	
#undef ADDVERTEX
}


void PPU_HardRenderBG_8x8(u32 setalpha, u32 num, int type, u32 pal, u32 prio, int ystart, int yend, u32 opt, u32 hi)
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

		o = &obg->Sections[0];
		oxoff = o->XScroll & 0xF8;
		oyoff = o->YScroll >> yshift;
		tilemapx = (u16 *)(PPU.VRAM + o->TilemapOffset + ((oyoff & 0xF8) << 3));
		tilemapy = (u16 *)(PPU.VRAM + o->TilemapOffset + (((oyoff + 8) & 0xF8) << 3));
		if(opt)
		{
			oyend = o->EndOffset;
			while(systart >= oyend)
			{
				o++;
				oyend = o->EndOffset;
			}
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

			tilemap = (u16 *)(PPU.VRAM + s->TilemapOffset + ((yoff & 0xF8) << 3));
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
					tilemap = (u16 *)(PPU.VRAM + s->TilemapOffset + ((vofs & 0xF8) << 3));
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
				u32 palid = ((curtile & 0x1C00) >> 10) + pal;
				
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
			PPU_StartBG(hi, 0);

			bglOutputBufferAccess(0, 1, 1, 0);		// Is writing to color buffer, but does need to read from the stencil
			
			bglScissor(0, systart, 256, syend);
			
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

void PPU_HardRenderBG_16x16(u32 setalpha, u32 num, int type, u32 pal, u32 prio, int ystart, int yend, u32 hi)
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
			tilemap = (u16 *)(PPU.VRAM + s->TilemapOffset + ((yoff & 0x1F0) << 2));
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
				u32 palid = ((curtile & 0x1C00) >> 10) + pal;
				
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
			PPU_StartBG(hi, 0);

			bglOutputBufferAccess(0, 1, 1, 0);	// Is writing to color buffer, but does need to read from the stencil
			
			bglScissor(0, systart, 256, syend);
			
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
		vptr = (float *)((((u32)vptr) + 0x1F) & ~0x1F);
		vertexPtr = vptr;
	}

	if(found->vertexLen)
	{
		doingBG = 0;

		bglUseShader(&hard7RenderShaderP);

		bglOutputBufferAccess(0, 1, 1, 0);	// Is writing to color buffer, but does need to read from the stencil

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

		bglUniformMatrix(GPU_GEOMETRY_SHADER, hard7RenderUniforms[1], snesProjMatrix);
	
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


		SET_UNIFORM(GPU_VERTEX_SHADER, hard7RenderUniforms[0], 1.0f/1024.0f, 1.0f/1024.0f, 1.0f, 1.0f);
		SET_UNIFORM(GPU_GEOMETRY_SHADER, hard7RenderUniforms[2], 0.0, 0.0f, 0.0f, 0.0f);
		bglTexImage(GPU_TEXUNIT0, (prio ? PPU_M7LayerExt : PPU_M7Layer), 1024, 1024, ((found->Sel >> 6) != 2 ? 0x2200 : 0x1100) | filter, GPU_RGBA5551);
		bglAttribBuffer(found->vertexStart);
		bglDrawArrays(GPU_GEOMETRY_PRIM, found->vertexLen*2);
		
		if(mode == 3)
		{
			// Prep for Tile0 enclosure
			SET_UNIFORM(GPU_VERTEX_SHADER, hard7RenderUniforms[0], 1.0f/8.0f, 1.0f/8.0f, 1.0f, 1.0f);
			SET_UNIFORM(GPU_GEOMETRY_SHADER, hard7RenderUniforms[2], 1.0, 0.0f, 0.0f, 0.0f);
			bglTexImage(GPU_TEXUNIT0, PPU_M7Cache + (prio ? 128*128 : 0), 8, 8, 0x2200 | filter, GPU_RGBA5551);
			bglAttribBuffer(found->vertexStart);
			bglDrawArrays(GPU_GEOMETRY_PRIM, found->vertexLen*2);
		}
	}
	
#undef ADDVERTEX
}


int PPU_HardRenderOBJ(u8* oam, u32 oamextra, int y, int height, int ystart, int yend)
{
	s32 xoff;
	u16 attrib;
	u32 idx;
	s32 x;
	s32 width = (s32)PPU.CurOBJSecSel->OBJWidth[(oamextra & 0x2) >> 1];
	u32 palid, prio;
	int yincr = 8;
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
	if(attrib & 0x100)
		idx += PPU.CurOBJSecSel->OBJGap;
	
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
	if (palid < 12) prio += 0x40;
	
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
			
			u32 addr = PPU.CurOBJSecSel->OBJTilesetAddr + idx;
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
					ADDVERTEX(x,   y,     prio, coord);
					ADDVERTEX(x+8, y+yincr,   prio, coord+0x0101);
					break;
					
				case 0x4000: // hflip
					ADDVERTEX(x,   y,     prio, coord+0x0001);
					ADDVERTEX(x+8, y+yincr,   prio, coord+0x0100);
					break;
					
				case 0x8000: // vflip
					ADDVERTEX(x,   y,     prio, coord+0x0100);
					ADDVERTEX(x+8, y+yincr,   prio, coord+0x0001);
					break;
					
				case 0xC000: // hflip+vflip
					ADDVERTEX(x,   y,     prio, coord+0x0101);
					ADDVERTEX(x+8, y+yincr,   prio, coord);
					break;
			}
			
			ntiles++;
			
			idx += (attrib & 0x4000) ? -32:32;
		}
		
		idx = firstidx + ((attrib & 0x8000) ? -512:512);
		x = firstx;
	}
	
#undef ADDVERTEX
	
	if(ntiles)
		objUseLayer[(prio & 0x70) >> 4] = 1;

	vertexPtr = vptr;
	
	return ntiles;
}

void PPU_HardRenderOBJs()
{
	//bprintf("VBL - %d\n", PPU.FirstOBJ);
	int i = PPU.FirstOBJ, j;
	i--;
	if (i < 0) i = 127;
	int last = i;
	int ntiles = 0;
	int ystart = 1, yend = SNES_Status->ScreenHeight;
	void* vstart = vertexPtr;
	
	for(j = 0; j < 8; j++)
		objUseLayer[j] = 0;

	do
	{
		u8* oam = &PPU.OAM[i << 2];
		u8 oamextra = PPU.OAM[0x200 + (i >> 2)] >> ((i & 0x03) << 1);
		s32 oy = (s32)oam[1] + 1;

		PPU.CurOBJSecSel = &PPU.OBJSections[0];
		while(PPU.CurOBJSecSel->EndOffset < oy && PPU.CurOBJSecSel->EndOffset < 240)
			PPU.CurOBJSecSel++;
		s32 oh = (s32)PPU.CurOBJSecSel->OBJHeight[(oamextra & 0x2) >> 1];

		if ((oy+oh) > ystart && oy < yend)
		{
			ntiles += PPU_HardRenderOBJ(oam, oamextra, oy, oh, ystart, yend);
		}
		if (oy >= 192)
		{
			oy -= 0x100;
			if ((oy+oh) > ystart)
			{
				ntiles += PPU_HardRenderOBJ(oam, oamextra, oy, oh, ystart, yend);
			}
		}

		i--;
		if (i < 0) i = 127;
	}
	while (i != last);
	if (!ntiles) return;
	
	bglUseShader(&hardRenderShaderP);
	
	bglOutputBuffers(0x20000, 0x3, OBJColorBuffer, OBJDepthBuffer, 256, 256);

	bglOutputBufferAccess(0, 1, 1, 1);	// Only need to write to color and depth/stencil buffer, but that access setting is not supported. Including depth/stencil read will make it valid
	
	bglScissorMode(GPU_SCISSOR_NORMAL);
		
	bglEnableStencilTest(false);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(false);
	bglEnableAlphaTest(true);
	bglAlphaFunc(GPU_GREATER, 0);
	
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglColorDepthMask(GPU_WRITE_ALL);
	
	bglUniformMatrix(GPU_VERTEX_SHADER, hardRenderUniforms[0], snesProjMatrix);
	SET_UNIFORM(GPU_VERTEX_SHADER, hardRenderUniforms[1], 1.0f/128.0f, 1.0f/128.0f, 1.0f, 1.0f);
	//SET_UNIFORM(GPU_VERTEX_SHADER, 5, 1.0f, 0.0f, 0.0f, 0.0f);
	
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
	
	vertexPtr = (u16*)((((u32)vertexPtr) + 0x1F) & ~0x1F);
	
	bglScissor(0, ystart, 256, yend);

	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 3);	// vertex
	bglAttribType(1, GPU_UNSIGNED_BYTE, 2);	// texcoord
	bglAttribBuffer(vstart);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, ntiles*2);
}

void PPU_HardRenderOBJLayer(u32 setalpha, u32 prio, int ystart, int yend)
{
	if(!(objUseLayer[(prio & 0x30) >> 4] | objUseLayer[((prio & 0x30) >> 4) + 4]))
		return;

	u16* vptr = (u16*)vertexPtr;
	
#define ADDVERTEX(x, y, z, s, t) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = z; \
	*vptr++ = s; \
	*vptr++ = t;
	
	doingBG = 0;

	bglUseShader(&hardRenderShaderP);

	bglOutputBufferAccess(0, 1, 1, 0);
	
	bglEnableStencilTest(true);
	bglStencilFunc(GPU_EQUAL, 0x00, 0x10, 0xFF);
	bglStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	
	bglEnableDepthTest(true);
	bglDepthFunc(GPU_EQUAL);
	bglEnableAlphaTest(true);
	bglAlphaFunc(GPU_GREATER, 0);
	
	bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
	bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	
	bglScissorMode(GPU_SCISSOR_DISABLE);
	
	bglColorDepthMask(GPU_WRITE_COLOR);
	
	bglUniformMatrix(GPU_VERTEX_SHADER, hardRenderUniforms[0], snesProjMatrix);
	SET_UNIFORM(GPU_VERTEX_SHADER, hardRenderUniforms[1], 1.0f/256.0f, 1.0f/256.0f, 1.0f, 1.0f);
	//SET_UNIFORM(GPU_VERTEX_SHADER, 5, 1.0f, 0.0f, 0.0f, 0.0f);
	
	bglEnableTextures(GPU_TEXUNIT0);
	
	bglDummyTexEnv(1);
	bglDummyTexEnv(2);
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
		
	bglTexImage(GPU_TEXUNIT0, OBJColorBuffer,256,256,0,GPU_RGBA5551);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_SHORT, 3);	// vertex
	bglAttribType(1, GPU_SHORT, 2);	// texcoord

	if(objUseLayer[(prio & 0x30) >> 4])
	{
		// set alpha to 128 if we need to disable color math in this section
		bglTexEnv(0, 
			GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
			GPU_TEVSOURCES(GPU_TEXTURE0, GPU_CONSTANT, 0),
			GPU_TEVOPERANDS(0,0,0), 
			GPU_TEVOPERANDS(0,0,0), 
			GPU_REPLACE, (setalpha&0x10) ? GPU_REPLACE:GPU_MODULATE, 
			(setalpha&0x10) ? 0xFFFFFFFF:0x80FFFFFF);

		bglAttribBuffer(vptr);

		ADDVERTEX(0, ystart,   prio,  0, ystart);
		ADDVERTEX(256, yend,   prio,  256, yend);
		vptr = (u16*)((((u32)vptr) + 0x1F) & ~0x1F);
	
		bglDrawArrays(GPU_GEOMETRY_PRIM, 2);
	}
	
	// sprites with no color math
	
	prio += 0x40;

	if(objUseLayer[(prio & 0x70) >> 4])
	{
		bglTexEnv(0, 
			GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
			GPU_TEVSOURCES(GPU_TEXTURE0, GPU_CONSTANT, 0),
			GPU_TEVOPERANDS(0,0,0), 
			GPU_TEVOPERANDS(0,0,0), 
			GPU_REPLACE, (setalpha&0x80) ? GPU_REPLACE:GPU_MODULATE, 
			(setalpha&0x80) ? 0xFFFFFFFF:0x80FFFFFF);

		bglAttribBuffer(vptr);
	
		ADDVERTEX(0, ystart,   prio,  0, ystart);
		ADDVERTEX(256, yend,   prio,  256, yend);
		vptr = (u16*)((((u32)vptr) + 0x1F) & ~0x1F);

		bglDrawArrays(GPU_GEOMETRY_PRIM, 2);
	}

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
			PPU_DecodeTile(&PPU.TileBitmap[0x70000 + (i * 64)], &TempPalette[0], &PPU_M7Cache[i * 64]);
		if(PPU.M7ExtBG)
		{
			for(i = 0; i < 256; i++)
				PPU_DecodeTileExt(&PPU.TileBitmap[0x70000 + (i * 64)], &TempPalette[0], &PPU_M7Cache[(i * 64) + 128*128]);
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
				PPU_DecodeTile(&PPU.TileBitmap[0x70000 + (i << 6)], &TempPalette[0], &PPU_M7Cache[i << 6]);
				if(PPU.M7ExtBG)
					PPU_DecodeTileExt(&PPU.TileBitmap[0x70000 + (i << 6)], &TempPalette[0], &PPU_M7Cache[(i << 6) + 128*128]);
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

				bglOutputBufferAccess(0, 1, 0, 0);

				bglEnableStencilTest(false);
				bglEnableDepthTest(false);
				bglDepthFunc(GPU_NEVER);
				bglEnableAlphaTest(false);
				bglBlendEquation(GPU_BLEND_ADD, GPU_BLEND_ADD);
				bglBlendFunc(GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
			
				bglScissorMode(GPU_SCISSOR_DISABLE);

				bglColorDepthMask(GPU_WRITE_COLOR);

				bglUniformMatrix(GPU_VERTEX_SHADER, hardRenderUniforms[0], mode7ProjMatrix);
				SET_UNIFORM(GPU_VERTEX_SHADER, hardRenderUniforms[1], 1.0f/16.0f, 1.0f/16.0f, 1.0f, 1.0f);

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
			bglOutputBuffers(0x20000, 0x3, PPU_M7Layer, NULL, 1024, 1024);
			bglViewport((curM7Sec & 0x1) * 512, (curM7Sec >> 1) * 512, 512, 512);
			bglTexImage(GPU_TEXUNIT0, PPU_M7Cache, 128, 128, 0, GPU_RGBA5551);
			bglAttribBuffer(&PPU_M7VertBuf[curM7Sec * 0x6000]);
			bglDrawArrays(GPU_GEOMETRY_PRIM, 0x1000 * 2);

			if(PPU.M7ExtBG)
			{
				bglOutputBuffers(0x20000, 0x3, PPU_M7LayerExt, NULL, 1024, 1024);
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

		curM7 = ((PPU.Mode & 0x7) == 7);

		PPU.CurOBJSection = &PPU.OBJSections[0];
		PPU.CurOBJSection->OBJWidth = PPU.OBJWidth;
		PPU.CurOBJSection->OBJHeight = PPU.OBJHeight;
		PPU.CurOBJSection->OBJTilesetAddr = PPU.OBJTilesetAddr;
		PPU.CurOBJSection->OBJGap = PPU.OBJGap;

		
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
		PPU_ComputeWindows_Hard(PPU.CurWindowSection->Window);
		PPU.WindowDirty = 0;
		
		PPU.CurColorEffect = &PPU.ColorEffectSections[0];
		PPU.CurColorEffect->ColorMath = (PPU.ColorMath2 & 0x80);
		PPU.CurColorEffect->Brightness = PPU.CurBrightness;
		PPU.ColorEffectDirty = 0;
		
		PPU.CurMainBackdrop = &PPU.MainBackdropSections[0];
		PPU.CurMainBackdrop->Color = TempPalette[0];
		PPU.CurMainBackdrop->ColorMath2 = PPU.ColorMath2;
		PPU.MainBackdropDirty = 0;
		

		PPU.CurSubBackdrop = &PPU.SubBackdropSections[0];
		PPU.CurSubBackdrop->Color = PPU.SubBackdrop;
		PPU.CurSubBackdrop->Div2 = (!(PPU.ColorMath1 & 0x02)) && (PPU.ColorMath2 & 0x40);
		PPU.SubBackdropDirty = 0;
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
			if(PPU.Mode7Dirty)
			{
				PPU_Mode7Section *cur = PPU.CurMode7Section;
				if((cur->Sel != PPU.M7Sel) ||
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
			
			PPU_ComputeWindows_Hard(PPU.CurWindowSection->Window);
			PPU.WindowDirty = 0;
		}
		
		if (PPU.ColorEffectDirty)
		{
			PPU.CurColorEffect->EndOffset = line;
			PPU.CurColorEffect++;
			
			PPU.CurColorEffect->ColorMath = (PPU.ColorMath2 & 0x80);
			PPU.CurColorEffect->Brightness = PPU.CurBrightness;
			PPU.ColorEffectDirty = 0;
		}

		if(PPU.MainBackdropDirty)
		{
			PPU.CurMainBackdrop->EndOffset = line;
			PPU.CurMainBackdrop++;
			
			PPU.CurMainBackdrop->Color = TempPalette[0];
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
}



#define RENDERBG(num, type, pal, prio, opt, hi) \
	{ \
		if ((mode & (0x10<<num)) && (mode!=6)) \
			PPU_HardRenderBG_16x16(colormath&(1<<num), num, type, pal, prio?0x2000:0, ystart, yend, hi); \
		else \
			PPU_HardRenderBG_8x8(colormath&(1<<num), num, type, pal, prio?0x2000:0, ystart, yend, opt, hi); \
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

	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0, 6, 1, 1);

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
}

void PPU_HardRender_Mode7(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if(PPU.M7ExtBG)
	{
		if(screen & 0x02)
			PPU_HardRenderBG_Mode7(colormath&0x02, 1, ystart, yend, 0);
	}

	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x00, ystart, yend);
	
	if(!PPU.M7ExtBG)
	{
		if(screen & 0x01)
			PPU_HardRenderBG_Mode7(colormath&0x01, 0, ystart, yend, 0);
	}
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(colormath&0x90, 0x10, ystart, yend);
	
	if(PPU.M7ExtBG)
	{
		if(screen & 0x02)
			PPU_HardRenderBG_Mode7(colormath&0x02, 1, ystart, yend, 1);
	}
	
	if (screen & 0x10)
	{
		PPU_HardRenderOBJLayer(colormath&0x90, 0x20, ystart, yend);
		PPU_HardRenderOBJLayer(colormath&0x90, 0x30, ystart, yend);
	}
}

void PPU_HardRender(u32 snum)
{
	PPU_ModeSection* s = &PPU.ModeSections[0];
	int ystart = 1;

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
		
		if (s->EndOffset >= 240) break;
		ystart = s->EndOffset;
		s++;
	}
}


void PPU_VBlank_Hard(int endLine)
{
	int i;

	GX_MemoryFill((u32 *)PPU_LayerGroup, 0x0000, (u32 *)&PPU_LayerGroup[256*256*4], GX_FILL_TRIGGER | GX_FILL_16BIT_DEPTH, (u32 *)&PPU_LayerGroup[256*256*4], 0x0000, (u32 *)&PPU_LayerGroup[256*256*8], GX_FILL_TRIGGER | GX_FILL_16BIT_DEPTH);

	PPU.CurModeSection->EndOffset = endLine;
	
	PPU.CurOBJSection->EndOffset = endLine;

	for (i = 0; i < 4; i++)
	{
		PPU_Background* bg = &PPU.BG[i];
		bg->CurSection->EndOffset = endLine;
	}
	
	PPU.CurMode7Section->EndOffset = endLine;
	
	PPU.CurWindowSection->EndOffset = endLine;
	
	PPU.CurColorEffect->EndOffset = endLine;
	PPU.CurMainBackdrop->EndOffset = endLine;
	PPU.CurSubBackdrop->EndOffset = endLine;
	
		
	vertexPtr = vertexBuf;

	

	if(curM7)
		PPU_UpdateMode7();
	lastM7 = curM7;
	
	
	bglViewport(0, 0, 256, 256);

	//memcpy(TempPalette, PPU.Palette, 512);
	PPU_ClearMainScreen();
	PPU_DrawWindowMask(0);


	// OBJ LAYER
	
	PPU_HardRenderOBJs();
	
	// MAIN SCREEN
	
	doingBG = 0;
	bglOutputBuffers(0x2, 0x3, MainScreenTex, OBJDepthBuffer, 256, 256);
	PPU_HardRender(0);
	PPU_ClearAlpha(0);
	
	// SUB SCREEN
	
	//memcpy(TempPalette, PPU.Palette, 512);
	PPU_ClearSubScreen();
	PPU_DrawWindowMask(1);

	doingBG = 0;
	bglOutputBuffers(0x2, 0x3, SubScreenTex, OBJDepthBuffer, 256, 256);
	PPU_HardRender(1);
	PPU_ClearAlpha(1);

	// reuse the color math system used by the soft renderer
	bglScissorMode(GPU_SCISSOR_DISABLE);
	PPU_BlendScreens(GPU_RGBA8);

	u32 taken = ((u32)vertexPtr - (u32)vertexBuf);
	GSPGPU_FlushDataCache(vertexBuf, taken);
	if (taken > 0x200000)
		bprintf("OVERFLOW %06lX/200000 (%lu%%)\n", taken, (taken*100)/0x200000);
		
	
	GSPGPU_FlushDataCache(PPU_TileCache, 1024*1024*sizeof(u16));
	
	gspWaitForPSC0();
	//GX_SetDisplayTransfer(NULL, (u32*)PPU_TileCacheRAM, 0x04000400, (u32*)PPU_TileCache, 0x04000400, 0x3308);
	//gspWaitForPPF();
	//GX_RequestDma(NULL, (u32*)PPU_TileCacheRAM, (u32*)PPU_TileCache, 1024*1024*sizeof(u16));
	//gspWaitForDMA();

}

