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
#include <stdarg.h>
#include <ctr/types.h>
#include <ctr/srv.h>
#include <ctr/APT.h>
#include <ctr/GSP.h>
#include <ctr/GX.h>
#include <ctr/HID.h>
#include <ctr/FS.h>
#include <ctr/svc.h>

#include "font.h"
#include "logo.h"
#include "mem.h"


u8* TopFBAddr[2];
u8* BottomFBAddr[2];
u8* TopFB;
u8* BottomFB;

#define TOPBUF_SIZE 0x2EE00
#define BOTTOMBUF_SIZE 0x38400

u8* gspHeap;
u32* gxCmdBuf;

int curTopBuffer, curBottomBuffer;

Handle gspEvent, gspSharedMemHandle;

Handle fsuHandle;
FS_archive sdmcArchive;

u32 pad_cur, pad_last;


#define CONSOLE_MAX 20
char consolebuf[20][33];
int consoleidx = 0;
int showconsole = 0;

int running = 0;
int pause = 0;


void strncpy_u2a(char* dst, u16* src, int n)
{
	int i = 0;
	while (i < n && src[i] != '\0')
	{
		if (src[i] & 0xFF00)
			dst[i] = 0x7F;
		else
			dst[i] = (char)src[i];
		
		i++;
	}
	
	dst[i] = '\0';
}

// REMOVEME
void derp_divmod(int num, int den, int* quo, int* rem)
{
	if (den == 0)
	{
		if (quo) *quo = 0xFFFFFFFF;
		if (rem) *rem = num;
		return;
	}
	
	int i = 0;
	while (num >= den)
	{
		num -= den;
		i++;
	}
	
	if (quo) *quo = i;
	if (rem) *rem = num;
}


void gspGpuInit()
{
	u32 regval;
	gspInit();

	GSPGPU_AcquireRight(NULL, 0x0);
	GSPGPU_SetLcdForceBlack(NULL, 0x0);

	//grab main left screen framebuffer addresses
	GSPGPU_ReadHWRegs(NULL, 0x400468, (u32*)&TopFBAddr, 8);
	GSPGPU_ReadHWRegs(NULL, 0x400568, (u32*)&BottomFBAddr, 8);
	
	GSPGPU_WriteHWRegs(NULL, 0x400494, (u32*)&TopFBAddr, 8);
	
	GSPGPU_ReadHWRegs(NULL, 0x400470, &regval, 4);
	regval &= 0xFFFFFFF8;
	regval |= 3; // 15bit color
	GSPGPU_WriteHWRegs(NULL, 0x400470, &regval, 4);
	regval = 480;
	GSPGPU_WriteHWRegs(NULL, 0x400490, &regval, 4);
	
	TopFBAddr[0] += 0x7000000;
	TopFBAddr[1] += 0x7000000;
	BottomFBAddr[0] += 0x7000000;
	BottomFBAddr[1] += 0x7000000;
	
	//BottomFB = BottomFBAddr[0];

	//convert PA to VA (assuming FB in VRAM)
	/*topLeftFramebuffers[0]+=0x7000000;
	topLeftFramebuffers[1]+=0x7000000;*/

	//setup our gsp shared mem section
	u8 threadID;
	svc_createEvent(&gspEvent, 0x0);
	GSPGPU_RegisterInterruptRelayQueue(NULL, gspEvent, 0x1, &gspSharedMemHandle, &threadID);
	svc_mapMemoryBlock(gspSharedMemHandle, 0x10002000, 0x3, 0x10000000);

	//map GSP heap
	svc_controlMemory((u32*)&gspHeap, 0x0, 0x0, 0x2000000, 0x10003, 0x3);
	
	TopFB = &gspHeap[0];
	BottomFB = &gspHeap[TOPBUF_SIZE];

	//wait until we can write stuff to it
	svc_waitSynchronization1(gspEvent, 0x55bcb0);

	//GSP shared mem : 0x2779F000
	gxCmdBuf=(u32*)(0x10002000+0x800+threadID*0x200);
}

