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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>

#include "main.h"
#include "config.h"

#include "blargGL.h"
#include "ui.h"
#include "audio.h"

#include "mem.h"
#include "cpu.h"
#include "spc700.h"
#include "ppu.h"
#include "snes.h"
#include "dsp.h"

#include "defaultborder.h"
#include "screenfill.h"

#include "final_shbin.h"
#include "render_soft_shbin.h"
#include "render_hard_shbin.h"
#include "render_hard7_shbin.h"
#include "render_hard_obj_shbin.h"
#include "plain_quad_shbin.h"
#include "window_mask_shbin.h"


aptHookCookie APTHook;


u32* gpuOut;
u32* SNESFrame;

DVLB_s* finalShader;
DVLB_s* softRenderShader;
DVLB_s* hardRenderShader;
DVLB_s* hard7RenderShader;
DVLB_s* hardRenderOBJShader;
DVLB_s* plainQuadShader;
DVLB_s* windowMaskShader;

shaderProgram_s finalShaderP;
shaderProgram_s softRenderShaderP;
shaderProgram_s hardRenderShaderP;
shaderProgram_s hard7RenderShaderP;
shaderProgram_s hardRenderOBJShaderP;
shaderProgram_s plainQuadShaderP;
shaderProgram_s windowMaskShaderP;


const int vertexBufSize = 0x80000*4;
void* vertexBuf;
void* vertexPtr;
u8 curVertexBuf;

int GPUState = 0;
DVLB_s* CurShader = NULL;

u32* BorderTex;
u16* MainScreenTex;
u16* SubScreenTex;


int forceexit = 0;
int running = 0;
int pause = 0;
u32 framecount = 0;

int VBlankCount = 0;
int FramesSkipped = 0;
int SkipNextFrame = 0;
int SkipThisFrame = 0;
u64 LastVBlank = 0;

// debug
u32 ntriangles = 0;

gxCmdQueue_s GXQueue;


Thread SPCThread = NULL;
#define SPC_THREAD_STACK_SIZE 4000
Handle SPCSync;
int exitspc = 0;

void SPCThreadFunc(void *arg)
{
	int audCnt = 512;
	u32 lastpos = 0;

	while (!exitspc)
	{
		svcWaitSynchronization(SPCSync, U64_MAX);
		svcClearEvent(SPCSync);
		
		if (!pause)
		{
			bool started = Audio_Begin();
			if (started)
			{
				audCnt = 512;
				lastpos = 0;
			}
			u32 curpos = ndspChnGetSamplePos(0);

			Audio_Mix(audCnt, started);

			s32 diff = curpos - lastpos;
			if (diff < 0) diff += MIXBUFSIZE;
			lastpos = curpos;

			audCnt = diff;
		}
		else
			Audio_Pause();
	}

	threadExit(0);
}



void dbg_save(char* path, void* buf, int size)
{
	FILE* pFile = fopen(path, "wb");
	if (pFile != NULL)
	{
		fwrite(buf, sizeof(char), size, pFile);
		fclose(pFile);
	}
}

void SPC_ReportUnk(u8 op, u32 pc)
{
	bprintf("SPC UNK %02X @ %04X\n", op, pc);
}

