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

#include "config.h"
#include "snes.h"
#include "ppu.h"


u32 Mem_WRAMAddr = 0;
u8 SPC_IOPorts[8];



const u8 PPU_OBJWidths[16] = 
{
	8, 16,
	8, 32,
	8, 64,
	16, 32,
	16, 64,
	32, 64,
	16, 32,
	16, 32
};
const u8 PPU_OBJHeights[16] = 
{
	8, 16,
	8, 32,
	8, 64,
	16, 32,
	16, 64,
	32, 64,
	32, 64,
	32, 32
};

// window combination.
// index is the XOR of the window segment's mask and the window mask register
// 0 00:00 == inside 1 and 2
// 1 00:01 == inside 2
// 2 00:10 == inside 2, 1 disabled
// 3 00:11 == inside 2, 1 disabled
// 4 01:00 == inside 1
// 5 01:01 == outside
// 6 01:10 == outside, 1 disabled
// 7 01:11 == outside, 1 disabled
// 8 10:00 == inside 1, 2 disabled
// 9 10:01 == outside, 2 disabled
// A 10:10 == outside, all disabled
// B 10:11 == outside, all disabled
// C 11:00 == inside 1, 2 disabled
// D 11:01 == outside, 2 disabled
// E 11:10 == outside, all disabled
// F 11:11 == outside, all disabled

const u16 PPU_WindowCombine[] = 
{
	/* OR
	1 1 1 1
	1 0 0 0
	1 0 0 0
	1 0 0 0 */
	0x111F,
	
	/* AND
	1 0 1 1
	0 0 0 0
	1 0 0 0
	1 0 0 0 */
	0x110D,
	
	/* XOR
	0 1 1 1
	1 0 0 0
	1 0 0 0
	1 0 0 0 */
	0x111E,
	
	/* XNOR
	1 0 1 1
	0 1 0 0
	1 0 0 0
	1 0 0 0 */
	0x112D
};


PPUState PPU;


void PPU_Init()
{
	PPU.MainBuffer = (u16*)linearAlloc(256*512*2);
	PPU.SubBuffer = &PPU.MainBuffer[256*256];
	
	SNES_Status->ScreenHeight = 224;
	
	PPU.SubBackdrop = 0x0001;
	
	PPU_ColorEffectSection* c = &PPU.ColorEffectSections[0];
	c->EndOffset = 240;
	c->ColorMath = 0;
	c->Brightness = 0xFF;
	
	PPU.HardwareRenderer = Config.HardwareRenderer;
	
	if (PPU.HardwareRenderer)
		PPU_Init_Hard();
	else
		PPU_Init_Soft();
}

void PPU_SwitchRenderers()
{
	int i;
	
	if (PPU.HardwareRenderer == Config.HardwareRenderer)
		return;
		
	if (PPU.HardwareRenderer)
		PPU_DeInit_Hard();
	else
		PPU_DeInit_Soft();
		
	PPU.HardwareRenderer = Config.HardwareRenderer;
	
	if (PPU.HardwareRenderer)
	{
		for (i = 1; i < 256; i++)
			PPU.Palette[i] |= 0x0001;
	}
	else
	{
		for (i = 0; i < 256; i++)
			PPU.Palette[i] &= ~0x0001;
	}
	
	if (PPU.HardwareRenderer)
		PPU_Init_Hard();
	else
		PPU_Init_Soft();
}

