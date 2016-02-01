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

#ifndef _CPU_H_
#define _CPU_H_

typedef union
{
	u16 val;
	struct
	{
		u16 C :1,
			Z :1,
			I :1,
			D :1,
			X :1,	// B in emulation mode
			M :1,
			V :1,
			N :1,
			E :1,	// actually not in P, but hey, we have to keep it somewhere
			W :1,	// not even an actual flag, set by WAI
			I2 :1,	// not an actual flag either, set when NMI's are disabled
			  :5;
	};
} CPU_P;

typedef struct
{
	u32 _memoryMap;
	u32 _opTable;
	u16 nLines;
	s16 nCycles;
	CPU_P P;
	u16 PC;
	u16 DBR;
	u16 D;
	u16 PBR;
	u16 S;
	u32 Y;
	u32 X;
	u32 A;
} CPU_Regs_t;

extern CPU_Regs_t CPU_Regs;

	
void CPU_Reset();
void CPU_Run();
void CPU_MainLoop();

void CPU_TriggerIRQ();
void CPU_TriggerNMI();

// debugging crap
u32 CPU_GetPC();
u32 CPU_GetReg(u32 reg);

#endif
