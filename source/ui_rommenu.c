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

#include <3ds.h>
#include <string.h>
#include "ui.h"
#include "config.h"


extern FS_Archive sdmcArchive;

int nfiles;
int menusel = 0;
int menuscroll = 0;
#define MENU_MAX 18
int menudirty = 0;

int scrolltouch_y = 0;
int scrolling = 0;

int marquee_pos = 0;
int marquee_dir = 0;

struct LIST
{
	struct LIST * pNext;
	void * item;
};

struct LISTITEM
{
	char * name;
	int type;
};

struct LIST * head = NULL;
struct LIST * curr = NULL;
struct LISTITEM ** fileIdx;

char dirshort[0x40];


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

int itemcmp(void * first, void * second)
{
	struct LISTITEM * firstli = (struct LISTITEM *)first;
	struct LISTITEM * secondli = (struct LISTITEM *)second;
	if(firstli->type != secondli->type)
	{
		if(firstli->type > secondli->type)
			return -1;
		else
			return 1;
	}
	return strncasecmp(firstli->name, secondli->name, 0x105);
}

struct LIST * CreateList(void * item)
{
	struct LIST * ptr = (struct LIST*)MemAlloc(sizeof(struct LIST));
	if(ptr == NULL)
		return NULL;
	ptr->item = item;
	ptr->pNext = NULL;
	head = curr = ptr;
	return ptr;
}

struct LIST * AddToList(void * item)
{
	if(head == NULL)
		return CreateList(item);
	struct LIST * ptr = (struct LIST*)MemAlloc(sizeof(struct LIST));
	if(ptr == NULL)
		return NULL;
	ptr->item = item;
	ptr->pNext = NULL;

	curr->pNext = ptr;
	curr = ptr;

	return ptr;
}

void DeleteItem(void * item)
{
	struct LISTITEM * thisitem = (struct LISTITEM *)(item);
	MemFree(thisitem->name);
	MemFree(item);
}

void DeleteList()
{
	while(head != NULL)
	{
		curr = head;
		DeleteItem(curr->item);
		head = head->pNext;
		MemFree(curr);
	}
}


struct LIST * SortList(struct LIST * pList) {
    // zero or one element in list
    if(pList == NULL || pList->pNext == NULL)
        return pList;
    // head is the first element of resulting sorted list
    struct LIST * head = NULL;
    while(pList != NULL) {
        struct LIST * current = pList;
        pList = pList->pNext;
        if(head == NULL || (itemcmp(current->item, head->item) < 0)) {
			//current->iValue < head->iValue) {
            // insert into the head of the sorted list
            // or as the first element into an empty sorted list
            current->pNext = head;
            head = current;
        } else {
            // insert current element into proper position in non-empty sorted list
            struct LIST * p = head;
            while(p != NULL) {
                if(p->pNext == NULL || // last element of the sorted list
					(itemcmp(current->item, p->pNext->item) < 0))
                   //current->iValue < p->pNext->iValue) // middle of the list
                {
                    // insert into middle of the sorted list or as the last element
                    current->pNext = p->pNext;
                    p->pNext = current;
                    break; // done
                }
                p = p->pNext;
            }
        }
    }
    return head;
}


bool IsGoodFile(FS_DirectoryEntry* entry)
{
	if (entry->attributes & FS_ATTRIBUTE_DIRECTORY) return true;
	
	char* ext = (char*)entry->shortExt;
	if (strncmp(ext, "SMC", 3) && strncmp(ext, "SFC", 3)) return false;
	
	return true;
}

