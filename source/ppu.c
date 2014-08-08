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

#include <ctr/types.h>

#include "snes.h"


extern u8* TopFB;


u32 Mem_WRAMAddr = 0;


u16 PPU_VCount = 0;

u16 PPU_ColorTable[0x10000];

u16 PPU_MainBuffer[256];
u16 PPU_SubBuffer[256];

u16 PPU_CGRAMAddr = 0;
u8 PPU_CGRAMVal = 0;
u16 PPU_CGRAM[256];

u16 PPU_VRAMAddr = 0;
u16 PPU_VRAMPref = 0;
u8 PPU_VRAMInc = 0;
u8 PPU_VRAMStep = 0;
u8 PPU_VRAM[0x10000];


u16 PPU_SubBackdrop = 0;

u8 PPU_BGOld = 0;


typedef struct
{
	u16* Tileset;
	u16* Tilemap;
	u8 Size;
	
	u16 XScroll, YScroll;

} PPU_Background;
PPU_Background PPU_BG[4];



void PPU_Reset()
{
	int i;
	
	memset(&PPU_BG[0], 0, sizeof(PPU_Background)*4);
	
	for (i = 0; i < 4; i++)
	{
		PPU_BG[i].Tileset = (u16*)PPU_VRAM;
		PPU_BG[i].Tilemap = (u16*)PPU_VRAM;
	}
}


inline void PPU_SetXScroll(int nbg, u8 val)
{
	/*u32 m7stuff = 0;
	
	if (nbg == 0)
	{
		m7stuff = (val << 8) | PPU_M7Old;
		PPU_M7Old = val;
		
		if (PPU_ModeNow == 7)
		{
			PPU_ScheduleLineChange(PPU_SetM7ScrollX, m7stuff);
			return;
		}
	}*/
	
	PPU_Background* bg = &PPU_BG[nbg];
	
	bg->XScroll = (val << 8) | (PPU_BGOld & 0xFFF8) | ((bg->XScroll >> 8) & 0x7);
	PPU_BGOld = val;
}

inline void PPU_SetYScroll(int nbg, u8 val)
{
	/*u32 m7stuff = 0;
	
	if (nbg == 0)
	{
		m7stuff = (val << 8) | PPU_M7Old;
		PPU_M7Old = val;
		
		if (PPU_ModeNow == 7)
		{
			PPU_ScheduleLineChange(PPU_SetM7ScrollY, m7stuff);
			return;
		}
	}*/
	
	PPU_Background* bg = &PPU_BG[nbg];
	
	bg->YScroll = (val << 8) | PPU_BGOld;
	PPU_BGOld = val;
}

inline void PPU_SetBGSCR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	bg->Size = (val & 0x03);
	bg->Tilemap = (u16*)&PPU_VRAM[(val & 0xFC) << 9];
}

inline void PPU_SetBGCHR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	bg->Tileset = (u16*)&PPU_VRAM[val << 13];
}


u8 PPU_Read8(u32 addr)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
	u8 ret = 0;
	switch (addr)
	{
		/*case 0x34: ret = PPU_MulResult & 0xFF; break;
		case 0x35: ret = (PPU_MulResult >> 8) & 0xFF; break;
		case 0x36: ret = (PPU_MulResult >> 16) & 0xFF; break;
		
		case 0x37:
			PPU_LatchHVCounters();
			break;
			
		case 0x38:
			ret = PPU_OAM[PPU_OAMAddr];
			PPU_OAMAddr++;
			break;*/
		
		case 0x39:
			{
				ret = PPU_VRAMPref & 0xFF;
				if (!(PPU_VRAMInc & 0x80))
				{
					PPU_VRAMPref = *(u16*)&PPU_VRAM[PPU_VRAMAddr];
					PPU_VRAMAddr += PPU_VRAMStep;
				}
			}
			break;
		case 0x3A:
			{
				ret = PPU_VRAMPref >> 8;
				if (PPU_VRAMInc & 0x80)
				{
					PPU_VRAMPref = *(u16*)&PPU_VRAM[PPU_VRAMAddr];
					PPU_VRAMAddr += PPU_VRAMStep;
				}
			}
			break;
			
		/*case 0x3C:
			if (PPU_OPHFlag)
			{
				PPU_OPHFlag = 0;
				ret = PPU_OPHCT >> 8;
			}
			else
			{
				PPU_OPHFlag = 1;
				ret = PPU_OPHCT & 0xFF;
			}
			break;
		case 0x3D:
			if (PPU_OPVFlag)
			{
				PPU_OPVFlag = 0;
				ret = PPU_OPVCT >> 8;
			}
			else
			{
				PPU_OPVFlag = 1;
				ret = PPU_OPVCT & 0xFF;
			}
			break;
			
		case 0x3E: ret = 0x01; break;
		case 0x3F: 
			ret = 0x01 | (ROM_Region ? 0x10 : 0x00) | PPU_OPLatch;
			PPU_OPLatch = 0;
			PPU_OPHFlag = 0;
			PPU_OPVFlag = 0;
			break;*/
		
		/*case 0x40: ret = IPC->SPC_IOPorts[4]; break;
		case 0x41: ret = IPC->SPC_IOPorts[5]; break;
		case 0x42: ret = IPC->SPC_IOPorts[6]; break;
		case 0x43: ret = IPC->SPC_IOPorts[7]; break;*/
		
		case 0x80: ret = SNES_SysRAM[Mem_WRAMAddr++]; break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
	return ret;
}