void ReportCrash()
{
	pause = 1;
	running = 0;
	
	ClearConsole();
	bprintf("Game has crashed (STOP)\n");
	
	extern u32 debugpc;
	bprintf("PC: %02X:%04X (%06X)\n", CPU_Regs.PBR, CPU_Regs.PC, debugpc);
	bprintf("P: %02X | M=%d X=%d E=%d\n", CPU_Regs.P.val&0xFF, CPU_Regs.P.M, CPU_Regs.P.X, CPU_Regs.P.E);
	bprintf("A: %04X X: %04X Y: %04X\n", CPU_Regs.A, CPU_Regs.X, CPU_Regs.Y);
	bprintf("S: %04X D: %02X DBR: %02X\n", CPU_Regs.S, CPU_Regs.D, CPU_Regs.DBR);
	
	bprintf("Stack\n");
	bprintf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
		SNES_SysRAM[CPU_Regs.S+0], SNES_SysRAM[CPU_Regs.S+1],
		SNES_SysRAM[CPU_Regs.S+2], SNES_SysRAM[CPU_Regs.S+3],
		SNES_SysRAM[CPU_Regs.S+4], SNES_SysRAM[CPU_Regs.S+5],
		SNES_SysRAM[CPU_Regs.S+6], SNES_SysRAM[CPU_Regs.S+7]);
	CPU_Regs.S += 8;
	bprintf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
		SNES_SysRAM[CPU_Regs.S+0], SNES_SysRAM[CPU_Regs.S+1],
		SNES_SysRAM[CPU_Regs.S+2], SNES_SysRAM[CPU_Regs.S+3],
		SNES_SysRAM[CPU_Regs.S+4], SNES_SysRAM[CPU_Regs.S+5],
		SNES_SysRAM[CPU_Regs.S+6], SNES_SysRAM[CPU_Regs.S+7]);
	CPU_Regs.S += 8;
	bprintf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
		SNES_SysRAM[CPU_Regs.S+0], SNES_SysRAM[CPU_Regs.S+1],
		SNES_SysRAM[CPU_Regs.S+2], SNES_SysRAM[CPU_Regs.S+3],
		SNES_SysRAM[CPU_Regs.S+4], SNES_SysRAM[CPU_Regs.S+5],
		SNES_SysRAM[CPU_Regs.S+6], SNES_SysRAM[CPU_Regs.S+7]);
	CPU_Regs.S += 8;
	bprintf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
		SNES_SysRAM[CPU_Regs.S+0], SNES_SysRAM[CPU_Regs.S+1],
		SNES_SysRAM[CPU_Regs.S+2], SNES_SysRAM[CPU_Regs.S+3],
		SNES_SysRAM[CPU_Regs.S+4], SNES_SysRAM[CPU_Regs.S+5],
		SNES_SysRAM[CPU_Regs.S+6], SNES_SysRAM[CPU_Regs.S+7]);
		
	bprintf("Full RAM dump can be found on SD\n");
	
	u32 pc = (CPU_Regs.PBR<<16)|CPU_Regs.PC;
	u32 ptr = Mem_PtrTable[pc >> 13];
	bprintf("Ptr table entry: %08X\n", ptr);
	
	bprintf("Tell Arisotura\n");
	
	dbg_save("/SNESRAM.bin", SNES_SysRAM, 0x20000);
	dbg_save("/SNESPtrChunk.bin", (void*)(ptr&~0xF), 0x2000);
}

void dbgcolor(u32 col)
{
	u32 regData=0x01000000|col;
	GSPGPU_WriteHWRegs(0x202204, &regData, 4);
	
	/*u8* zarp = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	int i;
	for (i = 0; i < 320*240; i++) 
	{
		zarp[i*3+0] = col;
		zarp[i*3+1] = col >> 8;
		zarp[i*3+2] = col >> 16;
	}
	gfxFlushBuffers();
	gfxSwapBuffers();*/
}


float vertexList[] = 
{
	// border
	0.0, 0.0, 0.9,      0.78125, 0.0625,
	240.0, 400.0, 0.9,  0.0, 1.0,
	
	// screen
	8.0, 72.0, 0.9,     1.0, 0.87890625,
	232.0, 328.0, 0.9,  0.0, 0.00390625,
};
float* borderVertices;
float* screenVertices;


