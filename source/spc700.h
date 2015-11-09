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

#ifndef _SPC700_H_
#define _SPC700_H_

typedef union
{
	u32 Val;
	struct
	{
		u16 LowPart;
		u16 HighPart;
	};
	
} SPC_Timer;

extern u8 SPC_RAM[0x10040];

extern u32 SPC_ElapsedCycles;
extern u32 SPC_CycleRatio;

extern u8 SPC_TimerEnable;
extern u32 SPC_TimerReload[3];
extern SPC_Timer SPC_TimerVal[3];

typedef struct
{
	u32 _memoryMap;
	s32 nCycles;
	u16 PSW;
	u16 PC;
	u32 SP;
	u32 Y;
	u32 X;
	u32 A;
} SPC_Regs_t;

extern SPC_Regs_t SPC_Regs;

extern u8 SPC_ROM[0x40];

	
void SPC_Reset();
void SPC_Run(int cycles);

void SPC_InitMisc();

u8 SPC_IORead8(u16 addr);
u16 SPC_IORead16(u16 addr);
void SPC_IOWrite8(u16 addr, u8 val);
void SPC_IOWrite16(u16 addr, u16 val);


void DSP_Reset();

void DSP_Mix();

u8 DSP_Read(u8 reg);
void DSP_Write(u8 reg, u8 val);

#endif