void PPU_Reset()
{
	int i;
	
	u8 hardrend = PPU.HardwareRenderer;
	u16* mbuf = PPU.MainBuffer;
	u16* sbuf = PPU.SubBuffer;
	
	memset(&PPU, 0, sizeof(PPUState));
	
	ApplyScaling();
	
	PPU.HardwareRenderer = hardrend;
	PPU.MainBuffer = mbuf;
	PPU.SubBuffer = sbuf;
	
	for (i = 0; i < 4; i++)
	{
		PPU.BG[i].Tileset = (u16*)PPU.VRAM;
		PPU.BG[i].Tilemap = (u16*)PPU.VRAM;
		
		PPU.BG[i].WindowCombine = PPU_WindowCombine[0];
	}
	
	PPU.OBJWindowCombine = PPU_WindowCombine[0];
	PPU.ColorMathWindowCombine = PPU_WindowCombine[0];
	
	PPU_WindowSegment* s = &PPU.Window[0];
	s->EndOffset = 256;
	s->WindowMask = 0x0F;
	s->ColorMath = 0x10;
	
	PPU_ColorEffectSection* c = &PPU.ColorEffectSections[0];
	c->EndOffset = 240;
	c->ColorMath = 0;
	c->Brightness = 0xFF;
	
	PPU.OBJTileset = (u16*)PPU.VRAM;
	
	PPU.OBJWidth = &PPU_OBJWidths[0];
	PPU.OBJHeight = &PPU_OBJHeights[0];
	
	PPU.SubBackdrop = 0x0001;
	
	if (PPU.HardwareRenderer)
	{
		for (i = 1; i < 256; i++)
			PPU.Palette[i] = 0x0001;
	}
}

void PPU_DeInit()
{
	linearFree(PPU.MainBuffer);
	
	if (PPU.HardwareRenderer)
		PPU_DeInit_Hard();
	else
		PPU_DeInit_Soft();
}


inline void PPU_SetXScroll(int nbg, u8 val)
{
	if (nbg == 0)
	{
		PPU.M7XScroll = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
		PPU.Mode7Dirty = 1;
	}
	
	PPU_Background* bg = &PPU.BG[nbg];
	
	bg->XScroll = (val << 8) | (PPU.BGOld & 0xF8) | ((bg->XScroll >> 8) & 0x7);
	PPU.BGOld = val;
}

inline void PPU_SetYScroll(int nbg, u8 val)
{
	if (nbg == 0)
	{
		PPU.M7YScroll = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
		PPU.Mode7Dirty = 1;
	}
	
	PPU_Background* bg = &PPU.BG[nbg];
	
	bg->YScroll = (val << 8) | PPU.BGOld;
	PPU.BGOld = val;
}

inline void PPU_SetBGSCR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU.BG[nbg];
	
	bg->Size = (val & 0x03);
	bg->TilemapOffset = (val & 0xFC) << 9;
	bg->Tilemap = (u16*)&PPU.VRAM[bg->TilemapOffset];
}

inline void PPU_SetBGCHR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU.BG[nbg];
	
	bg->TilesetOffset = val << 13;
	bg->Tileset = (u16*)&PPU.VRAM[bg->TilesetOffset];
}


inline void PPU_SetColor(u32 num, u16 val)
{
	val &= ~0x8000;
	if (PPU.CGRAM[num] == val) return;
	PPU.CGRAM[num] = val;
	
	// RGB555, the 3DS way
	u16 temp = (val & 0x001F) << 11;
	temp    |= (val & 0x03E0) << 1;
	temp    |= (val & 0x7C00) >> 9;
	
	if (PPU.HardwareRenderer)
	{
		// check if the write is happening mid-frame
#if 0
		if (PPU.VCount < PPU.ScreenHeight-1 && !PPU.ForcedBlank)
		{
			// writes happening during this scanline will be applied to the next one
			// (we assume they happen during HBlank)
			u32 line = PPU.VCount + 1;
			
			u32 n = PPU.NumPaletteChanges[line];
			PPU.PaletteChanges[line][n].Address = num;
			PPU.PaletteChanges[line][n].Color = temp | 0x0001;
			PPU.NumPaletteChanges[line] = n+1;
			
			// tell the hardware renderer to start a new section
			// TODO: also check if the OBJ palette was updated
			PPU.ModeDirty = 1;
		}
		else
#endif
		{
			PPU.Palette[num] = temp | 0x0001;
			PPU.PaletteUpdateCount[num >> 2]++;
			PPU.PaletteUpdateCount256++;
		}
	}
	else
		PPU.Palette[num] = temp;
}

