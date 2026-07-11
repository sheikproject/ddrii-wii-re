#ifndef DDRII_RUNTIME_MEMORY_H
#define DDRII_RUNTIME_MEMORY_H

void MemoryPool_Free(int poolIndex, int allocation);
unsigned long long Runtime_EnterCriticalSection(void);
int Runtime_GetCurrentThreadContext(void);
void MemoryMutex_Lock(int mutexRecord);
void MemoryMutex_Unlock(int mutexRecord);
void *MemoryPool_AllocateAligned(int allocator, int size, int alignment);
void *CopyMemoryOverlapSafe(void *dest, const void *src, unsigned int size);
void *ClearMemory(void *dest, int value, int size);

#endif
