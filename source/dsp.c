// This code has been taken from SNemulDS which is licensed under GPLv2.
// Credits go to Archeide and whoever else participated in this.

#include <string.h>

#include "ui_console.h"
#include "dsp.h"


// DSP write buffers
// * 32 16-sample periods
// * over a 16-sample period -> 512 cycles
// * 4 cycles atleast per write -> 128 writes at most
// * double-buffered
// -> 8192 entries (16K)


u16 DSP_WriteDSPBuffer[2][8192];
u16* DSP_WritingWriteDSPBuffer;
u16* DSP_PlayingWriteDSPBuffer;

u16 DSP_WriteTimeBuffer[2][8192];
u16* DSP_WritingWriteTimeBuffer;
u16* DSP_PlayingWriteTimeBuffer;

// Envelope timing table.  Number of counts that should be subtracted from the counter
// The counter starts at 30720 (0x7800).
static const s16 ENVCNT_START = 0x7800;
static const s16 ENVCNT[0x20] = {
	0x0000,0x000F,0x0014,0x0018,0x001E,0x0028,0x0030,0x003C,
	0x0050,0x0060,0x0078,0x00A0,0x00C0,0x00F0,0x0140,0x0180,
	0x01E0,0x0280,0x0300,0x03C0,0x0500,0x0600,0x0780,0x0A00,
	0x0C00,0x0F00,0x1400,0x1800,0x1E00,0x2800,0x3C00,0x7800
};

void DspSetEndOfSample(u32 channel);

#define APU_MEM SPC_RAM

DspChannel channels[8];
u8 DSP_MEM[0x100];

s8 firFilter[16];
s16 brrTab[16 * 16];
u32 echoBase;
u32 echoDelay ALIGNED;
u16 dspPreamp ALIGNED = 0x100;
u16 echoRemain ALIGNED;


// externs from dspmixer.S
u32 DecodeSampleBlockAsm(u8 *blockPos, s16 *samplePos, DspChannel *channel);
void DspMixSamplesStereoAsm(u32 samples, s16 *mixBuf);
extern u16 firOffset;
extern s16 noiseSample;
extern u16 noiseStep;

u32 playSamples;
u32 writeSamples;


u32 DecodeSampleBlock(DspChannel *channel, u32 channelNum) {
    u8 *cur = (u8*)&(APU_MEM[channel->blockPos]);
    s16 *sample = channel->decoded;

    if (channel->blockPos > 0x10000 - 9) {
        // Set end of block, with no loop
        DspSetEndOfSample(channelNum);
        return 1;
    }

    // Is this the last block?
    if (channel->brrHeader & 1) {
        // channelNum will be set from asm
        DSP_MEM[DSP_ENDX] |= (1 << channelNum);

        if (channel->brrHeader & 2) {
            // Looping
    	    u8 *sampleBase = APU_MEM + (DSP_MEM[DSP_DIR] << 8);
	        u16 *lookupPointer = (u16*)(sampleBase + (DSP_MEM[(channelNum << 4) + DSP_SRC] << 2));
            channel->blockPos = lookupPointer[1];
            cur = (u8*)&(APU_MEM[channel->blockPos]);
        } else {
            DspSetEndOfSample(channelNum);
            return 1;
        }
    }

    channel->brrHeader = *cur;

    DecodeSampleBlockAsm(cur, sample, channel);

    channel->blockPos += 9;

    return 0;
}


