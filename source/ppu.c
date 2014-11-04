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
	
	PPU.SubBackdrop = 0x0001;
}

void PPU_Reset()
{
	int i;
	
	u16* mbuf = PPU.MainBuffer;
	u16* sbuf = PPU.SubBuffer;
	
	memset(&PPU, 0, sizeof(PPUState));
	
	PPU.MainBuffer = mbuf;
	PPU.SubBuffer = sbuf;
	
	memset(PPU.Brightness, 0xFF, 224);
	
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
	
	PPU.OBJTileset = (u16*)PPU.VRAM;
	
	PPU.OBJWidth = &PPU_OBJWidths[0];
	PPU.OBJHeight = &PPU_OBJHeights[0];
	
	PPU.SubBackdrop = 0x0001;
}

void PPU_DeInit()
{
	linearFree(PPU.MainBuffer);
}


inline void PPU_SetXScroll(int nbg, u8 val)
{
	if (nbg == 0)
	{
		PPU.M7XScroll = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
	}
	
	PPU_Background* bg = &PPU.BG[nbg];
	
	bg->XScroll = (val << 8) | (PPU.BGOld & 0xFFF8) | ((bg->XScroll >> 8) & 0x7);
	PPU.BGOld = val;
}

inline void PPU_SetYScroll(int nbg, u8 val)
{
	if (nbg == 0)
	{
		PPU.M7YScroll = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
	}
	
	PPU_Background* bg = &PPU.BG[nbg];
	
	bg->YScroll = (val << 8) | PPU.BGOld;
	PPU.BGOld = val;
}

inline void PPU_SetBGSCR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU.BG[nbg];
	
	bg->Size = (val & 0x03);
	bg->Tilemap = (u16*)&PPU.VRAM[(val & 0xFC) << 9];
}

inline void PPU_SetBGCHR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU.BG[nbg];
	
	bg->Tileset = (u16*)&PPU.VRAM[val << 13];
}


inline void PPU_SetColor(u32 num, u16 val)
{
	PPU.CGRAM[num] = val;
	
	// RGB555, the 3DS way
	u16 temp = (val & 0x001F) << 11;
	temp    |= (val & 0x03E0) << 1;
	temp    |= (val & 0x7C00) >> 9;
	PPU.Palette[num] = temp;
}


