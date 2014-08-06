
#ifndef MEM_H
#define MEM_H

extern Result memerror;

void* MemAlloc(u32 size);
void MemFree(void* ptr, u32 size);

#endif
