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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>

#include "blargGL.h"
#include "ui.h"
#include "audio.h"

#include "mem.h"
#include "cpu.h"
#include "spc700.h"
#include "ppu.h"
#include "snes.h"

#include "defaultborder.h"
#include "screenfill.h"

#include "final_vsh_shbin.h"
#include "render_soft_vsh_shbin.h"
#include "render_hard_vsh_shbin.h"
#include "plain_quad_vsh_shbin.h"


u32* gpuOut;
u32* gpuDOut;
u32* SNESFrame;

DVLB_s* finalShader;
DVLB_s* softRenderShader;
DVLB_s* hardRenderShader;
DVLB_s* plainQuadShader;

void* vertexBuf;
void* vertexPtr;

int GPUState = 0;
DVLB_s* CurShader = NULL;

u32* BorderTex;
u16* MainScreenTex;
u16* SubScreenTex;

FS_archive sdmcArchive;


int running = 0;
int pause = 0;
u32 framecount = 0;

u8 RenderState = 0;
int FramesSkipped = 0;
bool SkipThisFrame = false;
u64 LastVBlank = 0;

// hax
extern Handle gspEventThread;
extern Handle gspEvents[GSPEVENT_MAX];


Result svcSetThreadPriority(Handle thread, s32 prio)
{
	asm("svc 0xC");
}


Handle spcthread = NULL;
u8 spcthreadstack[0x4000] __attribute__((aligned(8)));
Handle SPCSync;
int exitspc = 0;

// TODO: correction
// mixes 127995 samples every 4 seconds, instead of 128000 (128038.1356)
// +43 samples every 4 seconds
// +1 sample every 32 second
void SPCThread(u32 blarg)
{
	int i;
	
	while (!exitspc)
	{
		svcWaitSynchronization(SPCSync, U64_MAX);
		svcClearEvent(SPCSync);
		
		if (!pause)
		{
			for (i = 0; i < 32; i++)
			{
				DSP_ReplayWrites(i);
				Audio_Mix();
			}
			
			Audio_MixFinish();
		}
	}
	
	svcExitThread();
}



void dbg_save(char* path, void* buf, int size)
{
	Handle sram;
	FS_path sramPath;
	sramPath.type = PATH_CHAR;
	sramPath.size = strlen(path) + 1;
	sramPath.data = (u8*)path;
	
	Result res = FSUSER_OpenFile(NULL, &sram, sdmcArchive, sramPath, FS_OPEN_CREATE|FS_OPEN_WRITE, FS_ATTRIBUTE_NONE);
	if ((res & 0xFFFC03FF) == 0)
	{
		u32 byteswritten = 0;
		FSFILE_Write(sram, &byteswritten, 0, (u32*)buf, size, 0x10001);
		FSFILE_Close(sram);
	}
}

void debugcrapo(u32 op, u32 op2)
{
	bprintf("DBG: %08X %08X\n", op, op2);
	DrawConsole();
	//SwapBottomBuffers(0);
	//ClearBottomBuffer();
}

void SPC_ReportUnk(u8 op, u32 pc)
{
	bprintf("SPC UNK %02X @ %04X\n", op, pc);
}

void ReportCrash()
{
	pause = 1;
	
	ClearConsole();
	bprintf("Game has crashed (STOP)\n");
	
	bprintf("PC: %02X|%04X\n", CPU_Regs.PBR, CPU_Regs.PC);
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
	
	bprintf("Tell StapleButter\n");
	
	dbg_save("/SNESRAM.bin", SNES_SysRAM, 0x20000);
	dbg_save("/SNESPtrChunk.bin", (void*)(ptr&~0xF), 0x2000);
}

void dbgcolor(u32 col)
{
	u32 regData=0x01000000|col;
	GSPGPU_WriteHWRegs(NULL, 0x202204, &regData, 4);
}



float screenProjMatrix[16] = 
{
	2.0f/240.0f, 0, 0, -1,
	0, 2.0f/400.0f, 0, -1,
	0, 0, 1, -1,
	0, 0, 0, 1
};