void PPU_LatchHVCounters()
{
	// TODO simulate this one based on CPU cycle counter
	PPU.OPHCT = 22;
	
	PPU.OPVCT = 1 + PPU.VCount;
	if (PPU.OPVCT > 261) PPU.OPVCT = 0;
	
	PPU.OPLatch = 0x40;
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
					PPU.VRAMPref = *(u16*)&PPU.VRAM[PPU.VRAMAddr];
					PPU.VRAMAddr += PPU.VRAMStep;
				}
			}
			break;
		case 0x3A:
			{
				ret = PPU.VRAMPref >> 8;
				if (PPU.VRAMInc & 0x80)
				{
					PPU.VRAMPref = *(u16*)&PPU.VRAM[PPU.VRAMAddr];
					PPU.VRAMAddr += PPU.VRAMStep;
				}
			}
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
		
		case 0x3B: bprintf("CGRAM read\n"); break;

		default:
			if (addr >= 0x84) // B-Bus open bus
				ret = 0x21; // high address byte
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
				
				if (val == 0xF) 
					PPU.CurBrightness = 0xFF;
				else if (val)
					PPU.CurBrightness = (val + 1) << 4;
				else
					PPU.CurBrightness = 0;
			}
			break;
			
		case 0x01:
			{
				PPU.OBJWidth = &PPU_OBJWidths[(val & 0xE0) >> 4];
				PPU.OBJHeight = &PPU_OBJHeights[(val & 0xE0) >> 4];
				
				PPU.OBJTileset = (u16*)&PPU.VRAM[(val & 0x03) << 14];
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
			if ((val & 0x0C) != 0x00) bprintf("UNSUPPORTED VRAM MODE %02X\n", val);
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
			PPU.VRAMPref = *(u16*)&PPU.VRAM[PPU.VRAMAddr];
			break;
		case 0x17:
			PPU.VRAMAddr &= 0x01FE;
			PPU.VRAMAddr |= ((val & 0x7F) << 9);
			PPU.VRAMPref = *(u16*)&PPU.VRAM[PPU.VRAMAddr];
			break;
		
		case 0x18: // VRAM shit
			{
				PPU.VRAM[PPU.VRAMAddr] = val;
				if (!(PPU.VRAMInc & 0x80))
					PPU.VRAMAddr += PPU.VRAMStep;
			}
			break;
		case 0x19:
			{
				PPU.VRAM[PPU.VRAMAddr+1] = val;
				if (PPU.VRAMInc & 0x80)
					PPU.VRAMAddr += PPU.VRAMStep;
			}
			break;
			
		case 0x1A:
			PPU.M7Sel = val;
			break;
			
		case 0x1B: // multiply/mode7 shiz
			{
				u16 fval = (u16)(PPU.M7Old | (val << 8));
				PPU.MulA = (s16)fval;
				PPU.MulResult = (s32)PPU.MulA * (s32)PPU.MulB;
				PPU.M7A = (s16)fval;
				PPU.M7Old = val;
			}
			break;
		case 0x1C:
			PPU.M7B = (s16)((val << 8) | PPU.M7Old);
			PPU.M7Old = val;
			PPU.MulB = (s8)val;
			PPU.MulResult = (s32)PPU.MulA * (s32)PPU.MulB;
			break;
		case 0x1D:
			PPU.M7C = (s16)((val << 8) | PPU.M7Old);
			PPU.M7Old = val;
			break;
		case 0x1E:
			PPU.M7D = (s16)((val << 8) | PPU.M7Old);
			PPU.M7Old = val;
			break;
			
		case 0x1F: // mode7 center
			PPU.M7RefX = (s16)((val << 8) | PPU.M7Old);
			PPU.M7Old = val;
			break;
		case 0x20:
			PPU.M7RefY = (s16)((val << 8) | PPU.M7Old);
			PPU.M7Old = val;
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
			PPU.MainLayerEnable = val;
			break;
		case 0x2D:
			PPU.SubLayerEnable = val;
			break;
			
		case 0x2E: // window enable
			PPU.MainWindowEnable = val;
			break;
		case 0x2F:
			PPU.SubWindowEnable = val;
			break;
		
		case 0x30:
			PPU.ColorMath1 = val;
			break;
		case 0x31:
			PPU.ColorMath2 = val;
			break;
			
		case 0x32:
			{
				u8 intensity = val & 0x1F;
				if (val & 0x20) PPU.SubBackdrop = (PPU.SubBackdrop & ~0xF800) | (intensity << 11);
				if (val & 0x40) PPU.SubBackdrop = (PPU.SubBackdrop & ~0x07C0) | (intensity << 6);
				if (val & 0x80) PPU.SubBackdrop = (PPU.SubBackdrop & ~0x003E) | (intensity << 1);
			}
			break;
			
		case 0x33: // SETINI
			if (val & 0x80) bprintf("!! PPU EXT SYNC\n");
			if (val & 0x40) bprintf("!! MODE7 EXTBG\n");
			if (val & 0x08) bprintf("!! PSEUDO HIRES\n");
			if (val & 0x02) bprintf("!! SMALL SPRITES\n");
			break;
			
		case 0x40: SPC_IOPorts[0] = val; break;
		case 0x41: SPC_IOPorts[1] = val; break;
		case 0x42: SPC_IOPorts[2] = val; break;
		case 0x43: SPC_IOPorts[3] = val; break;
		
		case 0x80: SNES_SysRAM[Mem_WRAMAddr++] = val; Mem_WRAMAddr &= ~0x20000; break;
		case 0x81: Mem_WRAMAddr = (Mem_WRAMAddr & 0x0001FF00) | val; break;
		case 0x82: Mem_WRAMAddr = (Mem_WRAMAddr & 0x000100FF) | (val << 8); break;
		case 0x83: Mem_WRAMAddr = (Mem_WRAMAddr & 0x0000FFFF) | ((val & 0x01) << 16); break;
				
		default:
			//iprintf("PPU_Write8(%08X, %08X)\n", addr, val);
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
			PPU.VRAMPref = *(u16*)&PPU.VRAM[PPU.VRAMAddr];
			break;
			
		case 0x18:
			*(u16*)&PPU.VRAM[PPU.VRAMAddr] = val;
			PPU.VRAMAddr += PPU.VRAMStep;
			break;
			
		case 0x40: *(u16*)&SPC_IOPorts[0] = val; break;
		case 0x41: *(u16*)&SPC_IOPorts[1] = val; break;
		case 0x42: *(u16*)&SPC_IOPorts[2] = val; break;
		
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

inline void PPU_ComputeSingleWindow(u16 x1, u16 x2, u8 mask)
{
	PPU_WindowSegment* s = &PPU.Window[0];
	
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

void PPU_ComputeWindows()
{
	PPU_WindowSegment* s;
	
	// check for cases that would disable windows fully
	if ((!((PPU.MainScreen|PPU.SubScreen) & 0x1F00)) && 
		(((PPU.ColorMath1 & 0x30) == 0x00) || ((PPU.ColorMath1 & 0x30) == 0x30)))
	{
		s = &PPU.Window[0];
		s->EndOffset = 256;
		s->WindowMask = 0x0F;
		s->ColorMath = 0x10;
		return;
	}
	
	// first, check single-window cases

	if (PPU.WinX[2] >= PPU.WinX[3])
	{
		PPU_ComputeSingleWindow(PPU.WinX[0], PPU.WinX[1], WINMASK_1);
	}
	else if (PPU.WinX[0] >= PPU.WinX[1])
	{
		PPU_ComputeSingleWindow(PPU.WinX[2], PPU.WinX[3], WINMASK_2);
	}
	else
	{
		// okay, we have two windows
		
		s = &PPU.Window[0];
				
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
	s = &PPU.Window[0];
	for (;;)
	{
		u16 isinside = PPU.ColorMathWindowCombine & (1 << (s->WindowMask ^ PPU.ColorMathWindowMask));
		s->ColorMath = isinside ? 0x20 : 0x10;
		
		if (s->EndOffset >= 256) break;
		s++;
	}
}


#define COLMATH(n) ((colormath & (1<<n)) ? 1:0)
#define COLMATH_OBJ ((colormath & 0x80) ? 0xFF:(colormath&0x40))


void PPU_RenderTile_2bpp(u16 curtile, u16 tilepixels, u16* buffer, u16* pal, u32 alpha, int start, int end)
{
	u32 idx, colorval;
	
	if (curtile & 0x4000)
	{
		tilepixels >>= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x0001) colorval |= 0x01;
			if (tilepixels & 0x0100) colorval |= 0x02;
			tilepixels >>= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
	else
	{
		tilepixels <<= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x0080) colorval |= 0x01;
			if (tilepixels & 0x8000) colorval |= 0x02;
			tilepixels <<= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
}

void PPU_RenderTile_4bpp(u16 curtile, u32 tilepixels, u16* buffer, u16* pal, u32 alpha, int start, int end)
{
	u32 idx, colorval;
	
	if (curtile & 0x4000)
	{
		tilepixels >>= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x00000001) colorval |= 0x01;
			if (tilepixels & 0x00000100) colorval |= 0x02;
			if (tilepixels & 0x00010000) colorval |= 0x04;
			if (tilepixels & 0x01000000) colorval |= 0x08;
			tilepixels >>= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
	else
	{
		tilepixels <<= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x00000080) colorval |= 0x01;
			if (tilepixels & 0x00008000) colorval |= 0x02;
			if (tilepixels & 0x00800000) colorval |= 0x04;
			if (tilepixels & 0x80000000) colorval |= 0x08;
			tilepixels <<= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
}

void PPU_RenderTile_8bpp(u16 curtile, u32 tilepixels1, u32 tilepixels2, u16* buffer, u16* pal, u32 alpha, int start, int end)
{
	u32 idx, colorval;
	
	if (curtile & 0x4000)
	{
		tilepixels1 >>= start;
		tilepixels2 >>= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels1 & 0x00000001) colorval |= 0x01;
			if (tilepixels1 & 0x00000100) colorval |= 0x02;
			if (tilepixels1 & 0x00010000) colorval |= 0x04;
			if (tilepixels1 & 0x01000000) colorval |= 0x08;
			if (tilepixels2 & 0x00000001) colorval |= 0x10;
			if (tilepixels2 & 0x00000100) colorval |= 0x20;
			if (tilepixels2 & 0x00010000) colorval |= 0x40;
			if (tilepixels2 & 0x01000000) colorval |= 0x80;
			tilepixels1 >>= 1;
			tilepixels2 >>= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
	else
	{
		tilepixels1 <<= start;
		tilepixels2 <<= start;
		
		for (idx = 0; idx < end; idx++)
		{
			colorval = 0;
			if (tilepixels1 & 0x00000080) colorval |= 0x01;
			if (tilepixels1 & 0x00008000) colorval |= 0x02;
			if (tilepixels1 & 0x00800000) colorval |= 0x04;
			if (tilepixels1 & 0x80000000) colorval |= 0x08;
			if (tilepixels2 & 0x00000080) colorval |= 0x10;
			if (tilepixels2 & 0x00008000) colorval |= 0x20;
			if (tilepixels2 & 0x00800000) colorval |= 0x40;
			if (tilepixels2 & 0x80000000) colorval |= 0x80;
			tilepixels1 <<= 1;
			tilepixels2 <<= 1;
			
			if (colorval)
				buffer[idx] = pal[colorval] | alpha;
		}
	}
}

void PPU_RenderTile_OBJ(u16 attrib, u32 tilepixels, u16* buffer, u16 paloffset_prio)
{
	if (!tilepixels) return;
	u32 idx, colorval;
	
	if (attrib & 0x4000)
	{
		for (idx = 0; idx < 8; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x00000001) colorval |= 0x01;
			if (tilepixels & 0x00000100) colorval |= 0x02;
			if (tilepixels & 0x00010000) colorval |= 0x04;
			if (tilepixels & 0x01000000) colorval |= 0x08;
			tilepixels >>= 1;
			
			if (colorval)
				buffer[idx] = colorval | paloffset_prio;
		}
	}
	else
	{
		for (idx = 0; idx < 8; idx++)
		{
			colorval = 0;
			if (tilepixels & 0x00000080) colorval |= 0x01;
			if (tilepixels & 0x00008000) colorval |= 0x02;
			if (tilepixels & 0x00800000) colorval |= 0x04;
			if (tilepixels & 0x80000000) colorval |= 0x08;
			tilepixels <<= 1;
			
			if (colorval)
				buffer[idx] = colorval | paloffset_prio;
		}
	}
}

inline void PPU_DeferTile(PPU_Background* bg, u16* buffer, u16 attrib, u16* pixels, u32 alpha, int start, int end)
{
	PPU_DeferredTile* deftile = &(bg->DeferredTiles[bg->NumDeferredTiles]);
	deftile->Dest = buffer;
	deftile->SrcPixels = pixels;
	deftile->Attrib = attrib;
	deftile->Alpha = (u8)alpha;
	deftile->Start = (u16)start;
	deftile->End = (u16)end;
	bg->NumDeferredTiles++;
}


void PPU_RenderBG_2bpp_8x8(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0xF8) << 2;
	if (line & 0x100)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 7;
				int end = 8 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0xF8) >> 3;
				if (xoff & 0x100)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 3;
				if (curtile & 0x8000) 	idx += (7 - tiley);
				else					idx += tiley;
				
				if (curtile & 0x2000) // high prio
				{
					PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
				}
				else
				{
					tilepixels = tileset[idx];
					if (tilepixels)
						PPU_RenderTile_2bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 8], finalalpha, start, end);
				}
				
				i += end;
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}

