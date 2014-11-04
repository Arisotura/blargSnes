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

#include "ui.h"
#include "audio.h"

#include "mem.h"
#include "cpu.h"
#include "spc700.h"
#include "ppu.h"
#include "snes.h"

#include "defaultborder.h"
#include "screenfill.h"
#include "blarg_shbin.h"


extern u32* gxCmdBuf;
u32* gpuOut;
u32* gpuDOut;
u32* SNESFrame;
DVLB_s* shader;

u32 gpuCmdSize;
u32* gpuCmd0;
u32* gpuCmd1;
u32* gpuCmd;
int curCmd = 0;

u32* BorderTex;
u16* MainScreenTex;
u16* SubScreenTex;
u8* BrightnessTex;

FS_archive sdmcArchive;


int running = 0;
int pause = 0;
u32 framecount = 0;

int RenderState = 0;
int FramesSkipped = 0;
bool SkipThisFrame = false;

// hax
extern Handle gspEventThread;
extern Handle gspEvents[GSPEVENT_MAX];


Result svcSetThreadPriority(Handle thread, s32 prio)
{
	asm("svc 0xC");
}


Handle gputhread = NULL;
u8 gputhreadstack[0x4000] __attribute__((aligned(8)));
Handle GPURenderTrigger, GPURenderDone;
int exitgpu = 0;

void GPUThread(u32 blarg)
{
	for (;;)
	{
		svcWaitSynchronization(GPURenderTrigger, U64_MAX);
		svcClearEvent(GPURenderTrigger);
		if (exitgpu) break;
		
		
		svcClearEvent(GPURenderDone);
		
		RenderTopScreen();
		GPUCMD_Finalize();
		GPUCMD_Run(gxCmdBuf);
		gspWaitForP3D();
		
		GX_SetDisplayTransfer(gxCmdBuf, gpuOut, 0x019001E0, (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0x019001E0, 0x01001000);
		curCmd ^= 1;
		gpuCmd = curCmd ? gpuCmd1 : gpuCmd0;
		GPUCMD_SetBuffer(gpuCmd, gpuCmdSize, 0);
		gspWaitForPPF();
		
		gfxSwapBuffersGpu();
		gspWaitForVBlank();
		svcSignalEvent(GPURenderDone);
	}
	
	svcExitThread();
}


Handle spcthread = NULL;
u8 spcthreadstack[0x4000] __attribute__((aligned(8)));
Handle SPCSync;
int exitspc = 0;

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
	0, 2.0f/240.0f, 0, -1,
	0, 0, 1, -1,
	0, 0, 0, 1
};

float mvMatrix[16] = 
{
	1, 0, 0, 0,
	0, 1, 0, 0, 
	0, 0, 1, 0, 
	0, 0, 0, 1
};

float vertexList[] = 
{
	// border
	/*0.0, 0.0, 0.9,      0.78125, 0.0625,
	240.0, 0.0, 0.9,    0.78125, 1.0,
	240.0, 400.0, 0.9,  0, 1.0,
	
	0.0, 0.0, 0.9,      0.78125, 0.0625,
	240.0, 400.0, 0.9,  0, 1.0,
	0.0, 400.0, 0.9,    0, 0.0625,*/
	
	0.0, 0.0, 0.9,      1.0, 0.0625,
	240.0, 0.0, 0.9,    1.0, 0.9375, // should really be 0.875 -- investigate
	240.0, 400.0, 0.9,  0.0, 0.9375,
	
	0.0, 0.0, 0.9,      1.0, 0.0625,
	240.0, 400.0, 0.9,  0.0, 0.9375,
	0.0, 400.0, 0.9,    0.0, 0.0625,
	
	// screen
	/*8.0, 72.0, 0.5,     1.0, 0.125,  0.125, 0.125,
	232.0, 72.0, 0.5,   1.0, 1.0,    0.125, 1.0,
	232.0, 328.0, 0.5,  0.0, 1.0,    0.0, 1.0,
	
	8.0, 72.0, 0.5,     1.0, 0.125,  0.125, 0.125,
	232.0, 328.0, 0.5,  0.0, 1.0,    0.0,   1.0,
	8.0, 328.0, 0.5,    0.0, 0.125,  0.0,   0.125,*/
	0.0, 0.0, 0.5,     0.0, 0.125,  0.125, 0.125,
	256.0, 0.0, 0.5,   1.0, 0.125,    0.125, 1.0,
	256.0, 224.0, 0.5,  1.0, 1.0,    0.0, 1.0,
	
	0.0, 0.0, 0.5,     0.0, 0.125,  0.125, 0.125,
	256.0, 224.0, 0.5,  1.0, 1.0,    0.0,   1.0,
	0.0, 224.0, 0.5,    0.0, 1.0,  0.0,   0.125,
};
float* borderVertices;
float* screenVertices;

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