float snesProjMatrix[16] = 
{
	2.0f/256.0f, 0, 0, -1,
	0, 2.0f/256.0f, 0, -1,
	0, 0, 1.0f/128.0f, -1,
	0, 0, 0, 1
};

float vertexList[] = 
{
	// border
	0.0, 0.0, 0.9,      0.78125, 0.0625,
	240.0, 0.0, 0.9,    0.78125, 1.0,
	240.0, 400.0, 0.9,  0.0, 1.0,
	
	0.0, 0.0, 0.9,      0.78125, 0.0625,
	240.0, 400.0, 0.9,  0.0, 1.0,
	0.0, 400.0, 0.9,    0.0, 0.0625,
	
	// screen
	8.0, 72.0, 0.9,      1.0, 0.875,
	232.0, 72.0, 0.9,    1.0, 0.0,
	232.0, 328.0, 0.9,  0.0, 0.0,
	
	8.0, 72.0, 0.9,      1.0, 0.875,
	232.0, 328.0, 0.9,  0.0, 0.0,
	8.0, 328.0, 0.9,    0.0, 0.875,
};
float* borderVertices;
float* screenVertices;

// TODO retire all this junk now that we have blargGL

void setUniformMatrix(u32 startreg, float* m)
{
	float param[16];
	param[0x0]=m[3]; //w
	param[0x1]=m[2]; //z
	param[0x2]=m[1]; //y
	param[0x3]=m[0]; //x
	param[0x4]=m[7];
	param[0x5]=m[6];
	param[0x6]=m[5];
	param[0x7]=m[4];
	param[0x8]=m[11];
	param[0x9]=m[10];
	param[0xa]=m[9];
	param[0xb]=m[8];
	param[0xc]=m[15];
	param[0xd]=m[14];
	param[0xe]=m[13];
	param[0xf]=m[12];
	GPU_SetUniform(startreg, (u32*)param, 4);
}

void GPU_SetDummyTexEnv(u8 num)
{
	GPU_SetTexEnv(num, 
		GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0), 
		GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, 
		GPU_REPLACE, 
		0xFFFFFFFF);
}

void myGPU_Reset()
{
	GPUState = 0;
	CurShader = NULL;
}

void myGPU_DrawArray(u32 type, u32 num)
{
	GPU_DrawArray(type, num);
	GPUState = 1;
}

void myGPU_SetShaderAndViewport(DVLB_s* shader, u32* depth, u32* color, u32 x, u32 y, u32 w, u32 h)
{
	// FinishDrawing is required before changing shaders
	// but not before changing viewport
	
	if (shader != CurShader)
	{
		if (GPUState) GPU_FinishDrawing();
		SHDR_UseProgram(shader, 0);
		CurShader = shader;
	}
	
	GPU_SetViewport(depth, color, x, y, w, h);
}