void PPU_RenderBG_2bpp_16x16(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0x1F0) << 1;
	if (line & 0x200)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 15;
				int end = 16 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0x1F0) >> 4;
				if (xoff & 0x200)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 3;
				if (curtile & 0x8000) 
				{
					idx += (7 - tiley);
					if (!(line & 0x08)) idx += 128;
				}
				else
				{
					idx += tiley;
					if (line & 0x08) idx += 128;
				}
				
				if (curtile & 0x4000) idx += 8;
				
				if (curtile & 0x2000) // high prio
				{
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 8;
						else                  idx += 8;
						
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
						i += end;
					}
				}
				else
				{
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						
						tilepixels = tileset[idx];
						if (tilepixels)
							PPU_RenderTile_2bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 8], finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 8;
						else                  idx += 8;
						
						tilepixels = tileset[idx];
						if (tilepixels)
							PPU_RenderTile_2bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 8], finalalpha, start, end);
							
						i += end;
					}
				}
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}

void PPU_RenderBG_4bpp_8x8(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0xF8) << 2;
	if (line & 0x100)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 7;
				int end = 8 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0xF8) >> 3;
				if (xoff & 0x100)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 4;
				if (curtile & 0x8000) 	idx += (7 - tiley);
				else					idx += tiley;
				
				if (curtile & 0x2000) // high prio
				{
					PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
				}
				else
				{
					tilepixels = tileset[idx] | (tileset[idx+8] << 16);
					if (tilepixels)
						PPU_RenderTile_4bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 6], finalalpha, start, end);
				}
				
				i += end;
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}