void DspReset() {
    // Delay for 1 sample
    echoDelay = 4;
	echoRemain = 1;
    echoBase = 0;
	int i=0,c=0;
	
	memset(DSP_WriteDSPBuffer, 0, sizeof(DSP_WriteDSPBuffer));
	memset(DSP_WriteTimeBuffer, 0, sizeof(DSP_WriteTimeBuffer));
	
	DSP_WritingWriteDSPBuffer = DSP_WriteDSPBuffer[0];
	DSP_PlayingWriteDSPBuffer = DSP_WriteDSPBuffer[1];

	DSP_WritingWriteTimeBuffer = DSP_WriteTimeBuffer[0];
	DSP_PlayingWriteTimeBuffer = DSP_WriteTimeBuffer[1];

    firOffset = 0;
	for(i = 0; i > 8; i++)
		firFilter[i] = 0;

 
    memset(DSP_MEM, 0, 0x100);
    // Disable echo emulation
	DSP_MEM[DSP_FLAG] = 0x60;

	noiseSample = 0x8000;
	noiseStep = 1;

	for (i = 0; i < 8; i++)
		memset(&channels[i], 0, sizeof(DspChannel));


    // Build a lookup table for the range values (thanks to trac)
    for (i = 0; i < 13; i++) {
		for (c = 0; c < 16; c++)
			brrTab[(i << 4) + c] = (s16)((((c ^ 8) - 8) << i) >> 1);
	}
	// range 13-15
    for (i = 13; i < 16; i++) {
		for (c = 0; c < 8; c++)
			brrTab[(i << 4) + c] = 0;
		for(c = 8; c < 16; c++)
			brrTab[(i << 4) + c] = 0xF800;
	}

}

void DspSetFIRCoefficient(u32 index) {
	firFilter[index] = DSP_MEM[(index << 4) + DSP_FIR];
}

void DspSetChannelVolume(u32 channel) {
    channels[channel].leftVolume = DSP_MEM[(channel << 4) + DSP_VOL_L];
    channels[channel].rightVolume = DSP_MEM[(channel << 4) + DSP_VOL_R];

    channels[channel].leftCalcVolume = (channels[channel].leftVolume * channels[channel].envx) >> 7;
    channels[channel].rightCalcVolume = (channels[channel].rightVolume * channels[channel].envx) >> 7;
}

void DspSetChannelPitch(u32 channel) {
	u16 rawFreq = ((DSP_MEM[(channel << 4) + DSP_PITCH_H] << 8) + DSP_MEM[(channel << 4) + DSP_PITCH_L]) & 0x3fff;

    // Clear low bit of sample speed so we can do a little optimization in dsp mixing
//	channels[channel].sampleSpeed = (((rawFreq << 3) << 12) / MIXRATE) & (~1);
	//channels[channel].sampleSpeed = (((rawFreq << 3) << 12) / MIXRATE);
	channels[channel].sampleSpeed = rawFreq;
}

void DspSetChannelSource(u32 channel) {
	u8 *sampleBase = APU_MEM + (DSP_MEM[DSP_DIR] << 8);
	u16 *lookupPointer = (u16*)(sampleBase + (DSP_MEM[(channel << 4) + DSP_SRC] << 2));

    channels[channel].blockPos = lookupPointer[0];
}

void DspSetEndOfSample(u32 channel) {
    channels[channel].active = false;
    channels[channel].envState = ENVSTATE_NONE;

    channels[channel].envx = 0;
    DSP_MEM[(channel << 4) | DSP_ENVX] = 0;
    DSP_MEM[(channel << 4) | DSP_OUTX] = 0;
}

void DspSetChannelEnvelopeHeight(u32 channel, u8 height) {
    channels[channel].envx = height << 8;

    channels[channel].leftCalcVolume = (channels[channel].leftVolume * channels[channel].envx) >> 7;
    channels[channel].rightCalcVolume = (channels[channel].rightVolume * channels[channel].envx) >> 7;
}

