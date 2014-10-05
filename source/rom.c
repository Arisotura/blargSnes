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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <3ds/types.h>
#include <3ds/services/fs.h>
#include <3ds/svc.h>

#include "cpu.h"
#include "snes.h"


u8* ROM_Buffer;
u32 ROM_BufferSize;
u32 ROM_FileSize;
u32 ROM_BaseOffset;
u32 ROM_HeaderOffset;
u32 ROM_NumBanks;

extern FS_archive sdmcArchive;


// TODO find a better way to do speedhacks
// (like, detecting branches with offset -4 or -5 at runtime)
void ROM_ApplySpeedHacks(int banknum, u8* bank)
{
	int i;
	int bsize = SNES_HiROM ? 0x10000 : 0x8000;

	for (i = 2; i < bsize-4;)
	{
		//if (bank[i] == 0xA5 && bank[i+2] == 0xF0 && bank[i+3] == 0xFC)
		if (bank[i] == 0xA5 && (bank[i+2] & 0x1F) == 0x10 && bank[i+3] == 0xFC)
		{
			u8 branchtype = bank[i+2];
			bank[i+2] = 0x42;
			bank[i+3] = (bank[i+3] & 0x0F) | (branchtype & 0xF0);
			
			bprintf("Speed hack installed @ %02X:%04X\n", banknum, (SNES_HiROM?0:0x8000)+i);
			
			i += 4;
		}
		else if ((bank[i] == 0xAD || bank[i] == 0xCD)
			&& (bank[i+3] & 0x1F) == 0x10 && bank[i+4] == 0xFB)
		{
			u16 addr = bank[i+1] | (bank[i+2] << 8);
			
			if ((addr & 0xFFF0) != 0x2140)
			{
				u8 branchtype = bank[i+3];
				bank[i+3] = 0x42;
				bank[i+4] = (bank[i+4] & 0x0F) | (branchtype & 0xF0);
				
				bprintf("Speed hack installed @ %02X:%04X\n", banknum, (SNES_HiROM?0:0x8000)+i);
			}
			
			i += 5;
		}
		else
			i++;
	}
}