void PPU_RenderBG_4bpp_16x16(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0x1F0) << 1;
	if (line & 0x200)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 15;
				int end = 16 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0x1F0) >> 4;
				if (xoff & 0x200)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 4;
				if (curtile & 0x8000) 
				{
					idx += (7 - tiley);
					if (!(line & 0x08)) idx += 256;
				}
				else
				{
					idx += tiley;
					if (line & 0x08) idx += 256;
				}
				
				if (curtile & 0x4000) idx += 16;
				
				if (curtile & 0x2000) // high prio
				{
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 16;
						else                  idx += 16;
						
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
						i += end;
					}
				}
				else
				{
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						
						tilepixels = tileset[idx] | (tileset[idx+8] << 16);
						if (tilepixels)
							PPU_RenderTile_4bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 6], finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 16;
						else                  idx += 16;
						
						tilepixels = tileset[idx] | (tileset[idx+8] << 16);
						if (tilepixels)
							PPU_RenderTile_4bpp(curtile, tilepixels, &buffer[i], &pal[(curtile & 0x1C00) >> 6], finalalpha, start, end);
							
						i += end;
					}
				}
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}

void PPU_RenderBG_8bpp_8x8(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels1, tilepixels2;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0xF8) << 2;
	if (line & 0x100)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 7;
				int end = 8 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0xF8) >> 3;
				if (xoff & 0x100)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 5;
				if (curtile & 0x8000) 	idx += (7 - tiley);
				else					idx += tiley;
				
				if (curtile & 0x2000) // high prio
				{
					PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
				}
				else
				{
					// TODO: palette offset for direct color
					
					tilepixels1 = tileset[idx] | (tileset[idx+8] << 16);
					tilepixels2 = tileset[idx+16] | (tileset[idx+24] << 16);
					
					if (tilepixels1|tilepixels2)
						PPU_RenderTile_8bpp(curtile, tilepixels1, tilepixels2, &buffer[i], pal, finalalpha, start, end);
				}
				
				i += end;
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}

