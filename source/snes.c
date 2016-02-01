/*
    Copyright 2014-2015 StapleButter

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
#include <stdio.h>
#include <string.h>

#include "ui_console.h"
#include "mem.h"
#include "snes.h"
#include "cpu.h"
#include "ppu.h"
#include "spc700.h"


u8* ROM_Bank0;
u8* ROM_Bank0End;

u8 ROM_Region;

bool SNES_HiROM;
u8 SNES_SysRAM[0x20000] __attribute__((aligned(256)));
u32 SNES_SRAMMask;
u8* SNES_SRAM = NULL;

char SNES_SRAMPath[300];


// addressing: BBBBBBBB:AAAaaaaa:aaaaaaaa
// bit4-31: argument
// bit0: access speed (0 = 6 cycles, 1 = 8 cycles)
// bit1: special bit (0 = argument is a RAM pointer, 1 = other case)
// bit2: write permission (0 = can write, 1 = read-only)
// bit3: SRAM bit
// common cases:
// * b1=0, b2=0: system RAM, SRAM; arg = pointer to RAM
// * b1=1, b2=0: I/O, expansion RAM; arg = zero
// * b1=0, b2=1: ROM; arg = pointer to RAM
//
// cheat: we place stuff before the start of the actual array-- those 
// can be accessed quickly by the CPU core since it keeps a pointer to
// this table in one of the CPU registers
//
// table[-1] -> SRAM dirty flag
// table[-2] -> HBlank/VBlank flags
u32 _Mem_PtrTable[(SNESSTATUS_SIZE >> 2) + 0x800 + 0x1000];
u32* Mem_PtrTable;
SNES_StatusData* SNES_Status;

u8 SNES_HVBJOY = 0x00;
u8 SNES_WRIO = 0;

u8 SNES_AutoJoypad = 0;
u8 SNES_JoyBit = 0;
u32 SNES_JoyBuffer = 0;
u8 SNES_Joy16 = 0;

u8 SNES_MulA = 0;
u16 SNES_MulRes = 0;
u16 SNES_DivA = 0;
u16 SNES_DivRes = 0;

bool SNES_FastROM = false;

extern u8 DMA_HDMAFlag;
extern u8 DMA_HDMACurFlag;
extern u8 DMA_HDMAEnded;

// execution trap
// I/O regions are mapped to this buffer, so that when an accidental jump to those regions occurs,
// we can trace it instead of just crashing
#ifdef OPENBUS_EXEC_TRAP
u8 SNES_ExecTrap[8192] __attribute__((aligned(256)));
#else
#define SNES_ExecTrap ((u8*)NULL)
#endif


void SNES_Init()
{
	// TODO get rid of this junk!
	SNES_Status = (SNES_StatusData *)&_Mem_PtrTable[0];
	Mem_PtrTable = &_Mem_PtrTable[SNESSTATUS_SIZE >> 2];
}


bool SNES_LoadROM(char* path)
{
	if (!ROM_LoadFile(path))
		return false;
		
	ROM_Bank0 = ROM_Buffer;
	ROM_Bank0End = ROM_Bank0 + (SNES_HiROM ? 0x10000:0x8000);

	u8 sramsize = ROM_Buffer[ROM_HeaderOffset + 0x18];
	u8 region = ROM_Buffer[ROM_HeaderOffset + 0x19];
	
	if (region <= 0x01 || (region >= 0x0D && region <= 0x10))
		ROM_Region = 0;
	else
		ROM_Region = 1;
		
	SNES_Status->TotalLines = (ROM_Region ? 312 : 262) >> 1;
	SNES_Status->ScreenHeight = 224;
	
	//SNES_Status->SPC_CycleRatio = ROM_Region ? 0x000C51D9 : 0x000C39C6;
	//SNES_Status->SPC_CycleRatio += 0x1000; // hax -- TODO investigate why we need this to run at a somewhat proper rate
	SNES_Status->SPC_CycleRatio = 6400;//6418;//6400;//ROM_Region ? 132990 : 134013;
	SNES_Status->SPC_CyclesPerLine = SNES_Status->SPC_CycleRatio * 1364;
	//SNES_Status->SPC_CyclesPerLine = ROM_Region ? 0x41A41A42 : 0x4123D3B5;
	
	SPC_CycleRatio = ROM_Region ? 132990 : 134013;
	
	SNES_SRAMMask = sramsize ? ((1024 << sramsize) - 1) : 0;
	SNES_SRAMMask &= 0x000FFFFF;
	bprintf("SRAM size: %luKB\n", (SNES_SRAMMask+1) >> 10);
	
	if (SNES_SRAMMask)
	{
		strncpy(SNES_SRAMPath, path, strlen(path)-3);
		strncpy(SNES_SRAMPath + strlen(path)-3, "srm", 3);
		SNES_SRAMPath[strlen(path)] = '\0';

		FILE *pFile = fopen(SNES_SRAMPath, "rb+");
		if(pFile == NULL)
		{
			pFile = fopen(SNES_SRAMPath, "wb");
			if(pFile == NULL)
				bprintf("Error while trying to open the savefile.\nMake sure it isn't read-only.\n");
			else
			{
				u8* temp = linearAlloc(SNES_SRAMMask + 1);
				memset(temp, 0, SNES_SRAMMask + 1);
				fwrite(temp, sizeof(char), SNES_SRAMMask + 1, pFile);
				linearFree(temp);
			}
		}
		if(pFile != NULL)
			fclose(pFile);
	}

	return true;
}

void SNES_Reset()
{
	u32 i, a, b;
	
	// generate random garbage to fill the RAM with
	u64 t = osGetTime();
	u32 randblarg = (u32)(t ^ (t >> 32ULL) ^ (t >> 19ULL) ^ (t << 7ULL) ^ (t >> 53ULL));

	for (i = 0; i < (128 * 1024); i += 4)
	{
		*(u32*)&SNES_SysRAM[i] = randblarg ^ (randblarg << 15) ^ (randblarg << 26) ^ (randblarg * 0x00700000);
		randblarg = (randblarg * 0x17374) ^ (randblarg * 0x327) ^ (randblarg << 2) ^ (randblarg << 17);
	}

	
	// fill it with STP opcodes
#ifdef OPENBUS_EXEC_TRAP
	memset(SNES_ExecTrap, 0xDB, 8192);
#endif
		
	SNES_FastROM = false;
	
	DMA_HDMAFlag = 0;

	if (SNES_SRAM) 
	{
		linearFree(SNES_SRAM);
		SNES_SRAM = NULL;
	}
	if (SNES_SRAMMask)
	{
		SNES_SRAM = (u8*)linearAlloc(SNES_SRAMMask + 1);
		for (i = 0; i <= SNES_SRAMMask; i += 4)
			*(u32*)&SNES_SRAM[i] = 0;
		
		FILE *pFile = fopen(SNES_SRAMPath, "rb");
		if(pFile != NULL)
		{
			fread(SNES_SRAM, sizeof(char), SNES_SRAMMask + 1, pFile);
			fclose(pFile);
		}

	}
	
	SNES_Status->SRAMDirty = 0;
	SNES_Status->HVBFlags = 0x00;
	SNES_Status->SRAMMask = SNES_SRAMMask;
	SNES_Status->IRQCond = 0;
	
	SNES_Status->VCount = 0;
	SNES_Status->HCount = 0;
	SNES_Status->IRQ_VMatch = 0;
	SNES_Status->IRQ_HMatch = 0;
	SNES_Status->IRQ_CurHMatch = 0x8000;
	
	SNES_Status->SPC_LastCycle = 0;
	
	for (b = 0; b < 0x40; b++)
	{
		MEM_PTR(b, 0x0000) = MEM_PTR(0x80 + b, 0x0000) = MPTR_SLOW | (u32)&SNES_SysRAM[0];
		MEM_PTR(b, 0x2000) = MEM_PTR(0x80 + b, 0x2000) = MPTR_SPECIAL | (u32)&SNES_ExecTrap[0];
		MEM_PTR(b, 0x4000) = MEM_PTR(0x80 + b, 0x4000) = MPTR_SPECIAL | (u32)&SNES_ExecTrap[0];
		
		if ((b >= 0x30) && SNES_HiROM && SNES_SRAMMask)
			MEM_PTR(b, 0x6000) = MEM_PTR(0x80 + b, 0x6000) = MPTR_SLOW | MPTR_SRAM | (u32)&SNES_SRAM[(b << 13) & SNES_SRAMMask];
		else
			MEM_PTR(b, 0x6000) = MEM_PTR(0x80 + b, 0x6000) = MPTR_SLOW | MPTR_SPECIAL | (u32)&SNES_ExecTrap[0];
	}

	if (SNES_HiROM)
	{
		for (b = 0; b < 0x02; b++)
			for (a = 0; a < 0x10000; a += 0x2000)
				MEM_PTR(0x7E + b, a) = MPTR_SLOW | (u32)&SNES_SysRAM[(b << 16) + a];
	}
	else
	{
		if (SNES_SRAMMask)
		{
			for (b = 0; b < 0x0E; b++)
				for (a = 0; a < 0x8000; a += 0x2000)
					MEM_PTR(0x70 + b, a) = MEM_PTR(0xF0 + b, a) = MPTR_SLOW | MPTR_SRAM | (u32)&SNES_SRAM[((b << 15) + a) & SNES_SRAMMask];
			for (a = 0; a < 0x8000; a += 0x2000)
			{
				MEM_PTR(0xFE + b, a) = MPTR_SLOW | MPTR_SRAM | (u32)&SNES_SRAM[((0xE << 15) + a) & SNES_SRAMMask];
				MEM_PTR(0xFF + b, a) = MPTR_SLOW | MPTR_SRAM | (u32)&SNES_SRAM[((0xF << 15) + a) & SNES_SRAMMask];
			}
		}
		else
		{
			for (b = 0; b < 0x0E; b++)
				for (a = 0; a < 0x8000; a += 0x2000)
					MEM_PTR(0x70 + b, a) = MEM_PTR(0xF0 + b, a) = MPTR_SLOW | MPTR_SPECIAL | (u32)&SNES_ExecTrap[0];
			for (a = 0; a < 0x8000; a += 0x2000)
			{
				MEM_PTR(0xFE + b, a) = MPTR_SLOW | MPTR_SPECIAL | (u32)&SNES_ExecTrap[0];
				MEM_PTR(0xFF + b, a) = MPTR_SLOW | MPTR_SPECIAL | (u32)&SNES_ExecTrap[0];
			}
		}

		for (b = 0; b < 0x02; b++)
			for (a = 0; a < 0x10000; a += 0x2000)
				MEM_PTR(0x7E + b, a) = MEM_PTR(0xFE + b, a) = MPTR_SLOW | (u32)&SNES_SysRAM[(b << 16) + a];
	}
	
	SNES_HVBJOY = 0x00;
	SNES_WRIO = 0;
	
	SNES_MulA = 0;
	SNES_MulRes = 0;
	SNES_DivA = 0;
	SNES_DivRes = 0;
	
	SNES_AutoJoypad = 0;
	SNES_JoyBit = 0;
	SNES_JoyBuffer = 0;
	SNES_Joy16 = 0;



	PPU_Reset();

}


void SNES_SaveSRAM()
{
	if (!SNES_SRAMMask) 
		return;
	
	if (!SNES_Status->SRAMDirty)
		return;
	
	FILE *pFile = fopen(SNES_SRAMPath, "wb");
	if(pFile != NULL)
	{
		fwrite(SNES_SRAM, sizeof(char), SNES_SRAMMask + 1, pFile);
		fclose(pFile);
		bprintf("SRAM saved\n");
	}
	else
		bprintf("SRAM save failed\n");

	SNES_Status->SRAMDirty = 0;
}


inline u8 IO_ReadKeysLow()
{
	u32 keys = hidKeysHeld();
	u8 ret = 0;
	
	if (keys & KEY_A) ret |= 0x80;
	if (keys & KEY_X) ret |= 0x40;
	if (keys & KEY_L) ret |= 0x20;
	if (keys & KEY_R) ret |= 0x10;
	
	return ret;
}

inline u8 IO_ReadKeysHigh()
{
	u32 keys =  hidKeysHeld();
	u8 ret = 0;
	
	if (keys & KEY_B) 		ret |= 0x80;
	if (keys & KEY_Y) 		ret |= 0x40;
	if (keys & KEY_SELECT)	ret |= 0x20;
	if (keys & KEY_START)	ret |= 0x10;
	if (keys & KEY_UP) 		ret |= 0x08;
	if (keys & KEY_DOWN) 	ret |= 0x04;
	if (keys & KEY_LEFT) 	ret |= 0x02;
	if (keys & KEY_RIGHT) 	ret |= 0x01;
	
	return ret;
}

void IO_ManualReadKeys()
{
	// normal joypad
	SNES_JoyBuffer = 0xFFFF0000 | IO_ReadKeysLow() | (IO_ReadKeysHigh() << 8);
}


void SNES_RescheduleIRQ(u8 val)
{
	switch (val & 0x30)
	{
		case 0x00: SNES_Status->IRQ_CurHMatch = 0x8000; break;
		case 0x10: 
			SNES_Status->IRQ_CurHMatch = (SNES_Status->HCount > SNES_Status->IRQ_HMatch) ? 0x8000:SNES_Status->IRQ_HMatch; 
			break;
		case 0x20:
			SNES_Status->IRQ_CurHMatch = (SNES_Status->VCount != SNES_Status->IRQ_VMatch) ? 0x8000:0; 
			break;
		case 0x30:
			SNES_Status->IRQ_CurHMatch = 
				((SNES_Status->VCount != SNES_Status->IRQ_VMatch) || 
				 (SNES_Status->HCount > SNES_Status->IRQ_HMatch))
				 ? 0x8000:SNES_Status->IRQ_HMatch; 
			break;
	}
}

extern u32 debugpc;
u8 SNES_GIORead8(u32 addr)
{
	u8 ret = 0;
	
	switch (addr)
	{
		case 0x10:
			if (SNES_Status->HVBFlags & 0x20)
			{
				ret = 0x80;
				SNES_Status->HVBFlags &= 0xDF;//bprintf("read 4210: %02X %02X %d %06X\n", ret, SNES_Status->HVBFlags, SNES_Status->VCount, debugpc);
			}
			break;
			
		case 0x11:
			if (SNES_Status->HVBFlags & 0x10)
			{
				ret = 0x80;
				SNES_Status->HVBFlags &= 0xEF;
			}
			break;
			
		case 0x12:
			ret = SNES_Status->HVBFlags & 0x80;
			if (SNES_Status->HCount >= 1024) ret |= 0x40;
			break;
			
		case 0x14:
			ret = SNES_DivRes & 0xFF;
			break;
		case 0x15:
			ret = SNES_DivRes >> 8;
			break;
			
		case 0x16:
			ret = SNES_MulRes & 0xFF;
			break;
		case 0x17:
			ret = SNES_MulRes >> 8;
			break;
			
		case 0x18:
			ret = SNES_JoyBuffer & 0xFF;
			break;

		case 0x19:
			ret = (SNES_JoyBuffer >> 8) & 0xFF;
			break;
			
		case 0x13:
		case 0x1A:
		case 0x1B:
		case 0x1C:
		case 0x1D:
		case 0x1E:
		case 0x1F:
			// unimplemented
			break;
			
		default: // open bus
			ret = SNES_Status->LastBusVal;
			break;
	}

	return ret;
}

u16 SNES_GIORead16(u32 addr)
{
	u16 ret = 0;
	switch (addr)
	{
		case 0x14:
			ret = SNES_DivRes;
			break;
			
		case 0x16:
			ret = SNES_MulRes;
			break;
			
		case 0x18:
			ret = SNES_JoyBuffer & 0xFFFF;
			break;
			
		default:
			ret = SNES_GIORead8(addr);
			ret |= (SNES_GIORead8(addr + 1) << 8);
			break;
	}

	return ret;
}

void SNES_GIOWrite8(u32 addr, u8 val)
{
	switch (addr)
	{
		case 0x00:
			if ((SNES_Status->IRQCond ^ val) & 0x30) // reschedule the IRQ if needed
				SNES_RescheduleIRQ(val);
			if (!(val & 0x30)) // acknowledge current IRQ if needed
				SNES_Status->HVBFlags &= 0xEF;
			SNES_Status->IRQCond = val;
			SNES_AutoJoypad = (val & 0x01);
			break;
			
		case 0x01:
			if ((SNES_WRIO & ~val) & 0x80) PPU_LatchHVCounters();
			SNES_WRIO = val;
			break;
			
		case 0x02:
			SNES_MulA = val;
			break;
		case 0x03:
			SNES_MulRes = (u16)SNES_MulA * (u16)val;
			SNES_DivRes = (u16)val;
			break;
			
		case 0x04:
			SNES_DivA = (SNES_DivA & 0xFF00) | val;
			break;
		case 0x05:
			SNES_DivA = (SNES_DivA & 0x00FF) | (val << 8);
			break;
		case 0x06:
			{
				if (val == 0)
				{
					SNES_DivRes = 0xFFFF;
					SNES_MulRes = SNES_DivA;
				}
				else
				{
					SNES_DivRes = (u16)(SNES_DivA / val);
					SNES_MulRes = (u16)(SNES_DivA % val);
				}
			}
			break;
			
		case 0x07:
			SNES_Status->IRQ_HMatch &= 0x0400;
			SNES_Status->IRQ_HMatch |= (val << 2);
			if (SNES_Status->IRQCond & 0x10) SNES_RescheduleIRQ(SNES_Status->IRQCond);
			break;
		case 0x08:
			SNES_Status->IRQ_HMatch &= 0x03FC;
			SNES_Status->IRQ_HMatch |= ((val & 0x01) << 10);
			if (SNES_Status->IRQCond & 0x10) SNES_RescheduleIRQ(SNES_Status->IRQCond);
			break;
			
		case 0x09:
			SNES_Status->IRQ_VMatch &= 0x0100;
			SNES_Status->IRQ_VMatch |= val;
			if (SNES_Status->IRQCond & 0x20) SNES_RescheduleIRQ(SNES_Status->IRQCond);
			break;
		case 0x0A:
			SNES_Status->IRQ_VMatch &= 0x00FF;
			SNES_Status->IRQ_VMatch |= ((val & 0x01) << 8);
			if (SNES_Status->IRQCond & 0x20) SNES_RescheduleIRQ(SNES_Status->IRQCond);
			break;
			
		case 0x0B:
			DMA_Enable(val);
			break;
		case 0x0C:
			DMA_HDMAFlag = val;
			DMA_HDMACurFlag = val & ~DMA_HDMAEnded;
			break;
			
		case 0x0D:
			{
				bool fast = (val & 0x01);
				if (fast ^ SNES_FastROM)
				{
					SNES_FastROM = fast;
					ROM_SpeedChanged();
				}
			}
			break;
	}
}

void SNES_GIOWrite16(u32 addr, u16 val)
{
	switch (addr)
	{
		case 0x02:
			SNES_MulA = val & 0xFF;
			SNES_MulRes = (u16)SNES_MulA * (val >> 8);
			SNES_DivRes = (u16)val;
			break;
			
		case 0x04:
			SNES_DivA = val;
			break;
			
		case 0x07:
			SNES_Status->IRQ_HMatch = (val & 0x01FF) << 2;
			if (SNES_Status->IRQCond & 0x10) SNES_RescheduleIRQ(SNES_Status->IRQCond);
			break;
			
		case 0x09:
			SNES_Status->IRQ_VMatch = val & 0x01FF;
			if (SNES_Status->IRQCond & 0x20) SNES_RescheduleIRQ(SNES_Status->IRQCond);
			break;
			
		case 0x0B:
			DMA_Enable(val & 0xFF);
			DMA_HDMAFlag = val >> 8;
			DMA_HDMACurFlag = DMA_HDMAFlag & ~DMA_HDMAEnded;
			break;
			
		default:
			SNES_GIOWrite8(addr, val & 0xFF);
			SNES_GIOWrite8(addr + 1, val >> 8);
			break;
	}
}


u8 SNES_JoyRead8(u32 addr)
{
	u8 ret = 0;

	if (addr == 0x16)
	{
		if (SNES_Joy16 & 0x01)
		{
			// Return Controller connected status (to which Pad 1 is always connected and Pad 3 is not, Pad 2/4 are linked to 4017h, but neither are connected)
			ret = 0x1;
		}
		else
		{
			if (SNES_JoyBit == 0) IO_ManualReadKeys();
			
			if(SNES_JoyBit < 16)
			{
				ret = (SNES_JoyBuffer >> (SNES_JoyBit ^ 15)) & 1;
				SNES_JoyBit++;
			}
			else
				ret = 0x1;
		}
	}
	else if (addr != 0x17) 
		ret = SNES_Status->LastBusVal;

	return ret;
}

u16 SNES_JoyRead16(u32 addr)
{
	return SNES_JoyRead8(addr) | (SNES_JoyRead8(addr+1) << 8);
}

void SNES_JoyWrite8(u32 addr, u8 val)
{
	if (addr == 0x16)
	{
		if (!(SNES_Joy16 & 0x01) && (val & 0x01))
			SNES_JoyBit = 0;
		
		SNES_Joy16 = val;
	}
}

void SNES_JoyWrite16(u32 addr, u16 val)
{
	SNES_JoyWrite8(addr, val&0xFF);
	SNES_JoyWrite8(addr+1, val>>8);
}


// this used for DMA
// I/O only available for 4210-421F, they say

u8 SNES_Read8(u32 addr)
{
	u32 ptr = Mem_PtrTable[addr >> 13];
	if (ptr & MPTR_SPECIAL)
	{
		if ((addr & 0xFFF0) != 0x4210)
			return 0xFF;
		
		return SNES_IORead8(addr);
	}
	else
	{
		u8* mptr = (u8*)(ptr & 0xFFFFFFF0);
		return mptr[addr & 0x1FFF];
	}
}

u16 SNES_Read16(u32 addr)
{
	u32 ptr = Mem_PtrTable[addr >> 13];
	if (ptr & MPTR_SPECIAL)
	{
		if ((addr & 0xFFF0) != 0x4210)
			return 0xFFFF;
		
		return SNES_IORead16(addr);
	}
	else
	{
		u8* mptr = (u8*)(ptr & 0xFFFFFFF0);
		addr &= 0x1FFF;
		return mptr[addr] | (mptr[addr + 1] << 8);
	}
}

void SNES_Write8(u32 addr, u8 val)
{
	u32 ptr = Mem_PtrTable[addr >> 13];
	if (ptr & MPTR_READONLY) return;
	if (ptr & MPTR_SPECIAL)
	{
		// CHECKME: what are the writable ranges with DMA?
		if ((addr & 0xFFF0) != 0x4000)
			SNES_IOWrite8(addr, val);
	}
	else
	{
		u8* mptr = (u8*)(ptr & 0xFFFFFFF0);
		mptr[addr & 0x1FFF] = val;
	}
}

void SNES_Write16(u32 addr, u8 val)
{
	u32 ptr = Mem_PtrTable[addr >> 13];
	if (ptr & MPTR_READONLY) return;
	if (ptr & MPTR_SPECIAL)
	{
		// CHECKME: what are the writable ranges with DMA?
		if ((addr & 0xFFF0) != 0x4000)
			SNES_IOWrite16(addr, val);
	}
	else
	{
		u8* mptr = (u8*)(ptr & 0xFFFFFFF0);
		addr &= 0x1FFF;
		mptr[addr] = val & 0xFF;
		mptr[addr + 1] = val >> 8;
	}
}
