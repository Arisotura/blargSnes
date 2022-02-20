/*
    Copyright 2014-2022 Arisotura

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
#include <string.h>

#include "dsp.h"
#include "mixrate.h"


// 0 = none, 2 = DSP
int Audio_Type = 0;

ndspWaveBuf waveBuf;

s16* Audio_Buffer;

s32 cursample = 0;

bool isPlaying = false;


void Audio_Init()
{
	if (Audio_Type)
		return;

	if (R_FAILED(ndspInit()))
		return;

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspSetOutputCount(1);
	ndspSetMasterVol(1.0f);


	Audio_Buffer = (s16*)linearAlloc(MIXBUFSIZE*2*2);
	memset(Audio_Buffer, 0, MIXBUFSIZE*2*2);
	
	Audio_Type = 2;
	cursample = 0;

	isPlaying = false;
	
}

void Audio_DeInit()
{
	if (!Audio_Type)
		return;

	linearFree(Audio_Buffer);

	ndspChnWaveBufClear(0);
	ndspExit();

	Audio_Type = 0;
	isPlaying = false;
}

void Audio_Pause()
{
	if (!Audio_Type)
		return;

	if(isPlaying)
	{
		// stop

		ndspChnWaveBufClear(0);

		memset(Audio_Buffer, 0, MIXBUFSIZE*2*2);
		DSP_FlushDataCache(Audio_Buffer, MIXBUFSIZE*2*2);
		
		isPlaying = false;
	}
}

void Audio_Mix(u32 samples, bool restart)
{
	if (!Audio_Type)
		return;
	
	cursample = DspMixSamplesStereo(samples, Audio_Buffer, MIXBUFSIZE, cursample, restart);
}

bool Audio_Begin()
{
	if (!Audio_Type)
		return 0;

	if (isPlaying)
	    return 0;
 
	float mix[12];
	memset(mix, 0, sizeof(mix));

	mix[0] = mix[1] = 1.0f;
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, 32000.0f);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
	ndspChnSetMix(0, mix);

	memset(&waveBuf, 0, sizeof(ndspWaveBuf));
	waveBuf.data_vaddr = Audio_Buffer;
	waveBuf.nsamples = MIXBUFSIZE;
	waveBuf.looping  = true;
	waveBuf.status = NDSP_WBUF_FREE;

	memset(Audio_Buffer, 0, MIXBUFSIZE*2*2);
	DSP_FlushDataCache(Audio_Buffer, MIXBUFSIZE*2*2);

	ndspChnWaveBufAdd(0, &waveBuf);
 
	cursample = MIXBUFSIZE >> 1;
	isPlaying = true;
	return 1;
}
