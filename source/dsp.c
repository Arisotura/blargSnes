// This code has been taken from SNemulDS which is licensed under GPLv2.
// Credits go to Archeide and whoever else participated in this.

#include <3ds.h>

#include "spc700.h"
#include "dsp.h"
#include "mixrate.h"

// DSP write buffers
// * 32 16-sample periods
// * over a 16-sample period -> 512 cycles
// * 4 cycles atleast per write -> 128 writes at most
// * double-buffered
// -> 8192 entries (16K)

u16 DSP_WriteBuffer[2][32*128];
u16* DSP_WritingWriteBuffer;
u16* DSP_PlayingWriteBuffer;

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

s32 mixBuffer[DSPMIXBUFSIZE * 2];
//s32 echoBuffer[DSPMIXBUFSIZE * 2];
s16 brrTab[16 * 16];
//u32 firTable[8 * 2 * 2];
u8 *echoBase;
u16 dspPreamp ALIGNED = 0x140;
u16 echoDelay ALIGNED;
u16 echoCursor ALIGNED;
//u8 firOffset ALIGNED;

// externs from dspmixer.S
u32 DecodeSampleBlockAsm(u8 *blockPos, s16 *samplePos, DspChannel *channel);
extern u8 channelNum;

u32 DecodeSampleBlock(DspChannel *channel) {
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
    echoCursor = 0;
    echoBase = APU_MEM;
	int i=0,c=0;
	
	memset(DSP_WriteBuffer, 0, sizeof(DSP_WriteBuffer));
	
	DSP_WritingWriteBuffer = DSP_WriteBuffer[0];
	DSP_PlayingWriteBuffer = DSP_WriteBuffer[1];

/*    firOffset = 0;
    for (i = 0; i < 8*2*2; i++) {
        firTable[i] = 0;
    }*/
 
    memset(DSP_MEM, 0, 0x100);
    // Disable echo emulation
    DSP_MEM[DSP_FLAG] = 0x20;

	for (i = 0; i < 8; i++) {
		memset(&channels[i], 0, sizeof(DspChannel));
        /*channels[i].samplePos = 0;
        channels[i].envCount = 0;
        channels[i].active = false;
        channels[i].echoEnabled = false;*/
	}

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

    channels[i].envCount = ENVCNT_START;
    channels[i].active = true;
	
	/*bprintf("%d -> %04X %04X\n", 
		i, 
		channels[i].blockPos, channels[i].sampleSpeed);*/

    if ((DSP_MEM[DSP_NOV]>>i)&1) {
        channels[i].active = false;
        // Noise sample
    }

    DSP_MEM[DSP_ENDX] &= ~(1 << i);
}

void DspPrepareStateAfterReload() {
    // Set up echo delay
    DspWriteByte(DSP_MEM[DSP_EDL], DSP_EDL);

    echoBase = APU_MEM + (DSP_MEM[DSP_ESA] << 8);
    memset(echoBase, 0, echoDelay);

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
	int i;
	
	u16* tmp = DSP_WritingWriteBuffer;
	DSP_WritingWriteBuffer = DSP_PlayingWriteBuffer;
	DSP_PlayingWriteBuffer = tmp;
	
	for (i = 0; i < 32; i++)
		DSP_WritingWriteBuffer[(i << 7) + 127] = 0;
	
	svcSignalEvent(SPCSync);
}

void DspWriteByte(u8 val, u8 address)
{
	if (address > 0x7f) return;
	
	u16* buffer = &DSP_WritingWriteBuffer[(SPC_ElapsedCycles & 0x3E00) >> 2];
	
	if (buffer[127] >= 127) 
	{
		bprintf("!! DSP WRITEBUFFER OVERFLOW\n");
		return;
	}
	
	buffer[buffer[127]] = (address << 8) | val;
	buffer[127]++;
}

void DSP_ReplayWrites(u32 idx)
{
	u16* buffer = &DSP_PlayingWriteBuffer[idx << 7];
	u16 i;
	
	u16 num = buffer[127];
	for (i = 0; i < num; i++)
	{
		u16 val = buffer[i];
		DspReplayWriteByte(val & 0xFF, val >> 8);
	}
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

            case (DSP_NOV >> 4):{
			int i=0;
                for (; i<8; i++)
                if ((val>>i)&1) {
                    // TODO: Need to implement noise channels
                    channels[i].active = false;
                }}
                break;

            case (DSP_ESA >> 4):
                echoBase = APU_MEM + (DSP_MEM[DSP_ESA] << 8);
                break;

            case (DSP_EON >> 4):{
			int i=0;
                for (; i < 8; i++) {
                    channels[i].echoEnabled = (val >> i) & 1;
                }}
                break;
            }
    }

/*
		case DSP_PMOD:
			for (int i=0; i<8; i++)
				if ((val>>i)&1) {
//					dspunimpl(DSP_PMOD);
//					channels[i].active = 0;
				}
			break;
            */
}
