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
#include <3ds/types.h>
#include <3ds/svc.h>

void* MemAlloc(u32 size)
{
	return memalign(0x10, size);
}

void MemFree(void* ptr)
{
	free(ptr);
}


// gross VRAM allocator

#define VRAM_BASE 0x1F000000
#define VRAM_SIZE 0x600000
#define VRAM_BLOCK_SIZE 0x800 // this size allows allocating framebuffers without wasting VRAM
#define VRAM_NUM_BLOCKS (VRAM_SIZE / VRAM_BLOCK_SIZE)

// 00 = free, 01 = start of allocated block, 02 = allocated block cont.
u8 VRAM_Status[VRAM_NUM_BLOCKS];


void VRAM_Init()
{
	memset(VRAM_Status, 0, sizeof(VRAM_Status));
}

void* VRAM_Alloc(u32 size)
{
	u32 i;
	u32 startid = 0, numfree = 0;
	u32 good = 0;
	
	size = (size + (VRAM_BLOCK_SIZE-1)) / VRAM_BLOCK_SIZE;
	
	for (i = 0; i < VRAM_NUM_BLOCKS; i++)
	{
		if (VRAM_Status[i] != 0)
		{
			numfree = 0;
			continue;
		}
		
		if (!numfree) startid = i;
		numfree++;
		
		if (numfree >= size)
		{
			good = 1;
			break;
		}
	}
	
	if (!good)
	{
		// VRAM full
		return 0;
	}
	
	VRAM_Status[startid] = 1;
	for (i = 1; i < size; i++)
		VRAM_Status[startid+i] = 2;
	
	return (void*)(VRAM_BASE + (startid * VRAM_BLOCK_SIZE));
}

void VRAM_Free(void* _ptr)
{
	u32 ptr = (u32)_ptr;
	if (ptr < VRAM_BASE) return;
	
	ptr -= VRAM_BASE;
	if (ptr >= VRAM_SIZE) return;
	
	ptr /= VRAM_BLOCK_SIZE;
	if (VRAM_Status[ptr] != 1) return;
	
	VRAM_Status[ptr++] = 0;
	while (VRAM_Status[ptr] == 2)
		VRAM_Status[ptr++] = 0;
}
