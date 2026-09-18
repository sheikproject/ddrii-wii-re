#include "resource/czan_link.h"

#include "runtime/memory.h"
#include "ui/czan_ui.h"

#include <stddef.h>
#include <stdint.h>

#define CZAN_LINK_MANAGER_HOST_STATE_CAPACITY 64

typedef struct CzanLinkManagerHostState {
    int *manager;
    void *linkData;
    unsigned int linkSize;
} CzanLinkManagerHostState;

static CzanLinkManagerHostState gCzanLinkManagerHostStates[CZAN_LINK_MANAGER_HOST_STATE_CAPACITY];

static unsigned int ReadBe32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) |
           ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) |
           (unsigned int)p[3];
}

static CzanLinkManagerHostState *CzanLinkManager_FindHostState(int *linkManager, int create) {
    int i;
    int freeIndex;

    if (linkManager == 0) {
        return 0;
    }

    freeIndex = -1;
    for (i = 0; i < CZAN_LINK_MANAGER_HOST_STATE_CAPACITY; i++) {
        if (gCzanLinkManagerHostStates[i].manager == linkManager) {
            return &gCzanLinkManagerHostStates[i];
        }
        if (freeIndex < 0 && gCzanLinkManagerHostStates[i].manager == 0) {
            freeIndex = i;
        }
    }

    if (!create || freeIndex < 0) {
        return 0;
    }

    gCzanLinkManagerHostStates[freeIndex].manager = linkManager;
    gCzanLinkManagerHostStates[freeIndex].linkData = 0;
    gCzanLinkManagerHostStates[freeIndex].linkSize = 0;
    return &gCzanLinkManagerHostStates[freeIndex];
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

void CzanLinkManager_Init(int *linkManager) {
    CzanLinkManagerHostState *state;

    if (linkManager == 0) {
        return;
    }

    linkManager[0] = 0;
    linkManager[1] = 0;
    linkManager[2] = 0;
    linkManager[3] = 0;
    linkManager[4] = 0;

    state = CzanLinkManager_FindHostState(linkManager, 1);
    if (state != 0) {
        state->linkData = 0;
        state->linkSize = 0;
    }
}

void CzanLinkManager_SetLink(int *linkManager, void *linkData) {
    CzanLinkManagerHostState *state;
    unsigned int linkSize;

    if (linkManager == 0) {
        return;
    }

    state = CzanLinkManager_FindHostState(linkManager, 1);
    linkSize = HostCzan_GetRegisteredLinkSize(linkData);

    if (state != 0) {
        state->linkData = linkData;
        state->linkSize = linkSize;
    }

    linkManager[0] = (int)(uintptr_t)linkData;
    linkManager[1] = 0;
    linkManager[2] = (int)CzanLinkResource_GetBlockCount(linkData, linkSize);
    linkManager[3] = 0;
}

int CzanLinkManager_InitAndSetLink(int linkManager, int linkData) {
    /* 0x8016032C sets the CzanLinkManager vtable at +0x10 to PTR_PTR_802C0790,
       then calls CzanLinkManager_SetLink(linkManager, linkData). The listing confirms
       r3 is preserved as linkManager and the incoming r4 is passed through as linkData. */
    CzanLinkManager_Init((int *)(uintptr_t)linkManager);
    CzanLinkManager_SetLink((int *)(uintptr_t)linkManager, (void *)(uintptr_t)linkData);
    return linkManager;
}

unsigned int CzanLinkManager_GetBlockCount(int *linkManager) {
    CzanLinkManagerHostState *state;
    unsigned int linkSize;

    state = CzanLinkManager_FindHostState(linkManager, 0);
    if (state != 0 && CzanLinkResource_IsValid(state->linkData, state->linkSize)) {
        return CzanLinkResource_GetBlockCount(state->linkData, state->linkSize);
    }

    linkSize = HostCzan_GetRegisteredLinkSize(linkManager);
    return CzanLinkResource_GetBlockCount(linkManager, linkSize);
}

const unsigned char *CzanLinkManager_GetBlock(int *linkManager, int blockIndex, unsigned int *outSize) {
    void *block;
    int size;

    block = 0;
    size = 0;
    if (!CzanLinkManager_GetBlockInfo(linkManager, blockIndex, &block, &size)) {
        if (outSize != 0) {
            *outSize = 0;
        }
        return 0;
    }

    if (outSize != 0) {
        *outSize = (unsigned int)size;
    }
    return (const unsigned char *)block;
}

int CzanLinkManager_GetBlockInfo(int *linkManager, int blockIndex, void **outBlock, int *outSize) {
    /* 0x80160504 reads one CzanLinkManager block-table entry. Original behavior:
       if blockIndex < *(int *)(*linkManager + 8), it writes the entry size to
       outSize, writes the block pointer to outBlock when size > 0 else NULL, and
       returns 1. Otherwise it leaves zero outputs and returns 0. */
    unsigned int linkSize;
    CzanLinkBlock block;
    CzanLinkManagerHostState *state;

    if (outBlock != 0) {
        *outBlock = 0;
    }
    if (outSize != 0) {
        *outSize = 0;
    }

    state = CzanLinkManager_FindHostState(linkManager, 0);
    if (state != 0 && CzanLinkResource_IsValid(state->linkData, state->linkSize)) {
        if (!CzanLinkResource_GetBlock(state->linkData, state->linkSize, (unsigned int)blockIndex, &block)) {
            return 0;
        }
    } else {
        linkSize = HostCzan_GetRegisteredLinkSize(linkManager);
        if (!CzanLinkResource_IsValid(linkManager, linkSize)) {
            return 0;
        }
        if (!CzanLinkResource_GetBlock(linkManager, linkSize, (unsigned int)blockIndex, &block)) {
            return 0;
        }
    }

    if (outSize != 0) {
        *outSize = (int)block.size;
    }
    if (outBlock != 0 && block.size != 0) {
        *outBlock = (void *)block.data;
    }
    return 1;
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
