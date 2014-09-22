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

#include "defaultborder.h"
#include "screenfill.h"
#include "blarg_shbin.h"


Result svcCreateTimer(Handle* timer, int resettype);
Result svcSetTimer(Handle timer, s64 initial, s64 interval);
Result svcClearTimer(Handle timer);


extern u32* gxCmdBuf;
u32* gpuOut = (u32*)0x1F119400;
u32* gpuDOut = (u32*)0x1F370800;
DVLB_s* shader;

u32 gpuCmdSize;
u32* gpuCmd;

u32* BorderTex;
u32* MainScreenTex;
u32* SubScreenTex;
u8* BrightnessTex;

FS_archive sdmcArchive;


int running = 0;
int pause = 0;
int exitemu = 0;
u32 framecount = 0;


Handle SPCSync;

void SPCThread(u32 blarg)
{
	while (!exitemu)
	{
		if (!pause)
			SPC_Run();
		
		svcWaitSynchronization(SPCSync, (s64)(17*1000*1000));
	}
	
	svcExitThread();
}


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



float projMatrix[16] = 
{
	2.0f/240.0f, 0, 0, -1,
	0, 2.0f/400.0f, 0, -1,
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
	0.0, 0.0, 0.9,      0.78125, 0.0625,
	240.0, 0.0, 0.9,    0.78125, 1.0,
	240.0, 400.0, 0.9,  0, 1.0,
	
	0.0, 0.0, 0.9,      0.78125, 0.0625,
	240.0, 400.0, 0.9,  0, 1.0,
	0.0, 400.0, 0.9,    0, 0.0625,
	
	// screen
	8.0, 72.0, 0.5,     0.53125, 0.125,  0.125, 0.125,
	232.0, 72.0, 0.5,   0.53125, 1.0,    0.125, 1.0,
	232.0, 328.0, 0.5,  0.03125, 1.0,    0.0, 1.0,
	
	8.0, 72.0, 0.5,     0.53125, 0.125,  0.125, 0.125,
	232.0, 328.0, 0.5,  0.03125, 1.0,    0.0,   1.0,
	8.0, 328.0, 0.5,    0.03125, 0.125,  0.0,   0.125,
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

void GPU_SetTexture1(u32* data, u16 width, u16 height, u32 param, GPU_TEXCOLOR colorType)
{
	GPUCMD_AddSingleParam(0x000F0096, colorType);
	GPUCMD_AddSingleParam(0x000F0095, ((u32)data)>>3);
	GPUCMD_AddSingleParam(0x000F0092, (width)|(height<<16));
	GPUCMD_AddSingleParam(0x000F0093, param);
}

void GPU_SetTexture2(u32* data, u16 width, u16 height, u32 param, GPU_TEXCOLOR colorType)
{
	GPUCMD_AddSingleParam(0x000F009E, colorType);
	GPUCMD_AddSingleParam(0x000F009D, ((u32)data)>>3);
	GPUCMD_AddSingleParam(0x000F009A, (width)|(height<<16));
	GPUCMD_AddSingleParam(0x000F009B, param);
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
	// notes on the drawing process 
	// GPU hangs if we attempt to draw an even number of arrays :/ which is why we draw the border 'twice'
	// textures used here are actually 512x256. TODO: investigate if GPU_SetTexture() really has the params in the wrong order
	// or if we did something wrong.
	
	
	//general setup
	GPU_SetViewport((u32*)osConvertVirtToPhys((u32)gpuDOut),(u32*)osConvertVirtToPhys((u32)gpuOut),0,0,240*2,400);
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00);
	GPU_SetDepthTest(false, GPU_ALWAYS, 0x1F);
	
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
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE0, 0, 0), 
		GPU_TEVSOURCES(GPU_CONSTANT, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_REPLACE, GPU_REPLACE, 
		0xFFFFFFFF);
	GPU_SetDummyTexEnv(1);
	GPU_SetDummyTexEnv(2);
	GPU_SetDummyTexEnv(3);
	GPU_SetDummyTexEnv(4);
	GPU_SetDummyTexEnv(5);
	//texturing stuff
	GPU_SetTexture((u32*)osConvertVirtToPhys((u32)BorderTex),256,512,0,GPU_RGBA8); // texture is actually 512x256
	
	//setup matrices
	setUniformMatrix(0x24, mvMatrix);
	setUniformMatrix(0x20, projMatrix);
	
	// border
	GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)borderVertices),
		GPU_ATTRIBFMT(0, 3, GPU_FLOAT)|GPU_ATTRIBFMT(1, 2, GPU_FLOAT),
		0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
		
	GPU_DrawArray(GPU_TRIANGLES, 3);
	GPU_DrawArray(GPU_TRIANGLES, 2*3);


	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00);
	GPU_SetDepthTest(false, GPU_ALWAYS, 0x1F);
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0100, 0x00000100);
	GPUCMD_AddSingleParam(0x000F0101, 0x01010000);
	GPUCMD_AddSingleParam(0x000F0104, 0x00000010);
	
	//texturing stuff
	GPUCMD_AddSingleParam(0x0002006F, 0x00000700); // enables/disables texcoord output
	GPUCMD_AddSingleParam(0x000F0080, 0x00011007); //enables/disables texturing
	// TEXTURE ENV STAGES
	// ---
	// blending operation: (Main.Color +- (Sub.Color * Main.Alpha)) * Sub.Alpha
	// Main.Alpha = 0/255 depending on color math
	// Sub.Alpha = 128/255 depending on color div2
	// note: the main/sub intensities are halved to prevent overflow during the operations.
	// (each TEV stage output is clamped to [0,255])
	// stage 4 makes up for this
	// ---
	// STAGE 1: Out.Color = Sub.Color * Main.Alpha, Out.Alpha = Sub.Alpha + (1-Main.Alpha) (cancel out div2 when color math doesn't happen)
	GPU_SetTexEnv(0, 
		GPU_TEVSOURCES(GPU_TEXTURE1, GPU_TEXTURE0, 0), 
		GPU_TEVSOURCES(GPU_TEXTURE1, GPU_TEXTURE0, 0),
		GPU_TEVOPERANDS(0,2,0), 
		GPU_TEVOPERANDS(0,1,0), 
		GPU_MODULATE, 
		GPU_ADD, 
		0xFFFFFFFF);
	// STAGE 2: Out.Color = Main.Color +- Prev.Color, Out.Alpha = Prev.Alpha
	GPU_SetTexEnv(1, 
		GPU_TEVSOURCES(GPU_TEXTURE0, GPU_PREVIOUS, 0), 
		GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		(PPU_ColorMath & 0x80) ? GPU_SUBTRACT:GPU_ADD, 
		GPU_REPLACE, 
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
	// STAGE 4: Out.Color = Prev.Color + Prev.Color (doubling color intensity), Out.Alpha = Const.Alpha
	GPU_SetTexEnv(3, 
		GPU_TEVSOURCES(GPU_PREVIOUS, GPU_PREVIOUS, 0), 
		GPU_TEVSOURCES(GPU_CONSTANT, 0, 0),
		GPU_TEVOPERANDS(0,0,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_ADD, 
		GPU_REPLACE, 
		0xFFFFFFFF);
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
	

	GPU_SetAttributeBuffers(3, (u32*)osConvertVirtToPhys((u32)screenVertices),
		GPU_ATTRIBFMT(0, 3, GPU_FLOAT)|GPU_ATTRIBFMT(1, 2, GPU_FLOAT)|GPU_ATTRIBFMT(2, 2, GPU_FLOAT),
		0xFFC, 0x210, 1, (u32[]){0x00000000}, (u64[]){0x210}, (u8[]){3});
		
	GPU_SetTexture((u32*)osConvertVirtToPhys((u32)MainScreenTex),256,512,0,GPU_RGBA8);
	GPU_SetTexture1((u32*)osConvertVirtToPhys((u32)SubScreenTex),256,512,0,GPU_RGBA8);
	GPU_SetTexture2((u32*)osConvertVirtToPhys((u32)BrightnessTex),256,8,0x200,GPU_A8);
	
	GPU_DrawArray(GPU_TRIANGLES, 2*3);
}



void CopyBitmapToTexture(u8* src, u32* dst, u32 width, u32 height, u32 alpha, u32 startx, u32 stride, bool tiled)
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
			if (tiled)
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
			
			dst[di] = alpha | (b << 8) | (g << 16) | (r << 24);
		}
	}
}