void ROM_MapBank(u32 bank, u8* ptr)
{
	u32 hi_slow = SNES_FastROM ? 0 : MPTR_SLOW;
	
	if (SNES_HiROM)
	{
		for (; bank < 0x80; bank += ROM_NumBanks)
		{
			if (bank < 0x7E)
			{
				MEM_PTR(bank, 0x0000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
				MEM_PTR(bank, 0x2000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
				MEM_PTR(bank, 0x4000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
				MEM_PTR(bank, 0x6000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
				MEM_PTR(bank, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x8000];
				MEM_PTR(bank, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xA000];
				MEM_PTR(bank, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xC000];
				MEM_PTR(bank, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xE000];
			}
			
			MEM_PTR(0x80 + bank, 0x0000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x0000];
			MEM_PTR(0x80 + bank, 0x2000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x2000];
			MEM_PTR(0x80 + bank, 0x4000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x4000];
			MEM_PTR(0x80 + bank, 0x6000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x6000];
			MEM_PTR(0x80 + bank, 0x8000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x8000];
			MEM_PTR(0x80 + bank, 0xA000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xA000];
			MEM_PTR(0x80 + bank, 0xC000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xC000];
			MEM_PTR(0x80 + bank, 0xE000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xE000];
			
			MEM_PTR(bank - 0x40, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x8000];
			MEM_PTR(bank - 0x40, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xA000];
			MEM_PTR(bank - 0x40, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xC000];
			MEM_PTR(bank - 0x40, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xE000];
			
			MEM_PTR(0x40 + bank, 0x8000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x8000];
			MEM_PTR(0x40 + bank, 0xA000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xA000];
			MEM_PTR(0x40 + bank, 0xC000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xC000];
			MEM_PTR(0x40 + bank, 0xE000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xE000];
		}
	}
	else
	{
		for (; bank < 0x80; bank += ROM_NumBanks)
		{
			if (bank >= 0x40 && bank < 0x70)
			{
				MEM_PTR(bank, 0x0000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
				MEM_PTR(bank, 0x2000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
				MEM_PTR(bank, 0x4000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
				MEM_PTR(bank, 0x6000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
				
				MEM_PTR(0x80 + bank, 0x0000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x0000];
				MEM_PTR(0x80 + bank, 0x2000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x2000];
				MEM_PTR(0x80 + bank, 0x4000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x4000];
				MEM_PTR(0x80 + bank, 0x6000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x6000];
			}
			
			if (bank < 0x7E)
			{
				MEM_PTR(bank, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
				MEM_PTR(bank, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
				MEM_PTR(bank, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
				MEM_PTR(bank, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
			}
			
			MEM_PTR(0x80 + bank, 0x8000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x0000];
			MEM_PTR(0x80 + bank, 0xA000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x2000];
			MEM_PTR(0x80 + bank, 0xC000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x4000];
			MEM_PTR(0x80 + bank, 0xE000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x6000];
		}
	}
	
	ROM_ApplySpeedHacks(bank, ptr);
}

bool ROM_CheckHeader(Handle file, u32 offset)
{
	if ((offset + 0x20) >= ROM_FileSize)
		return false;
	
	u16 chksum, chkcomp;
	u32 bytesread;
	FSFILE_Read(file, &bytesread, offset + 0x1C, (u32*)&chkcomp, 2);
	FSFILE_Read(file, &bytesread, offset + 0x1E, (u32*)&chksum, 2);
	
	return (chkcomp ^ chksum) == 0xFFFF;
}

bool ROM_LoadFile(char* name)
{
	Handle fileHandle;
	FS_path filePath;
	filePath.type = PATH_CHAR;
	filePath.size = strlen(name) + 1;
	filePath.data = (u8*)name;
	
	Result res = FSUSER_OpenFile(NULL, &fileHandle, sdmcArchive, filePath, FS_OPEN_READ, FS_ATTRIBUTE_NONE);
	if ((res & 0xFFFC03FF) != 0)
	{
		bprintf("Error %08X while opening file\n", res);
		return false;
	}
		
	u64 size;
	FSFILE_GetSize(fileHandle, &size);
	if (size < 16 || size >= 0x100000000ULL)
	{
		FSFILE_Close(fileHandle);
		bprintf("File size bad: size=%lld\n", size);
		return false;
	}
	ROM_FileSize = (u32)size;
	
	if (ROM_CheckHeader(fileHandle, 0x81C0) || ROM_FileSize == 0x8200) // headered, LoROM
	{
		ROM_BaseOffset = 0x200;
		SNES_HiROM = false;
		ROM_HeaderOffset = 0x7FC0;
		bprintf("ROM type: headered LoROM\n");
	}
	else if (ROM_CheckHeader(fileHandle, 0x101C0)) // headered, HiROM
	{
		ROM_BaseOffset = 0x200;
		SNES_HiROM = true;
		ROM_HeaderOffset = 0xFFC0;
		bprintf("ROM type: headered HiROM\n");
	}
	else if (ROM_CheckHeader(fileHandle, 0x7FC0) || ROM_FileSize == 0x8000) // headerless, LoROM
	{
		ROM_BaseOffset = 0;
		SNES_HiROM = false;
		ROM_HeaderOffset = 0x7FC0;
		bprintf("ROM type: headerless LoROM\n");
	}
	else if (ROM_CheckHeader(fileHandle, 0xFFC0)) // headerless, HiROM
	{
		ROM_BaseOffset = 0;
		SNES_HiROM = true;
		ROM_HeaderOffset = 0xFFC0;
		bprintf("ROM type: headerless HiROM\n");
	}
	else // whatever piece of shit
	{
		// assume header at 0x81C0
		// TODO use 0x7FC0 instead if no header
		// we can guess from the filesize but that isn't accurate (eg homebrew)
		ROM_BaseOffset = 0x200;
		SNES_HiROM = false;
		ROM_HeaderOffset = 0x81C0;
		bprintf("ROM type: not found, assuming headered LoROM\n");
	}
	
	size -= ROM_BaseOffset;
	
	u32 nbanks = (size + (SNES_HiROM ? 0xFFFF:0x7FFF)) >> (SNES_HiROM ? 16:15);
	ROM_NumBanks = 1;
	while (ROM_NumBanks < nbanks) ROM_NumBanks <<= 1;
	
	bprintf("ROM size: %dKB / %d banks\n", ((u32)size) >> 10, ROM_NumBanks);
	
	ROM_BufferSize = ROM_NumBanks << (SNES_HiROM ? 16:15);
	ROM_Buffer = (u8*)MemAlloc(ROM_BufferSize);
	if (!ROM_Buffer)
	{
		FSFILE_Close(fileHandle);
		bprintf("Error %08X while allocating ROM buffer\n", res);
		return false;
	}
	
	u32 bytesread;
	FSFILE_Read(fileHandle, &bytesread, ROM_BaseOffset, (u32*)ROM_Buffer, (u32)size);
	FSFILE_Close(fileHandle);
	
	u32 b = 0;
	u32 offset = 0;
	for (; b < ROM_NumBanks; b++)
	{
		ROM_MapBank((SNES_HiROM ? 0x40:0x00) + b, &ROM_Buffer[offset]);
		offset += (SNES_HiROM ? 0x10000:0x8000);
	}

	return true;
}


void ROM_SpeedChanged()
{
	u32 b, a;
	
	if (SNES_FastROM)
	{
		bprintf("Fast ROM\n");
		
		for (b = 0x80; b < 0xC0; b++)
			for (a = 0x8000; a < 0x10000; a += 0x2000)
				MEM_PTR(b, a) &= ~MPTR_SLOW;
				
		for (b = 0xC0; b < 0x100; b++)
			for (a = 0x0000; a < 0x10000; a += 0x2000)
				MEM_PTR(b, a) &= ~MPTR_SLOW;
	}
	else
	{
		bprintf("Slow ROM\n");
		
		for (b = 0x80; b < 0xC0; b++)
			for (a = 0x8000; a < 0x10000; a += 0x2000)
				MEM_PTR(b, a) |= MPTR_SLOW;
				
		for (b = 0xC0; b < 0x100; b++)
			for (a = 0x0000; a < 0x10000; a += 0x2000)
				MEM_PTR(b, a) |= MPTR_SLOW;
	}
}
