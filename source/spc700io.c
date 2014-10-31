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

#include <3ds/types.h>

#include "snes.h"
#include "spc700.h"
#include "dsp.h"


struct SPC_TimersStruct
{
	// 0x00
	u8 EnableMask;
	u8 __fgqgqdfh;
	
	// timer 0: 0x02
	// timer 1: 0x08
	// timer 2: 0x0E
	struct
	{
		u16 CycleCount;
		u16 Reload;
		u8 Val;
		u8 __zerfzergdf;
		
	} Timer[3];
	
} SPC_Timers;

u8 SPC_ROMAccess;
u8 SPC_DSPAddr;


void SPC_InitMisc()
{
	SPC_ROMAccess = 1;
	SPC_DSPAddr = 0;
	
	memset(&SPC_RAM[0], 0, 0x10040);
	memcpy(&SPC_RAM[0xFFC0], &SPC_ROM[0], 64);
	
	*(u32*)&SPC_IOPorts[0] = 0;
	*(u32*)&SPC_IOPorts[4] = 0;
	
	SPC_Timers.EnableMask = 0;
	SPC_Timers.Timer[0].CycleCount = 0;
	SPC_Timers.Timer[0].Reload = 0;
	SPC_Timers.Timer[0].Val = 0;
	SPC_Timers.Timer[1].CycleCount = 0;
	SPC_Timers.Timer[1].Reload = 0;
	SPC_Timers.Timer[1].Val = 0;
	SPC_Timers.Timer[2].CycleCount = 0;
	SPC_Timers.Timer[2].Reload = 0;
	SPC_Timers.Timer[2].Val = 0;
	
	DspReset();
}

u8 SPC_IORead8(u16 addr)
{
	u8 ret = 0;
	switch (addr)
	{
		case 0xF2: ret = SPC_DSPAddr; break;
		case 0xF3: ret = DSP_MEM[SPC_DSPAddr]; break;
		
		case 0xF4: ret = SPC_IOPorts[0]; break;
		case 0xF5: ret = SPC_IOPorts[1]; break;
		case 0xF6: ret = SPC_IOPorts[2]; break;
		case 0xF7: ret = SPC_IOPorts[3]; break;
		
		case 0xFD: ret = SPC_Timers.Timer[0].Val; SPC_Timers.Timer[0].Val = 0; break;
		case 0xFE: ret = SPC_Timers.Timer[1].Val; SPC_Timers.Timer[1].Val = 0; break;
		case 0xFF: ret = SPC_Timers.Timer[2].Val; SPC_Timers.Timer[2].Val = 0; break;
	}

	return ret;
}

u16 SPC_IORead16(u16 addr)
{
	u16 ret = 0;
	switch (addr)
	{
		case 0xF4: ret = *(u16*)&SPC_IOPorts[0]; break;
		case 0xF6: ret = *(u16*)&SPC_IOPorts[2]; break;
		
		default:
			ret = SPC_IORead8(addr);
			ret |= ((u16)SPC_IORead8(addr+1) << 8);
			break;
	}

	return ret;
}

void SPC_IOWrite8(u16 addr, u8 val)
{
	switch (addr)
	{
		case 0xF1:
			{
				SPC_Timers.EnableMask = val & 0x07;
				
				if (!(val & 0x01)) SPC_Timers.Timer[0].Val = 0;
				else { SPC_Timers.Timer[0].CycleCount = SPC_Timers.Timer[0].Reload; }
				if (!(val & 0x02)) SPC_Timers.Timer[1].Val = 0;
				else { SPC_Timers.Timer[1].CycleCount = SPC_Timers.Timer[1].Reload; }
				if (!(val & 0x04)) SPC_Timers.Timer[2].Val = 0;
				else { SPC_Timers.Timer[2].CycleCount = SPC_Timers.Timer[2].Reload; }
				
				if (val & 0x10) *(u16*)&SPC_IOPorts[0] = 0x0000;
				if (val & 0x20) *(u16*)&SPC_IOPorts[2] = 0x0000;
				
				SPC_ROMAccess = (val & 0x80) ? 1:0;
			}
			break;
			
		case 0xF2: SPC_DSPAddr = val; break;
		case 0xF3: DspWriteByte(val, SPC_DSPAddr); break;
			
		case 0xF4: SPC_IOPorts[4] = val; break;
		case 0xF5: SPC_IOPorts[5] = val; break;
		case 0xF6: SPC_IOPorts[6] = val; break;
		case 0xF7: SPC_IOPorts[7] = val; break;
		
		case 0xFA: SPC_Timers.Timer[0].Reload = val << 7; break;
		case 0xFB: SPC_Timers.Timer[1].Reload = val << 7; break;
		case 0xFC: SPC_Timers.Timer[2].Reload = val << 4; break;
	}
}

void SPC_IOWrite16(u16 addr, u16 val)
{
	switch (addr)
	{
		case 0xF4: *(u16*)&SPC_IOPorts[4] = val; break;
		case 0xF6: *(u16*)&SPC_IOPorts[6] = val; break;
		
		default:
			SPC_IOWrite8(addr, val & 0xFF);
			SPC_IOWrite8(addr+1, val >> 8);
			break;
	}
}
