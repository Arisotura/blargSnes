#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctr/types.h>
#include <ctr/srv.h>
#include <ctr/APT.h>
#include <ctr/GSP.h>
#include <ctr/GX.h>
#include <ctr/HID.h>
#include <ctr/FS.h>
#include <ctr/svc.h>
#include "font.h"


u8* TopFB[2];
u8* BottomFB[2];
u8* BottomFB0;

u8* gspHeap;
u32* gxCmdBuf;

u8 currentBuffer;
int curBottomBuffer;

Handle gspEvent, gspSharedMemHandle;

Handle fsuHandle;
FS_archive sdmcArchive;

u32 pad_cur, pad_last;


void gspGpuInit()
{
	gspInit();

	GSPGPU_AcquireRight(NULL, 0x0);
	GSPGPU_SetLcdForceBlack(NULL, 0x0);

	//grab main left screen framebuffer addresses
	GSPGPU_ReadHWRegs(NULL, 0x400468, (u32*)&TopFB, 8);
	GSPGPU_ReadHWRegs(NULL, 0x400568, (u32*)&BottomFB, 8);
	
	TopFB[0] += 0x7000000;
	TopFB[1] += 0x7000000;
	BottomFB[0] += 0x7000000;
	BottomFB[1] += 0x7000000;
	
	//BottomFB0 = BottomFB[0];

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
	
	BottomFB0 = &gspHeap[0x46800*4];

	//wait until we can write stuff to it
	svc_waitSynchronization1(gspEvent, 0x55bcb0);

	//GSP shared mem : 0x2779F000
	gxCmdBuf=(u32*)(0x10002000+0x800+threadID*0x200);

	currentBuffer=0;
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


void setBottomBuffer(int buf)
{
	u32 regData;
	GSPGPU_ReadHWRegs(NULL, 0x400578, &regData, 4);
	regData &= 0xFFFFFFFE;
	regData |= buf;
	GSPGPU_WriteHWRegs(NULL, 0x400578, &regData, 4);
	curBottomBuffer = buf;
}

void copyBottomBuffer()
{
	//curBottomBuffer ^= 1;
	
	//copy topleft FB
	u8 copiedBuffer=4+curBottomBuffer;
	u8* bufAdr=&gspHeap[0x46800*copiedBuffer];
	GSPGPU_FlushDataCache(NULL, bufAdr, 0x46500);

	GX_RequestDma(gxCmdBuf, (u32*)bufAdr, (u32*)BottomFB[curBottomBuffer], 0x46500);
	
	setBottomBuffer(curBottomBuffer);
}

void copyBuffer()
{
	//copy topleft FB
	u8 copiedBuffer=currentBuffer^1;
	u8* bufAdr=&gspHeap[0x46500*copiedBuffer];
	GSPGPU_FlushDataCache(NULL, bufAdr, 0x46500);

	GX_RequestDma(gxCmdBuf, (u32*)bufAdr, (u32*)TopFB[copiedBuffer], 0x46500);
}


void clearBottomBuffer()
{
	int x, y;
	
	for (y = 0; y < 240; y++)
	{
		for (x = 0; x < 320; x++)
		{
			int idx = (x*240) + (239-y);
			BottomFB0[idx*3+0] = 32;
			BottomFB0[idx*3+1] = 0;
			BottomFB0[idx*3+2] = 0;
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
		
		ptr = &font[(str[i]-0x20) << 4];
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
					BottomFB0[idx*3+0] = 255;
					BottomFB0[idx*3+1] = 255;
					BottomFB0[idx*3+2] = 255;
				}
			}
		}
		x += glyphsize;
		x++;
	}
}

void DrawUnicodeText(int x, int y, u16* str)
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
				if (val & (1 << cx))
				{
					int idx = ((x+cx)*240) + (239 - (y + cy));
					BottomFB0[idx*3+0] = 255;
					BottomFB0[idx*3+1] = 255;
					BottomFB0[idx*3+2] = 255;
				}
			}
		}
		x += glyphsize;
		x++;
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
					BottomFB0[idx*3+0] = 255;
					BottomFB0[idx*3+1] = 255;
					BottomFB0[idx*3+2] = 0;
				}
			}
		}
		x += glyphsize;
		x++;
	}
}



