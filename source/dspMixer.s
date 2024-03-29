@ This code has been taken and modified from SNemulDS which is licensed under GPLv2.
@ Credits go to Archeide and whoever else participated in this.

	.TEXT
	.ARM
	.ALIGN

#include "mixrate.h"

@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ Function called with:
@ r0 - Raw brr data (s8*)
@ r1 - Decoded sample data (s16*)
@ r2 - DspChannel *channel
@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ Function Data:
@ r4 - sample1
@ r5 - sample2
@ r6,r7 - tmp
@ r8 - shift amount
@ r9 - number of iterations left
@ r10 - 0xf
@ r11 - low clip
@ r12 - high clip

#define PREV0 r2
#define PREV1 r3
#define SAMPLE1 r4
#define SAMPLE2 r5
#define BRR_TAB r8
#define ITER_LEFT r9
#define CONST_F r10

.GLOBAL brrHash
brrHash:
.word 0

.GLOBAL DecodeSampleBlockAsm
DecodeSampleBlockAsm:
    stmfd sp!, {r4-r12,r14}

    @ Save the channel pointer
    mov r14, r2

	@ copy the last 3 "decoded" samples into the "prevDecode" sections (for Gaussian interpolation)
	ldrh r4, [r14, #50]
	strh r4, [r14, #18]
	ldrh r4, [r14, #52]
	strh r4, [r14, #20]
	ldrh r4, [r14, #54]
	strh r4, [r14, #22]	
	
	
    @ Load prev0 and prev1
    ldrsh PREV0, [r14, #62]
    ldrsh PREV1, [r14, #64]
    
    ldrb r4, [r0], #1
    @ Compute the index into the brrTab to load the bytes from
    mov r9, r4, lsr #4
    ldr r8, =brrTab
    add r8, r8, r9, lsl #5 @ brrTabPtr = brrTab + (r4 * 32)

    mov CONST_F, #0xf << 1
    ldr r11, =0xffff800c
    ldr r12, =0x7ff0

    @ 16 samples to decode, but do two at a time
    mov ITER_LEFT, #8
    @ Figure out the type of decode filter
    mov r4, r4, lsr #2
    and r4, r4, #3
    ldr pc, [pc, r4, lsl #2]
    nop
.word case0
.word case1
.word case2
.word case3
case0:
    ldrb r4, [r0], #1
    and r5, CONST_F, r4, lsl #1
    ldrsh SAMPLE2, [BRR_TAB, r5]
    and r4, CONST_F, r4, lsr #3
    ldrsh SAMPLE1, [BRR_TAB, r4]

    mov r4, r4, lsl #1
    mov r5, r5, lsl #1
    strh r4, [r1], #2
    strh r5, [r1], #2

    subs ITER_LEFT, ITER_LEFT, #1
    bne case0

    @ Set up prev0 and prev1
    ldrsh PREV0, [r1, #-2]
    ldrsh PREV1, [r1, #-4]
    
    b doneDecode

case1:
    ldrb r4, [r0], #1
    and r5, CONST_F, r4, lsl #1
    ldrsh SAMPLE2, [BRR_TAB, r5]
    and r4, CONST_F, r4, lsr #3
    ldrsh SAMPLE1, [BRR_TAB, r4]

    @ prev1 = sample1 + (last1 >> 1) - (last1 >> 5)    
    add PREV1, SAMPLE1, PREV0, asr #1
    sub PREV1, PREV1, PREV0, asr #5
    
    cmp PREV1, r12
    movgt PREV1, r12
    cmp PREV1, r11
    movlt PREV1, r11
	mov PREV1, PREV1, lsl #1
    strh PREV1, [r1], #2
    ldrsh PREV1, [r1, #-2]

    @ same for prev0 now
    add PREV0, SAMPLE2, PREV1, asr #1
    sub PREV0, PREV0, PREV1, asr #5

    cmp PREV0, r12
    movgt PREV0, r12
    cmp PREV0, r11
    movlt PREV0, r11
	mov PREV0, PREV0, lsl #1
    strh PREV0, [r1], #2
    ldrsh PREV0, [r1, #-2]

    subs ITER_LEFT, ITER_LEFT, #1
    bne case1

    b doneDecode

case2:
    ldrb r4, [r0], #1
    and r5, CONST_F, r4, lsl #1
    ldrsh SAMPLE2, [BRR_TAB, r5]
    and r4, CONST_F, r4, lsr #3
    ldrsh SAMPLE1, [BRR_TAB, r4]

    @ Sample 1
    mov r6, PREV1, asr #1
    rsb r6, r6, PREV1, asr #5
    mov PREV1, PREV0
    add r7, PREV0, PREV0, asr #1
    rsb r7, r7, #0
    add r6, r6, r7, asr #5
    add r7, SAMPLE1, PREV0
    add PREV0, r6, r7

    cmp PREV0, r12
    movgt PREV0, r12
    cmp PREV0, r11
    movlt PREV0, r11
	mov PREV0, PREV0, lsl #1
    strh PREV0, [r1], #2
    ldrsh PREV0, [r1, #-2]

    @ Sample 2
    mov r6, PREV1, asr #1
    rsb r6, r6, PREV1, asr #5
    mov PREV1, PREV0
    add r7, PREV0, PREV0, asr #1
    rsb r7, r7, #0
    add r6, r6, r7, asr #5
    add r7, SAMPLE2, PREV0
    add PREV0, r6, r7

    cmp PREV0, r12
    movgt PREV0, r12
    cmp PREV0, r11
    movlt PREV0, r11
	mov PREV0, PREV0, lsl #1
    strh PREV0, [r1], #2
    ldrsh PREV0, [r1, #-2]
    
    subs ITER_LEFT, ITER_LEFT, #1
    bne case2

    b doneDecode

case3:
    ldrb r4, [r0], #1
    and r5, CONST_F, r4, lsl #1
    ldrsh SAMPLE2, [BRR_TAB, r5]
    and r4, CONST_F, r4, lsr #3
    ldrsh SAMPLE1, [BRR_TAB, r4]

    @ Sample 1
    add r6, PREV1, PREV1, asr #1
    mov r6, r6, asr #4
    sub r6, r6, PREV1, asr #1
    mov PREV1, PREV0
    add r7, PREV0, PREV0, lsl #2
    add r7, r7, PREV0, lsl #3
    rsb r7, r7, #0
    add r6, r6, r7, asr #7
    add r6, r6, PREV0
    add PREV0, SAMPLE1, r6

	cmp PREV0, r12
    movgt PREV0, r12
    cmp PREV0, r11
    movlt PREV0, r11
    mov PREV0, PREV0, lsl #1
    strh PREV0, [r1], #2
    ldrsh PREV0, [r1, #-2]

    @ Sample 2
    add r6, PREV1, PREV1, asr #1
    mov r6, r6, asr #4
    sub r6, r6, PREV1, asr #1
    mov PREV1, PREV0
    add r7, PREV0, PREV0, lsl #2
    add r7, r7, PREV0, lsl #3
    rsb r7, r7, #0
    add r6, r6, r7, asr #7
    add r6, r6, PREV0
    add PREV0, SAMPLE2, r6

    cmp PREV0, r12
    movgt PREV0, r12
    cmp PREV0, r11
    movlt PREV0, r11
	mov PREV0, PREV0, lsl #1
    strh PREV0, [r1], #2
    ldrsh PREV0, [r1, #-2]

    subs ITER_LEFT, ITER_LEFT, #1
    bne case3

doneDecode:
/*    sub r1, r1, #32
    ldmia r1, {r4-r11}
    ldmfd sp!, {r1}
    stmia r1, {r4-r11}*/

doneDecodeCached:
    @ Store prev0 and prev1
    strh PREV0, [r14, #62]
    strh PREV1, [r14, #64]
	@strh PREV0, [r14, #22]

    ldmfd sp!, {r4-r12,r14}
    bx lr

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

@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ Function called with:
@ r0 - int Number of samples to mix
@ r1 - u16* mix buffer (left first, right second, channels interleaved for MIXBUFSIZE * 2 bytes)
@@@@@@@@@@@@@@@@@@@@@@@@@@@@

/*
struct DspChannel {
int sampleSpeed;        0
int samplePos;          4
int envCount;           8
int envSpeed;           12
s16 prevDecode[4];      16
s16 decoded[16];        24
u16 decaySpeed;         56
u16 sustainSpeed;       58
s16 envx;               60
s16 prevSamp1;          62
s16 prevSamp2;          64
u16 blockPos;           66
s16 leftCalcVolume;     68
s16 rightCalcVolume;    70
u8 envState;            72
u8 sustainLevel;        73
s8 leftVolume;          74
s8 rightVolume;         75
s8 keyWait;             76
bool active;            77
u8 brrHeader;           78
bool echoEnabled;       79
bool noiseEnabled;		80
bool pmodEnabled;		81
bool pmodWrite;			82
u8 empty;				83
};
*/

#define DSPCHANNEL_SIZE 84

#define SAMPLESPEED_OFFSET 0
#define SAMPLEPOS_OFFSET 4
#define ENVCOUNT_OFFSET 8
#define ENVSPEED_OFFSET 12
#define PREVDECODE_OFFSET 16
#define DECODED_OFFSET 24
#define DECAYSPEED_OFFSET 56
#define SUSTAINSPEED_OFFSET 58
#define ENVX_OFFSET 60
#define BLOCKPOS_OFFSET 66
#define LEFTCALCVOL_OFFSET 68
#define RIGHTCALCVOL_OFFSET 70
#define ENVSTATE_OFFSET 72
#define SUSTAINLEVEL_OFFSET 73
#define LEFTVOL_OFFSET 74
#define RIGHTVOL_OFFSET 75
#define KEYWAIT_OFFSET 76
#define ACTIVE_OFFSET 77
#define ECHOENABLED_OFFSET 79
#define NOISEENABLED_OFFSET 80
#define PMODENABLED_OFFSET 81

@ r0 - channel structure base
@ r1 - mix buffer
@ r2 - echo buffer ptr
@ r3 - numSamples
@ r4 - sampleSpeed
@ r5 - samplePos
@ r6 - envCount
@ r7 - envSpeed
@ r8 - sampleValue (value of the current sample)
@ r9 - tmp
@ r10 - leftCalcVol
@ r11 - rightCalcVol
@ r12 - tmp
@ r13 - tmp
@ r14 - tmp

#define CHAN_STRUCT		r0
#define CHAN_CUR		r1

#define SAMPLE_SPEED	r2
#define SAMPLE_POS		r3
#define ENV_COUNT		r2
#define ENV_SPEED		r3

#define SAMPLE_VALUE	r6

#define LEFT_CALC_VOL	r7
#define RIGHT_CALC_VOL	r8



.GLOBAL DspMixSamplesStereoAsm
.FUNC DspMixSamplesStereoAsm
DspMixSamplesStereoAsm:

    stmfd sp!, {r4-r12, lr}

mixLoopback:

	@ Store the sample count and mix buffer, because we don't need them
	@	until towards the end of each processed sample
	stmfd sp!, {r0,r1}

	@ Clear the L/R sample/echo values, as well as the PMOD value
	mov r9, #0
	mov r10, #0
	mov r11, #0
	mov r12, #0
	mov r14, #0

	stmfd sp!, {r9-r12}

processNoise:		@ r0-r8

	@ Is the noise flag set?
	ldr r2, =DSP_MEM
	ldrb r2, [r2, #0x6C]
	and r2, r2, #0x1F
	cmp r2, #0
	beq channelStart
	
	@ Skip new noise calculation if step equals 0
	ldr r1, =noiseStep
	ldrh r0, [r1]
	cmp r0, #0
	bne endNoise

	@ Calculate new noise sample (from -4000h .. +3FFFh, but actual stored sample ranges -8000h .. +7FFE)
	@ sample = ((sample >> 1) & 0x3FFF) | ((sample.bit0 ^ sample.bit1) << 14)
	ldr r3, =noiseSample
	ldrsh r4, [r3]

	mov r4, r4, asr #1		@ (sample)
	mov r5, r4, asr #1		@ (sample >> 1)
	eor r4, r4, r5			
	and r4, r4, #0x01		
	mov r4, r4, lsl #14		@ ((sample.bit0 ^ sample.bit1) << 14)
	ldr r6, =0x3FFF
	and r5, r5, r6			@ ((sample >> 1) & 0x3FFF)
	orr r4, r4, r5			@ ((sample >> 1) & 0x3FFF) | ((sample.bit0 ^ sample.bit1) << 14)

	mov r4, r4, lsl #1		
	strh r4, [r3]

	@ Assign new noise step value
	ldr r4, =noiseSteps
	mov r2, r2, lsl #1
	ldrh r0, [r4, r2]

endNoise:		@ r2-r8

	@ Decrement noise step
	sub r0, r0, #1
	strh r0, [r1]

channelStart:

	@ Load the starting channel structure
	ldr CHAN_STRUCT, =channels
	mov CHAN_CUR, #0

channelLoopback:		@ r2-r8

	@ Check if the channel itself is active
	ldrb r2, [CHAN_STRUCT, #ACTIVE_OFFSET]
	cmp r2, #0
	moveq r14, #0
	beq endChannel

	@ Load Sample speed and position, Env count and speed, and volume calculations

	ldrd ENV_COUNT, [CHAN_STRUCT, #ENVCOUNT_OFFSET]

	ldrsh LEFT_CALC_VOL, [CHAN_STRUCT, #LEFTCALCVOL_OFFSET]		@ r7
	ldrsh RIGHT_CALC_VOL, [CHAN_STRUCT, #RIGHTCALCVOL_OFFSET]	@ r8

	@ Begin ENV calculations
	subs ENV_COUNT, ENV_COUNT, ENV_SPEED
	bpl noEnvelopeUpdate

	mov ENV_COUNT, #0x7800
	
	ldrsh r4, [CHAN_STRUCT, #ENVX_OFFSET]
	ldrb r6, [CHAN_STRUCT, #ENVSTATE_OFFSET]

	ldr pc, [pc, r6, lsl #2]
	nop

@ Jump table for envelope handling
.word noEnvelopeUpdate
.word envStateAttack
.word envStateDecay
.word envStateSustain
.word envStateRelease
.word noEnvelopeUpdate      @ Actually direct, but we don't need to do anything
.word envStateIncrease
.word envStateBentline
.word envStateDecrease
.word envStateSustain       @ Actually decrease exponential, but it's the same code

#define ENVX_SHIFT	8
#define ENVX_MAX	0x7f00

envStateAttack:

    add r4, r4, #4 << ENVX_SHIFT
    cmp r4, #ENVX_MAX
    ble storeEnvx

    @ envx = 0x7f, state = decay, speed = decaySpeed
    mov r4, #ENVX_MAX
    mov r6, #ENVSTATE_DECAY
    strb r6, [CHAN_STRUCT, #ENVSTATE_OFFSET]
    ldrh ENV_SPEED, [CHAN_STRUCT, #DECAYSPEED_OFFSET]
    b storeEnvx

envStateDecay:

	rsb r4, r4, r4, lsl #8
    mov r4, r4, asr #8

    ldrb r6, [CHAN_STRUCT, #SUSTAINLEVEL_OFFSET]
    cmp r4, r6, lsl #ENVX_SHIFT
    bge storeEnvx

    @ state = sustain, speed = sustainSpeed
    mov r6, #ENVSTATE_SUSTAIN
    strb r6, [CHAN_STRUCT, #ENVSTATE_OFFSET]
    ldrh ENV_SPEED, [CHAN_STRUCT, #SUSTAINSPEED_OFFSET]
    
    @ Make sure envx > 0
    cmp r4, #0
    bge storeEnvx
    
    @ If not, end channel, then go to next channel
    stmfd sp!, {r0-r3, r14}
	mov r0, CHAN_CUR
    bl DspSetEndOfSample
    ldmfd sp!, {r0-r3, r14}
	mov r14, #0
    b finishChannel

envStateSustain:

	rsb r4, r4, r4, lsl #8
    mov r4, r4, asr #8

    @ Make sure envx > 0
    cmp r4, #0
    bge storeEnvx

    @ If not, end channel, then go to next channel
    stmfd sp!, {r0-r3, r14}
	mov r0, CHAN_CUR
    bl DspSetEndOfSample
    ldmfd sp!, {r0-r3, r14}
	mov r14, #0
    b finishChannel

envStateRelease:

    sub r4, r4, #1 << ENVX_SHIFT

    @ Make sure envx > 0
    cmp r4, #0
    bge storeEnvx

    @ If not, end channel, then go to next channel
    stmfd sp!, {r0-r3, r14}
    mov r0, CHAN_CUR
    bl DspSetEndOfSample
    ldmfd sp!, {r0-r3, r14}
	mov r14, #0
    b finishChannel

envStateIncrease:

    add r4, r4, #4 << ENVX_SHIFT

    cmp r4, #ENVX_MAX
    ble storeEnvx

    @ envx = 0x7f, state = direct, speed = 0
    mov r4, #ENVX_MAX
    mov r6, #ENVSTATE_DIRECT
    strb r6, [CHAN_STRUCT, #ENVSTATE_OFFSET]
    mov ENV_SPEED, #0
    b storeEnvx

envStateBentline:

    cmp r4, #0x5f << ENVX_SHIFT
    addgt r4, r4, #1 << ENVX_SHIFT
    addle r4, r4, #4 << ENVX_SHIFT

    cmp r4, #ENVX_MAX
    blt storeEnvx

    @ envx = 0x7f, state = direct, speed = 0
    mov r4, #ENVX_MAX
    mov r6, #ENVSTATE_DIRECT
    strb r6, [CHAN_STRUCT, #ENVSTATE_OFFSET]
    mov ENV_SPEED, #0
    b storeEnvx

envStateDecrease:

    sub r4, r4, #4 << ENVX_SHIFT

    @ Make sure envx > 0
    cmp r4, #0
    bge storeEnvx
    
    @ If not, end channel, then go to next channel
    stmfd sp!, {r0-r3, r14}
	mov r0, CHAN_CUR
    bl DspSetEndOfSample
    ldmfd sp!, {r0-r3, r14}
	mov r14, #0
    b finishChannel

storeEnvx:		@ r5-r6

	strh r4, [CHAN_STRUCT, #ENVX_OFFSET]

    @ Recalculate leftCalcVol and rightCalcVol
    ldrsb LEFT_CALC_VOL, [CHAN_STRUCT, #LEFTVOL_OFFSET]
    mul LEFT_CALC_VOL, r4, LEFT_CALC_VOL
    mov LEFT_CALC_VOL, LEFT_CALC_VOL, asr #7

    ldrsb RIGHT_CALC_VOL, [CHAN_STRUCT, #RIGHTVOL_OFFSET]
    mul RIGHT_CALC_VOL, r4, RIGHT_CALC_VOL
    mov RIGHT_CALC_VOL, RIGHT_CALC_VOL, asr #7


noEnvelopeUpdate:		@ r4-r6, r14 (PMOD stuff gets written back with a new value later on)

	strd ENV_COUNT, [r0, #ENVCOUNT_OFFSET]
	ldrd SAMPLE_SPEED, [r0, #SAMPLESPEED_OFFSET]

	ldrb r4, [r0, #PMODENABLED_OFFSET]
	cmp r4, #1
	bne sampleUpdate

	mov r14, r14, asr #6
	add r14, #0x400
	mul SAMPLE_SPEED, r14, SAMPLE_SPEED
	mov SAMPLE_SPEED, SAMPLE_SPEED, asr #10
	ldr r14, =0x3FFF
	cmp SAMPLE_SPEED, r14
	movgt SAMPLE_SPEED, r14
	mov r14, #0

sampleUpdate:

	add SAMPLE_POS, SAMPLE_POS, SAMPLE_SPEED
    cmp SAMPLE_POS, #16 << 12
    blo noSampleUpdate

	@ Decode next 16 bytes...
    sub SAMPLE_POS, SAMPLE_POS, #16 << 12

    @ Decode the sample block, r0 = DspChannel*
    stmfd sp!, {r0-r3, r14}
    bl DecodeSampleBlock
    cmp r0, #1
    ldmfd sp!, {r0-r3, r14}
    beq finishChannel

noSampleUpdate:		@ r2, r4-r6, r14

	str SAMPLE_POS, [CHAN_STRUCT, #SAMPLEPOS_OFFSET]

	@ First, check if this channel uses noise
	ldrb r6, [CHAN_STRUCT, #NOISEENABLED_OFFSET]
	cmp  r6, #1
	bne gaussianInterpolation

	@ Take from the pre-processed noise sample
	ldr r6, =noiseSample
	ldrsh r6, [r6]

	b finishSample

gaussianInterpolation:		@ r2, r4-r6, r14 (SAMPLE_POS = r3)

	mov r4, SAMPLE_POS, lsr #12
	ldr r2, =gaussian
	add r4, CHAN_STRUCT, r4, lsl #1
	and r3, SAMPLE_POS, #0xFF0
	add r4, #DECODED_OFFSET
	lsr r3, #3
	sub r4, #6
	
	@ r2 = gaussian pointer
	@ r3 = interpolation index
	@ r4 = sample pointer
	@ r5 = tmp
	@ r6 = out sample
	@ r14 = interpolation data


	ldr r5, =0x1FE
	rsb r5, r3, r5
	ldrh r14, [r2, r5]
	ldrsh r5, [r4], #2
	mul r6, r14, r5
	asr r6, #10

	ldr r5, =0x3FE
	rsb r5, r3, r5
	ldrh r14, [r2, r5]
	ldrsh r5, [r4], #2
	mul r5, r14, r5
	add r6, r5, asr #10

	ldr r5, =0x200
	add r5, r3
	ldrh r14, [r2, r5]
	ldrsh r5, [r4], #2
	mul r5, r14, r5
	add r6, r5, asr #10

	mov r5, r3
	ldrh r14, [r2, r5]
	ldrsh r5, [r4]
	mul r5, r14, r5
	add r6, r5, asr #10

	mov r6, r6, asr #1
	ssat r6, #16, r6

finishSample:		@ r2-r5, r14

	ldmfd sp!, {r9-r12}
	mla r11, r6,  LEFT_CALC_VOL, r11
	mla r12, r6, RIGHT_CALC_VOL, r12

	ldrb r2, [CHAN_STRUCT, #ECHOENABLED_OFFSET]
	cmp r2, #1
	mov r3, #0
	moveq r3, r6

	mla  r9, r3,  LEFT_CALC_VOL, r9
	mla r10, r3, RIGHT_CALC_VOL, r10

	stmfd sp!, {r9-r12}

finishChannel:		@ r2-r5, r14

	@ Set ENVX and OUTX
	ldr r2, =DSP_MEM
	add r2, r2, CHAN_CUR, lsl #4

	@ Set ENVX
	ldrsh r3, [CHAN_STRUCT, #ENVX_OFFSET]
	mov r3, r3, asr #ENVX_SHIFT
	strb r3, [r2, #0x8]

	@ Set OUTX
	mov r14, r6
	mul r3, r6, r3
	mov r3, r3, asr #15
	strb r3, [r2, #0x9]

	strh  LEFT_CALC_VOL, [CHAN_STRUCT,  #LEFTCALCVOL_OFFSET]
	strh RIGHT_CALC_VOL, [CHAN_STRUCT, #RIGHTCALCVOL_OFFSET]

endChannel:		@ r2-r8
	
	add CHAN_STRUCT, CHAN_STRUCT, #DSPCHANNEL_SIZE
	add CHAN_CUR, CHAN_CUR, #1
	cmp CHAN_CUR, #8
	blt channelLoopback

@ This is the end of normal mixing
@ Onto the echo mixing


processEcho:		@ r0-r8, r14

@ r0	FIR L/R sample / tmp
@ r1	FIR Filter value / Echo Remain value / tmp
@ r2	FIR 8Tap Count / Echo Remain / tmp
@ r3	FIR Filter / DSP_MEM
@ r4	Left Buffer Sample
@ r5	Right Buffer Sample
@ r6	FIR Offset
@ r7	FIR Buffer
@ r8	Echo Base / Echo Delay
@ r9	Left Echo Sample
@ r10	Right Echo Sample
@ r11	RESERVED
@ r12	RESERVED

@ r14	APU_MEM / Preamp

	ldmfd sp!, {r9-r12}


	ldr r14, =SPC_RAM
	ldr  r8, =echoBase
	ldr  r8, [r8]
	add  r8, r14, r8

	ldr  r7, =firBuffer
	ldr  r6, =firOffset
	ldrh r6, [r6]


	ldrsh r4, [r8]
	mov r4, r4, asr #1
	ldrsh r5, [r8, #2]
	mov r5, r5, asr #1

	strh r4, [r7, r6]
	add r6, r6, #2
	strh r5, [r7, r6]
	add r6, r6, #2

	mov r4, #0
	mov r5, #0
	ldr r3, =firFilter
	mov r2, #8


echo8TapLoop:		@ r0-r1

	and r6, r6, #0x1F
	ldrsb r1, [r3], #1

	ldrsh r0, [r7, r6]
	mul r0, r1, r0
	add r4, r4, r0, asr #6
	add r6, r6, #2

	ldrsh r0, [r7, r6]
	mul r0, r1, r0
	add r5, r5, r0, asr #6
	add r6, r6, #2

	subs r2, r2, #1
	bne echo8TapLoop

echo8TapEnd:		@ r0-r3, r7

	@ Wrap FIR Offset
	and r6, r6, #0x1F

	@ I'ma give these samples....THE CLAMPS!!!
	ssat r4, #16, r4
	ssat r5, #16, r5

	ldr r3, =DSP_MEM
	
	ldrb r2, [r3, #0x6C]
	and r2, r2, #0x20
	cmp r2, #0x20
	beq echoSkipWrite

echoWrite:		@ r0-r2, r7

	ldrsb r2, [r3, #0x0D]

	mov r1, r9, asr #15
	mul r0, r2, r4
	add r1, r1, r0, asr #7
	ssat r1, #16, r1
	strh r1, [r8]

	mov r1, r10, asr #15
	mul r0, r2, r5
	add r1, r1, r0, asr #7
	ssat r1, #16, r1
	strh r1, [r8, #2]

echoSkipWrite:		@ r0-r2, r7

	mov r9, r4
	mov r10, r5

	@ Wrap Echo Base
	add r8, r8, #4
	sub r2, r8, r14
	cmp r2, #0x10000
	subge r8, r8, #0x10000

	@ Reload Echo Base and Remain if needed

	ldr r2, =echoRemain
	ldrh r1, [r2]
	subs r1, r1, #1
	bne echoSkipReset

echoResetBase:		@ r0-r1, r4-r5, r7

	ldr r8, =echoDelay
	ldrh r1, [r8]
	mov r1, r1, lsr #2

	ldrb r8, [r3, #0x6D]
	add r8, r14, r8, lsl #8

echoSkipReset:		@ r0, r3-r5, r7

	strh r1, [r2]

	ldr r7, =firOffset
	strh r6, [r7]

	sub r8, r8, r14
	ldr r14, =echoBase
	str r8, [r14]

clipAndMix:		@ r0-r2, r4-r8, r14

	@ Reload the sample count and mix buffer, and test if not equal to 0
	ldmfd sp!, {r0,r1}

	ldr r14, =dspPreamp
	ldrh r14, [r14]

	ldr r3, =DSP_MEM
	ldrsb r5, [r3, #0x0C]
	ldrsb r6, [r3, #0x1C]
	ldrsb r7, [r3, #0x2C]
	ldrsb r8, [r3, #0x3C]
	
	mul r5, r14, r5
	mov r5, r5, asr #7
	mul r6, r14, r6
	mov r6, r6, asr #7

	mul r7, r14, r7
	mov r7, r7, asr #7
	mul r8, r14, r8
	mov r8, r8, asr #7

	@ Load and scale by volume (LEFT)
	mov r11, r11, asr #15
	mul r11, r5, r11
	mov r11, r11, asr #7
	mul r9, r7, r9
	add r11, r11, r9, asr #7

	ssat r11, #16, r11
	strh r11, [r1], #2

	@ Load and scale by volume (RIGHT)
	mov r12, r12, asr #15
	mul r12, r6, r12
	mov r12, r12, asr #7
	mul r10, r8, r10
	add r12, r12, r10, asr #7

	ssat r12, #16, r12
	strh r12, [r1], #2

	@ Are we done yet?
	subs r0, r0, #1
	bne mixLoopback

mixDone:

	@ We are done
    ldmfd sp!, {r4-r12, lr}
    bx lr

.ENDFUNC

.GLOBAL firOffset
.GLOBAL noiseSample
.GLOBAL noiseStep

.data

firOffset:
.hword 0
firBuffer:
.hword 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
gaussian:
.hword 0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000
.hword 0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x002,0x002,0x002,0x002,0x002
.hword 0x002,0x002,0x003,0x003,0x003,0x003,0x003,0x004,0x004,0x004,0x004,0x004,0x005,0x005,0x005,0x005
.hword 0x006,0x006,0x006,0x006,0x007,0x007,0x007,0x008,0x008,0x008,0x009,0x009,0x009,0x00A,0x00A,0x00A
.hword 0x00B,0x00B,0x00B,0x00C,0x00C,0x00D,0x00D,0x00E,0x00E,0x00F,0x00F,0x00F,0x010,0x010,0x011,0x011
.hword 0x012,0x013,0x013,0x014,0x014,0x015,0x015,0x016,0x017,0x017,0x018,0x018,0x019,0x01A,0x01B,0x01B
.hword 0x01C,0x01D,0x01D,0x01E,0x01F,0x020,0x020,0x021,0x022,0x023,0x024,0x024,0x025,0x026,0x027,0x028
.hword 0x029,0x02A,0x02B,0x02C,0x02D,0x02E,0x02F,0x030,0x031,0x032,0x033,0x034,0x035,0x036,0x037,0x038
.hword 0x03A,0x03B,0x03C,0x03D,0x03E,0x040,0x041,0x042,0x043,0x045,0x046,0x047,0x049,0x04A,0x04C,0x04D
.hword 0x04E,0x050,0x051,0x053,0x054,0x056,0x057,0x059,0x05A,0x05C,0x05E,0x05F,0x061,0x063,0x064,0x066
.hword 0x068,0x06A,0x06B,0x06D,0x06F,0x071,0x073,0x075,0x076,0x078,0x07A,0x07C,0x07E,0x080,0x082,0x084
.hword 0x086,0x089,0x08B,0x08D,0x08F,0x091,0x093,0x096,0x098,0x09A,0x09C,0x09F,0x0A1,0x0A3,0x0A6,0x0A8
.hword 0x0AB,0x0AD,0x0AF,0x0B2,0x0B4,0x0B7,0x0BA,0x0BC,0x0BF,0x0C1,0x0C4,0x0C7,0x0C9,0x0CC,0x0CF,0x0D2
.hword 0x0D4,0x0D7,0x0DA,0x0DD,0x0E0,0x0E3,0x0E6,0x0E9,0x0EC,0x0EF,0x0F2,0x0F5,0x0F8,0x0FB,0x0FE,0x101
.hword 0x104,0x107,0x10B,0x10E,0x111,0x114,0x118,0x11B,0x11E,0x122,0x125,0x129,0x12C,0x130,0x133,0x137
.hword 0x13A,0x13E,0x141,0x145,0x148,0x14C,0x150,0x153,0x157,0x15B,0x15F,0x162,0x166,0x16A,0x16E,0x172
.hword 0x176,0x17A,0x17D,0x181,0x185,0x189,0x18D,0x191,0x195,0x19A,0x19E,0x1A2,0x1A6,0x1AA,0x1AE,0x1B2
.hword 0x1B7,0x1BB,0x1BF,0x1C3,0x1C8,0x1CC,0x1D0,0x1D5,0x1D9,0x1DD,0x1E2,0x1E6,0x1EB,0x1EF,0x1F3,0x1F8
.hword 0x1FC,0x201,0x205,0x20A,0x20F,0x213,0x218,0x21C,0x221,0x226,0x22A,0x22F,0x233,0x238,0x23D,0x241
.hword 0x246,0x24B,0x250,0x254,0x259,0x25E,0x263,0x267,0x26C,0x271,0x276,0x27B,0x280,0x284,0x289,0x28E
.hword 0x293,0x298,0x29D,0x2A2,0x2A6,0x2AB,0x2B0,0x2B5,0x2BA,0x2BF,0x2C4,0x2C9,0x2CE,0x2D3,0x2D8,0x2DC
.hword 0x2E1,0x2E6,0x2EB,0x2F0,0x2F5,0x2FA,0x2FF,0x304,0x309,0x30E,0x313,0x318,0x31D,0x322,0x326,0x32B
.hword 0x330,0x335,0x33A,0x33F,0x344,0x349,0x34E,0x353,0x357,0x35C,0x361,0x366,0x36B,0x370,0x374,0x379
.hword 0x37E,0x383,0x388,0x38C,0x391,0x396,0x39B,0x39F,0x3A4,0x3A9,0x3AD,0x3B2,0x3B7,0x3BB,0x3C0,0x3C5
.hword 0x3C9,0x3CE,0x3D2,0x3D7,0x3DC,0x3E0,0x3E5,0x3E9,0x3ED,0x3F2,0x3F6,0x3FB,0x3FF,0x403,0x408,0x40C
.hword 0x410,0x415,0x419,0x41D,0x421,0x425,0x42A,0x42E,0x432,0x436,0x43A,0x43E,0x442,0x446,0x44A,0x44E
.hword 0x452,0x455,0x459,0x45D,0x461,0x465,0x468,0x46C,0x470,0x473,0x477,0x47A,0x47E,0x481,0x485,0x488
.hword 0x48C,0x48F,0x492,0x496,0x499,0x49C,0x49F,0x4A2,0x4A6,0x4A9,0x4AC,0x4AF,0x4B2,0x4B5,0x4B7,0x4BA
.hword 0x4BD,0x4C0,0x4C3,0x4C5,0x4C8,0x4CB,0x4CD,0x4D0,0x4D2,0x4D5,0x4D7,0x4D9,0x4DC,0x4DE,0x4E0,0x4E3
.hword 0x4E5,0x4E7,0x4E9,0x4EB,0x4ED,0x4EF,0x4F1,0x4F3,0x4F5,0x4F6,0x4F8,0x4FA,0x4FB,0x4FD,0x4FF,0x500
.hword 0x502,0x503,0x504,0x506,0x507,0x508,0x50A,0x50B,0x50C,0x50D,0x50E,0x50F,0x510,0x511,0x511,0x512
.hword 0x513,0x514,0x514,0x515,0x516,0x516,0x517,0x517,0x517,0x518,0x518,0x518,0x518,0x518,0x519,0x519
noiseSample:
.hword 0x8000
noiseStep:
.hword 1
noiseSteps:
.hword 0xFFFF,0x0800,0x0600,0x0500,0x0400,0x0300,0x0280,0x0200
.hword 0x0180,0x0140,0x0100,0x00C0,0x00A0,0x0080,0x0060,0x0050
.hword 0x0040,0x0030,0x0028,0x0020,0x0018,0x0014,0x0010,0x000C
.hword 0x000A,0x0008,0x0006,0x0005,0x0004,0x0003,0x0002,0x0001


.align
.pool
