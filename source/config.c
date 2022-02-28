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

#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <3ds.h>

#include "main.h"
#include "config.h"

Config_t Config;

const char* configFilePath = "/blargSnes.ini";

const char* configFileL = 
	"HardwareRenderer=%d\n"
	"ScaleMode=%d\n"
	"DirPath=%[^\t\n]\n"
	"VSync=%d\n"
	"FrameSkip=%d\n";

const char* configFileS = 
	"HardwareRenderer=%d\n"
	"ScaleMode=%d\n"
	"DirPath=%s\n"
	"VSync=%d\n"
	"FrameSkip=%d\n";

char lastDir[0x106];


void LoadConfig(u8 init)
{
	char tempDir[0x106];
	Config.HardwareRenderer = 1;
	Config.ScaleMode = 0;
	Config.VSync = 0;
	Config.FrameSkip = 0;
	if(init) 
	{
		strncpy(Config.DirPath,"/\0",2);
		strncpy(lastDir,"/\0",2);
	}

	FILE *pFile = fopen(configFilePath, "rb");
	if (pFile == NULL)
		return;

	fseek(pFile, 0, SEEK_END);
	u32 size = ftell(pFile);
	if (!size)
	{
		fclose(pFile);
		return;
	}

	char* tempbuf = (char*)linearAlloc(size + 1);
	fseek(pFile, 0, SEEK_SET);
	fread(tempbuf, sizeof(char), size, pFile);
	tempbuf[size] = '\0';

	sscanf(tempbuf, configFileL, 
		&Config.HardwareRenderer,
		&Config.ScaleMode,
		tempDir);

	if (Config.HardwareMode7Filter == -1)
		Config.HardwareMode7Filter = 0;

	if (init && strlen(tempDir) > 0 && tempDir[0] == '/')
	{
		DIR *pDir = opendir(tempDir);
		if (pDir != NULL)
		{
			strncpy(Config.DirPath, tempDir, 0x106);
			strncpy(lastDir, tempDir, 0x106);
			closedir(pDir);
		}

	}
	
	fclose(pFile);
	linearFree(tempbuf);
}

void SaveConfig(u8 saveCurDir)
{
	char tempDir[0x106];
	if (!saveCurDir)
		strncpy(tempDir,lastDir,0x106);
	else
		strncpy(tempDir,Config.DirPath,0x106);

	FILE *pFile = fopen(configFilePath, "wb");
	if (pFile == NULL)
	{
		bprintf("Error while saving config\n");
		return;
	}

	
	char* tempbuf = (char*)malloc(1024);
	u32 size = snprintf(tempbuf, 1024, configFileS, 
		Config.HardwareRenderer,
		Config.ScaleMode,
		tempDir);
	
	fwrite(tempbuf, sizeof(char), size, pFile);
	fclose(pFile);


	free(tempbuf);

	if (saveCurDir)
		strncpy(lastDir,Config.DirPath,0x106);
}
