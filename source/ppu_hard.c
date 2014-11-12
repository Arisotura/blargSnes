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

#include "mem.h"
#include "snes.h"
#include "ppu.h"


#define SET_UNIFORM(n, v0, v1, v2, v3) \
	{ \
		float blarg[4] = {(v3), (v2), (v1), (v0)}; \
		GPU_SetUniform(n, (u32*)blarg, 1); \
	}


extern u32* gxCmdBuf;
extern void* vertexBuf;

// ugly global but makes shit easier to deal with
extern void* vertexPtr;

extern DVLB_s* hardRenderShader;
extern DVLB_s* plainQuadShader;

extern float snesProjMatrix[16];

extern float* screenVertices;
extern u32* gpuOut;
extern u32* gpuDOut;
extern u32* SNESFrame;
extern u16* MainScreenTex;
extern u16* SubScreenTex;

u32* OBJColorBuffer;
u32* OBJDepthBuffer;


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

// * video mode

// * mosaic

// * master brightness
// * color math add/sub

// + layer scroll
// + layer tileset/tilemap address
// * mode7 scroll/matrix/etc
// * layer enable/disable

// * OBJ tileset address

// * window registers

// * color math layer sel

// * sub backdrop color


// STENCIL BUFFER USAGE
// * bit0: window mask
// * bit1: 0=clear alpha, 1=keep alpha

// DEPTH BUFFER USAGE
// * 0x00-0x30: OBJ pal 0-3 (prio 0-3 resp.)
// * 0x40-0x70: OBJ pal 4-7 (prio 0-3 resp.)
// * 0x80: BG1
// * 0x90: BG2
// * 0xA0: BG3
// * 0xB0: BG4
// * 0x100: backdrop
// (note that final depth values are transformed to range between 0 and -1 
//  -- GPU_GREATER in the depth test actually means LESS)


#define TILE_2BPP 0
#define TILE_4BPP 1


u16* PPU_TileCache;
u32 PPU_TileCacheIndex;

u16 PPU_TileCacheList[0x10000];
u32 PPU_TileCacheReverseList[16384];

u32 PPU_TileVRAMUpdate[0x10000];
u32 PPU_TilePalUpdate[0x10000];