bool LoadBitmap(char* path, u32 width, u32 height, u32* dst, u32 alpha, u32 startx, u32 stride, bool tiled)
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
	
	CopyBitmapToTexture(buf, dst, width, height, alpha, startx, stride, tiled);
	
	MemFree(buf);
	return true;
}

bool LoadBorder(char* path)
{
	return LoadBitmap(path, 400, 240, BorderTex, 0xFF, 0, 64, true);
}


Handle spcthread = NULL;
u8 spcthreadstack[0x400] __attribute__((aligned(8)));

bool StartROM(char* path)
{
	char temppath[300];
	Result res;
	
	// load the ROM
	strncpy(temppath, "/snes/", 6);
	strncpy(&temppath[6], path, 0x106);
	temppath[6+0x106] = '\0';
	bprintf("Loading %s...\n", temppath);
	
	if (!SNES_LoadROM(temppath))
		return false;

	running = 1;
	framecount = 0;
	
	CPU_Reset();
	
	SPC_Reset();

	// SPC700 thread (running on syscore)
	res = svcCreateThread(&spcthread, SPCThread, 0, (u32*)(spcthreadstack+0x400), 0x3F, 1);
	if (res)
	{
		bprintf("Failed to create SPC700 thread:\n -> %08X\n", res);
		spcthread = NULL;
	}
	
	bprintf("ROM loaded, running...\n");
	
	return true;
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


int main() 
{
	int i, x, y;
	
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
	aptOpenSession();
	APT_SetAppCpuTimeLimit(NULL, 30); // enables syscore usage
	aptCloseSession();

	gfxInit();
	hidInit(NULL);
	fsInit();
	
	GPU_Init(NULL);
	gpuCmdSize = 0x40000;
	gpuCmd = (u32*)linearAlloc(gpuCmdSize*4);
	GPU_Reset(gxCmdBuf, gpuCmd, gpuCmdSize);
	
	shader = SHDR_ParseSHBIN((u32*)blarg_shbin, blarg_shbin_size);
	
	GX_SetMemoryFill(gxCmdBuf, (u32*)gpuOut, 0x404040FF, (u32*)&gpuOut[0x2EE00], 0x201, (u32*)gpuDOut, 0x00000000, (u32*)&gpuDOut[0x2EE00], 0x201);
	gfxSwapBuffersGpu();
	
	UI_SetFramebuffer(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL));
	
	BorderTex = (u32*)linearAlloc(512*256*4);
	MainScreenTex = (u32*)linearAlloc(512*256*4);
	SubScreenTex = (u32*)linearAlloc(512*256*4);
	BrightnessTex = (u8*)linearAlloc(8*256);
	
	borderVertices = (float*)linearAlloc(5*3 * 2 * sizeof(float));
	screenVertices = (float*)linearAlloc(7*3 * 2 * sizeof(float));
	
	float* fptr = &vertexList[0];
	for (i = 0; i < 5*3*2; i++) borderVertices[i] = *fptr++;
	for (i = 0; i < 7*3*2; i++) screenVertices[i] = *fptr++;
	

	sdmcArchive = (FS_archive){0x9, (FS_path){PATH_EMPTY, 1, (u8*)""}};
	FSUSER_OpenArchive(NULL, &sdmcArchive);
	
	if (!LoadBorder("/blargSnesBorder.bmp"))
		CopyBitmapToTexture(defaultborder, BorderTex, 400, 240, 0xFF, 0, 64, true);
		
	CopyBitmapToTexture(screenfill, PPU_MainBuffer, 256, 224, 0, 16, 64, false);
	memset(PPU_SubBuffer, 0, 256*256*4);
	memset(PPU_Brightness, 0xFF, 224);
	
	UI_Switch(&UI_ROMMenu);
	
	svcCreateEvent(&SPCSync, 0);
	
	aptSetupEventHandler();

	
	APP_STATUS status;
	while((status = aptGetStatus()) != APP_EXITING)
	{
		if(status == APP_RUNNING)
		{
			svcSignalEvent(SPCSync);
			
			hidScanInput();
			u32 press = hidKeysDown();
			u32 held = hidKeysHeld();
			u32 release = hidKeysUp();
			
			GPUCMD_SetBuffer(gpuCmd, gpuCmdSize, 0);
			RenderTopScreen();
			GPUCMD_Finalize();
			GPUCMD_Run(gxCmdBuf);
			
			if (running)
			{
				// emulate
				
				CPU_Run(); // runs the SNES for one frame. Handles PPU rendering.
				
				// SRAM autosave check
				// TODO: also save SRAM under certain circumstances (pausing, returning to home menu, etc)
				framecount++;
				if (!(framecount & 7))
					SNES_SaveSRAM();
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
			}
			
			UI_SetFramebuffer(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL));
			UI_Render();
			
			/*GPUCMD_SetBuffer(gpuCmd, gpuCmdSize, 0);
			RenderTopScreen();
			GPUCMD_Finalize();
			GPUCMD_Run(gxCmdBuf);*/
			
			// flush the bottomscreen cache while the PICA200 is busy rendering
			GSPGPU_FlushDataCache(NULL, gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL), 0x38400);
			
			// wait for the PICA200 to finish drawing
			gspWaitForP3D();
			
			// copy new screen textures
			// SetDisplayTransfer with flags=2 converts linear graphics to the tiled format used for textures
			GX_SetDisplayTransfer(gxCmdBuf, PPU_MainBuffer, 0x01000200, MainScreenTex, 0x01000200, 0x2);
			gspWaitForPPF();
			GX_SetDisplayTransfer(gxCmdBuf, PPU_SubBuffer, 0x01000200, SubScreenTex, 0x01000200, 0x2);
			gspWaitForPPF();
			
			// copy brightness.
			// TODO do better
			u8* bptr = BrightnessTex;
			for (i = 0; i < 224;)
			{
				u32 pixels = *(u32*)&PPU_Brightness[i];
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
			
			// transfer the final color buffer to the LCD and clear it
			// we can mostly overlap those two operations
			GX_SetDisplayTransfer(gxCmdBuf, gpuOut, 0x019001E0, (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0x019001E0, 0x01001000);
			svcSleepThread(20000);
			GX_SetMemoryFill(gxCmdBuf, gpuOut, 0x404040FF, &gpuOut[0x2EE00], 0x201, gpuDOut, 0x00000000, &gpuDOut[0x2EE00], 0x201);
			gspWaitForPPF();
			gspWaitForPSC0();

			gspWaitForEvent(GSPEVENT_VBlank0, false);
			gfxSwapBuffersGpu();
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
	 
	exitemu = 1;
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
