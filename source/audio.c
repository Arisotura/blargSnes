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

s16* Audio_Buffer0;
s16* Audio_Buffer1;

s16* Audio_Buffer;

u8 curbuffer = 0;
u32 cursample = 0;
u32 curpos = 0;


void Audio_Init()
{
	Result res;
	
	Audio_Type = 0;
	
	Audio_Buffer0 = (s16*)linearAlloc(MIXBUFSIZE*4*4);
	Audio_Buffer1 = &Audio_Buffer0[MIXBUFSIZE*4];
	
	memset(Audio_Buffer0, 0, MIXBUFSIZE*4*4);
	
	curbuffer = 0;
	Audio_Buffer = Audio_Buffer0;
	
	cursample = 0;
	curpos = 0;
	
	// try using CSND
	res = CSND_initialize(NULL);
	if (!res)
	{
		Audio_Type = 1;
	}
	
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
}

void Audio_Pause()
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
	
	memset(Audio_Buffer0, 0, MIXBUFSIZE*4*4);
	GSPGPU_FlushDataCache(NULL, Audio_Buffer0, MIXBUFSIZE*4*4);
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

void Audio_MixFinish()
{
	GSPGPU_FlushDataCache(NULL, Audio_Buffer, MIXBUFSIZE*4*4);
	
	curpos++;
	if (curpos >= (MIXBUFSIZE/256))
	{
		if (Audio_Type == 1)
		{
			int newbuffer = curbuffer^1;
			
			myCSND_playsound(8+newbuffer,  1, CSND_ENCODING_PCM16, 32000, (u32*)&Audio_Buffer[0],            (u32*)&Audio_Buffer[(MIXBUFSIZE*2)-1], MIXBUFSIZE*4, 0xFFFF, 0);
			myCSND_playsound(10+newbuffer, 1, CSND_ENCODING_PCM16, 32000, (u32*)&Audio_Buffer[MIXBUFSIZE*2], (u32*)&Audio_Buffer[(MIXBUFSIZE*4)-1], MIXBUFSIZE*4, 0, 0xFFFF);
			
			CSND_setchannel_playbackstate(8+curbuffer,  0);
			CSND_setchannel_playbackstate(10+curbuffer, 0);
			
			CSND_sharedmemtype0_cmdupdatestate(0);
		}
		
		curbuffer ^= 1;
		Audio_Buffer = curbuffer ? Audio_Buffer1 : Audio_Buffer0;
		curpos = 0;
	}
}