void PPU_Init_Hard()
{
	int i;
	
	OBJColorBuffer = (u32*)VRAM_Alloc(256*256*4);
	OBJDepthBuffer = (u32*)VRAM_Alloc(256*256*4);
	
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
		
		default:
			bprintf("unknown tile type %d\n", type);
			return;
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
		case TILE_2BPP: PPU_DecodeTile_2bpp(&PPU.VRAM[addr], &PPU.Palette[palid << 2], dst); break;
		case TILE_4BPP: PPU_DecodeTile_4bpp(&PPU.VRAM[addr], &PPU.Palette[palid << 4], dst); break;
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



void PPU_ClearScreens()
{
	u8* vptr = (u8*)vertexPtr;
	
	u16 col = PPU.Palette[0];
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
	
	GPU_SetShader(plainQuadShader);
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)OBJDepthBuffer),(u32*)osConvertVirtToPhys((u32)MainScreenTex),0,0,256,256);
		
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0xFF);
	GPU_SetStencilOp(GPU_AND_NOT, GPU_AND_NOT, GPU_AND_NOT);	// clear the stencil buffer to 00
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_ALL); // clear the depth buffer
	
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	
	GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	GPU_SetAlphaTest(false, GPU_ALWAYS, 0);
	
	setUniformMatrix(0x20, snesProjMatrix);
	
	GPU_SetTextureEnable(0);
	
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0), 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	GPU_SetDummyTexEnv(1);
	GPU_SetDummyTexEnv(2);
	GPU_SetDummyTexEnv(3);
	GPU_SetDummyTexEnv(4);
	GPU_SetDummyTexEnv(5);
	
	GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)vptr),
		GPU_ATTRIBFMT(0, 3, GPU_SHORT)|GPU_ATTRIBFMT(1, 4, GPU_UNSIGNED_BYTE),
		0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
		
	u8 alpha = (PPU.ColorMath2 & 0x20) ? 0xFF:0x00;
	ADDVERTEX(0, 0, 0x40,      r, g, b, alpha);
	ADDVERTEX(256, 0, 0x40,    r, g, b, alpha);
	ADDVERTEX(256, 256, 0x40,  r, g, b, alpha);
	ADDVERTEX(0, 0, 0x40,      r, g, b, alpha);
	ADDVERTEX(256, 256, 0x40,  r, g, b, alpha);
	ADDVERTEX(0, 256, 0x40,    r, g, b, alpha);
	vptr = (u8*)((((u32)vptr) + 0xF) & ~0xF);
	
	GPU_DrawArray(GPU_TRIANGLES, 2*3);
	
	// clear the OBJ buffer
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)OBJDepthBuffer),(u32*)osConvertVirtToPhys((u32)OBJColorBuffer),0,0,256,256);
		
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0xFF);
	GPU_SetStencilOp(GPU_XOR, GPU_XOR, GPU_XOR); // invert stencil values 
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	
	GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	GPU_SetAlphaTest(false, GPU_ALWAYS, 0);
	
	setUniformMatrix(0x20, snesProjMatrix);
	
	GPU_SetTextureEnable(0);
	
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0), 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	GPU_SetDummyTexEnv(1);
	GPU_SetDummyTexEnv(2);
	GPU_SetDummyTexEnv(3);
	GPU_SetDummyTexEnv(4);
	GPU_SetDummyTexEnv(5);
	
	GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)vptr),
		GPU_ATTRIBFMT(0, 3, GPU_SHORT)|GPU_ATTRIBFMT(1, 4, GPU_UNSIGNED_BYTE),
		0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
		
	// Z here doesn't matter
	ADDVERTEX(0, 0, 0,      0, 0, 0, 0);
	ADDVERTEX(256, 0, 0,    0, 0, 0, 0);
	ADDVERTEX(256, 256, 0,  0, 0, 0, 0);
	ADDVERTEX(0, 0, 0,      0, 0, 0, 0);
	ADDVERTEX(256, 256, 0,  0, 0, 0, 0);
	ADDVERTEX(0, 256, 0,    0, 0, 0, 0);
	vptr = (u8*)((((u32)vptr) + 0xF) & ~0xF);
	
	GPU_DrawArray(GPU_TRIANGLES, 2*3);
	
	
	// TEMP HACK
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)OBJDepthBuffer),(u32*)osConvertVirtToPhys((u32)SubScreenTex),0,0,256,256);
		
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0xFF);
	GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	
	GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	GPU_SetAlphaTest(false, GPU_ALWAYS, 0);
	
	setUniformMatrix(0x20, snesProjMatrix);
	
	GPU_SetTextureEnable(0);
	
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0), 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	GPU_SetDummyTexEnv(1);
	GPU_SetDummyTexEnv(2);
	GPU_SetDummyTexEnv(3);
	GPU_SetDummyTexEnv(4);
	GPU_SetDummyTexEnv(5);
	
	GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)vptr),
		GPU_ATTRIBFMT(0, 3, GPU_SHORT)|GPU_ATTRIBFMT(1, 4, GPU_UNSIGNED_BYTE),
		0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
		
	col = PPU.SubBackdrop;
	r = (col & 0xF800) >> 8; r |= (r >> 5);
	g = (col & 0x07C0) >> 3; g |= (g >> 5);
	b = (col & 0x003E) << 2; b |= (b >> 5);
	alpha = 255;//(PPU.ColorMath2 & 0x20) ? 0xFF:0x00;
	ADDVERTEX(0, 0, 0x40,      r, g, b, alpha);
	ADDVERTEX(256, 0, 0x40,    r, g, b, alpha);
	ADDVERTEX(256, 256, 0x40,  r, g, b, alpha);
	ADDVERTEX(0, 0, 0x40,      r, g, b, alpha);
	ADDVERTEX(256, 256, 0x40,  r, g, b, alpha);
	ADDVERTEX(0, 256, 0x40,    r, g, b, alpha);
	vptr = (u8*)((((u32)vptr) + 0xF) & ~0xF);
	
	GPU_DrawArray(GPU_TRIANGLES, 2*3);
	
	
	GPU_FinishDrawing();
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
	
	GPU_SetShader(plainQuadShader);
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)OBJDepthBuffer),(u32*)osConvertVirtToPhys((u32)MainScreenTex),0,0,256,256);
		
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(true, GPU_EQUAL, 0x00, 0x02, 0xFF);
	GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_ALPHA);
	
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	
	GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	GPU_SetAlphaTest(false, GPU_ALWAYS, 0);
	
	setUniformMatrix(0x20, snesProjMatrix);
	
	GPU_SetTextureEnable(0);
	
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0), 
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	GPU_SetDummyTexEnv(1);
	GPU_SetDummyTexEnv(2);
	GPU_SetDummyTexEnv(3);
	GPU_SetDummyTexEnv(4);
	GPU_SetDummyTexEnv(5);
	
	GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)vptr),
		GPU_ATTRIBFMT(0, 3, GPU_SHORT)|GPU_ATTRIBFMT(1, 4, GPU_UNSIGNED_BYTE),
		0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
		
	ADDVERTEX(0, 0, 0,      255, 0, 255, 0);
	ADDVERTEX(256, 0, 0,    255, 0, 255, 0);
	ADDVERTEX(256, 256, 0,  255, 0, 255, 0);
	ADDVERTEX(0, 0, 0,      255, 0, 255, 0);
	ADDVERTEX(256, 256, 0,  255, 0, 255, 0);
	ADDVERTEX(0, 256, 0,    255, 0, 255, 0);
	vptr = (u8*)((((u32)vptr) + 0xF) & ~0xF);
	
	GPU_DrawArray(GPU_TRIANGLES, 2*3);

	vertexPtr = vptr;
	
