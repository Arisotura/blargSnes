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

s16* Audio_Buffer;

s32 cursample = 0;

bool isPlaying = false;


void Audio_Init()
{
	Result res;
	
	Audio_Type = 0;
	
	Audio_Buffer = (s16*)linearAlloc(MIXBUFSIZE*4*2);
	memset(Audio_Buffer, 0, MIXBUFSIZE*4*2);
	
	cursample = 0;
	
	// try using CSND
	res = csndInit();
	if (!res)
	{
		Audio_Type = 1;
	}

	isPlaying = false;
	
	// TODO: DSP black magic
}

void Audio_DeInit()
{
	if (Audio_Type == 1)
	{
		CSND_SetPlayState(8, 0);
		CSND_SetPlayState(9, 0);

		csndExecCmds(0);
		csndExit();
	}
	isPlaying = false;
}

void Audio_Pause()
{
	if(isPlaying)
	{
		// stop
		if (Audio_Type == 1)
		{
			CSND_SetPlayState(8, 0);
			CSND_SetPlayState(9, 0);

			csndExecCmds(0);
		}
	
		memset(Audio_Buffer, 0, MIXBUFSIZE*4*2);
		GSPGPU_FlushDataCache(NULL, Audio_Buffer, MIXBUFSIZE*4*2);
		isPlaying = false;
	}
}

void Audio_Mix()
{
	DspGenerateNoise();
	DspMixSamplesStereo(DSPMIXBUFSIZE, &Audio_Buffer[cursample]);
	cursample += DSPMIXBUFSIZE;
	cursample &= ((MIXBUFSIZE << 1) - 1);
}


// tweaked CSND_playsound() version. Allows setting multiple channels and calling updatestate once.
// the last two parameters are also repurposed for volume control


void myCSND_SetSound(u32 chn, u32 flags, u32 sampleRate, void* data0, void *data1, u32 size, u32 leftvol, u32 rightvol)
{
	u32 paddr0 = 0, paddr1 = 0;

	int encoding = (flags >> 12) & 3;
	int loopMode = (flags >> 10) & 3;

	if (encoding != CSND_ENCODING_PSG)
	{
		if (data0) paddr0 = osConvertVirtToPhys((u32)data0);
		if (data1) paddr1 = osConvertVirtToPhys((u32)data1);

		if (encoding == CSND_ENCODING_ADPCM)
		{
			int adpcmSample = ((s16*)data0)[-2];
			int adpcmIndex = ((u8*)data0)[-2];
			CSND_SetAdpcmState(chn, 0, adpcmSample, adpcmIndex);
		}
	}

	u32 timer = CSND_TIMER(sampleRate);
	if (timer < 0x0042) timer = 0x0042;
	else if (timer > 0xFFFF) timer = 0xFFFF;
	flags &= ~0xFFFF001F;
	flags |= SOUND_ENABLE | SOUND_CHANNEL(chn) | (timer << 16);

	CSND_SetChnRegs(flags, paddr0, paddr1, size);

	if (loopMode == CSND_LOOPMODE_NORMAL && paddr1 > paddr0)
	{
		// Now that the first block is playing, configure the size of the subsequent blocks
		size -= paddr1 - paddr0;
		CSND_SetBlock(chn, 1, paddr1, size);
	}
	CSND_SetVol(chn, leftvol, rightvol);
	CSND_SetPlayState(chn, 1);
}


bool Audio_Begin()
{
	if(isPlaying)
	    return 0;
 
	memset(Audio_Buffer, 0, MIXBUFSIZE*4*2);
	GSPGPU_FlushDataCache(NULL, Audio_Buffer, MIXBUFSIZE*4*2);
 
	if (Audio_Type == 1)
	{
		myCSND_SetSound(8, SOUND_FORMAT_16BIT | SOUND_REPEAT, 32000, &Audio_Buffer[0],            &Audio_Buffer[0],            MIXBUFSIZE*4, 0xFFFF, 0);
		myCSND_SetSound(9, SOUND_FORMAT_16BIT | SOUND_REPEAT, 32000, &Audio_Buffer[MIXBUFSIZE*2], &Audio_Buffer[MIXBUFSIZE*2], MIXBUFSIZE*4, 0, 0xFFFF);

		csndExecCmds(0);
	}
 
	cursample = MIXBUFSIZE;//(MIXBUFSIZE * 3) >> 1;
	isPlaying = true;
	return 1;
}

void Audio_Inc(int count)
{
	if(count < 1)
		return;
	s16 mySamples[2];
	s32 getSample = cursample - 1;
	if(getSample < 0)
		getSample += (MIXBUFSIZE << 1);
	mySamples[0] = Audio_Buffer[getSample];
	mySamples[1] = Audio_Buffer[getSample + (MIXBUFSIZE << 1)];
	int i;
	for(i = 0; i < count; i++)
	{
		int j;
		for(j = 0; j < DSPMIXBUFSIZE; j++)
		{
			Audio_Buffer[cursample + j] = mySamples[0];
			Audio_Buffer[cursample + j + (MIXBUFSIZE << 1)] = mySamples[1];
		}
		cursample += DSPMIXBUFSIZE;
		cursample &= ((MIXBUFSIZE << 1) - 1);
	}
}

void Audio_Dec(int count)
{
	if(count < 1)
		return;
	cursample -= (DSPMIXBUFSIZE * count);
	cursample &= ((MIXBUFSIZE << 1) - 1);
}