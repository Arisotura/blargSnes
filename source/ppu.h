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

extern u16 PPU_ColorTable[0x10000];

extern u16 PPU_CGRAM[256];
extern u8 PPU_VRAM[0x10000];
extern u8 PPU_OAM[0x220];

extern u16 PPU_CGRAMAddr;
extern u16 PPU_VRAMAddr;
extern u8 PPU_VRAMStep, PPU_VRAMInc;
extern u16 PPU_OAMAddr;

extern u16 PPU_VCount;

void PPU_Reset();

u8 PPU_Read8(u32 addr);
u16 PPU_Read16(u32 addr);
void PPU_Write8(u32 addr, u8 val);
void PPU_Write16(u32 addr, u16 val);

void PPU_RenderScanline(u16 line);
void PPU_VBlank();

#endif