u16 PPU_Read16(u32 addr)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
	u16 ret = 0;
	switch (addr)
	{
		// not in the right place, but well
		// our I/O functions are mapped to the whole $21xx range
		
		/*case 0x40: ret = *(u16*)&IPC->SPC_IOPorts[4]; break;
		case 0x42: ret = *(u16*)&IPC->SPC_IOPorts[6]; break;*/
		
		default:
			ret = PPU_Read8(addr);
			ret |= (PPU_Read8(addr+1) << 8);
			break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
	return ret;
}

void PPU_Write8(u32 addr, u8 val)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
	switch (addr)
	{
		/*case 0x00: // force blank/master brightness
			{
				u16 mb;
				
				if (val & 0x80) val = 0;
				else val &= 0x0F;
				if (val == 0x0F)
					mb = 0x0000;
				else if (val == 0x00)
					mb = 0x8010;
				else
					mb = 0x8000 | (15 - (val & 0x0F));
					
				PPU_ScheduleLineChange(PPU_SetMasterBright, mb);
			}
			break;
			
		case 0x01:
			{
				if (PPU_OBJSize != (val >> 5))
				{
					PPU_OBJSize = val >> 5;
					PPU_OBJSizes = _PPU_OBJSizes + (PPU_OBJSize << 1);
					PPU_UpdateOBJSize();
				}
				
				u16 base = (val & 0x07) << 14;
				u16 gap = (val & 0x18) << 10;
				PPU_SetOBJCHR(base, gap);
				//iprintf("OBJ base:%08X gap:%08X | %08X\n", base, gap, (u32)&PPU_VRAM + base);
			}
			break;
			
		case 0x02:
			PPU_OAMAddr = (PPU_OAMAddr & 0x200) | (val << 1);
			PPU_OAMReload = PPU_OAMAddr;
			break;
		case 0x03:
			PPU_OAMAddr = (PPU_OAMAddr & 0x1FE) | ((val & 0x01) << 9);
			PPU_OAMPrio = val & 0x80;
			PPU_OAMReload = PPU_OAMAddr;
			break;
			
		case 0x04:
			if (PPU_OAMAddr >= 0x200)
			{
				u16 addr = PPU_OAMAddr;
				addr &= 0x21F;
				
				if (PPU_OAM[addr] != val)
				{
					PPU_OAM[addr] = val;
					PPU_UpdateOAM(addr, val);
					PPU_OAMDirty = 1;
				}
			}
			else if (PPU_OAMAddr & 0x1)
			{
				PPU_OAMVal |= (val << 8);
				u16 addr = PPU_OAMAddr - 1;
				
				if (*(u16*)&PPU_OAM[addr] != PPU_OAMVal)
				{
					*(u16*)&PPU_OAM[addr] = PPU_OAMVal;
					PPU_UpdateOAM(addr, PPU_OAMVal);
					PPU_OAMDirty = 1;
				}
			}
			else
			{
				PPU_OAMVal = val;
			}
			PPU_OAMAddr++;
			PPU_OAMAddr &= 0x3FF;
			break;
			
		case 0x05:
			PPU_ModeNow = val & 0x07;
			if (PPU_ModeNow != PPU_Mode)
			{
				if (PPU_Mode == 7) 
				{
					PPU_ScheduleLineChange(PPU_SwitchFromMode7, val);
					break;
				}
				else if (PPU_ModeNow == 7) 
				{
					PPU_ScheduleLineChange(PPU_SwitchToMode7, 0);
					break;
				}
			}
			if (val & 0xF0) iprintf("!! 16x16 TILES NOT SUPPORTED\n");
			PPU_ScheduleLineChange(PPU_SetBG3Prio, val);
			break;
			
		case 0x06: // mosaic
			{
				if (PPU_ModeNow == 7)
				{
					*(u16*)0x0400000C = 0xC082 | (2 << 2) | (24 << 8) | (val & 0x01) << 6;
				}
				else
				{
					int i;
					for (i = 0; i < 4; i++)
					{
						PPU_Background* bg = &PPU_BG[i];
						if (val & (1 << i)) bg->BGCnt |= 0x0040;
						else bg->BGCnt &= 0xFFFFFFBF;
						
						*bg->BGCNT = bg->BGCnt;
					}
				}
			
				*(vu16*)0x0400004C = (val & 0xF0) | (val >> 4);
			}
			break;*/
			
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
			if ((val & 0x7C) != 0x00) bprintf("UNSUPPORTED VRAM MODE %02X\n", val);
			PPU_VRAMInc = val;
			switch (val & 0x03)
			{
				case 0x00: PPU_VRAMStep = 2; break;
				case 0x01: PPU_VRAMStep = 64; break;
				case 0x02:
				case 0x03: PPU_VRAMStep = 256; break;
			}
			break;
			
		case 0x16:
			PPU_VRAMAddr &= 0xFE00;
			PPU_VRAMAddr |= (val << 1);
			PPU_VRAMPref = *(u16*)&PPU_VRAM[PPU_VRAMAddr];
			break;
		case 0x17:
			PPU_VRAMAddr &= 0x01FE;
			PPU_VRAMAddr |= ((val & 0x7F) << 9);
			PPU_VRAMPref = *(u16*)&PPU_VRAM[PPU_VRAMAddr];
			break;
		
		case 0x18: // VRAM shit
			{
				PPU_VRAM[PPU_VRAMAddr] = val;
				if (!(PPU_VRAMInc & 0x80))
					PPU_VRAMAddr += PPU_VRAMStep;
			}
			break;
		case 0x19:
			{
				PPU_VRAM[PPU_VRAMAddr+1] = val;
				if (PPU_VRAMInc & 0x80)
					PPU_VRAMAddr += PPU_VRAMStep;
			}
			break;
			
		/*case 0x1B: // multiply/mode7 shiz
			{
				u16 fval = (u16)(PPU_M7Old | (val << 8));
				PPU_MulA = (s16)fval;
				PPU_MulResult = (s32)PPU_MulA * (s32)PPU_MulB;
				PPU_ScheduleLineChange(PPU_SetM7A, fval);
				PPU_M7Old = val;
			}
			break;
		case 0x1C:
			PPU_ScheduleLineChange(PPU_SetM7B, (val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			PPU_MulB = (s8)val;
			PPU_MulResult = (s32)PPU_MulA * (s32)PPU_MulB;
			break;
		case 0x1D:
			PPU_ScheduleLineChange(PPU_SetM7C, (val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;
		case 0x1E:
			PPU_ScheduleLineChange(PPU_SetM7D, (val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;
			
		case 0x1F: // mode7 center
			PPU_ScheduleLineChange(PPU_SetM7RefX, (val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;
		case 0x20:
			PPU_ScheduleLineChange(PPU_SetM7RefY, (val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;*/
			
		case 0x21:
			PPU_CGRAMAddr = val << 1;
			break;
		
		case 0x22:
			if (!(PPU_CGRAMAddr & 0x1))
				PPU_CGRAMVal = val;
			else
				PPU_CGRAM[PPU_CGRAMAddr >> 1] = (val << 8) | PPU_CGRAMVal;

			PPU_CGRAMAddr++;
			PPU_CGRAMAddr &= ~0x200; // prevent overflow
			break;
		
		/*case 0x2C:
			PPU_ScheduleLineChange(PPU_SetMainScreen, val & 0x1F);
			break;
		case 0x2D:
			PPU_ScheduleLineChange(PPU_SetSubScreen, val & 0x1F);
			break;
			
		case 0x31:
			PPU_ScheduleLineChange(PPU_SetColorMath, val);
			break;*/
			
		case 0x32:
			{
				u8 intensity = val & 0x1F;
				if (val & 0x20) PPU_SubBackdrop = (PPU_SubBackdrop & ~0x001F) | intensity;
				if (val & 0x40) PPU_SubBackdrop = (PPU_SubBackdrop & ~0x03E0) | (intensity << 5);
				if (val & 0x80) PPU_SubBackdrop = (PPU_SubBackdrop & ~0x7C00) | (intensity << 10);
			}
			break;
			
		/*case 0x33: // SETINI
			if (val & 0x80) iprintf("!! PPU EXT SYNC\n");
			if (val & 0x40) iprintf("!! MODE7 EXTBG\n");
			if (val & 0x08) iprintf("!! PSEUDO HIRES\n");
			if (val & 0x02) iprintf("!! SMALL SPRITES\n");
			break;*/
			
		/*case 0x40: IPC->SPC_IOPorts[0] = val; break;
		case 0x41: IPC->SPC_IOPorts[1] = val; break;
		case 0x42: IPC->SPC_IOPorts[2] = val; break;
		case 0x43: IPC->SPC_IOPorts[3] = val; break;*/
		
		case 0x80: SNES_SysRAM[Mem_WRAMAddr++] = val; break;
		case 0x81: Mem_WRAMAddr = (Mem_WRAMAddr & 0x0001FF00) | val; break;
		case 0x82: Mem_WRAMAddr = (Mem_WRAMAddr & 0x000100FF) | (val << 8); break;
		case 0x83: Mem_WRAMAddr = (Mem_WRAMAddr & 0x0000FFFF) | ((val & 0x01) << 16); break;
				
		default:
			//iprintf("PPU_Write8(%08X, %08X)\n", addr, val);
			break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
}

