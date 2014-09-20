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

u32* BorderTex;
u32* MainScreenTex;
u32* SubScreenTex;

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
	8.0, 72.0, 0.5,      1.0, 0.125,
	232.0, 72.0, 0.5,    1.0, 1.0,
	232.0, 328.0, 0.5,  0, 1.0,
	
	8.0, 72.0, 0.5,      1.0, 0.125,
	232.0, 328.0, 0.5,  0, 1.0,
	8.0, 328.0, 0.5,    0, 0.125,
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

void myGPU_DrawArray(GPU_Primitive_t primitive, u32 n)
{
	/*GPUCMD_AddSingleParam(0x0002025E, primitive);
	GPUCMD_AddSingleParam(0x0002025F, 1);
	
	GPUCMD_AddSingleParam(0x000F0027, 0x80000000);
	GPUCMD_AddSingleParam(0x00010253, 1);
	GPUCMD_AddSingleParam(0x000F0228, n);
	GPUCMD_AddSingleParam(0x000F022A, 0);
	GPUCMD_AddSingleParam(0x00010245, 0);
	GPUCMD_AddSingleParam(0x000F022E, 1);
	GPUCMD_AddSingleParam(0x00010245, 1);
	GPUCMD_AddSingleParam(0x000F0321, 1);
	GPUCMD_AddSingleParam(0x0008025E, 0);
	GPUCMD_AddSingleParam(0x0008025E, 0);
	GPUCMD_AddSingleParam(0x00010253, 0);
	GPUCMD_AddSingleParam(0x000F0111, 1);
	
	//GPUCMD_AddSingleParam(0x00010080, 0);
	GPUCMD_AddSingleParam(0x000C02BA, 0x7FFF0000);
	//GPUCMD_AddSingleParam(0x000C028A, 0x7FFF0000);
	
	GPUCMD_AddSingleParam(0x000F0111, 0x00000001);
	GPUCMD_AddSingleParam(0x000F0110, 0x00000001);
	
	return;*/
	// //?
	// GPUCMD_AddSingleParam(0x00040080, 0x00010000);
	//set primitive type
	GPUCMD_AddSingleParam(0x0002025E, primitive);
	GPUCMD_AddSingleParam(0x0002025F, 0x00000001);
	//index buffer not used for drawArrays but 0x000F0227 still required
	GPUCMD_AddSingleParam(0x000F0227, 0x80000000);
	//pass number of vertices
	GPUCMD_AddSingleParam(0x000F0228, n);

	GPUCMD_AddSingleParam(0x00010253, 0x00000001);

	GPUCMD_AddSingleParam(0x00010245, 0x00000000);
	GPUCMD_AddSingleParam(0x000F022E, 0x00000001);
	GPUCMD_AddSingleParam(0x00010245, 0x00000001);
	GPUCMD_AddSingleParam(0x000F0231, 0x00000001);
	
	//GPUCMD_AddSingleParam(0x000F0111, 1);
	GPUCMD_AddSingleParam(0x000C02BA, 0x7FFF0000);
	//GPUCMD_AddSingleParam(0x000C028A, 0x7FFF0000);

	GPUCMD_AddSingleParam(0x000F0063, 0x00000001);
	GPUCMD_AddSingleParam(0x000F0111, 0x00000001);
	GPUCMD_AddSingleParam(0x000F0110, 0x00000001);
}

void myGPU_SetViewport(u32* depthBuffer, u32* colorBuffer, u32 x, u32 y, u32 w, u32 h)
{
	u32 param[0x4];
	float fw=(float)w;
	float fh=(float)h;
	
	/*000F0111 00000001
	000F0110 00000001
	000F0117 <colorbuffer format>
	000F011D <colorbuffer physaddr>>3>
	000F0116 <depthbuffer format>
	000F011C <depthbuffer physaddr>>3>
	000F011E <viewportshiz|0x01000000>
	000F006E <viewportshiz|same>
	glViewport*/

	GPUCMD_AddSingleParam(0x000F0111, 0x00000001);
	GPUCMD_AddSingleParam(0x000F0110, 0x00000001);

	u32 f116e=0x01000000|(((h-1)&0xFFF)<<12)|(w&0xFFF);

	param[0x0]=((u32)depthBuffer)>>3;
	param[0x1]=((u32)colorBuffer)>>3;
	param[0x2]=f116e;
	
	GPUCMD_AddSingleParam(0x000F0117, 0x00000002); //color buffer format
	GPUCMD_AddSingleParam(0x000F011D, param[1]);
	GPUCMD_AddSingleParam(0x000F0116, 0x00000003); //depth buffer format
	GPUCMD_AddSingleParam(0x000F011C, param[0]);
	GPUCMD_AddSingleParam(0x000F011E, param[2]);
	//GPUCMD_Add(0x800F011C, param, 0x00000003);
	//GPUCMD_AddSingleParam(0x000F011B, 0x00000000); //?
	GPUCMD_AddSingleParam(0x000F006E, f116e);

	param[0x0]=f32tof24(fw/2);
	param[0x1]=computeInvValue(fw);
	param[0x2]=f32tof24(fh/2);
	param[0x3]=computeInvValue(fh);
	GPUCMD_Add(0x800F0041, param, 0x00000004);

	GPUCMD_AddSingleParam(0x000F0068, (y<<16)|(x&0xFFFF));

	param[0x0]=0x00000000;
	param[0x1]=0x00000000;
	param[0x2]=((h-1)<<16)|((w-1)&0xFFFF);
	GPUCMD_Add(0x800F0065, param, 0x00000003);

	//enable depth buffer
	param[0x0]=0x00000000;
	param[0x1]=0x0000000F;
	param[0x2]=0x00000002;
	param[0x3]=0x00000002;
	GPUCMD_Add(0x800F0112, param, 0x00000004);
}