u32 PPU_TranslateVRAMAddress(u32 addr)
{
	switch (PPU.VRAMInc & 0x0C)
	{
		case 0x00: return addr;
		
		case 0x04:
			return (addr & 0x1FE01) |
				  ((addr & 0x001C0) >> 5) |
				  ((addr & 0x0003E) << 3);
				  
		case 0x08:
			return (addr & 0x1FC01) |
				  ((addr & 0x00380) >> 6) |
				  ((addr & 0x0007E) << 3);
				  
		case 0x0C:
			return (addr & 0x1F801) |
				  ((addr & 0x00700) >> 7) |
				  ((addr & 0x000FE) << 3);
	}
}


void PPU_LatchHVCounters()
{
	if (!(SNES_WRIO & 0x80)) return;
	
	PPU.OPHCT = 22 + (SNES_Status->HCount >> 2);
	if (PPU.OPHCT >= 340) PPU.OPHCT -= 340;
	
	PPU.OPVCT = SNES_Status->VCount;
	
	PPU.OPLatch = 0x40;
}


void SPC_Compensate()
{
	int cyclenow = (SNES_Status->HCount * SNES_Status->SPC_CycleRatio);
	int torun = cyclenow - SNES_Status->SPC_LastCycle;
	torun >>= 24;
	if (torun > 0)
	{
		SPC_Run(torun);
		SNES_Status->SPC_LastCycle = cyclenow;
	}
}


u8 PPU_Read8(u32 addr)
{
	u8 ret = 0;
	switch (addr)
	{
		case 0x34: ret = PPU.MulResult & 0xFF; break;
		case 0x35: ret = (PPU.MulResult >> 8) & 0xFF; break;
		case 0x36: ret = (PPU.MulResult >> 16) & 0xFF; break;
		
		case 0x37:
			PPU_LatchHVCounters();
			ret = 0x21;
			break;
			
		case 0x38:
			if (PPU.OAMAddr >= 0x200)
				ret = PPU.OAM[PPU.OAMAddr & 0x21F];
			else
				ret = PPU.OAM[PPU.OAMAddr];
			PPU.OAMAddr++;
			PPU.OAMAddr &= ~0x400;
			break;
		
		case 0x39:
			{
				ret = PPU.VRAMPref & 0xFF;
				if (!(PPU.VRAMInc & 0x80))
				{
					addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
					PPU.VRAMPref = *(u16*)&PPU.VRAM[addr];
					PPU.VRAMAddr += PPU.VRAMStep;
				}
			}
			break;
		case 0x3A:
			{
				ret = PPU.VRAMPref >> 8;
				if (PPU.VRAMInc & 0x80)
				{
					addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
					PPU.VRAMPref = *(u16*)&PPU.VRAM[addr];
					PPU.VRAMAddr += PPU.VRAMStep;
				}
			}
			break;
			
		case 0x3B:
			ret = ((u8*)PPU.CGRAM)[PPU.CGRAMAddr];
			PPU.CGRAMAddr++;
			PPU.CGRAMAddr &= ~0x200; // prevent overflow
			break;
			
		case 0x3C:
			if (PPU.OPHFlag)
			{
				PPU.OPHFlag = 0;
				ret = PPU.OPHCT >> 8;
			}
			else
			{
				PPU.OPHFlag = 1;
				ret = PPU.OPHCT & 0xFF;
			}
			break;
		case 0x3D:
			if (PPU.OPVFlag)
			{
				PPU.OPVFlag = 0;
				ret = PPU.OPVCT >> 8;
			}
			else
			{
				PPU.OPVFlag = 1;
				ret = PPU.OPVCT & 0xFF;
			}
			break;
			
		case 0x3E: ret = 0x01 | PPU.OBJOverflow; break;
		case 0x3F: 
			ret = 0x01 | (ROM_Region ? 0x10 : 0x00) | PPU.OPLatch;
			PPU.OPLatch = 0;
			PPU.OPHFlag = 0;
			PPU.OPVFlag = 0;
			break;
		
		case 0x40: ret = SPC_IOPorts[4]; break;
		case 0x41: ret = SPC_IOPorts[5]; break;
		case 0x42: ret = SPC_IOPorts[6]; break;
		case 0x43: ret = SPC_IOPorts[7]; break;
		
		case 0x80: ret = SNES_SysRAM[Mem_WRAMAddr++]; Mem_WRAMAddr &= ~0x20000; break;

		default:
			if (addr >= 0x84) // B-Bus open bus
				ret = 0x21; // high address byte
			else if (addr >= 0x44 && addr < 0x80)
				bprintf("!! SPC IO MIRROR READ %02X\n", addr);
			else
				bprintf("Open bus 21%02X\n", addr); 
			break;
	}

	return ret;
}

