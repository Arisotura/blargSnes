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

#include <3ds.h>
#include "ui.h"


extern FS_archive sdmcArchive;

char* filelist;
int nfiles;
int menusel = 0;
int menuscroll = 0;
#define MENU_MAX 18
int menudirty = 0;

int scrolltouch_y = 0;
int scrolling = 0;

int marquee_pos = 0;
int marquee_dir = 0;


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


bool IsGoodFile(FS_dirent* entry)
{
	if (entry->isDirectory) return false;
	
	char* ext = (char*)entry->shortExt;
	if (strncmp(ext, "SMC", 3) && strncmp(ext, "SFC", 3)) return false;
	
	return true;
}

void DrawROMList()
{
	int i, x, y, y2;
	int maxfile;
	int menuy;
	
	DrawToolbar();
	
	menuy = 26;
	y = menuy;
	
	if (nfiles < 1)
	{
		DrawText(3, y, RGB(255,64,64), "No ROMs found in /snes");
		return;
	}
	
	if ((nfiles - menuscroll) <= MENU_MAX) maxfile = (nfiles - menuscroll);
	else maxfile = MENU_MAX;
	
	for (i = 0; i < maxfile; i++)
	{
		int xoffset = 3;
		
		// blue highlight for the selected ROM
		if ((menuscroll+i) == menusel)
		{
			FillRect(0, 319, y, y+11, RGB(0,0,255));
			
			int textwidth = MeasureText(&filelist[0x106 * (menuscroll+i)]);
			int maxwidth = (nfiles>MENU_MAX) ? 308:320;
			if (textwidth > maxwidth)
			{
				maxwidth -= 6;
				if (!marquee_dir)
				{
					marquee_pos -= 1;
					if (marquee_pos < -(textwidth-maxwidth)) 
					{
						marquee_pos = -(textwidth-maxwidth) - 1;
						marquee_dir = 1;
					}
				}
				else
				{
					marquee_pos += 1;
					if (marquee_pos >= 0) 
					{
						marquee_pos = 0;
						marquee_dir = 0;
					}
				}
				
				// force refreshing
				menudirty++;
			}
			else
			{
				marquee_dir = 0;
				marquee_pos = 0;
			}
			
			xoffset += marquee_pos;
		}
		
		DrawText(xoffset, y, RGB(255,255,255), &filelist[0x106 * (menuscroll+i)]);
		y += 12;
	}
	
	// scrollbar
	if (nfiles > MENU_MAX)
	{
		int shownheight = 240-menuy;
		int fullheight = 12*nfiles;
		
		int sbheight = (shownheight * shownheight) / fullheight;
		if (sbheight < 10) sbheight = 10;
		
		int sboffset = (menuscroll * 12 * shownheight) / fullheight;
		if ((sboffset+sbheight) > shownheight)
			sboffset = shownheight-sbheight;
			
		FillRect(308, 319, menuy, 239, RGB(0,0,64));
		FillRect(308, 319, menuy+sboffset, menuy+sboffset+sbheight-1, RGB(0,255,255));
	}
}


void ROMMenu_Init()
{
	Handle dirHandle;
	FS_path dirPath = (FS_path){PATH_CHAR, 6, (u8*)"/snes"};
	FS_dirent entry;
	int i;
	
	FSUSER_OpenDirectory(NULL, &dirHandle, sdmcArchive, dirPath);
	nfiles = 0;
	for (;;)
	{
		u32 nread = 0;
		FSDIR_Read(dirHandle, &nread, 1, &entry);
		if (!nread) break;
		if (!IsGoodFile(&entry)) continue;
		nfiles++;
	}
	FSDIR_Close(dirHandle);

	filelist = (char*)MemAlloc(0x106 * nfiles);
	
	// TODO: find out how to rewind it rather than reopening it?
	FSUSER_OpenDirectory(NULL, &dirHandle, sdmcArchive, dirPath);
	i = 0;
	for (;;)
	{
		u32 nread = 0;
		FSDIR_Read(dirHandle, &nread, 1, &entry);
		if (!nread) break;
		if (!IsGoodFile(&entry)) continue;
		
		// dirty way to copy an Unicode string
		strncpy_u2a(&filelist[0x106 * i], entry.name, 0x105);
		i++;
	}
	FSDIR_Close(dirHandle);
	
	// TODO: alphabetical sorting maybe
	
	menudirty = 2;
}

