/*
    Copyright 2014-2015 StapleButter

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

#ifndef _UI_H_
#define _UI_H_

#include <3ds/types.h>

#define RGB(r,g,b) ((b) | ((g) << 8) | ((r) << 16))

void UI_SetFramebuffer(u8* buffer);
void ClearFramebuffer();
void ClearFramebufferWithColor(u32 color);
void DrawRect(int x1, int x2, int y1, int y2, u32 color);
void DrawRectOutline(int x1, int x2, int y1, int y2, u32 colorin, u32 colorout);
void FillRect(int x1, int y1, int x2, int y2, u32 color);
int MeasureCharacter(char ch);
int MeasureText(char* str);
void DrawText(int x, int y, u32 color, char* str);

void DrawButton(int x, int y, int width, u32 color, char* text);
void DrawCheckBox(int x, int y, u32 color, char* text, bool check);
void DrawToolbar(char* dir);

bool HandleToolbar(u32 x, u32 y);

typedef struct
{
	void (*Init)();
	void (*DeInit)();
	
	void (*Render)(bool force);
	void (*ButtonPress)(u32 btn);
	void (*Touch)(bool touch, u32 x, u32 y);
	
} UIController;

extern UIController UI_ROMMenu;
extern UIController UI_Console;
extern UIController UI_Config;

void UI_Switch(UIController* ui);
void UI_SaveAndSwitch(UIController* ui);
void UI_Restore();
int UI_Level();

void UI_Render();
void UI_ButtonPress(u32 btn);
void UI_Touch(int touch, u32 x, u32 y);


bool StartROM(char* path, char* dir);

#endif
