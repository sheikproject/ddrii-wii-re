#include "runtime/memory.h"

#include <string.h>

void MemoryPool_Free(int poolIndex, int allocation) {
    /* 0x80144CF0 locks the pool mutex at DAT_802EE174 + poolIndex * 0x34,
       frees allocation through the pool allocator stored at DAT_802EE158[poolIndex * 0x0D],
       then unlocks the mutex. */
    (void)poolIndex;
    (void)allocation;
}

unsigned long long Runtime_EnterCriticalSection(void) {
    /* 0x801A9430 captures the current PowerPC MSR and returns it with a masked copy
       of selected state in the high word. MemoryMutex_Lock/Unlock pass this token to
       FUN_801A9470 when leaving the critical section. */
    return 0;
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

       The host implementation is only a placeholder until the pool allocator itself
       is needed outside documentation. */
    (void)allocator;
    (void)size;
    (void)alignment;
    return 0;
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
