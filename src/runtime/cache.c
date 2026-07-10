#include "runtime/cache.h"

unsigned int FlushDataCacheRange(unsigned int address, int size) {
    /* 0x801A48A0 flushes every 0x20-byte data-cache line touched by
       [address, address + size), then issues a syscall/sync operation. It returns
       the first address after the flushed line range.

       The PC host has coherent memory, so this is a documented no-op that preserves
       the original return convention. */
    unsigned int lineCount;

    if (size == 0) {
        return address;
    }

    lineCount = ((unsigned int)size + (address & 0x1fU) + 0x1fU) >> 5;
    return address + lineCount * 0x20U;
}
