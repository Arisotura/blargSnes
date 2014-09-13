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

#include "mem.h"
#include "cpu.h"
#include "spc700.h"
#include "ppu.h"
#include "snes.h"

#include "logo.h"
#include "blarg_shbin.h"


Result svcCreateTimer(Handle* timer, int resettype);
Result svcSetTimer(Handle timer, s64 initial, s64 interval);
Result svcClearTimer(Handle timer);


extern u32* gxCmdBuf;
u32* gpuOut = (u32*)0x1F119400;
u32* gpuDOut = (u32*)0x1F370800;
DVLB_s* shader;

FS_archive sdmcArchive;


int running = 0;
int pause = 0;
int exitemu = 0;





Handle SPCSync;

void SPCThread(u32 blarg)
{
	for (;;)
	{
		if (!pause)
			SPC_Run();
		
		if (exitemu)
			break;
		
		svcWaitSynchronization(SPCSync, (s64)(17*1000*1000));
	}
	
	svcExitThread();
}


u32 framecount = 0;

void debugcrapo(u32 op, u32 op2)
{
	bprintf("DBG: %08X %08X\n", op, op2);
	DrawConsole();
	//SwapBottomBuffers(0);
	//ClearBottomBuffer();
}

void dbgcolor(u32 col)
{
	u32 regData=0x01000000|col;
	GSPGPU_WriteHWRegs(NULL, 0x202204, &regData, 4);
}



extern Handle aptuHandle;

Result APT_EnableSyscoreUsage(u32 max_percent)
{
	aptOpenSession();
	
	u32* cmdbuf=getThreadCommandBuffer();
	cmdbuf[0]=0x4F0080; //request header code
	cmdbuf[1]=1;
	cmdbuf[2]=max_percent;
	
	Result ret=0;
	if((ret=svcSendSyncRequest(aptuHandle)))
	{
		aptCloseSession();
		return ret;
	}

	aptCloseSession();
	return cmdbuf[1];
}



float topProjMatrix[16] = 
{
	2.0f/240.0f, 0, 0, -1,
	0, 2.0f/400.0f, 0, -1,
	0, 0, 1, -1,
	0, 0, 0, 1
};

float bottomProjMatrix[16] = 
{
	2.0f/240.0f, 0, 0, -1,
	0, 2.0f/320.0f, 0, -1,
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

float _blargvert[] = 
{
	0.0, 0.0, 0.5,      1.0, 0.9375,
	240.0, 0.0, 0.5,    1.0, 0,
	240.0, 400.0, 0.5,  0, 0,
	
	0.0, 0.0, 0.5,      1.0, 0.9375,
	240.0, 400.0, 0.5,  0, 0,
	0.0, 400.0, 0.5,    0, 0.9375,
};
float* blargvert;

u8* blargtex;

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

int derpo = 0;

void doFrameBlarg(u32* colorbuf, int width)
{
	// 000F0101 -- alpha blending
	// 0-7: color blend equation
	// 8-15: alpha blend equation
	// 16-19: color src factor
	// 20-23: color dst factor
	// 24-27: alpha src factor
	// 28-31: alpha dst factor
	
	// blend equation:
	// 0 = GL_FUNC_ADD
	// 1 = GL_FUNC_SUBTRACT
	// 2 = GL_FUNC_REVERSE_SUBTRACT
	// 3 = GL_MIN
	// 4 = GL_MAX
	
	// src/dst factor:
	// 0 = GL_ZERO (?)
	// 1 = GL_ONE (?)
	// 2 = GL_SRC_COLOR (?)
	// 3 = GL_ONE_MINUS_SRC_COLOR
	// 4 = GL_DST_COLOR
	// 5 = GL_ONE_MINUS_DST_COLOR
	// 6 = GL_SRC_ALPHA
	// 7 = GL_ONE_MINUS_SRC_ALPHA
	// 8 = GL_DST_ALPHA
	// 9 = GL_ONE_MINUS_DST_ALPHA
	// 10 = GL_CONSTANT_COLOR (?)
	// 11 = GL_ONE_MINUS_CONSTANT_COLOR (?)
	// 12 = GL_CONSTANT_ALPHA (?)
	// 13 = GL_ONE_MINUS_CONSTANT_ALPHA (?)
	// 14 = GL_SRC_ALPHA_SATURATE
	
	// set to 01010000 to disable.
	
	//general setup
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)gpuDOut),(u32*)osConvertVirtToPhys((u32)colorbuf),0,0,240*2,width);
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00);
	GPU_SetDepthTest(true, GPU_GREATER, 0x1F);
	
	// ?
	GPUCMD_AddSingleParam(0x00010062, 0x00000000); //param always 0x0 according to code
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	
	//setup shader
	SHDR_UseProgram(shader, 0);
		
	//?
	GPUCMD_AddSingleParam(0x000F0100, 0x00E40100);
	GPUCMD_AddSingleParam(0x000F0101, 0x01010000);
	GPUCMD_AddSingleParam(0x000F0104, 0x00000010);
	
	//texturing stuff
	GPUCMD_AddSingleParam(0x0002006F, 0x00000100);
	GPUCMD_AddSingleParam(0x000F0080, 0x00011001); //enables/disables texturing
	//texenv
	GPU_SetTexEnv(3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00000000);
	GPU_SetTexEnv(4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00000000);
	GPU_SetTexEnv(5, GPU_TEVSOURCES(GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR), GPU_TEVSOURCES(GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR),
	GPU_TEVOPERANDS(0,0,0), GPU_TEVOPERANDS(0,0,0), GPU_MODULATE, GPU_MODULATE, 0xFFFFFFFF);
	//texturing stuff
	GPU_SetTexture((u32*)osConvertVirtToPhys((u32)blargtex),256,256,0x6,GPU_RGBA8);
	
	//setup matrices
	setUniformMatrix(0x24, mvMatrix);
	setUniformMatrix(0x20, topProjMatrix);

	GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)blargvert),
		GPU_ATTRIBFMT(0, 3, GPU_FLOAT)|GPU_ATTRIBFMT(1, 2, GPU_FLOAT),
		0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
	
	//draw model
	GPU_DrawArray(GPU_TRIANGLES, 2*3);
}