u16 PPU_Read16(u32 addr)
{
	u16 ret = 0;
	switch (addr)
	{
		// not in the right place, but well
		// our I/O functions are mapped to the whole $21xx range
		
		case 0x40: ret = *(u16*)&SPC_IOPorts[4]; break;
		case 0x42: ret = *(u16*)&SPC_IOPorts[6]; break;
		
		default:
			ret = PPU_Read8(addr);
			ret |= (PPU_Read8(addr+1) << 8);
			break;
	}

	return ret;
}

void PPU_Write8(u32 addr, u8 val)
{
	switch (addr)
	{
		case 0x00: // force blank/master brightness
			{
				PPU.ForcedBlank = val & 0x80;
				if (val & 0x80) val = 0;
				else val &= 0x0F;
				
				u8 newbright;
				if (val == 0xF) 
					newbright = 0xFF;
				else if (val)
					newbright = (val + 1) << 4;
				else
					newbright = 0;
					
				if (PPU.CurBrightness != newbright)
				{
					PPU.CurBrightness = newbright;
					PPU.ColorEffectDirty = 1;
				}
			}
			break;
			
		case 0x01:
			{
				PPU.OBJWidth = &PPU_OBJWidths[(val & 0xE0) >> 4];
				PPU.OBJHeight = &PPU_OBJHeights[(val & 0xE0) >> 4];
				
				PPU.OBJTilesetAddr = (val & 0x03) << 14;
				PPU.OBJTileset = (u16*)&PPU.VRAM[PPU.OBJTilesetAddr];
				PPU.OBJGap = (val & 0x1C) << 9;
			}
			break;
			
		case 0x02:
			PPU.OAMAddr = (PPU.OAMAddr & 0x200) | (val << 1);
			PPU.OAMReload = PPU.OAMAddr;
			PPU.FirstOBJ = PPU.OAMPrio ? ((PPU.OAMAddr >> 1) & 0x7F) : 0;
			break;
		case 0x03:
			PPU.OAMAddr = (PPU.OAMAddr & 0x1FE) | ((val & 0x01) << 9);
			PPU.OAMPrio = val & 0x80;
			PPU.OAMReload = PPU.OAMAddr;
			PPU.FirstOBJ = PPU.OAMPrio ? ((PPU.OAMAddr >> 1) & 0x7F) : 0;
			break;
			
		case 0x04:
			if (PPU.OAMAddr >= 0x200)
			{
				PPU.OAM[PPU.OAMAddr & 0x21F] = val;
			}
			else if (PPU.OAMAddr & 0x1)
			{
				*(u16*)&PPU.OAM[PPU.OAMAddr - 1] = PPU.OAMVal | (val << 8);
			}
			else
			{
				PPU.OAMVal = val;
			}
			PPU.OAMAddr++;
			PPU.OAMAddr &= ~0x400;
			break;
			
		case 0x05:
			PPU.Mode = val;
			PPU.ModeDirty = 1;
			break;
			
		case 0x06: // mosaic
			// effect is mostly used for screen transitions, ie unimportant
			// and tricky to implement efficiently.
			// TODO implement it later.
			break;
			
		case 0x07: PPU_SetBGSCR(0, val); break;
		case 0x08: PPU_SetBGSCR(1, val); break;
		case 0x09: PPU_SetBGSCR(2, val); break;
		case 0x0A: PPU_SetBGSCR(3, val); break;
			
		case 0x0B:
			PPU_SetBGCHR(0, val & 0x0F);
			PPU_SetBGCHR(1, val >> 4);
			break;
		case 0x0C:
			PPU_SetBGCHR(2, val & 0x0F);
			PPU_SetBGCHR(3, val >> 4);
			break;
		
		case 0x0D: PPU_SetXScroll(0, val); break;
		case 0x0E: PPU_SetYScroll(0, val); break;
		case 0x0F: PPU_SetXScroll(1, val); break;
		case 0x10: PPU_SetYScroll(1, val); break;
		case 0x11: PPU_SetXScroll(2, val); break;
		case 0x12: PPU_SetYScroll(2, val); break;
		case 0x13: PPU_SetXScroll(3, val); break;
		case 0x14: PPU_SetYScroll(3, val); break;
		
		case 0x15:
			PPU.VRAMInc = val;
			switch (val & 0x03)
			{
				case 0x00: PPU.VRAMStep = 2; break;
				case 0x01: PPU.VRAMStep = 64; break;
				case 0x02:
				case 0x03: PPU.VRAMStep = 256; break;
			}
			break;
			
		case 0x16:
			PPU.VRAMAddr &= 0xFE00;
			PPU.VRAMAddr |= (val << 1);
			addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
			PPU.VRAMPref = *(u16*)&PPU.VRAM[addr];
			break;
		case 0x17:
			PPU.VRAMAddr &= 0x01FE;
			PPU.VRAMAddr |= ((val & 0x7F) << 9);
			addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
			PPU.VRAMPref = *(u16*)&PPU.VRAM[addr];
			break;
		
		case 0x18: // VRAM shit
			{
				addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
				if (PPU.VRAM[addr] != val)
				{
					PPU.VRAM[addr] = val;
					PPU.VRAMUpdateCount[addr >> 4]++;
				}
				if (!(PPU.VRAMInc & 0x80))
					PPU.VRAMAddr += PPU.VRAMStep;
			}
			break;
		case 0x19:
			{
				addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
				if (PPU.VRAM[addr+1] != val)
				{
					PPU.VRAM[addr+1] = val;
					PPU.VRAMUpdateCount[addr >> 4]++;
				}
				if (PPU.VRAMInc & 0x80)
					PPU.VRAMAddr += PPU.VRAMStep;
			}
			break;
			
		case 0x1A:
			PPU.M7Sel = val;
			PPU.Mode7Dirty = 1;
			break;
			
		case 0x1B: // multiply/mode7 shiz
			{
				u16 fval = (u16)(PPU.M7Old | (val << 8));
				PPU.MulA = (s16)fval;
				PPU.MulResult = (s32)PPU.MulA * (s32)PPU.MulB;
				PPU.M7A = (s16)fval;
				PPU.M7Old = val;
			}
			PPU.Mode7Dirty = 1;
			break;
		case 0x1C:
			PPU.M7B = (s16)((val << 8) | PPU.M7Old);
			PPU.M7Old = val;
			PPU.MulB = (s8)val;
			PPU.MulResult = (s32)PPU.MulA * (s32)PPU.MulB;
			PPU.Mode7Dirty = 1;
			break;
		case 0x1D:
			PPU.M7C = (s16)((val << 8) | PPU.M7Old);
			PPU.M7Old = val;
			PPU.Mode7Dirty = 1;
			break;
		case 0x1E:
			PPU.M7D = (s16)((val << 8) | PPU.M7Old);
			PPU.M7Old = val;
			PPU.Mode7Dirty = 1;
			break;
			
		case 0x1F: // mode7 center
			PPU.M7RefX = (s16)((val << 8) | PPU.M7Old);
			PPU.M7Old = val;
			PPU.Mode7Dirty = 1;
			break;
		case 0x20:
			PPU.M7RefY = (s16)((val << 8) | PPU.M7Old);
			PPU.M7Old = val;
			PPU.Mode7Dirty = 1;
			break;
			
		case 0x21:
			PPU.CGRAMAddr = val << 1;
			break;
		
		case 0x22:
			if (!(PPU.CGRAMAddr & 0x1))
				PPU.CGRAMVal = val;
			else
				PPU_SetColor(PPU.CGRAMAddr >> 1, (val << 8) | PPU.CGRAMVal);

			PPU.CGRAMAddr++;
			PPU.CGRAMAddr &= ~0x200; // prevent overflow
			break;
			
		case 0x23:
			PPU.BG[0].WindowMask = val & 0x0F;
			PPU.BG[1].WindowMask = val >> 4;
			break;
		case 0x24:
			PPU.BG[2].WindowMask = val & 0x0F;
			PPU.BG[3].WindowMask = val >> 4;
			break;
		case 0x25:
			PPU.OBJWindowMask = val & 0x0F;
			PPU.ColorMathWindowMask = val >> 4;
			PPU.WindowDirty = 2;
			break;
		
		case 0x26: PPU.WinX[0] = val;   PPU.WindowDirty = 1; break;
		case 0x27: PPU.WinX[1] = val+1; PPU.WindowDirty = 1; break;
		case 0x28: PPU.WinX[2] = val;   PPU.WindowDirty = 1; break;
		case 0x29: PPU.WinX[3] = val+1; PPU.WindowDirty = 1; break;
		
		case 0x2A:
			PPU.BG[0].WindowCombine = PPU_WindowCombine[(val & 0x03)];
			PPU.BG[1].WindowCombine = PPU_WindowCombine[(val & 0x0C) >> 2];
			PPU.BG[2].WindowCombine = PPU_WindowCombine[(val & 0x30) >> 4];
			PPU.BG[3].WindowCombine = PPU_WindowCombine[(val & 0xC0) >> 6];
			break;
			
		case 0x2B:
			PPU.OBJWindowCombine = PPU_WindowCombine[(val & 0x03)];
			PPU.ColorMathWindowCombine = PPU_WindowCombine[(val & 0x0C) >> 2];
			PPU.WindowDirty = 2;
			break;
			
		case 0x2C:
			if (PPU.MainLayerEnable != val)
			{
				PPU.MainLayerEnable = val;
				PPU.ModeDirty = 1;
			}
			break;
		case 0x2D:
			if (PPU.SubLayerEnable != val)
			{
				PPU.SubLayerEnable = val;
				PPU.ModeDirty = 1;
			}
			break;
			
		case 0x2E: // window enable
			if (PPU.MainWindowEnable != val)
			{
				PPU.MainWindowEnable = val;
				if (PPU.HardwareRenderer) PPU.WindowDirty = 1;
			}
			break;
		case 0x2F:
			if (PPU.SubWindowEnable != val)
			{
				PPU.SubWindowEnable = val;
				if (PPU.HardwareRenderer) PPU.WindowDirty = 1;
			}
			break;
		
		case 0x30:
			if ((PPU.ColorMath1 ^ val) & 0x03)
				PPU.ModeDirty = 1;
			PPU.ColorMath1 = val;
			break;
		case 0x31:
			if ((PPU.ColorMath2 ^ val) & 0x80)
				PPU.ColorEffectDirty = 1;
			if ((PPU.ColorMath2 ^ val) & 0x7F)
				PPU.ModeDirty = 1;
			PPU.ColorMath2 = val;
			break;
			
		case 0x32:
			{
				u8 intensity = val & 0x1F;
				if (val & 0x20) PPU.SubBackdrop = (PPU.SubBackdrop & ~0xF800) | (intensity << 11);
				if (val & 0x40) PPU.SubBackdrop = (PPU.SubBackdrop & ~0x07C0) | (intensity << 6);
				if (val & 0x80) PPU.SubBackdrop = (PPU.SubBackdrop & ~0x003E) | (intensity << 1);
				PPU.SubBackdropDirty = 1;
			}
			break;
			
		case 0x33: // SETINI
			{
				u32 height = (val & 0x04) ? 239:224;
				if (height != SNES_Status->ScreenHeight)
				{
					SNES_Status->ScreenHeight = height;
					ApplyScaling();
				}
				if (val & 0x80) bprintf("!! PPU EXT SYNC\n");
				if (val & 0x40) bprintf("!! MODE7 EXTBG\n");
				if (val & 0x08) bprintf("!! PSEUDO HIRES\n");
				if (val & 0x02) bprintf("!! SMALL SPRITES\n");
			}
			break;
			
		case 0x40: SPC_Compensate(); SPC_IOPorts[0] = val; break;
		case 0x41: SPC_Compensate(); SPC_IOPorts[1] = val; break;
		case 0x42: SPC_Compensate(); SPC_IOPorts[2] = val; break;
		case 0x43: SPC_Compensate(); SPC_IOPorts[3] = val; break;
		
		case 0x80: SNES_SysRAM[Mem_WRAMAddr++] = val; Mem_WRAMAddr &= ~0x20000; break;
		case 0x81: Mem_WRAMAddr = (Mem_WRAMAddr & 0x0001FF00) | val; break;
		case 0x82: Mem_WRAMAddr = (Mem_WRAMAddr & 0x000100FF) | (val << 8); break;
		case 0x83: Mem_WRAMAddr = (Mem_WRAMAddr & 0x0000FFFF) | ((val & 0x01) << 16); break;
				
		default:
			iprintf("PPU_Write8(%08X, %08X)\n", addr, val);
			break;
	}
}

