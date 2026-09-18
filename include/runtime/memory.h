#ifndef DDRII_RUNTIME_MEMORY_H
#define DDRII_RUNTIME_MEMORY_H

void MemoryPool_Free(int poolIndex, int allocation);
unsigned long long Runtime_EnterCriticalSection(void);
unsigned int Runtime_LeaveCriticalSection(unsigned long long token);
long long Runtime_GetTimebase(void);
long long Runtime_GetBootTime(void);
int Runtime_GetCurrentThreadContext(void);
void MemoryMutex_Lock(int mutexRecord);
void MemoryMutex_Unlock(int mutexRecord);
void *MemoryPool_AllocateAligned(int allocator, int size, int alignment);
void *CopyMemoryOverlapSafe(void *dest, const void *src, unsigned int size);
void *ClearMemory(void *dest, int value, int size);
void RuntimeFile_ReleaseOwnedMemory(int *fileRecord);
void RuntimeDebugAssert(const char *file, int line, const char *message);

#endif