void PPU_RenderBG_8bpp_16x16(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels1, tilepixels2;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0x1F0) << 1;
	if (line & 0x200)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = 0;
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		int remaining = s->EndOffset - i;
		u16 hidden = window ? (bg->WindowCombine & (1 << (s->WindowMask ^ bg->WindowMask))) : 0;
		
		if (hidden)
		{
			i += remaining;
			xoff += remaining;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:alpha;
			
			while (remaining > 0)
			{
				int start = xoff & 15;
				int end = 16 - start;
				if (remaining < end) end = remaining;
				remaining -= end;
				
				idx = (xoff & 0x1F0) >> 4;
				if (xoff & 0x200)
				{
					if (bg->Size & 0x1)
						idx += 1024;
				}
				xoff += end;
				curtile = tilemap[idx];
				
				idx = (curtile & 0x3FF) << 5;
				if (curtile & 0x8000) 
				{
					idx += (7 - tiley);
					if (!(line & 0x08)) idx += 512;
				}
				else
				{
					idx += tiley;
					if (line & 0x08) idx += 512;
				}
				
				if (curtile & 0x4000) idx += 32;
				
				if (curtile & 0x2000) // high prio
				{
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 32;
						else                  idx += 32;
						
						PPU_DeferTile(bg, &buffer[i], curtile, &tileset[idx], finalalpha, start, end);
						i += end;
					}
				}
				else
				{
					// TODO: palette offset for direct color
					
					if (start < 8)
					{
						int end1 = 8-start;
						if (end1 > end) end1 = end;
						
						tilepixels1 = tileset[idx] | (tileset[idx+8] << 16);
						tilepixels2 = tileset[idx+16] | (tileset[idx+24] << 16);
						
						if (tilepixels1|tilepixels2)
							PPU_RenderTile_8bpp(curtile, tilepixels1, tilepixels2, &buffer[i], pal, finalalpha, start, end1);
						
						start = 0;
						end -= end1;
						i += end1;
					}
					else
						start -= 8;
					
					if (end > 0)
					{
						if (curtile & 0x4000) idx -= 32;
						else                  idx += 32;
						
						tilepixels1 = tileset[idx] | (tileset[idx+8] << 16);
						tilepixels2 = tileset[idx+16] | (tileset[idx+24] << 16);
						
						if (tilepixels1|tilepixels2)
							PPU_RenderTile_8bpp(curtile, tilepixels1, tilepixels2, &buffer[i], pal, finalalpha, start, end);
							
						i += end;
					}
				}
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}


void PPU_RenderBG_Mode7(u16* buffer, u32 line, u16* pal, u32 alpha, u32 window)
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
					if ((PPU.M7Sel & 0xC0) == 0x40)
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


void PPU_RenderDeferredTiles_2bpp(PPU_Background* bg, u16* pal)
{
	PPU_DeferredTile* deftile = &(bg->DeferredTiles[0]);
	PPU_DeferredTile* end = &(bg->DeferredTiles[bg->NumDeferredTiles]);
	
	for (; deftile < end; deftile++)
	{
		u16 tilepixels = deftile->SrcPixels[0];
		if (!tilepixels) continue;
		
		u16 attrib = deftile->Attrib;
		PPU_RenderTile_2bpp(attrib, tilepixels, deftile->Dest, &pal[(attrib & 0x1C00) >> 8], deftile->Alpha, deftile->Start, deftile->End);
	}
	
	bg->NumDeferredTiles = 0;
}