void PPU_Write16(u32 addr, u16 val)
{
	switch (addr)
	{
		// optimized route
		
		case 0x16:
			PPU.VRAMAddr = (val << 1) & 0xFFFEFFFF;
			addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
			PPU.VRAMPref = *(u16*)&PPU.VRAM[addr];
			break;
			
		case 0x18:
			addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
			if (*(u16*)&PPU.VRAM[addr] != val)
			{
				*(u16*)&PPU.VRAM[addr] = val;
				PPU.VRAMUpdateCount[addr >> 4]++;
			}
			PPU.VRAMAddr += PPU.VRAMStep;
			break;
			
		case 0x40: SPC_Compensate(); *(u16*)&SPC_IOPorts[0] = val; break;
		case 0x41: SPC_Compensate(); *(u16*)&SPC_IOPorts[1] = val; break;
		case 0x42: SPC_Compensate(); *(u16*)&SPC_IOPorts[2] = val; break;
		
		case 0x3F:
		case 0x43: bprintf("!! write $21%02X %04X\n", addr, val); break;
		
		case 0x81: Mem_WRAMAddr = (Mem_WRAMAddr & 0x00010000) | val; break;
		
		// otherwise, just do two 8bit writes
		default:
			PPU_Write8(addr, val & 0x00FF);
			PPU_Write8(addr+1, val >> 8);
			break;
	}
}