Handle spcthread;
u8* spcthreadstack;

bool StartROM(char* path)
{
	char temppath[300];
	
	// load the ROM
	strncpy(temppath, "/snes/", 6);
	strncpy(&temppath[6], path, 0x106);
	temppath[6+0x106] = '\0';
	bprintf("Loading %s...\n", temppath);
	
	if (!SNES_LoadROM(temppath))
		return false;

	running = 1;
	framecount = 0;
	
	SPC_Reset();

	// SPC700 thread (running on syscore)
	Result res = svcCreateThread(&spcthread, SPCThread, 0, (u32*)(spcthreadstack+0x400), 0x3F, 1);
	if (res)
	{
		bprintf("Failed to create SPC700 thread:\n -> %08X\n", res);
	}
	
	bprintf("ROM loaded, running...\n");

	CPU_Reset();
	return true;
}



int main() 
{
	int i;
	
	touchPosition lastTouch;
	u32 repeatkeys = 0;
	int repeatstate = 0;
	int repeatcount = 0;
	
	running = 0;
	pause = 0;
	exitemu = 0;
	
	
		
	PPU_Init();
	
	
	srvInit();
		
	aptInit();
	APT_EnableSyscoreUsage(30);

	gfxInit();
	hidInit(NULL);
	fsInit();
	
	GPU_Init(NULL);
	u32 gpuCmdSize = 0x40000;
	u32* gpuCmd = (u32*)MemAlloc(gpuCmdSize*4);
	GPU_Reset(gxCmdBuf, gpuCmd, gpuCmdSize);
	
	shader = SHDR_ParseSHBIN((u32*)blarg_shbin, blarg_shbin_size);
	
	GX_SetMemoryFill(gxCmdBuf, (u32*)gpuOut, 0x404040FF, (u32*)&gpuOut[0x2EE00], 0x201, (u32*)gpuDOut, 0x00000000, (u32*)&gpuDOut[0x2EE00], 0x201);
	gfxSwapBuffersGpu();
	
	UI_SetFramebuffer(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL));
	
	// TODO port me
	/*for (y = 0; y < 240; y++)
	{
		for (x = 0; x < 400; x++)
		{
			int idx = (x*240) + (239-y);
			((u16*)TopFB)[idx] = logo[(y*400)+x];
		}
	}*/
	//SwapTopBuffers(0);
	
	blargvert = MemAlloc(5*3*2*4);
	for (i = 0; i < 5*3*2; i++)
	{
		blargvert[i] = _blargvert[i];
	}
	GSPGPU_FlushDataCache(NULL, blargvert, 5*3*2*4);
	
	blargtex = MemAlloc(256*256*4);
	for (i = 0; i < 256*256; i++)
	{
		int x = i & 0xFF;
		int y = i >> 8;
		
		int j;
		
		j = x & 0x1;
		j += (y & 0x1) << 1;
		j += (x & 0x2) << 1;
		j += (y & 0x2) << 2;
		j += (x & 0x4) << 2;
		j += (y & 0x4) << 3;
		j += (x & 0xF8) << 3;
		j += ((y & 0xF8) << 3) * 32;
		
		if (x < 128)
			((u32*)blargtex)[j] = 0xFFFF00FF;
		else
		{
			if (y < 160) 
				((u32*)blargtex)[j] = 0xFF00FFFF;
			else
				((u32*)blargtex)[j] = (i&0x100) ? 0x00FF00FF : 0x0000FFFF;
		}
	}
	GSPGPU_FlushDataCache(NULL, blargtex, 256*256*4);
	
	aptSetupEventHandler();
	

	sdmcArchive = (FS_archive){0x9, (FS_path){PATH_EMPTY, 1, (u8*)""}};
	FSUSER_OpenArchive(NULL, &sdmcArchive);
	
	UI_Switch(&UI_ROMMenu);
	
	spcthreadstack = MemAlloc(0x400); // should be good enough for a stack
	svcCreateEvent(&SPCSync, 0);
	
	// TEST
	/*s16* tempshiz = MemAlloc(2048*2);
	for (i = 0; i < 2048; i += 8)
	{
		tempshiz[i+0] = 32767;
		tempshiz[i+1] = 32767;
		tempshiz[i+2] = 32767;
		tempshiz[i+3] = 32767;
		tempshiz[i+4] = -32768;
		tempshiz[i+5] = -32768;
		tempshiz[i+6] = -32768;
		tempshiz[i+7] = -32768;
	}*/
	//Result ohshit = CSND_initialize(NULL);
	//CSND_playsound(8, 1, 1/*PCM16*/, 32000, tempshiz, tempshiz, 4096, 2, 0);
	// TEST END
	
	APP_STATUS status;
	while((status=aptGetStatus())!=APP_EXITING)
	{
		if(status==APP_RUNNING)
		{
			svcSignalEvent(SPCSync);
			
			hidScanInput();
			u32 press = hidKeysDown();
			u32 held = hidKeysHeld();
			u32 release = hidKeysUp();
			
			if (running)
			{
				// emulate
				
				CPU_Run(); // runs the SNES for one frame. Handles PPU rendering.
				
				// SRAM autosave check
				// TODO: also save SRAM under certain circumstances (pausing, returning to home menu, etc)
				framecount++;
				if (!(framecount & 7))
					SNES_SaveSRAM();
					
				UI_SetFramebuffer(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL));
				UI_Render();
			}
			else
			{
				// update UI
				
				if (held & KEY_TOUCH)
				{
					hidTouchRead(&lastTouch);
					UI_Touch(true, lastTouch.px, lastTouch.py);
					held &= ~KEY_TOUCH;
				}
				else if (release & KEY_TOUCH)
				{
					UI_Touch(false, lastTouch.px, lastTouch.py);
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
				else if (held == repeatkeys)
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
				
				UI_SetFramebuffer(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL));
				UI_Render();
			}

			// PICA200 TEST ZONE
			
			GPUCMD_SetBuffer(gpuCmd, gpuCmdSize, 0);
			doFrameBlarg(gpuOut, 400);
			GPUCMD_Finalize();
			GPUCMD_Run(gxCmdBuf);
			
			// flush the bottomscreen cache while the PICA200 is busy rendering
			GSPGPU_FlushDataCache(NULL, gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL), 0x38400);
			
			// wait for the PICA200 to finish drawing
			gspWaitForP3D();
			
			// transfer the final color buffer to the LCD
			GX_SetDisplayTransfer(gxCmdBuf, gpuOut, 0x019001E0, (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0x019001E0, 0x01001000);
			gspWaitForPPF();
			
			// fill the color buffer
			GX_SetMemoryFill(gxCmdBuf, gpuOut, 0x404040FF, &gpuOut[0x2EE00], 0x201, gpuDOut, 0x00000000, &gpuDOut[0x2EE00], 0x201);

			gspWaitForEvent(GSPEVENT_VBlank0, false);
			gfxSwapBuffersGpu();
		}
		/*else if(status == APP_SUSPENDING)
		{
			aptReturnToMenu();
		}
		else if(status == APP_SLEEPMODE)
		{
			aptWaitStatusEvent();
		}*/
	}
	
	MemFree(gpuCmd, gpuCmdSize*4);
	MemFree(spcthreadstack, 0x400);

	fsExit();
	hidExit();
	aptExit();
	svcExitProcess();

    return 0;
}
