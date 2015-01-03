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

u32 cursample = 0;

bool isPlaying = false;


void Audio_Init()
{
	Result res;
	
	Audio_Type = 0;
	
	Audio_Buffer = (s16*)linearAlloc(MIXBUFSIZE*4*2);
	memset(Audio_Buffer, 0, MIXBUFSIZE*4*2);
	
	cursample = 0;
	
	// try using CSND
	res = CSND_initialize(NULL);
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
		CSND_setchannel_playbackstate(8, 0);
		CSND_setchannel_playbackstate(9, 0);
		CSND_setchannel_playbackstate(10, 0);
		CSND_setchannel_playbackstate(11, 0);
				
		CSND_sharedmemtype0_cmdupdatestate(0);
		
		CSND_shutdown();
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
			CSND_setchannel_playbackstate(8, 0);
			CSND_setchannel_playbackstate(9, 0);
			CSND_setchannel_playbackstate(10, 0);
			CSND_setchannel_playbackstate(11, 0);
				
			CSND_sharedmemtype0_cmdupdatestate(0);
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

// volume control
void myCSND_sharedmemtype0_cmd9(u32 channel, u16 leftvol, u16 rightvol)
{
	u32 cmdparams[0x18>>2];

	memset(cmdparams, 0, 0x18);

	cmdparams[0] = channel & 0x1f;
	cmdparams[1] = leftvol | (rightvol<<16);

	CSND_writesharedmem_cmdtype0(0x9, (u8*)&cmdparams);
}

// tweaked CSND_playsound() version. Allows setting multiple channels and calling updatestate once.
// the last two parameters are also repurposed for volume control
void myCSND_playsound(u32 channel, u32 looping, u32 encoding, u32 samplerate, u32 *vaddr0, u32 *vaddr1, u32 totalbytesize, u32 leftvol, u32 rightvol)
{
	u32 physaddr0 = 0;
	u32 physaddr1 = 0;

	physaddr0 = osConvertVirtToPhys((u32)vaddr0);
	physaddr1 = osConvertVirtToPhys((u32)vaddr1);

	CSND_sharedmemtype0_cmde(channel, looping, encoding, samplerate, 2/*unk0*/, 1/*unk1*/, physaddr0, physaddr1, totalbytesize);
	CSND_sharedmemtype0_cmd8(channel, samplerate);
	if(looping)
	{
		if(physaddr1>physaddr0)totalbytesize -= (u32)physaddr1 - (u32)physaddr0;
		CSND_sharedmemtype0_cmd3(channel, physaddr1, totalbytesize);
	}
	CSND_sharedmemtype0_cmd8(channel, samplerate);
	myCSND_sharedmemtype0_cmd9(channel, leftvol, rightvol); // volume
	CSND_setchannel_playbackstate(channel, 1);
}

bool Audio_Begin()
{
	if(isPlaying)
	    return 0;
 
	memset(Audio_Buffer, 0, MIXBUFSIZE*4*2);
	GSPGPU_FlushDataCache(NULL, Audio_Buffer, MIXBUFSIZE*4*2);
 
	myCSND_playsound(8, 1, CSND_ENCODING_PCM16, 32000, (u32*)&Audio_Buffer[0],            (u32*)&Audio_Buffer[0],            MIXBUFSIZE*4, 0xFFFF, 0);
	myCSND_playsound(9, 1, CSND_ENCODING_PCM16, 32000, (u32*)&Audio_Buffer[MIXBUFSIZE*2], (u32*)&Audio_Buffer[MIXBUFSIZE*2], MIXBUFSIZE*4, 0, 0xFFFF);
 
	CSND_sharedmemtype0_cmdupdatestate(0);
 
	cursample = MIXBUFSIZE;//(MIXBUFSIZE * 3) >> 1;
	isPlaying = true;
	return 1;
}
