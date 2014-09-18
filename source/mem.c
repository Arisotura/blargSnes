#include <stdlib.h>
#include <3ds/types.h>
#include <3ds/svc.h>

Result memerror;

void* MemAlloc(u32 size)
{
	return memalign(0x10, size);
}

void MemFree(void* ptr, u32 size)
{
	free(ptr);
}
