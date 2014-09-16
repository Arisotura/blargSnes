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
#include <3ds.h>
#include "ui.h"


#define CONSOLE_MAX 20
char consolebuf[CONSOLE_MAX][33] = {{0}};
int consoleidx = 0;
int consoledirty = 0;


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
		while (buf[i] != '\0' && buf[i] != '\n' && j<32)
			consolebuf[consoleidx][j++] = buf[i++];
		consolebuf[consoleidx][j] = '\0';
		
		consoleidx++;
		if (consoleidx >= CONSOLE_MAX) consoleidx = 0;
		
		if (buf[i] == '\0' || buf[i+1] == '\0')
			break;
	}
	
	consoledirty = 2;
}

void emergency_printf(char* fmt, ...)
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
		while (buf[i] != '\0' && buf[i] != '\n' && j<32)
			consolebuf[consoleidx][j++] = buf[i++];
		consolebuf[consoleidx][j] = '\0';
		
		consoleidx++;
		if (consoleidx >= CONSOLE_MAX) consoleidx = 0;
		
		if (buf[i] == '\0' || buf[i+1] == '\0')
			break;
	}
	
	DrawConsole();
	//SwapBottomBuffers(0);
	//ClearBottomBuffer();
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
			DrawText(0, y, RGB(255,255,255), consolebuf[j]);
			y += 12;
		}
		
		j++;
		if (j >= CONSOLE_MAX) j = 0;
	}
}


void Console_Init()
{
	/*int i;
	
	for (i = 0; i < CONSOLE_MAX; i++)
		consolebuf[i][0] = '\0';
	
	consoleidx = 0;
	consoledirty = 2;*/
}

void Console_DeInit()
{
}

void Console_Render()
{
	if (!consoledirty) return;
	consoledirty--;
	
	ClearFramebuffer();
	DrawConsole();
}

void Console_ButtonPress(u32 btn)
{
	if (btn & (KEY_A|KEY_B))
	{
		UI_Switch(&UI_ROMMenu);
	}
}

void Console_Touch(bool touch, u32 x, u32 y)
{
	// TODO
}


UIController UI_Console = 
{
	Console_Init,
	Console_DeInit,
	
	Console_Render,
	Console_ButtonPress,
	Console_Touch
};