void ApplyScaling()
{
	float texy = (float)(SNES_Status->ScreenHeight+1) / 256.0f;
	
	float x1, x2, y1, y2;
	
	int scalemode = Config.ScaleMode;
	if (!running && scalemode == 2) scalemode = 1;
	else if (!running && scalemode == 4) scalemode = 3;
	
	switch (scalemode)
	{
		case 1: // fullscreen
			x1 = 0.0f; x2 = 240.0f;
			y1 = 0.0f; y2 = 400.0f;
			break;
			
		case 2: // cropped
			{
				float bigy = ((float)SNES_Status->ScreenHeight * 240.0f) / (float)(SNES_Status->ScreenHeight-16);
				float margin = (bigy - 240.0f) / 2.0f;
				x1 = -margin; x2 = 240.0f+margin;
				y1 = 0.0f; y2 = 400.0f;
			}
			break;
			
		case 3: // 4:3
			x1 = 0.0f; x2 = 240.0f;
			y1 = 40.0f; y2 = 360.0f;
			break;
			
		case 4: // cropped 4:3
			{
				float bigy = ((float)SNES_Status->ScreenHeight * 240.0f) / (float)(SNES_Status->ScreenHeight-16);
				float margin = (bigy - 240.0f) / 2.0f;
				x1 = -margin; x2 = 240.0f+margin;
				y1 = 29.0f; y2 = 371.0f;
			}
			break;
			
		default: // 1:1
			if (SNES_Status->ScreenHeight == 239)
			{
				x1 = 1.0f; x2 = 240.0f;
			}
			else
			{
				x1 = 8.0f; x2 = 232.0f;
			}
			y1 = 72.0f; y2 = 328.0f;
			break;
	}
	
	screenVertices[5*0 + 0] = x1; screenVertices[5*0 + 1] = y1; screenVertices[5*0 + 4] = texy; 
	screenVertices[5*1 + 0] = x2; screenVertices[5*1 + 1] = y2; 
	
	GSPGPU_FlushDataCache((u32*)screenVertices, 5*2*sizeof(float));
}


void SwapVertexBuf();
void VSyncAndFrameskip();

void VBlankCB(void* blarg)
{
	VBlankCount++;
}

void RenderTopScreen()
{
	bglUseShader(&finalShaderP);

	bglOutputBuffers(gpuOut, NULL, GPU_RGBA8, 240, 400);
	bglViewport(0, 0, 240, 400);
	
	bglEnableDepthTest(false);
	bglColorDepthMask(GPU_WRITE_COLOR);
	
	bglEnableTextures(GPU_TEXUNIT0);
	
	bglTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	bglDummyTexEnv(1);
	bglDummyTexEnv(2);
	bglDummyTexEnv(3);
	bglDummyTexEnv(4);
	bglDummyTexEnv(5);
	
	bglTexImage(GPU_TEXUNIT0, BorderTex,512,256,0,GPU_RGBA8);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_FLOAT, 3);	// vertex
	bglAttribType(1, GPU_FLOAT, 2);	// texcoord
	bglAttribBuffer(borderVertices);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, 2); // border

	// filtering enabled only when scaling
	// filtering at 1:1 causes output to not be pixel-perfect, but not filtering at higher res looks like total shit
	bglTexImage(GPU_TEXUNIT0, SNESFrame,256,256, Config.ScaleMode?0x6:0 ,GPU_RGBA5551);
	
	bglAttribBuffer(screenVertices);
	
	bglDrawArrays(GPU_GEOMETRY_PRIM, 2); // screen
	
	// TODO: bottom screen UI should be rendered with the GPU too

	bglFlush();
	GX_DisplayTransfer(gpuOut, 0x019000F0, (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0x019000F0, 0x00001000);
	gxCmdQueueRun(&GXQueue);
	
	SwapVertexBuf();
	
	SkipThisFrame = SkipNextFrame;
}

void FinishRendering()
{
	gxCmdQueueWait(&GXQueue, -1);
	gxCmdQueueClear(&GXQueue);
	
	{
		u8* bottomfb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
		
		UI_SetFramebuffer(bottomfb);
		UI_Render();
		GSPGPU_FlushDataCache(bottomfb, 0x38400);
	}
	
	gfxSwapBuffersGpu();
	VSyncAndFrameskip();
}

u32 PALCount = 0;

