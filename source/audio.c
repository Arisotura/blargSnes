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

#include <3ds.h>

#include "dsp.h"
#include "mixrate.h"


// 0 = none, 1 = CSND, 2 = DSP
int Audio_Type;

s16* Audio_LeftBuffer;
s16* Audio_RightBuffer;


void Audio_Init()
{
	Result res;
	
	Audio_Type = 0;
	
	Audio_LeftBuffer = (s16*)linearAlloc(MIXBUFSIZE*4*2);
	Audio_RightBuffer = &Audio_LeftBuffer[MIXBUFSIZE*2];
	
	memset(Audio_LeftBuffer, 0, MIXBUFSIZE*4);
	memset(Audio_RightBuffer, 0, MIXBUFSIZE*4);
	
	// try using CSND
	res = CSND_initialize(NULL);
	if (!res)
	{
		Audio_Type = 1;
		
		// TODO: figure out how to do panning, if it's possible at all?
		CSND_playsound(8, 1, 1/*PCM16*/, 32000, (u32*)Audio_LeftBuffer, (u32*)Audio_LeftBuffer, MIXBUFSIZE*4, 2, 0);
		CSND_playsound(9, 1, 1/*PCM16*/, 32000, (u32*)Audio_RightBuffer, (u32*)Audio_RightBuffer, MIXBUFSIZE*4, 2, 0);
	}
	
	// TODO: DSP black magic
}

u32 cursample = 0;

void Audio_Mix()
{
	DspMixSamplesStereo(DSPMIXBUFSIZE, &Audio_LeftBuffer[cursample]);
	cursample += DSPMIXBUFSIZE;
	cursample &= ((MIXBUFSIZE << 1) - 1);
}