void RenderTopScreen()
{
	if (RenderState) gspWaitForP3D();
	GX_SetDisplayTransfer(NULL, gpuOut, 0x019001E0, (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0x019001E0, 0x01001000);
	gspWaitForPPF(); // TODO do this shit differently so we're not waiting doing nothing
	

	bglUseShader(finalShader);
	
	bglOutputBuffers(gpuOut, gpuDOut);
	bglViewport(0, 0, 240*2, 400);
	
	bglEnableDepthTest(false);
	
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
	
	bglUniformMatrix(0x20, screenProjMatrix);
	
	bglNumAttribs(2);
	bglAttribType(0, GPU_FLOAT, 3);	// vertex
	bglAttribType(1, GPU_FLOAT, 2);	// texcoord
	bglAttribBuffer(borderVertices);
	
	bglDrawArrays(GPU_TRIANGLES, 2*3); // border
	
	
	bglTexImage(GPU_TEXUNIT0, SNESFrame,256,256,/*0x6*/0,GPU_RGBA8);
	
	bglAttribBuffer(screenVertices);
	
	bglDrawArrays(GPU_TRIANGLES, 2*3); // screen
	
	
	bglFlush();
	RenderState = 1;
}


bool TakeScreenshot(char* path)
{
	int x, y;
	
	Handle file;
	FS_path filePath;
	filePath.type = PATH_CHAR;
	filePath.size = strlen(path) + 1;
	filePath.data = (u8*)path;
	
	Result res = FSUSER_OpenFile(NULL, &file, sdmcArchive, filePath, FS_OPEN_CREATE|FS_OPEN_WRITE, FS_ATTRIBUTE_NONE);
	if (res) 
		return false;
		
	u32 byteswritten;
	
	u32 bitmapsize = 400*480*3;
	u8* tempbuf = (u8*)MemAlloc(0x36 + bitmapsize);
	memset(tempbuf, 0, 0x36 + bitmapsize);
	
	FSFILE_SetSize(file, (u16)(0x36 + bitmapsize));
	
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
	
	FSFILE_Write(file, &byteswritten, 0, (u32*)tempbuf, 0x36 + bitmapsize, 0x10001);
	
	FSFILE_Close(file);
	MemFree(tempbuf);
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
	Handle file;
	FS_path filePath;
	filePath.type = PATH_CHAR;
	filePath.size = strlen(path) + 1;
	filePath.data = (u8*)path;
	
	Result res = FSUSER_OpenFile(NULL, &file, sdmcArchive, filePath, FS_OPEN_READ, FS_ATTRIBUTE_NONE);
	if (res) 
		return false;
		
	u32 bytesread;
	u32 temp;
	
	// magic
	FSFILE_Read(file, &bytesread, 0, (u32*)&temp, 2);
	if ((u16)temp != 0x4D42)
	{
		FSFILE_Close(file);
		return false;
	}
	
	// width
	FSFILE_Read(file, &bytesread, 0x12, (u32*)&temp, 4);
	if (temp != width)
	{
		FSFILE_Close(file);
		return false;
	}
	
	// height
	FSFILE_Read(file, &bytesread, 0x16, (u32*)&temp, 4);
	if (temp != height)
	{
		FSFILE_Close(file);
		return false;
	}
	
	// bitplanes
	FSFILE_Read(file, &bytesread, 0x1A, (u32*)&temp, 2);
	if ((u16)temp != 1)
	{
		FSFILE_Close(file);
		return false;
	}
	
	// bit depth
	FSFILE_Read(file, &bytesread, 0x1C, (u32*)&temp, 2);
	if ((u16)temp != 24)
	{
		FSFILE_Close(file);
		return false;
	}
	
	
	u32 bufsize = width*height*3;
	u8* buf = (u8*)MemAlloc(bufsize);
	
	FSFILE_Read(file, &bytesread, 0x36, buf, bufsize);
	FSFILE_Close(file);
	
	CopyBitmapToTexture(buf, dst, width, height, alpha, startx, stride, flags);
	
	MemFree(buf);
	return true;
}

bool LoadBorder(char* path)
{
	return LoadBitmap(path, 400, 240, BorderTex, 0xFF, 0, 64, 0x1);
}


bool StartROM(char* path)
{
	char temppath[300];
	Result res;
	
	if (spcthread)
	{
		exitspc = 1;
		svcWaitSynchronization(spcthread, U64_MAX);
		exitspc = 0;
	}
	
	ClearConsole();
	bprintf("blargSNES console\n");
	bprintf("http://blargsnes.kuribo64.net/\n");
	
	// load the ROM
	strncpy(temppath, "/snes/", 6);
	strncpy(&temppath[6], path, 0x106);
	temppath[6+0x106] = '\0';
	bprintf("Loading %s...\n", temppath);
	
	if (!SNES_LoadROM(temppath))
		return false;
		
	CPU_Reset();
	SPC_Reset();

	running = 1;
	pause = 0;
	framecount = 0;
	
	RenderState = 0;
	FramesSkipped = 0;
	SkipThisFrame = false;
	
	// SPC700 thread (running on syscore)
	res = svcCreateThread(&spcthread, SPCThread, 0, (u32*)(spcthreadstack+0x4000), 0x18, 1);
	if (res)
	{
		bprintf("Failed to create SPC700 thread:\n -> %08X\n", res);
		spcthread = NULL;
	}
	
	bprintf("ROM loaded, running...\n");
	
	return true;
}



int reported=0;
void reportshit(u32 pc)
{
	if (reported) return;
	reported = 1;
	bprintf("-- %06X\n", pc);
}


bool PeekEvent(Handle evt)
{
	// do a wait that returns immediately.
	// if we get a timeout error code, the event didn't occur
	Result res = svcWaitSynchronization(evt, 0);
	if (!res)
	{
		svcClearEvent(evt);
		return true;
	}
	
	return false;
}


void VSyncAndFrameskip()
{
	if (running && !pause && PeekEvent(gspEvents[GSPEVENT_VBlank0]) && FramesSkipped<5)
	{
		// we missed the VBlank
		// skip the next frames to compensate
		
		// TODO: doesn't work
		/*s64 time = (s64)(svcGetSystemTick() - LastVBlank);
		while (time > 4468724)
		{
			FramesSkipped++;
			time -= 4468724;
		}*/
		
		SkipThisFrame = true;
		FramesSkipped++;
	}
	else
	{
		SkipThisFrame = false;
		FramesSkipped = 0;
		
		gfxSwapBuffersGpu();
		gspWaitForEvent(GSPEVENT_VBlank0, false);
		//LastVBlank = svcGetSystemTick();
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
	
	running = 0;
	pause = 0;
	exitspc = 0;
	
	
	VRAM_Init();
	PPU_Init();
	
	
	srvInit();
		
	aptInit();
	aptOpenSession();
	APT_SetAppCpuTimeLimit(NULL, 30); // enables syscore usage
	aptCloseSession();

	gfxInit();
	hidInit(NULL);
	fsInit();
	
	GPU_Init(NULL);
	bglInit();
	
	vertexBuf = linearAlloc(0x40000);
	vertexPtr = vertexBuf;
	
	svcSetThreadPriority(gspEventThread, 0x30);
	
	gpuOut = (u32*)VRAM_Alloc(400*240*2*4);
	gpuDOut = (u32*)VRAM_Alloc(400*240*2*4);
	SNESFrame = (u32*)VRAM_Alloc(256*256*4);
	
	finalShader = SHDR_ParseSHBIN((u32*)final_vsh_shbin, final_vsh_shbin_size);
	softRenderShader = SHDR_ParseSHBIN((u32*)render_soft_vsh_shbin, render_soft_vsh_shbin_size);
	hardRenderShader = SHDR_ParseSHBIN((u32*)render_hard_vsh_shbin, render_hard_vsh_shbin_size);
	plainQuadShader = SHDR_ParseSHBIN((u32*)plain_quad_vsh_shbin, plain_quad_vsh_shbin_size);
	
	GX_SetMemoryFill(NULL, gpuOut, 0x404040FF, &gpuOut[0x2EE00], 0x201, gpuDOut, 0x00000000, &gpuDOut[0x2EE00], 0x201);
	gspWaitForPSC0();
	gfxSwapBuffersGpu();
	
	UI_SetFramebuffer(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL));
	ClearConsole();
	
	BorderTex = (u32*)linearAlloc(512*256*4);
	//MainScreenTex = (u16*)linearAlloc(256*512*2);
	//SubScreenTex = &MainScreenTex[256*256];
	
	if (false) // TODO (software renderer)
	{
		MainScreenTex = (u16*)VRAM_Alloc(256*512*2);
		SubScreenTex = &MainScreenTex[256*256];
	}
	else
	{
		MainScreenTex = (u16*)VRAM_Alloc(256*512*4);
		SubScreenTex = &((u32*)MainScreenTex)[256*256];
	}
	
	borderVertices = (float*)linearAlloc(5*3 * 2 * sizeof(float));
	screenVertices = (float*)linearAlloc(5*3 * 2 * sizeof(float));
	
	float* fptr = &vertexList[0];
	for (i = 0; i < 5*3*2; i++) borderVertices[i] = *fptr++;
	for (i = 0; i < 5*3*2; i++) screenVertices[i] = *fptr++;
	

	sdmcArchive = (FS_archive){0x9, (FS_path){PATH_EMPTY, 1, (u8*)""}};
	FSUSER_OpenArchive(NULL, &sdmcArchive);
	
	// load border
	if (!LoadBorder("/blargSnesBorder.bmp"))
		CopyBitmapToTexture(defaultborder, BorderTex, 400, 240, 0xFF, 0, 64, 0x1);

	// copy splashscreen
	u32* tempbuf = (u32*)linearAlloc(256*256*4);
	CopyBitmapToTexture(screenfill, tempbuf, 256, 224, 0xFF, 0, 32, 0x0);
	GX_SetDisplayTransfer(NULL, tempbuf, 0x01000100, (u32*)SNESFrame, 0x01000100, 0x3);
	gspWaitForPPF();
	linearFree(tempbuf);
	
	Audio_Init();
	
	UI_Switch(&UI_ROMMenu);

	svcCreateEvent(&SPCSync, 0);


	APP_STATUS status;
	while((status = aptGetStatus()) != APP_EXITING)
	{
		if(status == APP_RUNNING)
		{
			hidScanInput();
			u32 press = hidKeysDown();
			u32 held = hidKeysHeld();
			u32 release = hidKeysUp();
			
			if (running && !pause)
			{
				// emulate
				
				CPU_Run(); // runs the SNES for one frame. Handles PPU rendering.
				
				// SRAM autosave check
				// TODO: also save SRAM under certain circumstances (pausing, returning to home menu, etc)
				framecount++;
				if (!(framecount & 7))
					SNES_SaveSRAM();
					
				if (release & KEY_TOUCH) 
				{
					bprintf("Pause.\n");
					bprintf("Tap screen or press A to resume.\n");
					bprintf("Press Select to load another game.\n");
					pause = 1;
				}
			}
			else
			{
				// update UI
				
				// TODO move this chunk of code somewhere else?
				if (running)
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
						
						CopyBitmapToTexture(screenfill, MainScreenTex, 256, 224, 0, 0, 32, 0x3);
						memset(SubScreenTex, 0, 256*256*2);
					}
					else if (release & KEY_X)
					{
						bprintf("PC: %02X|%04X\n", CPU_Regs.PBR, CPU_Regs.PC);
					}
					
					if ((held & (KEY_L|KEY_R)) == (KEY_L|KEY_R))
					{
						if (!shot)
						{
							u32 timestamp = (u32)(svcGetSystemTick() / 446872);
							char file[256];
							snprintf(file, 256, "/blargSnes%08d.bmp", timestamp);
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
				
				//myGPU_Reset();
				RenderTopScreen();
				VSyncAndFrameskip();
				
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
			
			if (!SkipThisFrame)
			{
				u8* bottomfb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
				
				UI_SetFramebuffer(bottomfb);
				UI_Render();
				GSPGPU_FlushDataCache(NULL, bottomfb, 0x38400);
			}
			
			//if ((!SkipThisFrame) || (FramesSkipped > 1))
			//	gfxSwapBuffersGpu();
			
			//VSyncAndFrameskip();
			//gspWaitForEvent(GSPEVENT_VBlank0, false);
		}
		else if(status == APP_SUSPENDING)
		{
			aptReturnToMenu();
		}
		else if(status == APP_PREPARE_SLEEPMODE)
		{
			aptSignalReadyForSleep();
			aptWaitStatusEvent();
		}
	}
	 
	exitspc = 1;
	if (spcthread) svcWaitSynchronization(spcthread, U64_MAX);
	
	bglDeInit();
	
	PPU_DeInit();

	fsExit();
	hidExit();
	gfxExit();
	aptExit();
	svcExitProcess();

    return 0;
}
