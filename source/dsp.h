// This code has been taken from SNemulDS which is licensed under GPLv2.
// Credits go to Archeide and whoever else participated in this.

#include <3ds.h>

#include "spc700.h"
#include "mixrate.h"

#define ALIGNED __attribute__ ((aligned(4)))

void DspReset();
void DspPrepareStateAfterReload();

extern u8 DSP_MEM[0x100];
extern u16 dspPreamp;

extern u32 echoBase;
extern u16 echoRemain;
extern s8 firFilter[16];

u32 DspMixSamplesStereo(u32 samples, s16 *mixBuf, u32 length, u32 curpos, bool restart);
void DspWriteByte(u8 val, u8 address);

void DSP_BufferSwap();

struct _DspChannel {
    int sampleSpeed;
    int samplePos;
    int envCount;
    int envSpeed;
    s16 prevDecode[4];
    s16 decoded[16];
    u16 decaySpeed;
    u16 sustainSpeed;
    s16 envx;
    s16 prevSamp1, prevSamp2;
    u16 blockPos;
    s16 leftCalcVolume;
    s16 rightCalcVolume;
    u8 envState;
    u8 sustainLevel;
    s8 leftVolume;
    s8 rightVolume;
    s8 keyWait;
    bool active;
    u8 brrHeader;
    bool echoEnabled;
	bool noiseEnabled;
	bool pmodEnabled;
	u8 empty1;
	u8 empty2;
} ALIGNED;
typedef struct _DspChannel DspChannel;

extern DspChannel channels[8];

// DSP Register defintions

// Channel specific
#define DSP_VOL_L		0x00
#define DSP_VOL_R		0x01
#define DSP_PITCH_L		0x02
#define DSP_PITCH_H		0x03
#define DSP_SRC			0x04
#define DSP_ADSR1		0x05
#define DSP_ADSR2		0x06
#define DSP_GAIN		0x07
#define DSP_ENVX		0x08
#define DSP_OUTX		0x09

// Global
#define DSP_MAINVOL_L	0x0C
#define DSP_MAINVOL_R	0x1C
#define DSP_ECHOVOL_L	0x2C
#define DSP_ECHOVOL_R	0x3C
#define DSP_KON			0x4C
#define DSP_KOF			0x5C
#define DSP_FLAG		0x6C
#define DSP_ENDX		0x7C

#define DSP_EFB			0x0D
#define DSP_PMOD		0x2D
#define DSP_NON			0x3D
#define DSP_EON			0x4D
#define DSP_DIR			0x5D
#define DSP_ESA			0x6D
#define DSP_EDL			0x7D

#define DSP_FIR			0x0F

// Envelope state definitions
#define ENVSTATE_NONE       0
#define ENVSTATE_ATTACK		1
#define ENVSTATE_DECAY		2
#define ENVSTATE_SUSTAIN	3
#define ENVSTATE_RELEASE	4
#define ENVSTATE_DIRECT		5
#define ENVSTATE_INCREASE	6
#define ENVSTATE_BENTLINE	7
#define ENVSTATE_DECREASE	8
#define ENVSTATE_DECEXP		9