int shaderset = 0;
void GPU_SetShader()
{
	if (shaderset) return;
	shaderset = 1;
	
	SHDR_UseProgram(shader, 0);
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

void RenderTopScreen()
{
	shaderset = 0;
	// notes on the drawing process 
	// textures used here are actually 512x256. TODO: investigate if GPU_SetTexture() really has the params in the wrong order
	// or if we did something wrong.
	
	
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)gpuDOut),(u32*)osConvertVirtToPhys((u32)SNESFrame),0,0,256,240);
	
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
	GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_COLOR); // we don't care about depth testing in this pass
	
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	
	GPU_SetShader();
	
	GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	GPU_SetAlphaTest(false, GPU_ALWAYS, 0x00);
	
	GPU_SetTextureEnable(GPU_TEXUNIT0|GPU_TEXUNIT1|GPU_TEXUNIT2);
	
	// TEXTURE ENV STAGES
	// ---
	// blending operation: (Main.Color +- (Sub.Color * Main.Alpha)) * Sub.Alpha
	// Main.Alpha: 0 = no color math, 255 = color math
	// Sub.Alpha: 0 = div2, 1 = no div2
	// ---
	// STAGE 1: Out.Color = Sub.Color * Main.Alpha, Out.Alpha = Sub.Alpha + 0.5
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE1, GPU_TEXTURE0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE1, GPU_CONSTANT, 0),
		GPU_TEVOPERANDS(0,2,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_MODULATE, 
		GPU_ADD, 
		0x80FFFFFF);
	
	if (PPU.Subtract)
	{
		// COLOR SUBTRACT
		
		// STAGE 2: Out.Color = Main.Color - Prev.Color, Out.Alpha = Prev.Alpha + (1-Main.Alpha) (cancel out div2 when color math doesn't happen)
		GPU_SetTexEnv(1, 
			GPU_TEVSOURCES(GPU_TEXTURE0, GPU_PREVIOUS, 0), 
			GPU_TEVSOURCES(GPU_PREVIOUS, GPU_TEXTURE0, 0),
			GPU_TEVOPERANDS(0,0,0), 
			GPU_TEVOPERANDS(0,1,0), 
			GPU_SUBTRACT, 
			GPU_ADD, 
			0xFFFFFFFF);
		// STAGE 3: Out.Color = Prev.Color * Prev.Alpha, Out.Alpha = Prev.Alpha
		GPU_SetTexEnv(2, 
			GPU_TEVSOURCES(GPU_PREVIOUS, GPU_PREVIOUS, 0), 
			GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0),
			GPU_TEVOPERANDS(0,2,0), 
			GPU_TEVOPERANDS(0,0,0), 
			GPU_MODULATE, 
			GPU_REPLACE, 
			0xFFFFFFFF);
		// STAGE 4: dummy (no need to double color intensity)
		GPU_SetDummyTexEnv(3);
	}
	else
	{
		// COLOR ADDITION
		
		// STAGE 2: Out.Color = Main.Color*0.5 + Prev.Color*0.5 (prevents overflow), Out.Alpha = Prev.Alpha + (1-Main.Alpha) (cancel out div2 when color math doesn't happen)
		GPU_SetTexEnv(1, 
			GPU_TEVSOURCES(GPU_TEXTURE0, GPU_PREVIOUS, GPU_CONSTANT), 
			GPU_TEVSOURCES(GPU_PREVIOUS, GPU_TEXTURE0, 0),
			GPU_TEVOPERANDS(0,0,0), 
			GPU_TEVOPERANDS(0,1,0), 
			GPU_INTERPOLATE,
			GPU_ADD, 
			0xFF808080);
		// STAGE 3: Out.Color = Prev.Color * Prev.Alpha, Out.Alpha = Prev.Alpha
		GPU_SetTexEnv(2, 
			GPU_TEVSOURCES(GPU_PREVIOUS, GPU_PREVIOUS, 0), 
			GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0),
			GPU_TEVOPERANDS(0,2,0), 
			GPU_TEVOPERANDS(0,0,0), 
			GPU_MODULATE, 
			GPU_REPLACE, 
			0xFFFFFFFF);
		// STAGE 4: Out.Color = Prev.Color + Prev.Color (doubling color intensity), Out.Alpha = Const.Alpha
		GPU_SetTexEnv(3, 
			GPU_TEVSOURCES(GPU_PREVIOUS, GPU_PREVIOUS, 0), 
			GPU_TEVSOURCES(GPU_CONSTANT, 0, 0),
			GPU_TEVOPERANDS(0,0,0), 
			GPU_TEVOPERANDS(0,0,0), 
			GPU_ADD, 
			GPU_REPLACE, 
			0xFFFFFFFF);
	}
	
	// STAGE 5: master brightness - Out.Color = Prev.Color * Bright.Alpha, Out.Alpha = Const.Alpha
	GPU_SetTexEnv(4, 
		GPU_TEVSOURCES(GPU_PREVIOUS, GPU_TEXTURE2, 0), 
		GPU_TEVSOURCES(GPU_CONSTANT, 0, 0),
		GPU_TEVOPERANDS(0,2,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_MODULATE, 
		GPU_REPLACE, 
		0xFFFFFFFF);
	// STAGE 6: dummy
	GPU_SetDummyTexEnv(5);
		
	GPU_SetTexture(GPU_TEXUNIT0, (u32*)osConvertVirtToPhys((u32)MainScreenTex),256,256,0,GPU_RGBA5551);
	GPU_SetTexture(GPU_TEXUNIT1, (u32*)osConvertVirtToPhys((u32)SubScreenTex),256,256,0,GPU_RGBA5551);
	GPU_SetTexture(GPU_TEXUNIT2, (u32*)osConvertVirtToPhys((u32)BrightnessTex),256,8,0x200,GPU_A8);
	
	setUniformMatrix(0x24, mvMatrix);
	setUniformMatrix(0x20, snesProjMatrix);
	
	GPU_SetAttributeBuffers(3, (u32*)osConvertVirtToPhys((u32)screenVertices),
		GPU_ATTRIBFMT(0, 3, GPU_FLOAT)|GPU_ATTRIBFMT(1, 2, GPU_FLOAT)|GPU_ATTRIBFMT(2, 2, GPU_FLOAT),
		0xFFC, 0x210, 1, (u32[]){0x00000000}, (u64[]){0x210}, (u8[]){3});
	
	GPU_DrawArray(GPU_TRIANGLES, 2*3);
	//GPU_FinishDrawing();

	
	
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)gpuDOut),(u32*)osConvertVirtToPhys((u32)gpuOut),0,0,240*2,400);
	
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
	GPU_SetStencilOp(GPU_KEEP, GPU_KEEP, GPU_KEEP);
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_ALL);
	
	GPUCMD_AddSingleParam(0x00010062, 0); 
	GPUCMD_AddSingleParam(0x000F0118, 0);
	
	//setup shader
	GPU_SetShader();
	
	GPU_SetAlphaBlending(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	GPU_SetAlphaTest(false, GPU_ALWAYS, 0x00);
	
	GPU_SetTextureEnable(GPU_TEXUNIT0);
	
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	GPU_SetDummyTexEnv(1);
	GPU_SetDummyTexEnv(2);
	GPU_SetDummyTexEnv(3);
	GPU_SetDummyTexEnv(4);
	GPU_SetDummyTexEnv(5);
	
	//GPU_SetTexture(GPU_TEXUNIT0, (u32*)osConvertVirtToPhys((u32)BorderTex),256,512,0,GPU_RGBA8); // texture is actually 512x256
	GPU_SetTexture(GPU_TEXUNIT0, (u32*)osConvertVirtToPhys((u32)SNESFrame),256,256,0x6,GPU_RGBA8);
	
	//setup matrices
	setUniformMatrix(0x24, mvMatrix);
	setUniformMatrix(0x20, screenProjMatrix);
	
	// border
	GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)borderVertices),
		GPU_ATTRIBFMT(0, 3, GPU_FLOAT)|GPU_ATTRIBFMT(1, 2, GPU_FLOAT),
		0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
		
	GPU_DrawArray(GPU_TRIANGLES, 2*3); 
	GPU_FinishDrawing();
	

	// TODO: there are probably unneeded things in here. Investigate whenever we know the PICA200 better.
	
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

