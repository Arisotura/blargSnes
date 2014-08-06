#include <stdlib.h>
#include <ctr/types.h>
#include <ctr/svc.h>

Result memerror;

void* MemAlloc(u32 size)
{
	u32 ret = 0;
	Result res;
	
	size = (size + 0xFFF) & (~0xFFF);
	res = svc_controlMemory(&ret, 0, 0, size, 0x10000|MEMOP_COMMIT, 0x3);
	if ((res & 0xFFFC03FF) != 0)
	{
		memerror = res;
		return NULL;
	}
	
	return (void*)ret;
}

void MemFree(void* ptr, u32 size)
{
	u32 blarg = 0;
	
	size = (size + 0xFFF) & (~0xFFF);
	svc_controlMemory(&blarg, (u32)ptr, 0, size, MEMOP_FREE, 0x0);
}