//u16* filelist;
u16 filelist[0x106 * 50]; // MAX 50 files -- DIRTY
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
	
	//filelist = (u16*)malloc(0x20C * nfiles);
	
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
		memcpy(&filelist[0x106 * i], &entry[0], 0x20C);
		*(u16*)&filelist[(0x106 * i) + 0x105] = 0;
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
	
	DrawText(0, 0, "blargSnes 1.0 - by StapleButter");
	
	y = 13;
	for (x = 0; x < 320; x++)
	{
		int idx = (x*240) + (239-y);
		BottomFB0[idx*3+0] = 255;
		BottomFB0[idx*3+1] = 255;
		BottomFB0[idx*3+2] = 0;
	}
	y++;
	for (x = 0; x < 320; x++)
	{
		int idx = (x*240) + (239-y);
		BottomFB0[idx*3+0] = 255;
		BottomFB0[idx*3+1] = 128;
		BottomFB0[idx*3+2] = 0;
	}
	y += 5;
	
	if ((nfiles - menuscroll) <= MENU_MAX) maxfile = (nfiles - menuscroll);
	else maxfile = MENU_MAX;
	
	for (i = 0; i < maxfile; i++)
	{
		if ((menuscroll+i) == menusel)
		{
			for (y2 = y; y2 < (y+12); y2++)
			{
				for (x = 0; x < 320; x++)
				{
					int idx = (x*240) + (239-y2);
					BottomFB0[idx*3+0] = 255;
					BottomFB0[idx*3+1] = 0;
					BottomFB0[idx*3+2] = 0;
				}
			}
		}
		
		DrawUnicodeText(3, y, &filelist[0x106 * (menuscroll+i)]);
		y += 12;
	}
}



int main() 
{
	int i, x, y;
	u64 FrameTime;
	
	initSrv();
		
	aptInit(APPID_APPLICATION);

	gspGpuInit();

	hidInit(NULL);
	pad_cur = pad_last = 0;

	srv_getServiceHandle(NULL, &fsuHandle, "fs:USER");
	FSUSER_Initialize(fsuHandle);
	
	clearBottomBuffer();
	setBottomBuffer(0);
	copyBottomBuffer();
	
	aptSetupEventHandler();
	
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
	
	
	u32 regData=0x01FF0000;
	GSPGPU_WriteHWRegs(NULL, 0x202204, &regData, 4);
	
	APP_STATUS status;
	while((status=aptGetStatus())!=APP_EXITING)
	{
		if(status==APP_RUNNING)
		{
			u64 t1 = svc_getSystemTick();
			clearBottomBuffer();
			
			pad_cur = hidSharedMem[0x28>>2];
			u32 press = pad_cur & ~pad_last;
			if (press & 0x40000040) // up
			{
				menusel--;
				if (menusel < 0) menusel = 0;
				if (menusel < menuscroll) menuscroll = menusel;
			}
			else if (press & 0x80000080) // down
			{
				menusel++;
				if (menusel > nfiles-1) menusel = nfiles-1;
				if (menusel-(MENU_MAX-1) > menuscroll) menuscroll = menusel-(MENU_MAX-1);
			}
			
			DrawROMList();
			copyBottomBuffer();
			
			pad_last = pad_cur;
			
			u64 t2 = svc_getSystemTick();
			svc_sleepThread(FrameTime - (t2-t1));
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
		}
	}
	
	//regData=0x010000FF;
	//GSPGPU_WriteHWRegs(NULL, 0x202204, &regData, 4);

	svc_closeHandle(fsuHandle);
	hidExit();
	gspGpuExit();
	aptExit();
	svc_exitProcess();

    return 0;
}