void PPU_RenderDeferredTiles_4bpp(PPU_Background* bg, u16* pal)
{
	PPU_DeferredTile* deftile = &(bg->DeferredTiles[0]);
	PPU_DeferredTile* end = &(bg->DeferredTiles[bg->NumDeferredTiles]);
	
	for (; deftile < end; deftile++)
	{
		u32 tilepixels = deftile->SrcPixels[0] | (deftile->SrcPixels[8] << 16);
		if (!tilepixels) continue;
		
		u16 attrib = deftile->Attrib;
		PPU_RenderTile_4bpp(attrib, tilepixels, deftile->Dest, &pal[(attrib & 0x1C00) >> 6], deftile->Alpha, deftile->Start, deftile->End);
	}
	
	bg->NumDeferredTiles = 0;
}

void PPU_RenderDeferredTiles_8bpp(PPU_Background* bg, u16* pal)
{
	PPU_DeferredTile* deftile = &(bg->DeferredTiles[0]);
	PPU_DeferredTile* end = &(bg->DeferredTiles[bg->NumDeferredTiles]);
	
	for (; deftile < end; deftile++)
	{
		u32 tilepixels1 = deftile->SrcPixels[0] | (deftile->SrcPixels[8] << 16);
		u32 tilepixels2 = deftile->SrcPixels[16] | (deftile->SrcPixels[24] << 16);
		if (!(tilepixels1|tilepixels2)) continue;
		
		// TODO: pal for direct color
		
		u16 attrib = deftile->Attrib;
		PPU_RenderTile_8bpp(attrib, tilepixels1, tilepixels2, deftile->Dest, pal, deftile->Alpha, deftile->Start, deftile->End);
	}
	
	bg->NumDeferredTiles = 0;
}


int PPU_RenderOBJ(u8* oam, u32 oamextra, u32 ymask, u16* buffer, u32 line)
{
	u16* tileset = PPU.OBJTileset;
	s32 xoff;
	u16 attrib;
	u32 tilepixels;
	u32 idx;
	s32 i;
	s32 width = (s32)PPU.OBJWidth[(oamextra & 0x2) >> 1];
	u32 paloffset, prio;
	
	xoff = oam[0];
	if (oamextra & 0x1) // xpos bit8, sign bit
	{
		xoff = 0x100 - xoff;
		if (xoff >= width) return 0;
		i = -xoff;
	}
	else
		i = xoff;
		
	attrib = *(u16*)&oam[2];
	
	idx = (attrib & 0x01FF) << 4;
	
	if (attrib & 0x8000) line = ymask - line;
	idx += (line & 0x07) | ((line & 0x38) << 5);
	
	if (attrib & 0x4000)
		idx += ((width-1) & 0x38) << 1;
		
	width += i;
	if (width > 256) width = 256;
	
	paloffset = ((oam[3] & 0x0E) << 3);
	
	prio = oam[3] & 0x30;
	PPU.SpritesOnLine[prio >> 4] = 1;
	
	for (; i < width;)
	{
		// skip offscreen tiles
		if (i <= -8)
		{
			i += 8;
			idx += (attrib & 0x4000) ? -16:16;
			continue;
		}
		
		tilepixels = tileset[idx] | (tileset[idx+8] << 16);
		PPU_RenderTile_OBJ(attrib, tilepixels, &buffer[i], paloffset | (prio << 8));
		i += 8;
		idx += (attrib & 0x4000) ? -16:16;
	}
	
	return 1;
}

void PPU_PrerenderOBJs(u16* buf, s32 line)
{
	int i = PPU.FirstOBJ;
	i--;
	if (i < 0) i = 127;
	int last = i;
	int nrendered = 0;

	do
	{
		u8* oam = &PPU.OAM[i << 2];
		u8 oamextra = PPU.OAM[0x200 + (i >> 2)] >> ((i & 0x03) << 1);
		s32 oy = (s32)oam[1] + 1;
		s32 oh = (s32)PPU.OBJHeight[(oamextra & 0x2) >> 1];
		
		if (line >= oy && line < (oy+oh))
		{
			if (nrendered >= 32)
			{
				PPU.OBJOverflow |= 0x40;
				return;
			}
			nrendered += PPU_RenderOBJ(oam, oamextra, oh-1, buf, line-oy);
		}
		else if (oy >= 192)
		{
			oy -= 0x100;
			if ((oy+oh) > 1 && line < (oy+oh))
			{
				if (nrendered >= 32)
				{
					PPU.OBJOverflow |= 0x40;
					return;
				}
				nrendered += PPU_RenderOBJ(oam, oamextra, oh-1, buf, line-oy);
			}
		}

		i--;
		if (i < 0) i = 127;
	}
	while (i != last);
}