#define WINMASK_OUT (3|(3<<2))
#define WINMASK_1   (2|(3<<2))
#define WINMASK_2   (3|(2<<2))
#define WINMASK_12  (2|(2<<2))

inline void PPU_ComputeSingleWindow(PPU_WindowSegment* s, u32 x1, u32 x2, u32 mask)
{
	if (x1 < x2)
	{
		s->EndOffset = x1;
		s->WindowMask = WINMASK_OUT;
		s++;
		
		s->EndOffset = x2;
		s->WindowMask = mask;
		s++;
	}
	
	s->EndOffset = 256;
	s->WindowMask = WINMASK_OUT;
}

void PPU_ComputeWindows(PPU_WindowSegment* s)
{
	PPU_WindowSegment* first_s = s;
	
	// check for cases that would disable windows fully
	if ((!((PPU.MainScreen|PPU.SubScreen) & 0x1F00)) && 
		(((PPU.ColorMath1 & 0x30) == 0x00) || ((PPU.ColorMath1 & 0x30) == 0x30)))
	{
		s->EndOffset = 256;
		s->WindowMask = 0x0F;
		s->ColorMath = 0x10;
		return;
	}
	
	// first, check single-window cases

	if (PPU.WinX[2] >= PPU.WinX[3])
	{
		PPU_ComputeSingleWindow(s, PPU.WinX[0], PPU.WinX[1], WINMASK_1);
	}
	else if (PPU.WinX[0] >= PPU.WinX[1])
	{
		PPU_ComputeSingleWindow(s, PPU.WinX[2], PPU.WinX[3], WINMASK_2);
	}
	else
	{
		// okay, we have two windows

		if (PPU.WinX[0] < PPU.WinX[2])
		{
			// window 1 first
			
			// border to win1 x1
			s->EndOffset = PPU.WinX[0];
			s->WindowMask = WINMASK_OUT;
			s++;
			
			if (PPU.WinX[2] < PPU.WinX[1])
			{
				// windows overlapped
				
				// win1 x1 to win2 x1
				s->EndOffset = PPU.WinX[2];
				s->WindowMask = WINMASK_1;
				s++;
				
				// win2 x1 to win1 x2
				s->EndOffset = PPU.WinX[1];
				s->WindowMask = WINMASK_12;
				s++;
			}
			else
			{
				// windows separate
				
				// win1 x1 to win1 x2
				s->EndOffset = PPU.WinX[1];
				s->WindowMask = WINMASK_1;
				s++;
				
				// win1 x2 to win2 x1
				s->EndOffset = PPU.WinX[2];
				s->WindowMask = WINMASK_OUT;
				s++;
			}
			
			// to win2 x2
			s->EndOffset = PPU.WinX[3];
			s->WindowMask = WINMASK_2;
			s++;
			
			// win2 x2 to border
			s->EndOffset = 256;
			s->WindowMask = WINMASK_OUT;
		}
		else
		{
			// window 2 first
			
			// border to win2 x1
			s->EndOffset = PPU.WinX[2];
			s->WindowMask = WINMASK_OUT;
			s++;
			
			if (PPU.WinX[0] < PPU.WinX[3])
			{
				// windows overlapped
				
				// win2 x1 to win1 x1
				s->EndOffset = PPU.WinX[0];
				s->WindowMask = WINMASK_2;
				s++;
				
				// win1 x1 to win2 x2
				s->EndOffset = PPU.WinX[3];
				s->WindowMask = WINMASK_12;
				s++;
			}
			else
			{
				// windows separate
				
				// win2 x1 to win2 x2
				s->EndOffset = PPU.WinX[3];
				s->WindowMask = WINMASK_2;
				s++;
				
				// win2 x2 to win1 x1
				s->EndOffset = PPU.WinX[0];
				s->WindowMask = WINMASK_OUT;
				s++;
			}
			
			// to win1 x2
			s->EndOffset = PPU.WinX[1];
			s->WindowMask = WINMASK_1;
			s++;
			
			// win1 x2 to border
			s->EndOffset = 256;
			s->WindowMask = WINMASK_OUT;
		}
	}
	
	// precompute the final window for color math
	s = first_s;
	for (;;)
	{
		u16 isinside = PPU.ColorMathWindowCombine & (1 << (s->WindowMask ^ PPU.ColorMathWindowMask));
		s->ColorMath = isinside ? 0x20 : 0x10;
		
		if (s->EndOffset >= 256) break;
		s++;
	}
}


void PPU_RenderScanline(u32 line)
{
	if (!(line & 7))
		ContinueRendering();
	
	if (SkipThisFrame) return;
	
	if (PPU.HardwareRenderer)
		PPU_RenderScanline_Hard(line);
	else
		PPU_RenderScanline_Soft(line);
}

void PPU_VBlank()
{
	int i;
	
	FinishRendering();
	
	if (!SkipThisFrame)
	{
		if (PPU.HardwareRenderer)
			PPU_VBlank_Hard();
		else
			PPU_VBlank_Soft();
		
		RenderTopScreen();
	}
	else
		RenderState = 4;
	
	PPU.OAMAddr = PPU.OAMReload;
	
	if (!PPU.ForcedBlank)
		PPU.OBJOverflow = 0;
		
	if (SNES_AutoJoypad)
		SNES_JoyBit = 16;
}
