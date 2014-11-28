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

#include "main.h"
#include "config.h"

Config_t Config;

extern FS_archive sdmcArchive;
const char* configFilePath = "/blargSnes.ini";

const char* configFile = 
	"HardwareRenderer=%d\n"
	"ScaleMode=%d\n";


void LoadConfig()
{
	Config.HardwareRenderer = 1;
	Config.ScaleMode = 0;
	
	
	Handle file;
	FS_path filePath;
	filePath.type = PATH_CHAR;
	filePath.size = strlen(configFilePath) + 1;
	filePath.data = (u8*)configFilePath;
	
	Result res = FSUSER_OpenFile(NULL, &file, sdmcArchive, filePath, FS_OPEN_READ, FS_ATTRIBUTE_NONE);
	if (res) return;
	
	u64 size64 = 0;
	FSFILE_GetSize(file, &size64);
	u32 size = (u32)size64;
	if (!size) return;
	
	char* tempbuf = (char*)malloc(size+1);
	u32 bytesread = 0;
	FSFILE_Read(file, &bytesread, 0, (u32*)tempbuf, size);
	tempbuf[size] = '\0';
	
	sscanf(tempbuf, configFile, 
		&Config.HardwareRenderer,
		&Config.ScaleMode);
	
	FSFILE_Close(file);
	free(tempbuf);
}

void SaveConfig()
{
	Handle file;
	FS_path filePath;
	filePath.type = PATH_CHAR;
	filePath.size = strlen(configFilePath) + 1;
	filePath.data = (u8*)configFilePath;
	
	Result res = FSUSER_OpenFile(NULL, &file, sdmcArchive, filePath, FS_OPEN_CREATE|FS_OPEN_WRITE, FS_ATTRIBUTE_NONE);
	if (res)
	{
		bprintf("Error %08X while saving config\n", res);
		return;
	}
	
	char* tempbuf = (char*)malloc(1024);
	u32 size = snprintf(tempbuf, 1024, configFile, 
		Config.HardwareRenderer,
		Config.ScaleMode);
		
	FSFILE_SetSize(file, (u64)size);
	
	u32 byteswritten = 0;
	FSFILE_Write(file, &byteswritten, 0, (u32*)tempbuf, size, FS_WRITE_FLUSH);
	
	FSFILE_Close(file);
	free(tempbuf);
}