void DspStartChannelEnvelope(u32 channel) {
	u8 adsr1 = DSP_MEM[(channel << 4) + DSP_ADSR1];

    // ADSR mode, set envelope up
    // Attack rate goes into envelopeSpeed initially
  	u8 adsr2 = DSP_MEM[(channel << 4) + DSP_ADSR2];
    u8 decay = (adsr1 >> 4) & 0x7;
    u8 sustainLevel = adsr2 >> 5;
    u8 sustainRate = adsr2 & 0x1f;

    channels[channel].decaySpeed = ENVCNT[(decay << 1) + 0x10];
    channels[channel].sustainLevel = 0x10 * (sustainLevel + 1);
    channels[channel].sustainSpeed = ENVCNT[sustainRate];

    // Don't set envelope parameters when we are releasing the note
    if (adsr1 & 0x80) {
        u8 attack = adsr1 & 0xf;

        if (attack == 0xf) {
            // 0ms attack, go straight to full volume, and set decay
            DspSetChannelEnvelopeHeight(channel, 0x7f);
            channels[channel].envSpeed = channels[channel].decaySpeed;
            channels[channel].envState = ENVSTATE_DECAY;
        } else {
            DspSetChannelEnvelopeHeight(channel, 0);
            channels[channel].envSpeed = ENVCNT[(attack << 1) + 1];
            channels[channel].envState = ENVSTATE_ATTACK;
        }
    } else {
        // Gain mode
    	u8 gain = DSP_MEM[(channel << 4) + DSP_GAIN];

        if ((gain & 0x80) == 0) {
            // Direct designation
            DspSetChannelEnvelopeHeight(channel, gain & 0x7f);
            channels[channel].envState = ENVSTATE_DIRECT;
        } else {
            DspSetChannelEnvelopeHeight(channel, 0);
            channels[channel].envSpeed = ENVCNT[gain & 0x1f];

            switch ((gain >> 5) & 0x3) {
            case 0:
                // Linear decrease
                channels[channel].envState = ENVSTATE_DECREASE;
                break;
            case 1:
                // Exponential decrease
                channels[channel].envState = ENVSTATE_DECEXP;
                break;
            case 2:
                // Linear increase
                channels[channel].envState = ENVSTATE_INCREASE;
                break;
            case 3:
                // Bent line increase
                channels[channel].envState = ENVSTATE_BENTLINE;
                break;
            }
        }
    }
}

void DspChangeChannelEnvelopeGain(u32 channel) {
    // Don't set envelope parameters when we are releasing the note
    if (!channels[channel].active) return;
    if (channels[channel].envState == ENVSTATE_RELEASE) return;

    // If in ADSR mode, write to GAIN register has no effect
    if (DSP_MEM[(channel << 4) + DSP_ADSR1] & 0x80) return;

    // Otherwise treat it as GAIN change
	u8 gain = DSP_MEM[(channel << 4) + DSP_GAIN];

    if ((gain & 0x80) == 0) {
        // Direct designation
        DspSetChannelEnvelopeHeight(channel, gain & 0x7f);
        channels[channel].envState = ENVSTATE_DIRECT;
        channels[channel].envSpeed = 0;
    } else {
        channels[channel].envSpeed = ENVCNT[gain & 0x1f];

        switch ((gain >> 5) & 0x3) {
        case 0:
            // Linear decrease
            channels[channel].envState = ENVSTATE_DECREASE;
            break;
        case 1:
            // Exponential decrease
            channels[channel].envState = ENVSTATE_DECEXP;
            break;
        case 2:
            // Linear increase
            channels[channel].envState = ENVSTATE_INCREASE;
            break;
        case 3:
            // Bent line increase
            channels[channel].envState = ENVSTATE_BENTLINE;
            break;
        }
    }
}

void DspChangeChannelEnvelopeAdsr1(u32 channel, u8 orig) {
    // Don't set envelope parameters when we are releasing the note
    if (!channels[channel].active) return;
    if (channels[channel].envState == ENVSTATE_RELEASE) return;

	u8 adsr1 = DSP_MEM[(channel << 4) + DSP_ADSR1];

    u8 decay = (adsr1 >> 4) & 0x7;
    channels[channel].decaySpeed = ENVCNT[(decay << 1) + 0x10];

    if (channels[channel].envState == ENVSTATE_ATTACK) {
        u8 attack = adsr1 & 0xf;
        channels[channel].envSpeed = ENVCNT[(attack << 1) + 1];
    } else if (channels[channel].envState == ENVSTATE_DECAY) {
        channels[channel].envSpeed = channels[channel].decaySpeed;
    }

    if (adsr1 & 0x80) {
        if (!(orig & 0x80)) {
            // Switch to ADSR
            u8 attack = adsr1 & 0xf;
            channels[channel].envState = ENVSTATE_ATTACK;
            channels[channel].envSpeed = ENVCNT[(attack << 1) + 1];
        }
    } else {
        // Switch to gain mode
        DspChangeChannelEnvelopeGain(channel);
    }
}

void DspChangeChannelEnvelopeAdsr2(u32 channel) {
    // Don't set envelope parameters when we are releasing the note
    if (!channels[channel].active) return;
    if (channels[channel].envState == ENVSTATE_RELEASE) return;

	u8 adsr2 = DSP_MEM[(channel << 4) + DSP_ADSR2];
    u8 sustainRate = adsr2 & 0x1f;
    channels[channel].sustainSpeed = ENVCNT[sustainRate];

    if (channels[channel].envState == ENVSTATE_SUSTAIN) {
        channels[channel].envSpeed = channels[channel].sustainSpeed;
    }
}