void PPU_Write16(u32 addr, u16 val)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
	switch (addr)
	{
		// optimized route
		
		case 0x16:
			PPU_VRAMAddr = (val << 1) & 0xFFFEFFFF;
			PPU_VRAMPref = *(u16*)&PPU_VRAM[PPU_VRAMAddr];
			break;
			
		case 0x18:
			*(u16*)&PPU_VRAM[PPU_VRAMAddr] = val;
			PPU_VRAMAddr += PPU_VRAMStep;
			break;
			
		/*case 0x40: *(u16*)&IPC->SPC_IOPorts[0] = val; break;
		case 0x41: IPC->SPC_IOPorts[1] = val & 0xFF; IPC->SPC_IOPorts[2] = val >> 8; break;
		case 0x42: *(u16*)&IPC->SPC_IOPorts[2] = val; break;
		
		case 0x43: iprintf("!! write $21%02X %04X\n", addr, val); break;*/
		
		case 0x81: Mem_WRAMAddr = (Mem_WRAMAddr & 0x00010000) | val; break;
		
		// otherwise, just do two 8bit writes
		default:
			PPU_Write8(addr, val & 0x00FF);
			PPU_Write8(addr+1, val >> 8);
			break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
}



void PPU_RenderBG_2bpp_8x8(PPU_Background* bg, u16* buffer, u32 line, u16* pal)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	u32 colorval;
	u32 i;
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
	idx = (xoff & 0xF8) >> 3;
	if (xoff & 0x100)
	{
		if (bg->Size & 0x1)
			idx += 1024;
	}
	curtile = tilemap[idx];
	
	idx = (curtile & 0x3FF) << 3;
	if (curtile & 0x8000) 	idx += (7 - tiley);
	else					idx += tiley;
	
	tilepixels = tileset[idx];
	if (curtile & 0x4000)	tilepixels >>= (xoff & 0x7);
	else					tilepixels <<= (xoff & 0x7);
	
	for (i = 0; i < 256; i++)
	{
		colorval = 0;
		if (curtile & 0x4000) // hflip
		{
			if (tilepixels & 0x0001) colorval |= 0x01;
			if (tilepixels & 0x0100) colorval |= 0x02;
			tilepixels >>= 1;
		}
		else
		{
			if (tilepixels & 0x0080) colorval |= 0x01;
			if (tilepixels & 0x8000) colorval |= 0x02;
			tilepixels <<= 1;
		}
		
		if (colorval)
		{
			colorval |= (curtile & 0x1C00) >> 8;
			*buffer = pal[colorval];
		}
		buffer++;
		
		xoff++;
		if (!(xoff & 0x7)) // reload tile if needed
		{
			idx = xoff >> 3;
			if (xoff & 0x100)
			{
				if (bg->Size & 0x1)
					idx += 1024;
			}
			curtile = tilemap[idx];
			
			idx = (curtile & 0x3FF) << 3;
			if (curtile & 0x8000) 	idx += (7 - tiley);
			else					idx += tiley;
			
			tilepixels = tileset[idx];
		}
	}
}