#undef ADDVERTEX
}


void PPU_HardRenderBG_8x8(u32 setalpha, PPU_Background* bg, int type, u32 prio, int ystart, int yend)
{
	u16* tilemap;
	int tileaddrshift = ((int[]){4, 5})[type];
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
				{
					idx++;
					continue;
				}

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
			GPU_SetScissorTest(GPU_SCISSOR_NORMAL, 0, systart, 256, syend);
			
			//SET_UNIFORM(0x25, 0.0f, 1.0f, (float)(0x80+(num<<4)), 0.0f);
			
			// if needed, set stencil bits indicating where we'll need to clear alpha
			GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x02);
			if (setalpha) GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
			else          GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_AND_NOT);
			GPU_SetBlendingColor(0,0,0,0);
			
			GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)vertexPtr),
				GPU_ATTRIBFMT(0, 2, GPU_SHORT)|GPU_ATTRIBFMT(1, 2, GPU_UNSIGNED_BYTE),
				0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
			
			vptr = (u16*)((((u32)vptr) + 0xF) & ~0xF);
			vertexPtr = vptr;
			
			GPU_DrawArray(GPU_TRIANGLES, ntiles*2*3);
			GPU_FinishDrawing();
		}
		
		if (syend >= yend) break;
		systart = syend;
		s++;
	}
	
#undef ADDVERTEX
}


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
	//if (palid >= 12) prio += 0x40;
	
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
	
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)OBJDepthBuffer),(u32*)osConvertVirtToPhys((u32)OBJColorBuffer),0,0,256,256);
		
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
	GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_ALL);
	
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	
	GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	GPU_SetAlphaTest(true, GPU_GREATER, 0);
	
	setUniformMatrix(0x20, snesProjMatrix);
	SET_UNIFORM(0x24, 1.0f/128.0f, 1.0f/128.0f, 1.0f, 1.0f);
	
	GPU_SetTextureEnable(GPU_TEXUNIT0);
	
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	GPU_SetDummyTexEnv(1);
	GPU_SetDummyTexEnv(2);
	GPU_SetDummyTexEnv(3);
	GPU_SetDummyTexEnv(4);
	GPU_SetDummyTexEnv(5);
		
	GPU_SetTexture(GPU_TEXUNIT0, (u32*)osConvertVirtToPhys((u32)PPU_TileCache),1024,1024,0,GPU_RGBA5551);

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
	
	vertexPtr = (u16*)((((u32)vertexPtr) + 0xF) & ~0xF);
	
	GPU_SetScissorTest(GPU_SCISSOR_NORMAL, 0, ystart, 256, yend);
	
	GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)vstart),
		GPU_ATTRIBFMT(0, 3, GPU_SHORT)|GPU_ATTRIBFMT(1, 2, GPU_UNSIGNED_BYTE),
		0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
	
	GPU_DrawArray(GPU_TRIANGLES, ntiles*2*3);
}