void DspKeyOnChannel(u32 i) {
    channels[i].envState = ENVSTATE_NONE;

    DspSetChannelEnvelopeHeight(i, 0);
    DSP_MEM[(i << 4) | DSP_ENVX] = 0;
    DSP_MEM[(i << 4) | DSP_OUTX] = 0;

    DspSetChannelVolume(i);
    DspSetChannelPitch(i);
    DspSetChannelSource(i);
    DspStartChannelEnvelope(i);

    channels[i].samplePos = 16 << 12;

    channels[i].brrHeader = 0;
    channels[i].prevSamp1 = 0;
    channels[i].prevSamp2 = 0;

	channels[i].decoded[13] = 0;
	channels[i].decoded[14] = 0;
	channels[i].decoded[15] = 0;

    channels[i].envCount = ENVCNT_START;
    channels[i].active = true;
	
	/*bprintf("%d -> %04X %04X\n", 
		i, 
		channels[i].blockPos, channels[i].sampleSpeed);*/

    DSP_MEM[DSP_ENDX] &= ~(1 << i);
}

void DspPrepareStateAfterReload() {
    // Set up echo delay
    DspWriteByte(DSP_MEM[DSP_EDL], DSP_EDL);

    echoBase = ((u32)DSP_MEM[DSP_ESA] << 8);
    //memset(&APU_MEM[echoBase], 0, echoDelay);

	u32 i=0;
	for (i = 0; i < 8; i++) {
        channels[i].echoEnabled = (DSP_MEM[DSP_EON] >> i) & 1;

        if (DSP_MEM[DSP_KON] & (1 << i)) {
            DspKeyOnChannel(i);
        }
	}
}

extern Handle SPCSync;
void DspReplayWriteByte(u8 val, u8 address);

void DSP_BufferSwap()
{
	
	u16* tmp = DSP_WritingWriteDSPBuffer;
	DSP_WritingWriteDSPBuffer = DSP_PlayingWriteDSPBuffer;
	DSP_PlayingWriteDSPBuffer = tmp;
	
	tmp = DSP_WritingWriteTimeBuffer;
	DSP_WritingWriteTimeBuffer = DSP_PlayingWriteTimeBuffer;
	DSP_PlayingWriteTimeBuffer = tmp;

	DSP_WritingWriteDSPBuffer[8191] = 0;

	svcSignalEvent(SPCSync);
}

void DspWriteByte(u8 val, u8 address)
{
	if (address > 0x7f) return;

	u16* dsp = DSP_WritingWriteDSPBuffer;

	if(dsp[8191] >= 8191)
	{
		bprintf("!! DSP WRITEBUFFER OVERFLOW\n");
		return;
	}

	DSP_WritingWriteTimeBuffer[dsp[8191]] = SPC_ElapsedCycles;
	dsp[dsp[8191]] = (address << 8) | val;
	dsp[8191]++;
}

u32 DspMixSamplesStereo(u32 samples, s16 *mixBuf, u32 length, u32 curpos, bool restart)
{
	if(!samples)
		return curpos;

	u32 newlen = length << 1;
	u32 newpos = (curpos << 1) % newlen;

	if(restart)
	{
		playSamples = samples;
		writeSamples = 512;
	}
	else
	{
		playSamples += samples;
		writeSamples += 512;
	}

	if(playSamples >= 0x10000 && writeSamples >= 0x10000)
	{
		playSamples -= 0x10000;
		writeSamples -= 0x10000;
	}

	u32 procSamples = 512;
	if(playSamples > writeSamples) {
		procSamples += (playSamples - writeSamples);
		writeSamples = playSamples;
	}

	s16 *mix = &mixBuf[newpos];
	s16 *mixend = &mixBuf[newlen];
	u16 *dsp = DSP_PlayingWriteDSPBuffer;
	u16 *dspend = &dsp[dsp[8191]];
	u16 *time = DSP_PlayingWriteTimeBuffer;

	u32 totCycle = procSamples * 32;
	u32 curCycle = 0;
	bool loop;

	do
	{
		loop = false;

		// We work with one sample each time. We could try and group up a bunch of samples together, but then we'd need to deal with ring buffer wrap-around
		if(curCycle < totCycle)
		{
			loop = true;
			DspMixSamplesStereoAsm(1, mix);
			mix += 2;
			if(mix == mixend)
				mix = mixBuf;
			curCycle += 32;
		}

		// Only process DSP writes if we have any left and either if they are within the correct cycle period or all samples are accounted for
		while((dsp < dspend) && ((*time < curCycle) || (curCycle == totCycle)))
		{
			loop = true;
			u16 val = *dsp;
			DspReplayWriteByte(val & 0xFF, val >> 8);
			dsp++;
			time++;			
		}

	} while(loop);

	return ((curpos + procSamples) % length);
}

