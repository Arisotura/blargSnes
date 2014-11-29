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
#include "config.h"


int configdirty = 0;


void Config_Init()
{
	configdirty = 2;
}

void Config_DeInit()
{
}

void Config_Render(bool force)
{
	int x, y;
	
	if (force) configdirty = 2;
	if (!configdirty) return;
	configdirty--;
	
	ClearFramebuffer();
	DrawText(2, 2, RGB(255, 255, 255), "blargSNES config");
	
	y = 2 + 12 + 10;
	
	DrawCheckBox(10, y, RGB(255,255,255), "Hardware renderer", Config.HardwareRenderer);
	
	y += 26;
	
	DrawText(10, y+1, RGB(255,255,255), "Scaling:");
	x = 10 + MeasureText("Scaling:") + 6;
	
	char* scalemodes[] = {"1:1", "Fullscreen", "Cropped"};
	DrawButton(x, y-3, 140, RGB(255,255,255), scalemodes[Config.ScaleMode]);
	
	
	DrawButton(10, 212, 0, RGB(255,128,128), "Cancel");
	DrawButton(-10, 212, 0, RGB(128,255,128), "Save changes");
}

void Config_ButtonPress(u32 btn)
{
}

void Config_Touch(int touch, u32 x, u32 y)
{
	if (touch != 0) return;
	
	// bounding boxes are gross
	// TODO: eventually adopt a real widget system?
	
	if (y >= 24 && y < 44)
	{
		Config.HardwareRenderer = !Config.HardwareRenderer;
		configdirty = 2;
	}
	else if (y >= 50 && y < 70)
	{
		Config.ScaleMode++;
		if (Config.ScaleMode >= 3) Config.ScaleMode = 0;
		configdirty = 2;
	}
	else if (x < 106 && y >= 200)
	{
		LoadConfig();
		UI_Restore();
	}
	else if (x > 212 && y >= 200)
	{
		SaveConfig();
		UI_Restore();
	}
	
	ApplyScaling();
}


UIController UI_Config = 
{
	Config_Init,
	Config_DeInit,
	
	Config_Render,
	Config_ButtonPress,
	Config_Touch
};
