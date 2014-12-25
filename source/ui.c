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

#define UISTACK_SIZE 16
UIController* UIStack[UISTACK_SIZE];
u32 UIStackPointer = 0;

extern int forceexit;

void UI_Switch(UIController* ui)
{
	if (CurrentUI)
		(*CurrentUI->DeInit)();
	
	CurrentUI = ui;
	(*CurrentUI->Init)();
}

void UI_SaveAndSwitch(UIController* ui)
{
	if (UIStackPointer >= UISTACK_SIZE)
	{
		bprintf("!! UI STACK FULL\n");
		return;
	}
	
	UIStack[UIStackPointer] = CurrentUI;
	UIStackPointer++;
	
	CurrentUI = ui;
	(*CurrentUI->Init)();
}

void UI_Restore()
{
	if (UIStackPointer < 1)
	{
		bprintf("!! UI STACK EMPTY\n");
		return;
	}
	
	(*CurrentUI->DeInit)();
	
	UIStackPointer--;
	CurrentUI = UIStack[UIStackPointer];
	(*CurrentUI->Render)(true);
}

int UI_Level()
{
	return UIStackPointer;
}


void UI_Render()
{
	(*CurrentUI->Render)(false);
}

void UI_ButtonPress(u32 btn)
{
	(*CurrentUI->ButtonPress)(btn);
}

void UI_Touch(int touch, u32 x, u32 y)
{
	(*CurrentUI->Touch)(touch, x, y);
}


u8* BottomFB;

#define CLAMP(x, min, max) if (x < min) x = min; else if (x > max) x = max;

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

void DrawRect(int x1, int x2, int y1, int y2, u32 color)
{
	int x, y;
	
	CLAMP(x1, 0, 319); CLAMP(x2, 0, 319);
	CLAMP(y1, 0, 239); CLAMP(y2, 0, 239);
	
	for (x = x1; x <= x2; x++)
	{
		DRAW_PIXEL(x, y1, color);
		DRAW_PIXEL(x, y2, color);
	}
	
	for (y = y1; y <= y2; y++)
	{
		DRAW_PIXEL(x1, y, color);
		DRAW_PIXEL(x2, y, color);
	}
}

void DrawRectOutline(int x1, int x2, int y1, int y2, u32 colorin, u32 colorout)
{
	int x, y;
	
	CLAMP(x1, 1, 318); CLAMP(x2, 1, 318);
	CLAMP(y1, 1, 238); CLAMP(y2, 1, 238);
	
	for (x = x1; x <= x2; x++)
	{
		DRAW_PIXEL(x, y1-1, colorout);
		DRAW_PIXEL(x, y1, colorin);
		DRAW_PIXEL(x, y2, colorin);
		DRAW_PIXEL(x, y2+1, colorout);
	}
	
	for (y = y1; y <= y2; y++)
	{
		DRAW_PIXEL(x1-1, y, colorout);
		DRAW_PIXEL(x1, y, colorin);
		DRAW_PIXEL(x2, y, colorin);
		DRAW_PIXEL(x2+1, y, colorout);
	}
}

void FillRect(int x1, int x2, int y1, int y2, u32 color)
{
	int x, y;
	
	CLAMP(x1, 0, 319); CLAMP(x2, 0, 319);
	CLAMP(y1, 0, 239); CLAMP(y2, 0, 239);
	
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

int MeasureText(char* str)
{
	unsigned short* ptr;
	unsigned short glyphsize;
	int i, x = 0;
	
	for (i = 0; str[i] != '\0'; i++)
	{
		if (str[i] == 0x20)
		{
			x += 6;
			continue;
		}
		
		u32 ch = str[i];
		if (ch < 0x10 || ch > 0x7E) ch = 0x7F;
		
		ptr = &font[(ch-0x10) << 4];
		glyphsize = ptr[0];
		if (!glyphsize)
		{
			x += 6;
			continue;
		}
		
		x++;
		x += glyphsize;
		x++;
	}
	
	return x;
}

void DrawText(int x, int y, u32 color, char* str)
{
	unsigned short* ptr;
	unsigned short glyphsize;
	int i, cx, cy;
	
	for (i = 0; str[i] != '\0'; i++)
	{
		if (str[i] == 0x20)
		{
			x += 6;
			continue;
		}
		
		u32 ch = str[i];
		if (ch < 0x10 || ch > 0x7E) ch = 0x7F;
		
		ptr = &font[(ch-0x10) << 4];
		glyphsize = ptr[0];
		if (!glyphsize)
		{
			x += 6;
			continue;
		}
		
		x++;
		for (cy = 0; cy < 12; cy++)
		{
			if ((y+cy) >= 240) break;
			
			unsigned short val = ptr[4+cy];
			
			for (cx = 0; cx < glyphsize; cx++)
			{
				if ((x+cx) < 0) continue;
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


#define TOOLBAR_HEIGHT 22
#define BTN_WIDTH 26

void DrawToolbar(char * dir)
{
	u32 x;
	u32 basey = (TOOLBAR_HEIGHT-12)/2;

	DrawText(basey, basey, RGB(255,255,255), dir);
	//DrawText(basey, basey, RGB(255,255,255), "blargSNES 1.2+ Git");
		
	FillRect(0, 319, TOOLBAR_HEIGHT, TOOLBAR_HEIGHT, RGB(0,255,255));
	FillRect(0, 319, TOOLBAR_HEIGHT+1, TOOLBAR_HEIGHT+1, RGB(0,128,255));
	
	// X button
	x = 320-BTN_WIDTH;
	FillRect(x, x, 0, TOOLBAR_HEIGHT-1, RGB(0,255,255));
	DrawText(x+((BTN_WIDTH-12)/2), basey, RGB(255,32,32), "\x10");
	
	// config button
	x = 320-(BTN_WIDTH*2);
	FillRect(x, x, 0, TOOLBAR_HEIGHT-1, RGB(0,255,255));
	DrawText(x+((BTN_WIDTH-12)/2), basey, RGB(32,255,255), "\x11");
}

bool HandleToolbar(u32 x, u32 y)
{
	if (y >= TOOLBAR_HEIGHT) return false;
	
	u32 btn1 = 320-(BTN_WIDTH*2);
	u32 btn2 = 320-BTN_WIDTH;
	
	if (x >= btn1 && x < btn2)
	{
		UI_SaveAndSwitch(&UI_Config);
		return true;
	}
	else if (x >= btn2)
	{
		forceexit = 1;
		return true;
	}
	
	return false;
}


void DrawButton(int x, int y, int width, u32 color, char* text)
{
	int txtwidth = MeasureText(text);
	if (!width) width = txtwidth + 6;
	
	if (x < 0) x += 320 - width;
	
	DrawRectOutline(x, x+width, y, y+18, RGB(0,255,255), RGB(0,128,255));
	DrawText(x+((width-txtwidth)/2), y+4, color, text);
}

void DrawCheckBox(int x, int y, u32 color, char* text, bool check)
{
	DrawRectOutline(x+1, x+18, y, y+17, RGB(0,255,255), RGB(0,128,255));
	if (check) DrawText(x+4, y+4, RGB(255,255,255), "X");
	DrawText(x+18+6, y+4, color, text);
}