void DspReplayWriteByte(u8 val, u8 address) 
{
    u8 orig = DSP_MEM[address];
    DSP_MEM[address] = val;

    //if (address > 0x7f) return;

    switch (address & 0xf) {
        case DSP_VOL_L:
			DspSetChannelVolume(address >> 4);
            break;
		case DSP_VOL_R:
			DspSetChannelVolume(address >> 4);
			break;
		case DSP_PITCH_L:
			DspSetChannelPitch(address >> 4);
			break;
		case DSP_PITCH_H:
			DspSetChannelPitch(address >> 4);
			break;
		case DSP_ADSR1:
    		DspChangeChannelEnvelopeAdsr1(address >> 4, orig);
			break;
		case DSP_ADSR2:
			DspChangeChannelEnvelopeAdsr2(address >> 4);
			break;
		case DSP_GAIN:
			DspChangeChannelEnvelopeGain(address >> 4);
			break;

		case DSP_FIR:
			DspSetFIRCoefficient(address >> 4);
			break;

		case 0xC:
            switch (address >> 4) {
            case (DSP_KON >> 4):
//                val &= ~DSP_MEM[DSP_KOF];
//                DSP_MEM[DSP_KON] = val & DSP_MEM[DSP_KOF];

                if (val) {
                    DSP_MEM[DSP_KON] = val & DSP_MEM[DSP_KOF];
                    val &= ~DSP_MEM[DSP_KOF];
					u32 i=0;
                    for (; i<8; i++)
                        if ((val>>i)&1) {
                            DspKeyOnChannel(i);
                        }
                }
                break;

            case (DSP_KOF >> 4):{
			int i=0;
                for (; i<8; i++)
                    if (((val>>i)&1) && channels[i].active && channels[i].envState != ENVSTATE_RELEASE) {
                        // Set current state to release (ENDX will be set when release hits 0)
                        channels[i].envState = ENVSTATE_RELEASE;
                        channels[i].envCount = ENVCNT_START;
                        channels[i].envSpeed = ENVCNT[0x1C];
                    }}
    			break;
			case (DSP_FLAG >> 4):{
					if(val & 0x80)
					{
						DSP_MEM[DSP_FLAG] = 0x60;
						noiseSample = 0x8000;
						noiseStep = 1;
					}
					else
						DSP_MEM[DSP_FLAG] = val;
				}
				break;
				
            case (DSP_ENDX >> 4):
	    		DSP_MEM[DSP_ENDX] = 0;
		    	break;
            }
            break;

        case 0xD:
            switch (address >> 4) {

            case (DSP_EDL >> 4):
                val &= 0xf;
                if (val == 0) {
                    echoDelay = 4;
                } else {
                    echoDelay = ((u32)(val << 4) * 32) << 2;
                }
                break;

			case (DSP_PMOD >> 4):
				{
					int i=1;
					for (; i<8; i++)
						channels[i].pmodEnabled = (val >> i) & 0x1;
				}
				break;

            case (DSP_NON >> 4):
				{
					int i=0;
					for (; i<8; i++)
						channels[i].noiseEnabled = (val >> i) & 0x1;
				}
                break;

            case (DSP_ESA >> 4):
                echoBase = (u32)val << 8;
                break;

            case (DSP_EON >> 4):{
			int i=0;
                for (; i < 8; i++) {
                    channels[i].echoEnabled = (val >> i) & 1;
                }}
                break;
            }			
    }
}