void ROMMenu_DeInit()
{
	MemFree(filelist);
}

void ROMMenu_Render(bool force)
{
	if (force) menudirty = 2;
	if (!menudirty) return;
	menudirty--;
	
	ClearFramebuffer();
	DrawROMList();
}

void ROMMenu_ButtonPress(u32 btn)
{
	if (btn & (KEY_A|KEY_B))
	{
		if (!StartROM(&filelist[0x106*menusel]))
			bprintf("Failed to load this ROM\nPress A to return to menu\n");
			
		UI_Switch(&UI_Console);
	}
	else if (btn & KEY_UP) // up
	{
		menusel--;
		if (menusel < 0) 
		{
			menusel = nfiles - 1;
			//menuscroll = menusel-(MENU_MAX-1);
		}
		if (menusel < menuscroll) menuscroll = menusel;
		if (menusel-(MENU_MAX-1) > menuscroll) menuscroll = menusel-(MENU_MAX-1);
		
		menudirty = 2;
	}
	else if (btn & KEY_DOWN) // down
	{
		menusel++;
		if (menusel > nfiles-1) 
		{
			menusel = 0;
			//menuscroll = menusel;
		}
		if (menusel < menuscroll) menuscroll = menusel;
		if (menusel-(MENU_MAX-1) > menuscroll) menuscroll = menusel-(MENU_MAX-1);
		
		menudirty = 2;
	}
	else if (btn & KEY_LEFT) // left
	{
		menusel -= MENU_MAX;
		if (menusel < 0) menusel = 0;
		if (menusel < menuscroll) menuscroll = menusel;
		if (menusel-(MENU_MAX-1) > menuscroll) menuscroll = menusel-(MENU_MAX-1);
		
		menudirty = 2;
	}
	else if (btn & KEY_RIGHT) // right
	{
		menusel += MENU_MAX;
		if (menusel > nfiles-1) menusel = nfiles - 1;
		if (menusel < menuscroll) menuscroll = menusel;
		if (menusel-(MENU_MAX-1) > menuscroll) menuscroll = menusel-(MENU_MAX-1);
		
		menudirty = 2;
	}
	
	marquee_pos = 0;
	marquee_dir = 0;
}

void ROMMenu_Touch(int touch, u32 x, u32 y)
{
	int menuy = 26;
	
	if (y >= menuy)
	{
		if (x < 308 && touch == 0)
		{
			y -= menuy;
			y /= 12;
			
			y += menuscroll;
			if (y >= nfiles) return;
			
			menusel = y;
			if (!StartROM(&filelist[0x106*menusel]))
				bprintf("Failed to load this ROM\nPress A to return to menu\n");
				
			UI_Switch(&UI_Console);
		}
		/*else
		{
			if (touch == 0)
				scrolling = 0;
			else if (touch == 1)
			{
				scrolling = 1;
				scrolltouch_y = y;
			}
			else
			{
				int dy = y - scrolltouch_y;
				scrolltouch_y = y;
				
				int shownheight = 240-menuy;
				int fullheight = 12*nfiles;
				
				int sboffset = (menuscroll * 12 * shownheight) / fullheight;
				sboffset += dy;
				
				menuscroll = (sboffset * fullheight) / (12 * shownheight);
				if (menuscroll < 0) menuscroll = 0;
				else if (menuscroll >= nfiles) menuscroll = nfiles-1;
				
				if (menusel < menuscroll) menusel = menuscroll;
				else if (menusel >= menuscroll+MENU_MAX) menusel = menuscroll+MENU_MAX-1;
			}
			
			menudirty = 2;
		}*/
	}
	else if (touch == 0)
		HandleToolbar(x, y);
}


UIController UI_ROMMenu = 
{
	ROMMenu_Init,
	ROMMenu_DeInit,
	
	ROMMenu_Render,
	ROMMenu_ButtonPress,
	ROMMenu_Touch
};
