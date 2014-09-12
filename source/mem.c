#include <stdlib.h>
#include <3ds/types.h>
#include <3ds/svc.h>

Result memerror;

void* MemAlloc(u32 size)
{
	//return memalign(0x10, size);
	u32 ret = 0;
	Result res;
	
	size = (size + 0xFFF) & (~0xFFF);
	res = svcControlMemory(&ret, 0, 0, size, MEMOP_ALLOC_LINEAR, 0x3);
	if ((res & 0xFFFC03FF) != 0)
	{
		memerror = res;
		return NULL;
	}
	
	return (void*)ret;
}

void MemFree(void* ptr, u32 size)
{
	//free(ptr);
	//return;
	u32 blarg = 0;
	
	size = (size + 0xFFF) & (~0xFFF);
	svcControlMemory(&blarg, (u32)ptr, 0, size, MEMOP_FREE_LINEAR, 0x0);
}