void PPU_RenderBG_4bpp_8x8(PPU_Background* bg, u16* buffer, u32 line, u16* pal)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	u32 colorval;
	u32 i;
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
	idx = (xoff & 0xF8) >> 3;
	if (xoff & 0x100)
	{
		if (bg->Size & 0x1)
			idx += 1024;
	}
	curtile = tilemap[idx];
	
	idx = (curtile & 0x3FF) << 4;
	if (curtile & 0x8000) 	idx += (7 - tiley);
	else					idx += tiley;
	
	tilepixels = tileset[idx] | (tileset[idx+8] << 16);
	if (curtile & 0x4000)	tilepixels >>= (xoff & 0x7);
	else					tilepixels <<= (xoff & 0x7);
	
	for (i = 0; i < 256; i++)
	{
		colorval = 0;
		if (curtile & 0x4000) // hflip
		{
			if (tilepixels & 0x00000001) colorval |= 0x01;
			if (tilepixels & 0x00000100) colorval |= 0x02;
			if (tilepixels & 0x00010000) colorval |= 0x04;
			if (tilepixels & 0x01000000) colorval |= 0x08;
			tilepixels >>= 1;
		}
		else
		{
			if (tilepixels & 0x00000080) colorval |= 0x01;
			if (tilepixels & 0x00008000) colorval |= 0x02;
			if (tilepixels & 0x00800000) colorval |= 0x04;
			if (tilepixels & 0x80000000) colorval |= 0x08;
			tilepixels <<= 1;
		}
		
		if (colorval)
		{
			colorval |= (curtile & 0x1C00) >> 6;
			*buffer = pal[colorval];
		}
		buffer++;
		
		xoff++;
		if (!(xoff & 0x7)) // reload tile if needed
		{
			idx = xoff >> 3;
			if (xoff & 0x100)
			{
				if (bg->Size & 0x1)
					idx += 1024;
			}
			curtile = tilemap[idx];
			
			idx = (curtile & 0x3FF) << 4;
			if (curtile & 0x8000) 	idx += (7 - tiley);
			else					idx += tiley;
			
			tilepixels = tileset[idx] | (tileset[idx+8] << 16);
		}
	}
}

//int blarg=0;
void PPU_RenderScanline(u32 line)
{
	asm("stmdb sp!, {r12}");
	
	// test frameskip code
	/*if (line==0) blarg=!blarg;
	if (blarg)
	{
	asm("ldmia sp!, {r12}");
	return;
	}*/
	
	int i;
	u16* buf = &PPU_MainBuffer[0];
	
	u32 backdrop = PPU_CGRAM[0];
	backdrop |= (backdrop << 16);
	for (i = 0; i < 256; i += 2)
		*(u32*)&buf[i] = backdrop;
	
	PPU_RenderBG_4bpp_8x8(&PPU_BG[0], buf, line, &PPU_CGRAM[0]);
	PPU_RenderBG_2bpp_8x8(&PPU_BG[2], buf, line, &PPU_CGRAM[0]);
	
	// copy to final framebuffer
	u16* finalbuf = &((u16*)TopFB)[17512 - line];
	for (i = 0; i < 256; i++)
	{
		*finalbuf = PPU_ColorTable[*buf++];
		finalbuf += 240;
	}
	
	asm("ldmia sp!, {r12}");
}