void PPU_HardRenderOBJLayer(u32 prio, int ystart, int yend)
{
	u16* vptr = (u16*)vertexPtr;
	
#define ADDVERTEX(x, y, z, s, t) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = z; \
	*vptr++ = s; \
	*vptr++ = t;
	
	GPU_FinishDrawing();
	
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
	GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(true, GPU_EQUAL, GPU_WRITE_COLOR);
	
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	
	GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	GPU_SetAlphaTest(true, GPU_GREATER, 0);
	
	SET_UNIFORM(0x24, 1.0f/256.0f, 1.0f/256.0f, 1.0f, 1.0f);
	SET_UNIFORM(0x25, 1.0f, 0.0f, 0.0f, 0.0f);
	
	GPU_SetTextureEnable(GPU_TEXUNIT0);
	
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	GPU_SetDummyTexEnv(1);
	GPU_SetDummyTexEnv(2);
	GPU_SetDummyTexEnv(3);
	GPU_SetDummyTexEnv(4);
	GPU_SetDummyTexEnv(5);
		
	GPU_SetTexture(GPU_TEXUNIT0, (u32*)osConvertVirtToPhys((u32)OBJColorBuffer),256,256,0,GPU_RGBA8);
	
	GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)vptr),
		GPU_ATTRIBFMT(0, 3, GPU_SHORT)|GPU_ATTRIBFMT(1, 2, GPU_SHORT),
		0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
		
	ADDVERTEX(0, ystart,   prio,  0, ystart);
	ADDVERTEX(256, ystart, prio,  256, ystart);
	ADDVERTEX(256, yend,   prio,  256, yend);
	ADDVERTEX(0, ystart,   prio,  0, ystart);
	ADDVERTEX(256, yend,   prio,  256, yend);
	ADDVERTEX(0, yend,     prio,  0, yend);
	/*prio += 0x40;
	ADDVERTEX(0, ystart,   prio,  0, ystart);
	ADDVERTEX(256, ystart, prio,  256, ystart);
	ADDVERTEX(256, yend,   prio,  256, yend);
	ADDVERTEX(0, ystart,   prio,  0, ystart);
	ADDVERTEX(256, yend,   prio,  256, yend);
	ADDVERTEX(0, yend,     prio,  0, yend);*/
	vptr = (u8*)((((u32)vptr) + 0xF) & ~0xF);
	vertexPtr = vptr;
	
	GPU_DrawArray(GPU_TRIANGLES, 2*2*3);
	
	GPU_FinishDrawing();
	
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
	GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	
	GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	GPU_SetAlphaTest(true, GPU_GREATER, 0);
	
	SET_UNIFORM(0x24, 1.0f/128.0f, 1.0f/128.0f, 1.0f, 1.0f);
	
	GPU_SetTextureEnable(GPU_TEXUNIT0);
	
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	GPU_SetDummyTexEnv(1);
	GPU_SetDummyTexEnv(2);
	GPU_SetDummyTexEnv(3);
	GPU_SetDummyTexEnv(4);
	GPU_SetDummyTexEnv(5);
		
	GPU_SetTexture(GPU_TEXUNIT0, (u32*)osConvertVirtToPhys((u32)PPU_TileCache),1024,1024,0,GPU_RGBA5551);
	
#undef ADDVERTEX
}




