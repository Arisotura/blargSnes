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

#ifndef _SNES_H_
#define _SNES_H_

#include <stdio.h>
#include <ctr/types.h>

typedef struct
{
	u32 SRAMMask;	// -0xC
	
	u8 __pad0[2];	// -0x8
	
	u8 IRQCond;		// -0x6
	
	// bit7: vblank
	// bit6: hblank
	// bit5: vblank (ack)
	// bit4: IRQ (ack)
	// bit3: IRQ (sticky, per-scanline)
	u8 HVBFlags;	// -0x5
	
	u32 SRAMDirty;	// -0x4
	
} SNES_StatusData;

#define SNESSTATUS_SIZE ((sizeof(SNES_StatusData) + 3) & ~3)

#define MEM_PTR(b, a) Mem_PtrTable[((b) << 3) | ((a) >> 13)]

#define MPTR_SLOW		(1 << 0)
#define MPTR_SPECIAL	(1 << 1)
#define MPTR_READONLY	(1 << 2)
#define MPTR_SRAM		(1 << 3)

extern u32 ROM_BaseOffset;
extern u8* ROM_Buffer;
extern u32 ROM_HeaderOffset;

extern u8* ROM_Bank0;
extern u8* ROM_Bank0End;

extern u8 ROM_Region;

extern bool SNES_HiROM;
extern bool SNES_FastROM;
extern u32* Mem_PtrTable;

extern u8 SNES_SysRAM[0x20000];

extern u16 SNES_VMatch;
extern u16 SNES_HMatch;
extern u16 SNES_HCheck;


void ROM_MapBank(u32 bank, u8* ptr);
void ROM_SpeedChanged();

bool SNES_LoadROM(char* path);
void SNES_Reset();

void SNES_SaveSRAM();

u8 SNES_IORead8(u32 addr);
u16 SNES_IORead16(u32 addr);
void SNES_IOWrite8(u32 addr, u32 val);
void SNES_IOWrite16(u32 addr, u32 val);

void report_unk_lol(u32 op, u32 pc);
void reportBRK(u32 pc);

u8 SNES_GIORead8(u32 addr);
u16 SNES_GIORead16(u32 addr);
void SNES_GIOWrite8(u32 addr, u8 val);
void SNES_GIOWrite16(u32 addr, u16 val);

u8 SNES_JoyRead8(u32 addr);
u16 SNES_JoyRead16(u32 addr);
void SNES_JoyWrite8(u32 addr, u8 val);
void SNES_JoyWrite16(u32 addr, u16 val);

u8 DMA_Read8(u32 addr);
u16 DMA_Read16(u32 addr);
void DMA_Write8(u32 addr, u8 val);
void DMA_Write16(u32 addr, u16 val);
void DMA_Enable(u8 flag);

u8 SNES_Read8(u32 addr);
u16 SNES_Read16(u32 addr);
void SNES_Write8(u32 addr, u8 val);
void SNES_Write16(u32 addr, u8 val);

#endif