void RenderPipeline()
{
	// PICA200 rendering.
	// doing all this on a separate thread would normally work better,
	// but the 3DS threads don't like to cooperate. Oh well.
	
	if (RenderState != 0) return;
	
	// check if rendering finished
	if (PeekEvent(gspEvents[GSPEVENT_P3D]))
	{
		// in that case, send the color buffer to the LCD
		GX_SetDisplayTransfer(gxCmdBuf, gpuOut, 0x019001E0, (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0x019001E0, 0x01001000);
		RenderState = 1;
	}
}

void RenderPipelineVBlank()
{
	// SNES VBlank. Copy the freshly rendered framebuffers.
	
	GSPGPU_FlushDataCache(NULL, (u8*)PPU.MainBuffer, 256*512*2);
	
	// in case we arrived here too early
	if (RenderState != 1)
	{
		gspWaitForP3D();
		GX_SetDisplayTransfer(gxCmdBuf, gpuOut, 0x019001E0, (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0x019001E0, 0x01001000);
	}
	
	// wait for the previous copy to be done, just in case
	gspWaitForPPF();
	
	// copy new screen textures
	// SetDisplayTransfer with flags=2 converts linear graphics to the tiled format used for textures
	// since the two sets of buffers are contiguous, we can transfer them as one 256x512 texture
	GX_SetDisplayTransfer(gxCmdBuf, (u32*)PPU.MainBuffer, 0x02000100, (u32*)MainScreenTex, 0x02000100, 0x3302);
	
	// copy brightness.
	// TODO do better
	// although I don't think SetDisplayTransfer is fitted to handle alpha textures
	int i;
	u8* bptr = BrightnessTex;
	for (i = 0; i < 224;)
	{
		u32 pixels = *(u32*)&PPU.Brightness[i];
		i += 4;
		
		*bptr = (u8)pixels;
		pixels >>= 8;
		bptr += 2;
		*bptr = (u8)pixels;
		pixels >>= 8;
		bptr += 6;
		
		*bptr = (u8)pixels;
		pixels >>= 8;
		bptr += 2;
		*bptr = (u8)pixels;
		pixels >>= 8;
		bptr += 22;
	}
	GSPGPU_FlushDataCache(NULL, BrightnessTex, 8*256);
}


void VSyncAndFrameskip()
{
	if (running && !pause && PeekEvent(gspEvents[GSPEVENT_VBlank0]) && FramesSkipped<5)
	{
		// we missed the VBlank
		// skip the next frame to compensate
		// TODO: this doesn't work if we miss more than one VBlank!
		
		SkipThisFrame = true;
		FramesSkipped++;
	}
	else
	{
		SkipThisFrame = false;
		FramesSkipped = 0;
		
		gspWaitForEvent(GSPEVENT_VBlank0, false);
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
	gpuCmdSize = 0x40000;
	gpuCmd0 = (u32*)linearAlloc(gpuCmdSize*4);
	gpuCmd1 = (u32*)linearAlloc(gpuCmdSize*4);
	curCmd = 0;
	gpuCmd = gpuCmd0;
	GPU_Reset(gxCmdBuf, gpuCmd, gpuCmdSize);
	
	svcSetThreadPriority(gspEventThread, 0x30);
	
	gpuOut = (u32*)VRAM_Alloc(400*240*2*4);
	gpuDOut = (u32*)VRAM_Alloc(400*240*2*4);
	SNESFrame = (u32*)VRAM_Alloc(256*240*4);
	
	shader = SHDR_ParseSHBIN((u32*)blarg_shbin, blarg_shbin_size);
	
	GX_SetMemoryFill(gxCmdBuf, gpuOut, 0x404040FF, &gpuOut[0x2EE00], 0x201, gpuDOut, 0x00000000, &gpuDOut[0x2EE00], 0x201);
	gspWaitForPSC0();
	gfxSwapBuffersGpu();
	
	UI_SetFramebuffer(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL));
	ClearConsole();
	
	BorderTex = (u32*)linearAlloc(512*256*4);
	MainScreenTex = (u16*)linearAlloc(256*512*2);
	SubScreenTex = &MainScreenTex[256*256];
	BrightnessTex = (u8*)linearAlloc(8*256);
	
	borderVertices = (float*)linearAlloc(5*3 * 2 * sizeof(float));
	screenVertices = (float*)linearAlloc(7*3 * 2 * sizeof(float));
	
	float* fptr = &vertexList[0];
	for (i = 0; i < 5*3*2; i++) borderVertices[i] = *fptr++;
	for (i = 0; i < 7*3*2; i++) screenVertices[i] = *fptr++;
	

	sdmcArchive = (FS_archive){0x9, (FS_path){PATH_EMPTY, 1, (u8*)""}};
	FSUSER_OpenArchive(NULL, &sdmcArchive);
	
	if (!LoadBorder("/blargSnesBorder.bmp"))
		CopyBitmapToTexture(defaultborder, BorderTex, 400, 240, /*0xFF*/0x80, 0, 64, 0x1);
		
	CopyBitmapToTexture(screenfill, MainScreenTex, 256, 224, 0, 0, 32, 0x3);
	memset(SubScreenTex, 0, 256*256*2);
	memset(BrightnessTex, 0xFF, 224*8);
	
	Audio_Init();
	
	UI_Switch(&UI_ROMMenu);
	
	/*GPUCMD_SetBuffer(gpuCmd, gpuCmdSize, 0);
	
	svcCreateEvent(&GPURenderTrigger, 0);
	svcCreateEvent(&GPURenderDone, 0);
	svcCreateThread(&gputhread, GPUThread, 0, (u32*)(gputhreadstack+0x4000), 0x30, ~1);*/
	
	svcCreateEvent(&SPCSync, 0);


	APP_STATUS status;
	while((status = aptGetStatus()) != APP_EXITING)
	{
		if(status == APP_RUNNING)
		{
			//svcSignalEvent(SPCSync);
			
			hidScanInput();
			u32 press = hidKeysDown();
			u32 held = hidKeysHeld();
			u32 release = hidKeysUp();
			
			if (running && !pause)
			{
				if (!SkipThisFrame)
				{
					// start PICA200 rendering
					// we don't have to care about clearing the buffers since we always render a 400x240 border
					// and don't use depth test
					
					RenderState = 0;
					GPUCMD_SetBuffer(gpuCmd, gpuCmdSize, 0);
					RenderTopScreen();
					GPUCMD_Finalize();
					GPUCMD_Run(gxCmdBuf);
				}
				else
				{
					// when frameskipping, just copy the old frame
					
					GX_SetDisplayTransfer(gxCmdBuf, gpuOut, 0x019001E0, (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0x019001E0, 0x01001000);
				}
				
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
						memset(BrightnessTex, 0xFF, 224*8);
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
				
				GPUCMD_SetBuffer(gpuCmd, gpuCmdSize, 0);
				RenderTopScreen();
				GPUCMD_Finalize();
				GPUCMD_Run(gxCmdBuf);
				
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
				 
				gspWaitForP3D();

				GX_SetDisplayTransfer(gxCmdBuf, gpuOut, 0x019001E0, (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0x019001E0, 0x01001000);
			}
			
			u8* bottomfb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
			
			UI_SetFramebuffer(bottomfb);
			UI_Render();
			GSPGPU_FlushDataCache(NULL, bottomfb, 0x38400);
			
			// at this point, we were transferring a framebuffer. Wait for it to be done.
			gspWaitForPPF();
			gfxSwapBuffersGpu();
			VSyncAndFrameskip();
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
	
	linearFree(gpuCmd);
	
	PPU_DeInit();

	fsExit();
	hidExit();
	gfxExit();
	aptExit();
	svcExitProcess();

    return 0;
}