void PPU_RenderOBJs(u16* buf, u32 line, u32 prio, u32 colmathmask, u32 window)
{
	int i = 0;
	
	if (!PPU.SpritesOnLine[prio >> 12]) return;
	
	u16* srcbuf = &PPU.OBJBuffer[16];
	u16* pal = &PPU.Palette[128];
	
	PPU_WindowSegment* s = &PPU.Window[0];
	for (;;)
	{
		u16 hidden = window ? (PPU.OBJWindowCombine & (1 << (s->WindowMask ^ PPU.OBJWindowMask))) : 0;
		
		if (hidden)
		{
			i = s->EndOffset;
		}
		else
		{
			u32 finalalpha = (PPU.ColorMath1 & s->ColorMath) ? 0:1;
			
			while (i < s->EndOffset)
			{
				u16 val = srcbuf[i];
				if ((val & 0xFF00) == prio)
					buf[i] = pal[val & 0xFF] | ((val & colmathmask) ? finalalpha:0);
				i++;
			}
		}
		
		if (s->EndOffset >= 256) return;
		s++;
	}
}


// TODO maybe optimize? precompute that whenever the PPU mode is changed?
#define PPU_RENDERBG(depth, num, pal) \
	{ \
		if (PPU.Mode & (0x10<<num)) \
			PPU_RenderBG_##depth##_16x16(&PPU.BG[num], buf, line, &PPU.Palette[pal], COLMATH(num), screen&(0x100<<num)); \
		else \
			PPU_RenderBG_##depth##_8x8(&PPU.BG[num], buf, line, &PPU.Palette[pal], COLMATH(num), screen&(0x100<<num)); \
	}

