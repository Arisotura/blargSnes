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
#include "main.h"


u8* ROM_Buffer = NULL;
u32 ROM_BufferSize;
u32 ROM_FileSize;
u32 ROM_BaseOffset;
u32 ROM_HeaderOffset;
u32 ROM_NumBanks;


// TODO find a better way to do speedhacks
// (like, detecting branches with offset -4 or -5 at runtime)
void ROM_ApplySpeedHacks(int banknum, u8* bank)
{
	return;
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

int ROM_ScoreHeader(FILE *pFile, u32 offset)
{
	if ((offset + 0x20) >= ROM_FileSize)
		return -1;
		
	int score = 0;
	int i;
	
	// 1. check opcodes at reset vector
	
	u16 resetvec;
	fseek(pFile, offset + 0x3C, SEEK_SET);
	fread(&resetvec, sizeof(char), 2, pFile);

	if (resetvec < 0x8000)	// invalid reset vector, not likely to go anywhere with this header
		return -1;

	u32 firstops;
	fseek(pFile, (offset - 0x7FC0) + (resetvec - 0x8000), SEEK_SET);
	fread(&firstops, sizeof(char), 4, pFile);
	
	if ((firstops & 0xFFFFFF) == 0xFB1878) // typical SEI/CLC/XCE sequence
		score += 100;
	else if ((firstops & 0xFFFF) == 0xFB18) // CLC/XCE sequence
		score += 100;
	else if (firstops == 0xFB18D878) // SEI/CLD/CLC/XCE sequence
		score += 100;
	else if ((firstops & 0xFF) == 0x5C) // possible JML
	{
		// if a JML is used, chances are that it will go to the FastROM banks
		if (firstops >= 0x80000000) score += 90;
		else score += 80;
	}
	else // look for a more atypical sequence
	{
		u8 firstbytes[0x40];
		*(u32*)&firstbytes[0] = firstops;
		fseek(pFile, (offset - 0x7FC0) + (resetvec - 0x8000) + 4, SEEK_SET);
		fread(&firstbytes[4], sizeof(char), 0x3C, pFile);

		for (i = 0; i < 0x3F; i++)
		{
			if (*(u16*)&firstbytes[i] == 0xFB18)
			{
				score += 90;
				break;
			}
		}
	}
	
	// 2. check the checksum
	
	u16 chksum, chkcomp;
	fseek(pFile, offset + 0x1C, SEEK_SET);
	fread(&chkcomp, sizeof(char), 2, pFile);
	fseek(pFile, offset + 0x1E, SEEK_SET);
	fread(&chksum, sizeof(char), 2, pFile);
	
	if ((chkcomp ^ chksum) == 0xFFFF) score += 50;
	
	// 3. check the characters in the title
	
	char title[21];
	fseek(pFile, offset, SEEK_SET);
	fread(title, sizeof(char), 21, pFile);
	
	for (i = 0; i < 21; i++)
	{
		if (title[i] >= 0x20 && title[i] <= 0x7F)
			score++;
	}
	
	return score;
}

bool ROM_LoadFile(char* name)
{
	FILE *pFile = fopen(name, "rb");
	if(pFile == NULL)
	{
		bprintf("Error while opening file\n");
		return false;
	}

	fseek(pFile, 0, SEEK_END);
	u32 size = ftell(pFile);
	if((size < 16) || (size >= 0x100000000ULL))
	{
		fclose(pFile);
		bprintf("File size bad: size=%ld\n", size);
		return false;
	}
	ROM_FileSize = size;

	
	int bestone = 0;
	int score[4];
	score[0] = ROM_ScoreHeader(pFile, 0x7FC0);
	score[1] = ROM_ScoreHeader(pFile, 0x81C0);
	score[2] = ROM_ScoreHeader(pFile, 0xFFC0);
	score[3] = ROM_ScoreHeader(pFile, 0x101C0);

	if (score[1] > score[0])
	{
		score[0] = score[1];
		bestone = 1;
	}
	if (score[2] > score[0])
	{
		score[0] = score[2];
		bestone = 2;
	}
	if (score[3] > score[0])
		bestone = 3;
		
	if (bestone == 0 && score[0] < 0)
	{
		bprintf("Invalid ROM\n");
		return false;
	}
		
	ROM_BaseOffset = (bestone & 1) ? 0x200 : 0;
	SNES_HiROM = (bestone & 2) ? true : false;
	ROM_HeaderOffset = SNES_HiROM ? 0xFFC0 : 0x7FC0;
	
	bprintf("ROM type: %s %s\n", (bestone & 1) ? "headered":"headerless", SNES_HiROM ? "HiROM":"LoROM");
	
	size -= ROM_BaseOffset;
	
	u32 nbanks = (size + (SNES_HiROM ? 0xFFFF:0x7FFF)) >> (SNES_HiROM ? 16:15);
	ROM_NumBanks = 1;
	while (ROM_NumBanks < nbanks) ROM_NumBanks <<= 1;
	
	bprintf("ROM size: %luKB / %lu banks\n", ((u32)size) >> 10, ROM_NumBanks);
	
	if (ROM_Buffer)
	{
		linearFree(ROM_Buffer);
		ROM_Buffer = NULL;
	}
	
	ROM_BufferSize = ROM_NumBanks << (SNES_HiROM ? 16:15);
	ROM_Buffer = (u8*)linearAlloc(ROM_BufferSize);
	if (!ROM_Buffer)
	{
		fclose(pFile);
		bprintf("Error while allocating ROM buffer\n");
		return false;
	}
	
	fseek(pFile, ROM_BaseOffset, SEEK_SET);
	fread(ROM_Buffer, sizeof(char), size, pFile);
	fclose(pFile);

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
