#include "runtime/memory.h"

#include <malloc.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

static long long gRuntimeBootOffset;
static int gRuntimeBootOffsetInitialized;

void MemoryPool_Free(int poolIndex, int allocation) {
    /* 0x80144CF0 locks the pool mutex at DAT_802EE174 + poolIndex * 0x34,
       frees allocation through the pool allocator stored at DAT_802EE158[poolIndex * 0x0D],
       then unlocks the mutex. */
    (void)poolIndex;
    (void)allocation;
}

void RuntimeDebugAssert(const char *file, int line, const char *message) {
    /* 0x80143A98 is the assert/report target used by recovered runtime classes.
       The decompile body is empty in release form, so the host logs instead of
       terminating. */
    printf("runtime assert: %s:%d: %s\n",
           file != 0 ? file : "<unknown>",
           line,
           message != 0 ? message : "<no message>");
}

void RuntimeFile_ReleaseOwnedMemory(int *fileRecord) {
    /* 0x801448E8 releases a zanFile-owned memory record. It only frees records with
       +0x10 set and ownership mode +0x0C == 1; other owned modes report an error. */
    if (fileRecord == 0 || fileRecord[4] == 0) {
        return;
    }

    if (fileRecord[3] == 1) {
        if (fileRecord[5] != 0) {
            void (**vtable)(int *, int) = (void (**)(int *, int))(uintptr_t)(unsigned int)fileRecord[5];
            if (vtable[1] != 0) {
                vtable[1](fileRecord, 1);
            }
        }
        return;
    }

    RuntimeDebugAssert("zanFile.cpp", 0x3eb, "zanFile: Free Memory Error");
}

unsigned long long Runtime_EnterCriticalSection(void) {
    /* 0x801A9430 captures the current PowerPC MSR and returns it with a masked copy
       of selected state in the high word. MemoryMutex_Lock/Unlock pass this token to
       FUN_801A9470 when leaving the critical section. */
    return 0x20000;
}

unsigned int Runtime_LeaveCriticalSection(unsigned long long token) {
    /* 0x801A9470 restores the captured MSR token and returns the previous external
       interrupt enable bit. */
    return (unsigned int)((token >> 0x0f) & 1U);
}

long long Runtime_GetTimebase(void) {
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;

    /* 0x801AD410 is the raw timebase read helper. Ghidra shows an empty body because
       the return value is carried in registers. */
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart == 0) {
        return counter.QuadPart;
    }
    return (counter.QuadPart * 1000000LL) / frequency.QuadPart;
}

long long Runtime_GetBootTime(void) {
    unsigned long long token;
    long long timebase;

    /* 0x801AD440 returns FUN_801AD410() plus the 64-bit boot offset stored at
       DAT_800030D8/DAT_800030DC while bracketing the read with critical-section
       enter/leave. */
    if (gRuntimeBootOffsetInitialized == 0) {
        gRuntimeBootOffset = 0;
        gRuntimeBootOffsetInitialized = 1;
    }
    token = Runtime_EnterCriticalSection();
    timebase = Runtime_GetTimebase() + gRuntimeBootOffset;
    Runtime_LeaveCriticalSection(token);
    return timebase;
}

int Runtime_GetCurrentThreadContext(void) {
    /* 0x801AC0A0 returns DAT_800000E4, the current thread/context record used by
       the allocator mutex code. MemoryMutex_Lock uses fields around +0x2D0..+0x2F8
       on this record for wait state and held-lock list bookkeeping. */
    return 0;
}

void MemoryMutex_Lock(int mutexRecord) {
    /* 0x801AA6E0 is the recursive/thread-aware lock used by the allocator.

       Confirmed fields on mutexRecord:
       - +0x08 owner thread/context pointer
       - +0x0C recursive lock count
       - +0x10/+0x14 links in the owning thread's held-lock list

       If the lock is free, it becomes owned by FUN_801AC0A0's current context. If
       the current context already owns it, only the count is incremented. Otherwise
       the current context records the pending lock at +0x2F0 and waits/yields through
       FUN_801AC390 and FUN_801AD0F0 until ownership is available. Interrupts or the
       scheduler lock are bracketed by Runtime_EnterCriticalSection/FUN_801A9470. */
    (void)mutexRecord;
}

void MemoryMutex_Unlock(int mutexRecord) {
    /* 0x801AA7C0 releases the allocator recursive lock.

       It only acts when the current context owns mutexRecord. The recursion count at
       +0x0C is decremented; when it reaches zero, the record is unlinked from the
       owner's held-lock list, the owner at +0x08 is cleared, a waiting context may be
       selected through FUN_801AC1A0, and FUN_801AD1E0 wakes/continues waiters. */
    (void)mutexRecord;
}

void *MemoryPool_AllocateAligned(int allocator, int size, int alignment) {
    /* 0x801DC520 is the allocator entry used by AllocObjectAligned-style callers.

       Behavior:
       - size 0 is normalized to 1
       - size is rounded up to a 4-byte boundary
       - if allocator flags at +0x38 contain bit 2, it locks allocator +0x20 with
         MemoryMutex_Lock/Unlock
       - negative alignment dispatches to FUN_801DC200(allocator, size, -alignment)
       - non-negative alignment dispatches to FUN_801DC120(allocator, size, alignment)

       The host implementation uses the CRT aligned allocator while we keep mapping
       the original pool internals. */
    (void)allocator;
    if (size == 0) {
        size = 1;
    }
    size = (size + 3) & ~3;
    if (alignment < 0) {
        alignment = -alignment;
    }
    if (alignment <= 0) {
        alignment = 4;
    }
    return _aligned_malloc((size_t)size, (size_t)alignment);
}

void *CopyMemoryOverlapSafe(void *dest, const void *src, unsigned int size) {
    /* 0x80004000 is the optimized overlap-safe byte copy helper.

       The original chooses forward or backward copy depending on dest/src ordering,
       has aligned 4/8-byte fast paths for medium/large copies, and handles overlap
       like memmove rather than memcpy. It returns the destination pointer. */
    if (dest == 0 || src == 0 || size == 0) {
        return dest;
    }

    return memmove(dest, src, (size_t)size);
}

void *ClearMemory(void *dest, int value, int size) {
    /* 0x80004350 wraps FUN_8000429C and returns the original destination pointer.
       Call sites use it like memset(dest, value, size). */
    if (dest == 0 || size <= 0) {
        return dest;
    }

    memset(dest, value, (size_t)size);
    return dest;
}