void gspGpuExit()
{
	GSPGPU_UnregisterInterruptRelayQueue(NULL);

	//unmap GSP shared mem
	svc_unmapMemoryBlock(gspSharedMemHandle, 0x10002000);
	svc_closeHandle(gspSharedMemHandle);
	svc_closeHandle(gspEvent);
	
	gspExit();

	//free GSP heap
	svc_controlMemory((u32*)&gspHeap, (u32)gspHeap, 0x0, 0x2000000, MEMOP_FREE, 0x0);
}


void CopyTopBuffer()
{
	GSPGPU_FlushDataCache(NULL, TopFB, TOPBUF_SIZE);
	GX_RequestDma(gxCmdBuf, (u32*)TopFB, (u32*)TopFBAddr[curTopBuffer], TOPBUF_SIZE);
}

void CopyBottomBuffer()
{
	GSPGPU_FlushDataCache(NULL, BottomFB, BOTTOMBUF_SIZE);
	GX_RequestDma(gxCmdBuf, (u32*)BottomFB, (u32*)BottomFBAddr[curBottomBuffer], BOTTOMBUF_SIZE);
}

void SwapTopBuffers(int doswap)
{
	CopyTopBuffer();
	
	u32 regData;
	GSPGPU_ReadHWRegs(NULL, 0x400478, &regData, 4);
	regData &= 0xFFFFFFFE;
	regData |= curTopBuffer;
	GSPGPU_WriteHWRegs(NULL, 0x400478, &regData, 4);
	
	if (doswap) curTopBuffer ^= 1;
}

void SwapBottomBuffers(int doswap)
{
	CopyBottomBuffer();
	
	u32 regData;
	GSPGPU_ReadHWRegs(NULL, 0x400578, &regData, 4);
	regData &= 0xFFFFFFFE;
	regData |= curBottomBuffer;
	GSPGPU_WriteHWRegs(NULL, 0x400578, &regData, 4);
	
	if (doswap) curBottomBuffer ^= 1;
}


void ClearBottomBuffer()
{
	int x, y;
	
	for (y = 0; y < 240; y++)
	{
		for (x = 0; x < 320; x++)
		{
			int idx = (x*240) + (239-y);
			BottomFB[idx*3+0] = 32;
			BottomFB[idx*3+1] = 0;
			BottomFB[idx*3+2] = 0;
		}
	}
}

void DrawText(int x, int y, char* str)
{
	unsigned short* ptr;
	unsigned short glyphsize;
	int i, cx, cy;
	
	for (i = 0; str[i] != '\0'; i++)
	{
		if (str[i] < 0x21)
		{
			x += 6;
			continue;
		}
		
		u16 ch = str[i];
		if (ch > 0x7E) ch = 0x7F;
		
		ptr = &font[(ch-0x20) << 4];
		glyphsize = ptr[0];
		if (!glyphsize)
		{
			x += 6;
			continue;
		}
		
		x++;
		for (cy = 0; cy < 12; cy++)
		{
			unsigned short val = ptr[4+cy];
			
			for (cx = 0; cx < glyphsize; cx++)
			{
				if ((x+cx) >= 320) break;
				
				if (val & (1 << cx))
				{
					int idx = ((x+cx)*240) + (239 - (y + cy));
					BottomFB[idx*3+0] = 255;
					BottomFB[idx*3+1] = 255;
					BottomFB[idx*3+2] = 255;
				}
			}
		}
		x += glyphsize;
		x++;
		if (x >= 320) break;
	}
}

void DrawHex(int x, int y, unsigned int n)
{
	unsigned short* ptr;
	unsigned short glyphsize;
	int i, cx, cy;
	
	for (i = 0; i < 8; i++)
	{
		int digit = (n >> (28-(i*4))) & 0xF;
		int lolz;
		if (digit < 10) lolz = digit+'0';
		else lolz = digit+'A'-10;
		
		ptr = &font[(lolz-0x20) << 4];
		glyphsize = ptr[0];
		if (!glyphsize)
		{
			x += 6;
			continue;
		}
		
		x++;
		for (cy = 0; cy < 12; cy++)
		{
			unsigned short val = ptr[4+cy];
			
			for (cx = 0; cx < glyphsize; cx++)
			{
				if (val & (1 << cx))
				{
					int idx = ((x+cx)*240) + (239 - (y + cy));
					BottomFB[idx*3+0] = 255;
					BottomFB[idx*3+1] = 255;
					BottomFB[idx*3+2] = 0;
				}
			}
		}
		x += glyphsize;
		x++;
	}
}



