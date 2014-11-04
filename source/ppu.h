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

#ifndef PPU_H
#define PPU_H

typedef struct
{
	u16* Dest;
	u16* SrcPixels;
	u16 Attrib;
	u8 Alpha;
	u16 Start, End;
	
} PPU_DeferredTile;

typedef struct
{
	u16* Tileset;
	u16* Tilemap;
	u8 Size;
	
	u16 XScroll, YScroll;
	
	PPU_DeferredTile DeferredTiles[40];
	u32 NumDeferredTiles;
	
	u8 WindowMask;
	u16 WindowCombine;

} PPU_Background;

typedef struct
{
	// no start offset in here; start offset is the end offset of the previous segment
	u16 EndOffset;	// 256 = final segment
	u8 WindowMask;	// each 2 bits: 2=inside, 3=outside
	u8 ColorMath;	// 0x20 = inside color math window, 0x10 = outside
	
} PPU_WindowSegment;


typedef struct
{
	u16 VCount;

	// 16+256+16: we leave 16 extra pixels on both sides so we don't have to handle tiles that are partially offscreen
	u16* MainBuffer;
	u16* SubBuffer;
	u8 Brightness[224];

	// OBJ layer
	// bit0-7: color # (0-127, selecting from upper palette region)
	// bit8-15: BG-relative priority
	u16 OBJBuffer[16+256+16];

	u8 SpritesOnLine[4] __attribute__((aligned(4)));

	u16 CGRAMAddr;
	u8 CGRAMVal;
	u16 CGRAM[256];		// SNES CGRAM, xBGR1555
	u16 Palette[256];	// our own palette, converted to RGBx5551

	u16 VRAMAddr;
	u16 VRAMPref;
	u8 VRAMInc;
	u16 VRAMStep;
	u8 VRAM[0x10000];

	u16 OAMAddr;
	u8 OAMVal;
	u8 OAMPrio;
	u8 FirstOBJ;
	u16 OAMReload;
	u8 OAM[0x220];
	
	u8* OBJWidth;
	u8* OBJHeight;


	u8 CurBrightness;
	u8 ForcedBlank;

	u8 Mode;

	/*u16 MainScreen;
	u16 SubScreen;*/
	union
	{
		struct
		{
			u8 MainLayerEnable;
			u8 MainWindowEnable;
		};
		u16 MainScreen;
	};
	union
	{
		struct
		{
			u8 SubLayerEnable;
			u8 SubWindowEnable;
		};
		u16 SubScreen;
	};
	u8 ColorMath1;
	u8 ColorMath2;

	u16 SubBackdrop;

	u8 Subtract;

	u16 WinX[4];
	u8 WinSel[4] __attribute__((aligned(4)));
	u8 WinLogic[2] __attribute__((aligned(2)));

	u8 BGOld;
	u8 M7Old;

	s16 MulA;
	s8 MulB;
	s32 MulResult;

	u8 M7Sel;
	s16 M7A;
	s16 M7B;
	s16 M7C;
	s16 M7D;
	s16 M7RefX;
	s16 M7RefY;
	s16 M7XScroll;
	s16 M7YScroll;

	PPU_Background BG[4];

	u16* OBJTileset;
	u32 OBJGap;

	u8 OBJWindowMask;
	u16 OBJWindowCombine;
	u8 ColorMathWindowMask;
	u16 ColorMathWindowCombine;
	
	PPU_WindowSegment Window[5];

	u8 WindowDirty;


	u16 OPHCT, OPVCT;
	u8 OPHFlag, OPVFlag;
	u8 OPLatch;

	u8 OBJOverflow;
	
} PPUState;

extern PPUState PPU;


extern bool SkipThisFrame;

extern u16* PPU_MainBuffer;
extern u16* PPU_SubBuffer;
extern u8 PPU_Brightness[224];

extern u16 PPU_CGRAM[256];
extern u8 PPU_VRAM[0x10000];
extern u8 PPU_OAM[0x220];

extern u16 PPU_CGRAMAddr;
extern u16 PPU_VRAMAddr;
extern u16 PPU_VRAMStep;
extern u8 PPU_VRAMInc;
extern u16 PPU_OAMAddr;

extern u16 PPU_VCount;

extern u8 PPU_Subtract;


void PPU_Init();
void PPU_Reset();
void PPU_DeInit();

void PPU_SetColor(u32 num, u16 val);

u8 PPU_Read8(u32 addr);
u16 PPU_Read16(u32 addr);
void PPU_Write8(u32 addr, u8 val);
void PPU_Write16(u32 addr, u16 val);

void PPU_RenderScanline(u32 line);
void PPU_VBlank();

void RenderPipeline();
void RenderPipelineVBlank();

#endif