void PPU_RenderScanline_Hard(u32 line)
{
	int i;
	
	if (!line)
	{
		// initialize stuff upon line 0
		
		PPU.CurModeSection = &PPU.ModeSections[0];
		PPU.CurModeSection->Mode = PPU.Mode;
		PPU.LastMode = PPU.Mode;
		
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
	}
	else
	{
		if (PPU.Mode != PPU.LastMode)
		{
			PPU.CurModeSection->EndOffset = line;
			PPU.CurModeSection++;
			
			PPU.CurModeSection->Mode = PPU.Mode;
			PPU.LastMode = PPU.Mode;
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
	}
}


// TODO: later adjust for 16x16 layers
#define RENDERBG(num, type, prio) PPU_HardRenderBG_8x8(colormath&(1<<num), &PPU.BG[num], type, prio?0x2000:0, ystart, yend)

void PPU_HardRender_Mode1(int ystart, int yend, u32 screen, u32 mode, u32 colormath)
{
	if (screen & 0x04) RENDERBG(2, TILE_2BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(0x00, ystart, yend);
	
	if (screen & 0x04) 
	{
		if (!(mode & 0x08))
			RENDERBG(2, TILE_2BPP, 1);
	}
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(0x10, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 0);
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 0);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(0x20, ystart, yend);
	
	if (screen & 0x02) RENDERBG(1, TILE_4BPP, 1);
	if (screen & 0x01) RENDERBG(0, TILE_4BPP, 1);
	
	if (screen & 0x10) PPU_HardRenderOBJLayer(0x30, ystart, yend);
	
	if (screen & 0x04) 
	{
		if (mode & 0x08)
			RENDERBG(2, TILE_2BPP, 1);
	}
}

void PPU_HardRender(u32 screen, u32 colormath)
{
	PPU_ModeSection* s = &PPU.ModeSections[0];
	int ystart = 0;
	
	for (;;)
	{
		switch (s->Mode & 0x07)
		{
			case 1:
				PPU_HardRender_Mode1(ystart, s->EndOffset, screen, s->Mode, colormath);
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
	
	if (RenderState) 
	{
		gspWaitForP3D();
		RenderState = 0;
	}
	
	PPU.CurModeSection->EndOffset = 240;
	
	for (i = 0; i < 4; i++)
	{
		PPU_Background* bg = &PPU.BG[i];
		bg->CurSection->EndOffset = 240;
	}
	
	
	vertexPtr = vertexBuf;
	
	PPU_ClearScreens();
	
	
	GPU_SetShader(hardRenderShader);
	
	
	PPU_HardRenderOBJs();
	
	// MAIN SCREEN
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)OBJDepthBuffer),(u32*)osConvertVirtToPhys((u32)MainScreenTex),0,0,256,256);
		
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
	GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	
	GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	GPU_SetAlphaTest(true, GPU_GREATER, 0);
	
	setUniformMatrix(0x20, snesProjMatrix);
	SET_UNIFORM(0x24, 1.0f/128.0f, 1.0f/128.0f, 1.0f, 1.0f);
	
	GPU_SetTextureEnable(GPU_TEXUNIT0);
	
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	GPU_SetDummyTexEnv(1);
	GPU_SetDummyTexEnv(2);
	GPU_SetDummyTexEnv(3);
	GPU_SetDummyTexEnv(4);
	GPU_SetDummyTexEnv(5);
		
	GPU_SetTexture(GPU_TEXUNIT0, (u32*)osConvertVirtToPhys((u32)PPU_TileCache),1024,1024,0,GPU_RGBA5551);
	
	PPU_HardRender(PPU.MainScreen, PPU.ColorMath2);
	GPU_FinishDrawing();
	
	PPU_ClearAlpha();
	GPU_FinishDrawing();
	
	// SUB SCREEN
	GPU_SetShader(hardRenderShader);
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)OBJDepthBuffer),(u32*)osConvertVirtToPhys((u32)SubScreenTex),0,0,256,256);
		
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
	GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	
	GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	GPU_SetAlphaTest(true, GPU_GREATER, 0);
	
	setUniformMatrix(0x20, snesProjMatrix);
	SET_UNIFORM(0x24, 1.0f/128.0f, 1.0f/128.0f, 1.0f, 1.0f);
	
	GPU_SetTextureEnable(GPU_TEXUNIT0);
	
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	GPU_SetDummyTexEnv(1);
	GPU_SetDummyTexEnv(2);
	GPU_SetDummyTexEnv(3);
	GPU_SetDummyTexEnv(4);
	GPU_SetDummyTexEnv(5);
		
	GPU_SetTexture(GPU_TEXUNIT0, (u32*)osConvertVirtToPhys((u32)PPU_TileCache),1024,1024,0,GPU_RGBA5551);
	
	PPU_HardRender(PPU.SubScreen, 0x1F);
	GPU_FinishDrawing();
	
	// TODO: appropriately clear alpha (for div2 shiz)
	
	// reuse the color math system used by the soft renderer
	PPU_BlendScreens(GPU_RGBA8);
		
	
	GSPGPU_FlushDataCache(NULL, PPU_TileCache, 1024*1024*sizeof(u16));
}