void VSyncAndFrameskip()
{
	int nmissed = VBlankCount;
	
	gspWaitForEvent(GSPGPU_EVENT_VBlank0, Config.VSync != 0);
	
	VBlankCount = 0;
	
	if (Config.FrameSkip == 5) // auto frameskip
	{
		if (!SkipNextFrame)
		{
			SkipNextFrame = nmissed;
			if (SkipNextFrame > 4)
				SkipNextFrame = 4;
		}
		else
			SkipNextFrame--;
	}
	else if (Config.FrameSkip > 0)
	{
		if (!SkipNextFrame)
			SkipNextFrame = Config.FrameSkip;
		else
			SkipNextFrame--;
	}
	else
		SkipNextFrame = 0;
}

void SwapVertexBuf()
{
	curVertexBuf ^= 1;
	vertexPtr = &((u8*)vertexBuf)[curVertexBuf ? vertexBufSize : 0];
}


bool TakeScreenshot(char* path)
{
	int x, y;
	
	FILE *pFile = fopen(path, "wb");
	if(pFile == NULL)
		return false;
	
	u32 bitmapsize = 400*480*3;
	u8* tempbuf = (u8*)malloc(0x36 + bitmapsize);
	memset(tempbuf, 0, 0x36 + bitmapsize);

	
	*(u16*)&tempbuf[0x0] = 0x4D42;
	*(u32*)&tempbuf[0x2] = 0x36 + bitmapsize;
	*(u32*)&tempbuf[0xA] = 0x36;
	*(u32*)&tempbuf[0xE] = 0x28;
	*(u32*)&tempbuf[0x12] = 400; // width
	*(u32*)&tempbuf[0x16] = 480; // height
	*(u32*)&tempbuf[0x1A] = 0x00180001;
	*(u32*)&tempbuf[0x22] = bitmapsize;
	
	u8* framebuf = (u8*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	for (y = 0; y < 240; y++)
	{
		for (x = 0; x < 400; x++)
		{
			int si = ((239 - y) + (x * 240)) * 3;
			int di = 0x36 + (x + ((479 - y) * 400)) * 3;
			
			tempbuf[di++] = framebuf[si++];
			tempbuf[di++] = framebuf[si++];
			tempbuf[di++] = framebuf[si++];
		}
	}
	
	framebuf = (u8*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	for (y = 0; y < 240; y++)
	{
		for (x = 0; x < 320; x++)
		{
			int si = ((239 - y) + (x * 240)) * 3;
			int di = 0x36 + ((x+40) + ((239 - y) * 400)) * 3;
			
			tempbuf[di++] = framebuf[si++];
			tempbuf[di++] = framebuf[si++];
			tempbuf[di++] = framebuf[si++];
		}
	}

	fwrite(tempbuf, sizeof(char), 0x36 + bitmapsize, pFile);
	fclose(pFile);

	free(tempbuf);
	return true;
}


// flags: bit0=tiled, bit1=15bit color
void CopyBitmapToTexture(u8* src, void* dst, u32 width, u32 height, u32 alpha, u32 startx, u32 stride, u32 flags)
{
	int x, y;
	for (y = height-1; y >= 0; y--)
	{
		for (x = startx; x < startx+width; x++)
		{
			u8 b = *src++;
			u8 g = *src++;
			u8 r = *src++;
			
			int di;
			if (flags & 0x1)
			{
				di  = x & 0x1;
				di += (y & 0x1) << 1;
				di += (x & 0x2) << 1;
				di += (y & 0x2) << 2;
				di += (x & 0x4) << 2;
				di += (y & 0x4) << 3;
				di += (x & 0x1F8) << 3;
				di += ((y & 0xF8) << 3) * stride;
			}
			else
				di = x + (y * stride * 8);
			
			if (flags & 0x2)
				((u16*)dst)[di] = (alpha ? 1:0) | ((b & 0xF8) >> 2) | ((g & 0xF8) << 3) | ((r & 0xF8) << 8);
			else
				((u32*)dst)[di] = alpha | (b << 8) | (g << 16) | (r << 24);
		}
	}
}

bool LoadBitmap(char* path, u32 width, u32 height, void* dst, u32 alpha, u32 startx, u32 stride, u32 flags)
{
	u8 header[0x1E];
	FILE *pFile = fopen(path, "rb");
	if (pFile == NULL)
		return false;

	fread(header, sizeof(char), 0x1E, pFile);
	if ((*(u16*)&header[0] != 0x4D42) ||    // magic
	    (*(u32*)&header[0x12] != width) ||  // width
		(*(u32*)&header[0x16] != height) || // height
		(*(u16*)&header[0x1A] != 1) ||      // bitplanes
		(*(u16*)&header[0x1C] != 24))       // bit depth
	{
		fclose(pFile);
		return false;
	}

	
	u32 bufsize = width*height*3;
	u8* buf = (u8*)malloc(bufsize);
	
	fseek(pFile, 0x36, SEEK_SET);
	fread(buf, sizeof(char), bufsize, pFile);
	fclose(pFile);

	CopyBitmapToTexture(buf, dst, width, height, alpha, startx, stride, flags);
	
	free(buf);
	return true;
}

bool LoadBorder(char* path)
{
	return LoadBitmap(path, 400, 240, BorderTex, 0xFF, 0, 64, 0x1);
}


bool StartROM(char* path, char* dir)
{
	char temppath[0x210];

	if (SPCThread)
	{
		exitspc = 1; pause = 1;
		svcSignalEvent(SPCSync);
		//svcWaitSynchronization(SPCThread, U64_MAX);
		threadJoin(SPCThread, U64_MAX);
		exitspc = 0;
	}
	
	running = 1;
	pause = 0;
	framecount = 0;
	
	ClearConsole();
	bprintf("blargSNES %s\n", BLARGSNES_VERSION);
	bprintf("http://blargsnes.kuribo64.net/\n");
	
	// load the ROM
	strcpy(temppath, dir);
	strcpy(&temppath[strlen(dir)], path);
	temppath[strlen(dir)+strlen(path)] = '\0';
	bprintf("Loading %s...\n", path);
	
	if (!SNES_LoadROM(temppath))
		return false;

	SaveConfig(1);
	
	CPU_Reset();
	SPC_Reset();

	FramesSkipped = 0;
	SkipThisFrame = false;
	PALCount = 0;
	
	// SPC700 thread (running on syscore)
	SPCThread = threadCreate(SPCThreadFunc, 0x0, SPC_THREAD_STACK_SIZE, 0x18, 1, true);
	if (!SPCThread) 
	{
		bprintf("Failed to create DSP thread\n");
	}
	
	bprintf("ROM loaded, running...\n");
	
	return true;
}


void APTHookFunc(APT_HookType type, void* param)
{
	static int oldpause = 0;

	switch (type)
	{
	case APTHOOK_ONSUSPEND:
	case APTHOOK_ONSLEEP:
		oldpause = pause; pause = 1;
		svcSignalEvent(SPCSync);
		
		if (running) SNES_SaveSRAM();
		FinishRendering();
		break;
		
	case APTHOOK_ONRESTORE:
	case APTHOOK_ONWAKEUP:
		pause = oldpause;
		break;
		
	default:
		break;
	}
}

int main() 
{
	int i;
	int shot = 0;
	
	touchPosition lastTouch;
	u32 repeatkeys = 0;
	int repeatstate = 0;
	int repeatcount = 0;
	
	forceexit = 0;
	running = 0;
	pause = 0;
	exitspc = 0;
	
	ClearConsole();
	
	// Enable 804Mhz mode on New 3DS
	osSetSpeedupEnable(true);
	
	//aptOpenSession();
	APT_SetAppCpuTimeLimit(30); // enables syscore usage
	//aptCloseSession();

	gfxInitDefault();
	
	Config.HardwareMode7Filter = -1;
	LoadConfig(1);
	
	VRAM_Init();
	gpuOut = (u32*)VRAM_Alloc(400*240*4);
	
	//SNESFrame = (u32*)VRAM_Alloc(256*256*4);
	// TODO: these two have to sit in FCRAM because there isn't enough space left in VRAM
	// hardware renderer takes up 5.5MB out of 6
	SNESFrame = (u32*)linearAlloc(256*256*4);
	BorderTex = (u32*)linearAlloc(512*256*4);
	
	SNES_Init();
	PPU_Init();
	
	gfxSet3D(false);
	bglInit();
	
	vertexBuf = linearAlloc(vertexBufSize * 2);
	vertexPtr = vertexBuf;
	curVertexBuf = 0;

	finalShader = DVLB_ParseFile((u32*)final_shbin, final_shbin_size);
	softRenderShader = DVLB_ParseFile((u32*)render_soft_shbin, render_soft_shbin_size);
	hardRenderShader = DVLB_ParseFile((u32*)render_hard_shbin, render_hard_shbin_size);
	hard7RenderShader = DVLB_ParseFile((u32*)render_hard7_shbin, render_hard7_shbin_size);
	hardRenderOBJShader = DVLB_ParseFile((u32*)render_hard_obj_shbin, render_hard_obj_shbin_size);
	plainQuadShader = DVLB_ParseFile((u32*)plain_quad_shbin, plain_quad_shbin_size);
	windowMaskShader = DVLB_ParseFile((u32*)window_mask_shbin, window_mask_shbin_size);

	shaderProgramInit(&finalShaderP);		shaderProgramSetVsh(&finalShaderP, &finalShader->DVLE[0]);				shaderProgramSetGsh(&finalShaderP, &finalShader->DVLE[1], 4);
	shaderProgramInit(&softRenderShaderP);	shaderProgramSetVsh(&softRenderShaderP, &softRenderShader->DVLE[0]);	shaderProgramSetGsh(&softRenderShaderP, &softRenderShader->DVLE[1], 4);
	shaderProgramInit(&hardRenderShaderP);	shaderProgramSetVsh(&hardRenderShaderP, &hardRenderShader->DVLE[0]);	shaderProgramSetGsh(&hardRenderShaderP, &hardRenderShader->DVLE[1], 4);
	shaderProgramInit(&hard7RenderShaderP);	shaderProgramSetVsh(&hard7RenderShaderP, &hard7RenderShader->DVLE[0]);	shaderProgramSetGsh(&hard7RenderShaderP, &hard7RenderShader->DVLE[1], 4);
	shaderProgramInit(&hardRenderOBJShaderP);	shaderProgramSetVsh(&hardRenderOBJShaderP, &hardRenderOBJShader->DVLE[0]);	shaderProgramSetGsh(&hardRenderOBJShaderP, &hardRenderOBJShader->DVLE[1], 6);
	shaderProgramInit(&plainQuadShaderP);	shaderProgramSetVsh(&plainQuadShaderP, &plainQuadShader->DVLE[0]);		shaderProgramSetGsh(&plainQuadShaderP, &plainQuadShader->DVLE[1], 4);
	shaderProgramInit(&windowMaskShaderP);	shaderProgramSetVsh(&windowMaskShaderP, &windowMaskShader->DVLE[0]);	shaderProgramSetGsh(&windowMaskShaderP, &windowMaskShader->DVLE[1], 4);

	GX_MemoryFill(gpuOut, 0x404040FF, &gpuOut[240*400], 0x201, NULL, 0, NULL, 0);
	gspWaitForPSC0();
	gfxSwapBuffersGpu();
	
	UI_SetFramebuffer(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL));
	//ClearConsole();
	
	// copy some fixed vertices to linear memory
	borderVertices = (float*)linearAlloc(5*2 * sizeof(float));
	screenVertices = (float*)linearAlloc(5*2 * sizeof(float));
	
	float* fptr = &vertexList[0];
	for (i = 0; i < 5*2; i++) borderVertices[i] = *fptr++;
	for (i = 0; i < 5*2; i++) screenVertices[i] = *fptr++;
	ApplyScaling();
	
	
	// load border
	if (!LoadBorder("/blargSnesBorder.bmp"))
		CopyBitmapToTexture(defaultborder, BorderTex, 400, 240, 0xFF, 0, 64, 0x1);

	// copy splashscreen
	u32* tempbuf = (u32*)linearAlloc(256*256*2);
	CopyBitmapToTexture(screenfill, tempbuf, 256, 224, 0xFF, 0, 32, 0x2);
	GSPGPU_FlushDataCache(tempbuf, 256*256*2);

	GX_DisplayTransfer(tempbuf, 0x01000100, (u32*)SNESFrame, 0x01000100, 0x3303);
	gspWaitForPPF();

	linearFree(tempbuf);
	
	memset(&GXQueue, 0, sizeof(GXQueue));
	GXQueue.maxEntries = 32;
	GXQueue.entries = (gxCmdEntry_s*)malloc(GXQueue.maxEntries * sizeof(gxCmdEntry_s));
	GX_BindQueue(&GXQueue);
	
	gspSetEventCallback(GSPGPU_EVENT_VBlank0, VBlankCB, NULL, false);
	
	Audio_Init();
	svcCreateEvent(&SPCSync, 0); 
	
	aptHook(&APTHook, APTHookFunc, NULL);
	
	UI_Switch(&UI_ROMMenu);

	while (!forceexit && aptMainLoop())
	{
		if (aptIsActive())
		{
			hidScanInput();
			u32 press = hidKeysDown();
			u32 held = hidKeysHeld();
			u32 release = hidKeysUp();
			
			if (running && !pause)
			{
				// emulate
				CPU_MainLoop(); // runs the SNES for one frame. Handles PPU rendering.
				
				/*{
					extern u32 dbgcycles, nruns;
					bprintf("SPC: %d / 17066  %08X\n", dbgcycles, SNES_Status->SPC_CycleRatio);
					dbgcycles = 0; nruns=0;
				}*/
				/*if (press & KEY_X) SNES_Status->SPC_CycleRatio+=0x1000;
				if (press & KEY_Y) SNES_Status->SPC_CycleRatio-=0x1000;
				SNES_Status->SPC_CyclesPerLine = SNES_Status->SPC_CycleRatio*1364;*/
				
				// SRAM autosave check
				// TODO: also save SRAM under certain circumstances (pausing, returning to home menu, etc)
				framecount++;
				if (!(framecount & 7))
					SNES_SaveSRAM();
					
				if (release & KEY_TOUCH) 
				{
					SNES_SaveSRAM();
					bprintf("Pause.\n");
					bprintf("Tap screen or press A to resume.\n");
					bprintf("Press Select to load another game.\n");
					bprintf("Press Start to enter the config.\n");
					pause = 1;
					svcSignalEvent(SPCSync);
				}
			}
			else
			{
				// update UI
				
				// TODO move this chunk of code somewhere else?
				if (running && !UI_Level()) // only run this if not inside a child UI
				{
					if (release & (KEY_TOUCH|KEY_A))
					{
						bprintf("Resume.\n");
						pause = 0;
					}
					else if (release & KEY_SELECT)
					{
						running = 0;
						UI_Switch(&UI_ROMMenu);
						
						// copy splashscreen
						FinishRendering();
						SNES_Status->ScreenHeight = 224;
						ApplyScaling();
						u32* tempbuf = (u32*)linearAlloc(256*256*2);
						CopyBitmapToTexture(screenfill, tempbuf, 256, 224, 0xFF, 0, 32, 0x2);
						GSPGPU_FlushDataCache(tempbuf, 256*256*2);

						GX_DisplayTransfer(tempbuf, 0x01000100, (u32*)SNESFrame, 0x01000100, 0x3303);
						gspWaitForPPF();

						linearFree(tempbuf);
					}
					else if (release & KEY_START)
					{
						UI_SaveAndSwitch(&UI_Config);
					}
					else if (release & KEY_X)
					{
						bprintf("PC: CPU %02X:%04X  SPC %04X\n", CPU_Regs.PBR, CPU_Regs.PC, SPC_Regs.PC);
						dbg_save("/snesram.bin", SNES_SysRAM, 128*1024);
						dbg_save("/spcram.bin", SPC_RAM, 64*1024);
						dbg_save("/vram.bin", PPU.VRAM, 64*1024);
						dbg_save("/oam.bin", PPU.OAM, 0x220);
						dbg_save("/cgram.bin", PPU.CGRAM, 512);
					}
					
					if ((held & (KEY_L|KEY_R)) == (KEY_L|KEY_R))
					{
						if (!shot)
						{
							u32 timestamp = (u32)(svcGetSystemTick() / 446872);
							char file[256];
							snprintf(file, 256, "/blargSnes%08u.bmp", timestamp);
							if (TakeScreenshot(file))
							{
								bprintf("Screenshot saved as:\n");
								bprintf("SD:%s\n", file);
							}
							else
								bprintf("Error saving screenshot\n");
								
							shot = 1;
						}
					}
					else
						shot = 0;
				}
				
				RenderTopScreen();
				FinishRendering();
				
				if (held & KEY_TOUCH)
				{
					hidTouchRead(&lastTouch);
					UI_Touch((press & KEY_TOUCH) ? 1:2, lastTouch.px, lastTouch.py);
					held &= ~KEY_TOUCH;
				}
				else if (release & KEY_TOUCH)
				{
					UI_Touch(0, lastTouch.px, lastTouch.py);
					release &= ~KEY_TOUCH;
				}
				
				if (press)
				{
					UI_ButtonPress(press);
					
					// key repeat
					repeatkeys = press & (KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT);
					repeatstate = 1;
					repeatcount = 15;
				}
				else if (held && held == repeatkeys)
				{
					repeatcount--;
					if (!repeatcount)
					{
						repeatcount = 7;
						if (repeatstate == 2)
							UI_ButtonPress(repeatkeys);
						else
							repeatstate = 2;
					}
				}
			}
		}
	}
	
	if (running) SNES_SaveSRAM();
	
	aptUnhook(&APTHook);
	
	exitspc = 1; pause = 1;
	svcSignalEvent(SPCSync);
	if (SPCThread) 
	{
		//svcWaitSynchronization(SPCThread, U64_MAX);
		threadJoin(SPCThread, U64_MAX);
	}
	
	gxCmdQueueStop(&GXQueue);
	gxCmdQueueWait(&GXQueue, -1);
	GX_BindQueue(NULL);
	free(GXQueue.entries);

	//VRAM_Free(SNESFrame);
	linearFree(SNESFrame);
	VRAM_Free(gpuOut);

	Audio_DeInit();
	PPU_DeInit();

	shaderProgramFree(&finalShaderP);
	shaderProgramFree(&softRenderShaderP);
	shaderProgramFree(&hardRenderShaderP);
	shaderProgramFree(&hard7RenderShaderP);
	shaderProgramFree(&hardRenderOBJShaderP);
	shaderProgramFree(&plainQuadShaderP);
	shaderProgramFree(&windowMaskShaderP);

	DVLB_Free(finalShader);
	DVLB_Free(softRenderShader);
	DVLB_Free(hardRenderShader);
	DVLB_Free(hard7RenderShader);
	DVLB_Free(hardRenderOBJShader);
	DVLB_Free(plainQuadShader);
	DVLB_Free(windowMaskShader);


	linearFree(borderVertices);
	linearFree(screenVertices);
	
	linearFree(BorderTex);
	
	bglDeInit();

	gfxExit();

    return 0;
}