char* filelist;
int nfiles;

bool IsGoodFile(u8* entry)
{
	if (entry[0x21E] != 0x01) return false;
	
	char* ext = (char*)&entry[0x216];
	if (strncmp(ext, "SMC", 3) && strncmp(ext, "SFC", 3)) return false;
	
	return true;
}

void LoadROMList()
{
	Handle dirHandle;
	FS_path dirPath = (FS_path){PATH_CHAR, 6, (u8*)"/snes"};
	u8 entry[0x228];
	int i;
	
	FSUSER_OpenDirectory(fsuHandle, &dirHandle, sdmcArchive, dirPath);
	nfiles = 0;
	for (;;)
	{
		u32 nread = 0;
		FSDIR_Read(dirHandle, &nread, 1, (u16*)entry);
		if (!nread) break;
		if (!IsGoodFile(entry)) continue;
		nfiles++;
	}
	FSDIR_Close(dirHandle);

	filelist = (char*)MemAlloc(0x106 * nfiles);
	
	// TODO: find out how to rewind it rather than reopening it?
	FSUSER_OpenDirectory(fsuHandle, &dirHandle, sdmcArchive, dirPath);
	i = 0;
	for (;;)
	{
		u32 nread = 0;
		FSDIR_Read(dirHandle, &nread, 1, (u16*)entry);
		if (!nread) break;
		if (!IsGoodFile(entry)) continue;
		
		// dirty way to copy an Unicode string
		strncpy_u2a(&filelist[0x106 * i], (u16*)&entry[0], 0x105);
		i++;
	}
	FSDIR_Close(dirHandle);
}

int menusel = 0;
int menuscroll = 0;
#define MENU_MAX 18

void DrawROMList()
{
	int i, x, y, y2;
	int maxfile;
	int menuy;
	
	DrawText(0, 0, "blargSnes 1.0 - by StapleButter");
	
	y = 13;
	for (x = 0; x < 320; x++)
	{
		int idx = (x*240) + (239-y);
		BottomFB[idx*3+0] = 255;
		BottomFB[idx*3+1] = 255;
		BottomFB[idx*3+2] = 0;
	}
	y++;
	for (x = 0; x < 320; x++)
	{
		int idx = (x*240) + (239-y);
		BottomFB[idx*3+0] = 255;
		BottomFB[idx*3+1] = 128;
		BottomFB[idx*3+2] = 0;
	}
	y += 5;
	menuy = y;
	
	if ((nfiles - menuscroll) <= MENU_MAX) maxfile = (nfiles - menuscroll);
	else maxfile = MENU_MAX;
	
	for (i = 0; i < maxfile; i++)
	{
		// blue highlight for the selected ROM
		if ((menuscroll+i) == menusel)
		{
			for (y2 = y; y2 < (y+12); y2++)
			{
				for (x = 0; x < 320; x++)
				{
					int idx = (x*240) + (239-y2);
					BottomFB[idx*3+0] = 255;
					BottomFB[idx*3+1] = 0;
					BottomFB[idx*3+2] = 0;
				}
			}
		}
		
		DrawText(3, y, &filelist[0x106 * (menuscroll+i)]);
		y += 12;
	}
	
	// scrollbar
	if (nfiles > MENU_MAX)
	{
		int shownheight = 240-menuy;
		int fullheight = 12*nfiles;
		
		int sbheight = shownheight * shownheight;
		derp_divmod(sbheight, fullheight, &sbheight, NULL);
		
		int sboffset = menuscroll * 12 * shownheight;
		derp_divmod(sboffset, fullheight, &sboffset, NULL);
		if ((sboffset+sbheight) > shownheight)
			sboffset = shownheight-sbheight;
		
		for (y = menuy; y < 240; y++)
		{
			for (x = 308; x < 320; x++)
			{
				int idx = (x*240) + (239-y);
				
				if (y >= sboffset+menuy && y <= sboffset+menuy+sbheight)
				{
					BottomFB[idx*3+0] = 255;
					BottomFB[idx*3+1] = 255;
					BottomFB[idx*3+2] = 0;
				}
				else
				{
					BottomFB[idx*3+0] = 64;
					BottomFB[idx*3+1] = 0;
					BottomFB[idx*3+2] = 0;
				}
			}
		}
	}
}