void DrawROMList()
{
	int i, x, y, y2;
	int maxfile;
	int menuy;
	
	DrawToolbar(dirshort);
	
	menuy = 26;
	y = menuy;
	
	if (nfiles < 1)
	{
		DrawText(3, y, RGB(255,64,64), "No ROMs found.");
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
			
			int textwidth = MeasureText(fileIdx[(menuscroll+i)]->name);
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
		
		DrawText(xoffset, y, (fileIdx[(menuscroll+i)]->type ? RGB(255,255,64) : RGB(255,255,255)), fileIdx[(menuscroll+i)]->name);
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
	FS_Path dirPath = (FS_Path){PATH_ASCII, strlen(Config.DirPath)+1, (u8*)Config.DirPath};
	FS_DirectoryEntry entry;
	int i;
	
	FSUSER_OpenDirectory(&dirHandle, sdmcArchive, dirPath);
	
	
	head = NULL;
	curr = NULL;

	if(strcmp(Config.DirPath,"/") == 0)
	{
		nfiles = 0;
	}
	else
	{
		struct LISTITEM * newItem= (struct LISTITEM *)MemAlloc(sizeof(struct LISTITEM));
		newItem->name = (char*)MemAlloc(0x106);
		strncpy(newItem->name, "/..", 0x105);
		newItem->type = 2;
		AddToList((void *)(newItem));
		nfiles = 1;
	}

	for (;;)
	{
		u32 nread = 0;
		FSDIR_Read(dirHandle, &nread, 1, &entry);
		if (!nread) break;
		if (!IsGoodFile(&entry)) continue;

		struct LISTITEM * newItem = (struct LISTITEM *)MemAlloc(sizeof(struct LISTITEM));
		newItem->name = (char*)MemAlloc(0x106);
		newItem->type = ((entry.attributes & FS_ATTRIBUTE_DIRECTORY) ? 1 : 0);
		if(newItem->type)
		{
			newItem->name[0] = '/';
			strncpy_u2a(&(newItem->name[1]), entry.name, 0x104);
		}
		else
			strncpy_u2a(newItem->name, entry.name, 0x105);
		AddToList((void *)(newItem));
		nfiles++;
	}
	FSDIR_Close(dirHandle);

	head = SortList(head);

	fileIdx = (char**)MemAlloc(nfiles * sizeof(char*));

	curr = head;
	for(i = 0; i < nfiles; i++)
	{
		fileIdx[i] = (struct LISTITEM *)(curr->item);
		curr = curr->pNext;
	}

	char * dirname = Config.DirPath;
	char * dirend = strrchr(dirname, '/');
	char dirdeep = 0;	
	strcpy(dirshort, "/\0");
	if(dirname < dirend)
	{
		char * curdir = dirname;
		while(MeasureText(dirname) > (256 - (dirdeep ? 22 : 0)))
		{
			if((curdir = strchr(&(dirname[1]), '/')) == dirend)
				break;
			dirdeep = 1;
			dirname = curdir;
		}
		if(dirdeep)
			strcat(dirshort, "../");
		if(MeasureText(dirname) > (256 - (dirdeep ? 22 : 0)))
		{
			strncat(dirshort, &(dirname[1]), 62 - (dirdeep ? 3 : 0));
			dirend = dirshort + strlen(dirshort) - 1;
			while(MeasureText(dirshort) > (241 - (dirdeep ? 22 : 0)))
			{
				dirend[0] = '\0';
				dirend--;
			}
			strcat(dirshort,"...");
		}
		else
			strcat(dirshort, &(dirname[1]));
	}
		
	menudirty = 2;
}

void ROMMenu_DeInit()
{
	MemFree(fileIdx);
	DeleteList();
}

void ROMMenu_Render(bool force)
{
	if (force) menudirty = 2;
	if (!menudirty) return;
	menudirty--;
	
	ClearFramebuffer();
	DrawROMList();
}

void ROMMenu_ExamineExec()
{
	if(!fileIdx[menusel]->type)
	{
		if (!StartROM(fileIdx[menusel]->name, Config.DirPath))
			bprintf("Failed to load this ROM\nPress A to return to menu\n");
	
		UI_Switch(&UI_Console);
	}
	else
	{
		
		if(fileIdx[menusel]->type == 2)
		{
			char* findpath = strrchr(Config.DirPath,'/');
			if(findpath != Config.DirPath)
			{
				findpath[0] = '\0';
				findpath = strrchr(Config.DirPath,'/');
				findpath[1] = '\0';
			}
		}
		else
		{
			strcat(Config.DirPath,&(fileIdx[menusel]->name[1]));
			strcat(Config.DirPath,"/");
		}
		menusel = 0;
		ROMMenu_DeInit();
		ROMMenu_Init();
		ROMMenu_Render(2);
	}
}

void ROMMenu_ButtonPress(u32 btn)
{
	if (btn & (KEY_A|KEY_B))
		ROMMenu_ExamineExec();
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
			ROMMenu_ExamineExec();

			marquee_pos = 0;
			marquee_dir = 0;
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