void PPU_RenderMode0(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x08) PPU_RENDERBG(2bpp, 3, 96);
	if (screen & 0x04) PPU_RENDERBG(2bpp, 2, 64);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x08) PPU_RenderDeferredTiles_2bpp(&PPU.BG[3], &PPU.Palette[96]);
	if (screen & 0x04) PPU_RenderDeferredTiles_2bpp(&PPU.BG[2], &PPU.Palette[64]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RENDERBG(2bpp, 1, 32);
	if (screen & 0x01) PPU_RENDERBG(2bpp, 0, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RenderDeferredTiles_2bpp(&PPU.BG[1], &PPU.Palette[32]);
	if (screen & 0x01) PPU_RenderDeferredTiles_2bpp(&PPU.BG[0], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
}

void PPU_RenderMode1(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x04) PPU_RENDERBG(2bpp, 2, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x04) 
	{
		if (!(PPU.Mode & 0x08))
			PPU_RenderDeferredTiles_2bpp(&PPU.BG[2], &PPU.Palette[0]);
	}
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RENDERBG(4bpp, 1, 0);
	if (screen & 0x01) PPU_RENDERBG(4bpp, 0, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RenderDeferredTiles_4bpp(&PPU.BG[1], &PPU.Palette[0]);
	if (screen & 0x01) PPU_RenderDeferredTiles_4bpp(&PPU.BG[0], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x04)
	{
		if (PPU.Mode & 0x08)
			PPU_RenderDeferredTiles_2bpp(&PPU.BG[2], &PPU.Palette[0]);
	}
}

// TODO: offset per tile, someday
void PPU_RenderMode2(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x02) PPU_RENDERBG(4bpp, 1, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RENDERBG(4bpp, 0, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RenderDeferredTiles_4bpp(&PPU.BG[1], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RenderDeferredTiles_4bpp(&PPU.BG[0], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
}

void PPU_RenderMode3(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x02) PPU_RENDERBG(4bpp, 1, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RENDERBG(8bpp, 0, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RenderDeferredTiles_4bpp(&PPU.BG[1], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RenderDeferredTiles_8bpp(&PPU.BG[0], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
}

// TODO: offset per tile, someday
void PPU_RenderMode4(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x02) PPU_RENDERBG(2bpp, 1, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RENDERBG(8bpp, 0, 0);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x02) PPU_RenderDeferredTiles_2bpp(&PPU.BG[1], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RenderDeferredTiles_8bpp(&PPU.BG[0], &PPU.Palette[0]);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
}

void PPU_RenderMode7(u16* buf, u32 line, u16 screen, u8 colormath)
{
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000, COLMATH_OBJ, (screen&0x1000));
	
	if (screen & 0x01) PPU_RenderBG_Mode7(buf, line, &PPU.Palette[0], COLMATH(0), (screen&0x100));
	
	if (screen & 0x10)
	{
		PPU_RenderOBJs(buf, line, 0x1000, COLMATH_OBJ, (screen&0x1000));
		PPU_RenderOBJs(buf, line, 0x2000, COLMATH_OBJ, (screen&0x1000));
		PPU_RenderOBJs(buf, line, 0x3000, COLMATH_OBJ, (screen&0x1000));
	}
}


void PPU_RenderScanline(u32 line)
{
	if (SkipThisFrame) return;
	if (!(line&7)) RenderPipeline();
	
	PPU.Brightness[line] = PPU.CurBrightness;
	if (!PPU.CurBrightness) return;
	
	if (PPU.WindowDirty)
	{
		PPU_ComputeWindows();
		PPU.WindowDirty = 0;
	}
	
	PPU.Subtract = (PPU.ColorMath2 & 0x80);
	
	int i;
	u16* mbuf = &PPU.MainBuffer[line << 8];
	u16* sbuf = &PPU.SubBuffer[line << 8];
	
	// main backdrop
	u32 backdrop = PPU.Palette[0];
	if (PPU.ColorMath2 & 0x20)
	{
		PPU_WindowSegment* s = &PPU.Window[0];
		i = 0;
		for (;;)
		{
			u32 alpha = (PPU.ColorMath1 & s->ColorMath) ? 0:1;
			while (i < s->EndOffset)
				mbuf[i++] = backdrop|alpha;
			
			if (s->EndOffset >= 256) break;
			s++;
		}
	}
	else
	{
		backdrop |= (backdrop << 16);
		for (i = 0; i < 256; i += 2)
			*(u32*)&mbuf[i] = backdrop;
	}
	
	// sub backdrop
	backdrop = PPU.SubBackdrop;
	backdrop |= (backdrop << 16);
	for (i = 0; i < 256; i += 2)
		*(u32*)&sbuf[i] = backdrop;
	
	for (i = 16; i < 272; i += 2)
		*(u32*)&PPU.OBJBuffer[i] = 0xFFFFFFFF;
		
	*(u32*)&PPU.SpritesOnLine[0] = 0;
	
	
	if ((PPU.MainScreen|PPU.SubScreen) & 0x10)
		PPU_PrerenderOBJs(&PPU.OBJBuffer[16], line);
		
	u8 colormathmain, colormathsub, rendersub;
	if ((PPU.ColorMath1 & 0x30) == 0x30) // color math completely disabled
	{
		colormathmain = 0;
		rendersub = 0;
	}
	else
	{
		colormathmain = PPU.ColorMath2 & 0x0F;
		if (PPU.ColorMath2 & 0x10) colormathmain |= 0x40;
		colormathsub = (PPU.ColorMath2 & 0x40) ? 0:0xFF;
		rendersub = (PPU.ColorMath1 & 0x02);
	}
	
	switch (PPU.Mode & 0x07)
	{
		case 0:
			if (rendersub)
				PPU_RenderMode0(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode0(mbuf, line, PPU.MainScreen, colormathmain);
			break;
			
		case 1:
			if (rendersub)
				PPU_RenderMode1(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode1(mbuf, line, PPU.MainScreen, colormathmain);
			break;
		
		case 2:
			if (rendersub)
				PPU_RenderMode2(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode2(mbuf, line, PPU.MainScreen, colormathmain);
			break;
			
		case 3:
			if (rendersub)
				PPU_RenderMode3(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode3(mbuf, line, PPU.MainScreen, colormathmain);
			break;
			
		case 4:
			if (rendersub)
				PPU_RenderMode4(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode4(mbuf, line, PPU.MainScreen, colormathmain);
			break;
			
		// TODO: mode 5/6 (hires)
		
		case 7:
			if (rendersub)
				PPU_RenderMode7(sbuf, line, PPU.SubScreen, colormathsub);
			PPU_RenderMode7(mbuf, line, PPU.MainScreen, colormathmain);
			break;
	}
	
	// that's all folks.
	// color math and master brightness will be done in hardware from now on
}

void PPU_VBlank()
{
	if (!SkipThisFrame)
		RenderPipelineVBlank();
	
	PPU.OAMAddr = PPU.OAMReload;
	
	if (!PPU.ForcedBlank)
		PPU.OBJOverflow = 0;
}