void bprintf(char* fmt, ...)
{
	char buf[256];
	va_list args;
	
	va_start(args, fmt);
	vsnprintf(buf, 255, fmt, args);
	va_end(args);
	
	int i = 0, j = 0;
	for (;;)
	{
		j = 0;
		while (buf[i] != '\0' && buf[i] != '\n' && j<33)
			consolebuf[consoleidx][j++] = buf[i++];
		consolebuf[consoleidx][j] = '\0';
		
		consoleidx++;
		if (consoleidx >= CONSOLE_MAX) consoleidx = 0;
		
		if (buf[i] == '\0' || buf[i+1] == '\0')
			break;
	}
}

void DrawConsole()
{
	int i, j, y;
	
	y = 0;
	j = consoleidx;
	for (i = 0; i < CONSOLE_MAX; i++)
	{
		if (consolebuf[j][0] != '\0')
		{
			DrawText(0, y, consolebuf[j]);
			y += 12;
		}
		
		j++;
		if (j >= CONSOLE_MAX) j = 0;
	}
}



void CPUThread(u32 blarg)
{
	u32 regData=0x0100FF00;
	GSPGPU_WriteHWRegs(NULL, 0x202204, &regData, 4);
	
	bprintf("ROM loaded, running...\n");
	CPU_Reset();

	CPU_Run();
}


// return val: 1=continue running
int PostEmuFrame()
{
	asm("stmdb sp!, {r12}");
	
	APP_STATUS status = aptGetStatus();
	if (status == APP_EXITING)
	{
		asm("ldmia sp!, {r12}");
		return 0;
	}
	else if(status == APP_SUSPENDING)
	{
		aptReturnToMenu();
	}
	else if(status == APP_SLEEPMODE)
	{
		aptWaitStatusEvent();
	}
	
	// rudimentary way to pause
	if (hidSharedMem[0xCC>>2])
	{
		pause = 1;
		bprintf("pause\n");
		asm("ldmia sp!, {r12}");
		return 0;
	}
	
	DrawConsole();
	SwapBottomBuffers(0);
	ClearBottomBuffer();
	
	// TODO: VSYNC
	
	asm("ldmia sp!, {r12}");
	return 1;
}

void debugcrapo(u32 op, u32 op2)
{
	asm("stmdb sp!, {r0-r3, r12}");
	
	bprintf("DBG: %08X %08X\n", op, op2);
	DrawConsole();
	SwapBottomBuffers(0);
	ClearBottomBuffer();
	
	asm("ldmia sp!, {r0-r3, r12}");
}



char temppath[300];

