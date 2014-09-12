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

// ui.c -- generic bottomscreen UI functions

#include <3ds.h>
#include "ui.h"
#include "font.h"


UIController* CurrentUI = NULL;

void UI_Switch(UIController* ui)
{
	if (CurrentUI)
		(*CurrentUI->DeInit)();
	
	CurrentUI = ui;
	(*CurrentUI->Init)();
}

void UI_Render()
{
	(*CurrentUI->Render)();
}

void UI_ButtonPress(u32 btn)
{
	(*CurrentUI->ButtonPress)(btn);
}

void UI_Touch(bool touch, u32 x, u32 y)
{
	(*CurrentUI->Touch)(touch, x, y);
}


u8* BottomFB;

#define DRAW_PIXEL(x, y, color) \
	{ \
		int idx = ((x)*240) + (239-(y)); \
		BottomFB[idx*3+0] = (color); \
		BottomFB[idx*3+1] = (color) >> 8; \
		BottomFB[idx*3+2] = (color) >> 16; \
	} 

	
void UI_SetFramebuffer(u8* buffer)
{
	BottomFB = buffer;
}

void FillRect(int x1, int x2, int y1, int y2, u32 color)
{
	int x, y;
	
	for (y = y1; y <= y2; y++)
	{
		for (x = x1; x <= x2; x++)
		{
			DRAW_PIXEL(x, y, color);
		}
	}
}

void ClearFramebuffer()
{
	FillRect(0, 319, 0, 239, RGB(0,0,32));
}

void DrawText(int x, int y, u32 color, char* str)
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
					DRAW_PIXEL(x+cx, y+cy, color);
			}
		}
		x += glyphsize;
		x++;
		if (x >= 320) break;
	}
}
