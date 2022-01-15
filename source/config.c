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

extern FS_Archive sdmcArchive;
const char* configFilePath = "/blargSnes.ini";

const char* configFileL = 
	"HardwareRenderer=%d\n"
	"ScaleMode=%d\n"
	"DirPath=%[^\t\n]\n";

const char* configFileS = 
	"HardwareRenderer=%d\n"
	"ScaleMode=%d\n"
	"DirPath=%s\n";

char * lastDir[0x106];


void LoadConfig(u8 init)
{
	char tempDir[0x106];
	Config.HardwareRenderer = 1;
	Config.ScaleMode = 0;
	if(init) {
		strncpy(Config.DirPath,"/\0",2);
		strncpy(lastDir,"/\0",2);
	}
		
	Handle file;
	FS_Path filePath;
	filePath.type = PATH_ASCII;
	filePath.size = strlen(configFilePath) + 1;
	filePath.data = (u8*)configFilePath;
	
	Result res = FSUSER_OpenFile(&file, sdmcArchive, filePath, FS_OPEN_READ, 0);
	if (res) return;
	
	u64 size64 = 0;
	FSFILE_GetSize(file, &size64);
	u32 size = (u32)size64;
	if (!size) return;
	
	char* tempbuf = (char*)malloc(size+1);
	u32 bytesread = 0;
	FSFILE_Read(file, &bytesread, 0, (u32*)tempbuf, size);
	tempbuf[size] = '\0';
	
	sscanf(tempbuf, configFileL, 
		&Config.HardwareRenderer,
		&Config.ScaleMode,
		tempDir);

	if(Config.HardwareMode7 == -1)
		Config.HardwareMode7 = 0;

	if(init && strlen(tempDir) > 0 && tempDir[0] == '/')
	{
		Handle dirHandle;
		FS_Path dirPath = (FS_Path){PATH_ASCII, strlen(tempDir)+1, (u8*)tempDir};
		Result resDir = FSUSER_OpenDirectory(&dirHandle, sdmcArchive, dirPath);
		if (!resDir)
		{
			strncpy(Config.DirPath, tempDir, 0x106);
			strncpy(lastDir, tempDir, 0x106);
			FSDIR_Close(dirHandle);
		}
	}
	
	FSFILE_Close(file);
	free(tempbuf);
}

void SaveConfig(u8 saveCurDir)
{
	Handle file;
	FS_Path filePath;
	filePath.type = PATH_ASCII;
	filePath.size = strlen(configFilePath) + 1;
	filePath.data = (u8*)configFilePath;
	char tempDir[0x106];
	if(!saveCurDir)
		strncpy(tempDir,lastDir,0x106);
	else
		strncpy(tempDir,Config.DirPath,0x106);
	
	Result res = FSUSER_OpenFile(&file, sdmcArchive, filePath, FS_OPEN_CREATE|FS_OPEN_WRITE, 0);
	if (res)
	{
		bprintf("Error %08X while saving config\n", res);
		return;
	}
	
	char* tempbuf = (char*)malloc(1024);
	u32 size = snprintf(tempbuf, 1024, configFileS, 
		Config.HardwareRenderer,
		Config.ScaleMode,
		tempDir);
		
	FSFILE_SetSize(file, (u64)size);
	
	u32 byteswritten = 0;
	FSFILE_Write(file, &byteswritten, 0, (u32*)tempbuf, size, FS_WRITE_FLUSH);
	
	FSFILE_Close(file);
	free(tempbuf);

	if(saveCurDir)
		strncpy(lastDir,Config.DirPath,0x106);
}
