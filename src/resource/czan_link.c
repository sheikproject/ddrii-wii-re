#include "resource/czan_link.h"

#include "runtime/memory.h"

#include <stddef.h>

static unsigned int ReadBe32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) |
           ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) |
           (unsigned int)p[3];
}

int CzanLinkResource_IsValid(const void *linkData, unsigned int resourceSize) {
    const unsigned char *data = (const unsigned char *)linkData;
    unsigned int blockCount;

    if (data == 0 || resourceSize < 0x10) {
        return 0;
    }

    if (data[0] != 'W' || data[1] != 'I' || data[2] != 'I' || data[3] != '\0') {
        return 0;
    }

    blockCount = ReadBe32(data + 8);
    if (blockCount == 0 || blockCount > 0x1000) {
        return 0;
    }

    if (0x10u + blockCount * 8u > resourceSize) {
        return 0;
    }

    return 1;
}

unsigned int CzanLinkResource_GetBlockCount(const void *linkData, unsigned int resourceSize) {
    const unsigned char *data = (const unsigned char *)linkData;

    if (!CzanLinkResource_IsValid(linkData, resourceSize)) {
        return 0;
    }

    return ReadBe32(data + 8);
}

int CzanLinkResource_GetBlock(
    const void *linkData,
    unsigned int resourceSize,
    unsigned int blockIndex,
    CzanLinkBlock *outBlock) {
    const unsigned char *data = (const unsigned char *)linkData;
    unsigned int blockCount;
    unsigned int offset;
    unsigned int size;
    unsigned int entryOffset;

    if (outBlock != 0) {
        outBlock->data = 0;
        outBlock->size = 0;
    }

    if (!CzanLinkResource_IsValid(linkData, resourceSize) || outBlock == 0) {
        return 0;
    }

    blockCount = ReadBe32(data + 8);
    if (blockIndex >= blockCount) {
        return 0;
    }

    entryOffset = 0x10u + blockIndex * 8u;
    offset = ReadBe32(data + entryOffset);
    size = ReadBe32(data + entryOffset + 4);

    if (offset > resourceSize || size > resourceSize - offset) {
        return 0;
    }

    outBlock->data = data + offset;
    outBlock->size = size;
    return 1;
}

int CzanLinkManager_InitAndSetLink(int linkManager, int linkData) {
    /* 0x8016032C sets the CzanLinkManager vtable at +0x10 to PTR_PTR_802C0790,
       then calls CzanLinkManager_SetLink(linkManager, linkData). The listing confirms
       r3 is preserved as linkManager and the incoming r4 is passed through as linkData. */
    (void)linkData;
    return linkManager;
}

int CzanLinkManager_Release(int linkManager, short releaseMode) {
    /* 0x80160368 is the CzanLinkManager cleanup/release helper. It only calls
       the allocator release function when linkManager is nonzero and releaseMode
       is positive. Calls passing -1, which are common for stack managers, return
       without releasing memory. */
    if (linkManager != 0 && releaseMode > 0) {
        MemoryPool_Free(0, linkManager);
    }

    return linkManager;
}