int main() 
{
	int i, x, y;
	u64 FrameTime;
	
	Handle cputhread;
	u8* cputhreadstack;
	
	for (i = 0; i < CONSOLE_MAX; i++)
		consolebuf[i][0] = '\0';
	
	initSrv();
		
	aptInit(APPID_APPLICATION);

	gspGpuInit();

	hidInit(NULL);
	pad_cur = pad_last = 0;

	srv_getServiceHandle(NULL, &fsuHandle, "fs:USER");
	FSUSER_Initialize(fsuHandle);
	
	curTopBuffer = 0;
	curBottomBuffer = 0;
	ClearBottomBuffer();
	SwapBottomBuffers(0);
	
	for (y = 0; y < 240; y++)
	{
		for (x = 0; x < 400; x++)
		{
			int idx = (x*240) + (239-y);
			((u16*)TopFB)[idx] = logo[(y*400)+x];
		}
	}
	SwapTopBuffers(0);
	
	aptSetupEventHandler();
	
	FrameTime = 0ULL;
	for (i = 0; i < 16; i++)
	{
		u64 t1 = svc_getSystemTick();
		svc_sleepThread(16666667);
		u64 t2 = svc_getSystemTick();
		FrameTime += (t2-t1);
	}
	FrameTime >>= 4ULL;
	

	sdmcArchive = (FS_archive){0x9, (FS_path){PATH_EMPTY, 1, (u8*)""}};
	FSUSER_OpenArchive(fsuHandle, &sdmcArchive);
	
	LoadROMList();
	DrawROMList();
	SwapBottomBuffers(0);

	//u32 regData=0x01FF0000;
	//GSPGPU_WriteHWRegs(NULL, 0x202204, &regData, 4);
	
	cputhreadstack = MemAlloc(0x4000); // should be good enough for a stack
	
	APP_STATUS status;
	while((status=aptGetStatus())!=APP_EXITING)
	{
		if(status==APP_RUNNING)
		{
			//u64 t1 = svc_getSystemTick();
			ClearBottomBuffer();
			
			pad_cur = hidSharedMem[0x28>>2];
			u32 press = pad_cur & ~pad_last;
			
			if (!running)
			{
				if (press & (PAD_A|PAD_B))
				{
					if (!showconsole)
					{
						showconsole = 1;
						bprintf("blargSnes console\n");

						// load the ROM
						strncpy(temppath, "/snes/", 6);
						strncpy(&temppath[6], &filelist[0x106*menusel], 0x106);
						temppath[6+0x106] = '\0';
						bprintf("Loading %s...\n", temppath);
						
						if (!SNES_LoadROM(temppath))
							bprintf("Failed to load this ROM\nPress A to return to menu\n");
						else
						{
							//Result res = svc_createThread(&cputhread, &CPUThread, 0, cputhreadstack+0x4000, 16, 0);
							running = 1;
							
							bprintf("ROM loaded, running...\n");

							CPU_Reset();
							CPU_Run();
						}
					}
					else
						showconsole = 0;
				}
				else if (pad_cur & 0x40000040) // up
				{
					menusel--;
					if (menusel < 0) menusel = 0;
					if (menusel < menuscroll) menuscroll = menusel;
				}
				else if (pad_cur & 0x80000080) // down
				{
					menusel++;
					if (menusel > nfiles-1) menusel = nfiles-1;
					if (menusel-(MENU_MAX-1) > menuscroll) menuscroll = menusel-(MENU_MAX-1);
				}
			}
			else if (pause)
			{
				if (press & (PAD_A|PAD_B))
				{
					pause = 0;
					bprintf("resume\n");
					CPU_Run();
				}
			}
			
			if (showconsole) 
				DrawConsole();
			else
				DrawROMList();
				
			SwapBottomBuffers(0);
			
			pad_last = pad_cur;
			
			//u64 t2 = svc_getSystemTick();
			//svc_sleepThread(FrameTime - (t2-t1));
			svc_sleepThread(150 * 1000 * 1000);
		}
		else if(status == APP_SUSPENDING)
		{
			//regData=0x0100FF00;
			//GSPGPU_WriteHWRegs(NULL, 0x202204, &regData, 4);
			aptReturnToMenu();
		}
		else if(status == APP_SLEEPMODE)
		{
			//regData=0x0100FFFF;
			//GSPGPU_WriteHWRegs(NULL, 0x202204, &regData, 4);
			aptWaitStatusEvent();
			
			// nope, this isn't how you fix the lost buffers after closing/reopening the 3DS :(
			/*GSPGPU_WriteHWRegs(NULL, 0x400468, (u32*)&TopFBAddr, 8);
			GSPGPU_WriteHWRegs(NULL, 0x400494, (u32*)&TopFBAddr, 8);
			GSPGPU_WriteHWRegs(NULL, 0x400568, (u32*)&BottomFBAddr, 8);
			SwapTopBuffers(0);
			SwapBottomBuffers(0);*/
		}
	}
	
	//regData=0x010000FF;
	//GSPGPU_WriteHWRegs(NULL, 0x202204, &regData, 4);
	
	MemFree(cputhreadstack, 0x4000);
	
	MemFree(filelist, 0x106*nfiles);

	svc_closeHandle(fsuHandle);
	hidExit();
	gspGpuExit();
	aptExit();
	svc_exitProcess();

    return 0;
}

void* malloc(size_t size) { return NULL; }
void free(void* ptr) {}