void GPU_SetTexture1(u32* data, u16 width, u16 height, u32 param, GPU_TEXCOLOR colorType)
{
	GPUCMD_AddSingleParam(0x000F0096, colorType);
	GPUCMD_AddSingleParam(0x000F0095, ((u32)data)>>3);
	GPUCMD_AddSingleParam(0x000F0092, (width)|(height<<16));
	GPUCMD_AddSingleParam(0x000F0093, param);
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

void doFrameBlarg()
{
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
	GPU_SetTexture((u32*)osConvertVirtToPhys((u32)BorderTex),256,512,0x6,GPU_RGBA8); // texture is actually 512x256
	
	//setup matrices
	setUniformMatrix(0x24, mvMatrix);
	setUniformMatrix(0x20, projMatrix);
	
	// note on the drawing process: GPU hangs if we attempt to draw an even number of arrays :/
	
	// border
	GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)borderVertices),
		GPU_ATTRIBFMT(0, 3, GPU_FLOAT)|GPU_ATTRIBFMT(1, 2, GPU_FLOAT),
		0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
	myGPU_DrawArray(GPU_TRIANGLES, 2*3);

	//return;
	
	// TEMP
	GPU_DepthRange(-1.0f, 0.0f);
	GPU_SetFaceCulling(GPU_CULL_BACK_CCW);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00);
	GPU_SetDepthTest(false, GPU_ALWAYS, 0x1F);
	GPUCMD_AddSingleParam(0x00010062, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0118, 0x00000000);
	GPUCMD_AddSingleParam(0x000F0100, 0x00000100);
	GPUCMD_AddSingleParam(0x000F0101, 0x01010000);
	//GPUCMD_AddSingleParam(0x00020100, 0x00000100);
	GPUCMD_AddSingleParam(0x000F0104, 0x00000010);
	
	//texturing stuff
	GPUCMD_AddSingleParam(0x0002006F, 0x00000300);	// enables/disables texcoord output
	GPUCMD_AddSingleParam(0x000F0080, 0x00011003); //enables/disables texturing
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
	/*GPU_SetTexEnv(4, 
		GPU_TEVSOURCES(GPU_PREVIOUS, GPU_TEXTURE2, 0), 
		GPU_TEVSOURCES(GPU_CONSTANT, 0, 0),
		GPU_TEVOPERANDS(0,2,0), 
		GPU_TEVOPERANDS(0,0,0), 
		GPU_MODULATE, 
		GPU_REPLACE, 
		0xFFFFFFFF);*/
	GPU_SetDummyTexEnv(4);
	// STAGE 6: dummy
	GPU_SetDummyTexEnv(5);
	

	// mainscreen
	GPU_SetAttributeBuffers(2, (u32*)osConvertVirtToPhys((u32)screenVertices),
		GPU_ATTRIBFMT(0, 3, GPU_FLOAT)|GPU_ATTRIBFMT(1, 2, GPU_FLOAT),
		0xFFC, 0x10, 1, (u32[]){0x00000000}, (u64[]){0x10}, (u8[]){2});
		
	GPU_SetTexture((u32*)osConvertVirtToPhys((u32)MainScreenTex),256,256,0x6,GPU_RGBA8);
	GPU_SetTexture1((u32*)osConvertVirtToPhys((u32)SubScreenTex),256,256,0x6,GPU_RGBA8);
	
	myGPU_DrawArray(GPU_TRIANGLES, 2*3);
	
	// subscreen
	myGPU_DrawArray(GPU_TRIANGLES, 2*3);
}



void CopyBitmapToTexture(u8* src, u32* dst, u32 width, u32 height, u32 alpha, u32 stride)
{
	int x, y;
	for (y = height-1; y >= 0; y--)
	{
		for (x = 0; x < width; x++)
		{
			u8 b = *src++;
			u8 g = *src++;
			u8 r = *src++;
			
			int di;
			di  = x & 0x1;
			di += (y & 0x1) << 1;
			di += (x & 0x2) << 1;
			di += (y & 0x2) << 2;
			di += (x & 0x4) << 2;
			di += (y & 0x4) << 3;
			di += (x & 0x1F8) << 3;
			di += ((y & 0xF8) << 3) * stride;
			
			dst[di] = alpha | (b << 8) | (g << 16) | (r << 24);
		}
	}
}

