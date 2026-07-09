#include "runtime/memory.h"

void MemoryPool_Free(int poolIndex, int allocation) {
    /* 0x80144CF0 locks the pool mutex at DAT_802EE174 + poolIndex * 0x34,
       frees allocation through the pool allocator stored at DAT_802EE158[poolIndex * 0x0D],
       then unlocks the mutex. */
    (void)poolIndex;
    (void)allocation;
}
