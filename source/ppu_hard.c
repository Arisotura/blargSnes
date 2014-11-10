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

#include "snes.h"
#include "ppu.h"


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




u16* PPU_TileCache;
u32 PPU_TileCacheIndex;

u16 PPU_TileCacheList[0x10000];
u32 PPU_TileCacheReverseList[16384];

u32 PPU_TileVRAMUpdate[0x10000];
u32 PPU_TilePalUpdate[0x10000];


void PPU_Init_Hard()
{
	int i;
	
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
	
#undef DO_MINIBLOCK
}

void PPU_DecodeTile_4bpp(u16* vram, u16* pal, u32* dst)
{
	int i;
	u8 p1, p2, p3, p4;
	u32 col1, col2;
	
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
		case 0: 
			paldirty = PPU.PaletteUpdateCount[palid];
			vramdirty = PPU.VRAMUpdateCount[addr >> 4];
			break;
			
		case 1: 
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
		case 0: PPU_DecodeTile_2bpp(&PPU.VRAM[addr], &PPU.Palette[palid << 2], dst); break;
		case 1: PPU_DecodeTile_4bpp(&PPU.VRAM[addr], &PPU.Palette[palid << 4], dst); break;
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



#define ADDVERTEX(x, y, coord) \
	*vptr++ = x; \
	*vptr++ = y; \
	*vptr++ = coord;


int PPU_HardBGTest(u16* vptr, PPU_Background* bg, int ystart, int yend)
{
	u16* tilemap;// = bg->Tilemap;
	u32 xoff, yoff;
	u16 curtile;
	int x, y;
	u32 idx;
	int ntiles = 0;
	
	yoff = bg->YScroll;
	ystart -= (yoff & 7);
	
	for (y = ystart; y < yend; y += 8, yoff += 8)
	{
		tilemap = bg->Tilemap + ((yoff & 0xF8) << 2);
		if (yoff & 0x100)
		{
			if (bg->Size & 0x2)
				tilemap += (bg->Size & 0x1) ? 2048 : 1024;
		}
		
		xoff = bg->XScroll;
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
				//if (curtile!=0x0122 && curtile!= 0 && curtile!=0x10F8) bprintf("%d/%d %d/%d %04X\n", x, y, xoff, yoff, curtile);
				// render the tile
				
				// TODO: do this less hackishly
				u32 addr = (((u32)bg->Tileset) - ((u32)&PPU.VRAM[0])) + ((curtile & 0x03FF) << 5);
				u32 palid = (curtile & 0x1C00) >> 10;
				
				u32 coord = PPU_StoreTileInCache(1, palid, addr);
				
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
						ADDVERTEX(x,   y+8,   coord+0x0000);
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
	
	return ntiles;
}



// TEST

void PPU_RenderScanline_Hard(u32 line)
{
	//
}


extern u32* gxCmdBuf;
extern void* vertexBuf;

extern DVLB_s* hardRenderShader;

extern float snesProjMatrix[16];
extern float mvMatrix[16];

extern float* screenVertices;
extern u32* gpuOut;
extern u32* gpuDOut;
extern u32* SNESFrame;

	
int lolz=0;
void PPU_VBlank_Hard()
{
	if (RenderState) 
	{
		gspWaitForP3D();
		RenderState = 0;
	}
	
	u16* vptr = (u16*)vertexBuf;
	
	GPU_SetShader(hardRenderShader);
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)gpuDOut),(u32*)osConvertVirtToPhys((u32)SNESFrame),0,0,256,256);
	

		//bprintf("section %d %d %02X %d\n", startoffset, s->EndOffset, s->ColorMath, s->Brightness);
		
		GPU_DepthRange(-1.0f, 0.0f);
		GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
		GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
		GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
		GPU_SetBlendingColor(0,0,0,0);
		GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_COLOR); // we don't care about depth testing in this pass
		
		GPUCMD_AddSingleParam(0x00010062, 0x00000000);
		GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
		
		GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
		GPU_SetAlphaTest(false, GPU_ALWAYS, 0x00);
		
		setUniformMatrix(0x24, mvMatrix);
		setUniformMatrix(0x20, snesProjMatrix);
		
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
		
		GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)vptr),
			GPU_ATTRIBFMT(0, 2, GPU_SHORT)|GPU_ATTRIBFMT(1, 2, GPU_UNSIGNED_BYTE),
			0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
			
		int ntiles = PPU_HardBGTest(vptr, &PPU.BG[0], 0, 224);
		
		vptr = (u16*)((((u32)vptr) + 0xF) & ~0xF);
	
		
		GPU_DrawArray(GPU_TRIANGLES, ntiles*2*3);
		
		GPU_FinishDrawing();
		
		/*if (s->EndOffset == 240) break;
		
		startoffset = s->EndOffset;
		s++;
	}*/
	
	GSPGPU_FlushDataCache(NULL, PPU_TileCache, 1024*1024*sizeof(u16));
}