bool LoadBitmap(char* path, u32 width, u32 height, u32* dst, u32 alpha, u32 stride)
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
	
	CopyBitmapToTexture(buf, dst, width, height, alpha, stride);
	
	MemFree(buf, 0);
	return true;
}

bool LoadBorder(char* path)
{
	return LoadBitmap(path, 400, 240, BorderTex, 0xFF, 64);
}



Handle spcthread = NULL;
u8 spcthreadstack[0x400] __attribute__((aligned(8)));

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
		spcthread = NULL;
	}
	
	bprintf("ROM loaded, running...\n");

	CPU_Reset();
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
	APT_SetAppCpuTimeLimit(NULL, 30);
	aptCloseSession();

	gfxInit();
	hidInit(NULL);
	fsInit();
	
	GPU_Init(NULL);
	u32 gpuCmdSize = 0x40000;
	u32* gpuCmd = (u32*)linearAlloc(gpuCmdSize*4);
	GPU_Reset(gxCmdBuf, gpuCmd, gpuCmdSize);
	
	shader = SHDR_ParseSHBIN((u32*)blarg_shbin, blarg_shbin_size);
	
	GX_SetMemoryFill(gxCmdBuf, (u32*)gpuOut, 0x404040FF, (u32*)&gpuOut[0x2EE00], 0x201, (u32*)gpuDOut, 0x00000000, (u32*)&gpuDOut[0x2EE00], 0x201);
	gfxSwapBuffersGpu();
	
	UI_SetFramebuffer(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL));
	
	BorderTex = (u32*)linearAlloc(512*256*4);
	MainScreenTex = (u32*)linearAlloc(256*256*4);
	SubScreenTex = (u32*)linearAlloc(256*256*4);
	
	borderVertices = (float*)linearAlloc(5*3 * 2 * sizeof(float));
	screenVertices = (float*)linearAlloc(5*3 * 2 * sizeof(float));
	
	float* fptr = &vertexList[0];
	for (i = 0; i < 5*3*2; i++) borderVertices[i] = *fptr++;
	for (i = 0; i < 5*3*2; i++) screenVertices[i] = *fptr++;
	
	aptSetupEventHandler();
	

	sdmcArchive = (FS_archive){0x9, (FS_path){PATH_EMPTY, 1, (u8*)""}};
	FSUSER_OpenArchive(NULL, &sdmcArchive);
	
	if (!LoadBorder("/blargSnesBorder.bmp"))
		CopyBitmapToTexture(defaultborder, BorderTex, 400, 240, 0xFF, 64);
		
	CopyBitmapToTexture(screenfill, MainScreenTex, 256, 224, 0, 32);
	memset(SubScreenTex, 0, 256*256*4);
	
	UI_Switch(&UI_ROMMenu);
	
	svcCreateEvent(&SPCSync, 0);
	
	//int gg = 0;
	//for (gg = 0x1000; gg < 0x1001; gg++)
	/*u32 blargie=0;
	if (0)
	{
		u32 flag = 0;

		for (y = 0; y < 256; y++)
		{
			for (x = 0; x < 512; x++)
			{
				int idx = x+(y*512);

				if (x < 4)
				//if (y>=248)
					derpo[idx] = (y&4) ? 0x00FF00FF : 0xFF0000FF;
				else if (x >= 400-4)
					derpo[idx] = 0xFF00FFFF;
				else
					derpo[idx] = 0x0000FFFF | (y << 16); // blue/cyan
				
				//derpo[idx] = flag;
				//flag++;
			}
		}

		GSPGPU_FlushDataCache(NULL, derpo, 512*256*4);
		//GX_SetTextureCopy(gxCmdBuf, derpo, 0x01000200, BorderTex, 0x01000200, 512*256, 6); // bit12-15 = destination format?
		GX_SetDisplayTransfer(gxCmdBuf, derpo, 0x01000200, BorderTex, 0x01000200, 0x2);
		gspWaitForVBlank();
		gspWaitForVBlank();
		
		/*char path[256];
		sprintf(path, "/texcopy%04X.bin", gg);
		dbg_save(path, BorderTex, 512*256*4);*-/
		dbg_save("/derpcrapo.bin", BorderTex, 512*256*4);
	}*/

	
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
			
			GPUCMD_SetBuffer(gpuCmd, gpuCmdSize, 0);
			doFrameBlarg();
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
		else if(status == APP_SUSPENDING)
		{
			aptReturnToMenu();
		}
		else if(status == APP_PREPARE_SLEEPMODE)
		{
			//gspWaitForVBlank();g  
			aptSignalReadyForSleep();
			aptWaitStatusEvent();
			//gspWaitForVBlank();
		}
	}
	 
	exitemu = 1;
	if (spcthread) svcWaitSynchronization(spcthread, U64_MAX);
	
	linearFree(gpuCmd);

	fsExit();
	hidExit();
	gfxExit();
	aptExit();
	svcExitProcess();

    return 0;
}
