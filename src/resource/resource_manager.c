#include "resource/resource_manager.h"

#include "model/czan_model.h"
#include "render/render_engine.h"
#include "resource/czan_link.h"
#include "resource/czan_snd_read.h"
#include "resource/thp_decoder.h"
#include "game/cgame.h"
#include "runtime/math.h"
#include "runtime/memory.h"
#include "runtime/module_system.h"
#include "select/csel_mode.h"
#include "ui/czan_ui.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static ResourceHandle gLastLoadedResource;
static unsigned char *gLastLoadedResourceData;
static int *gActiveControllerMovieSlotManager;
static void *gResourceHostPointers[256];
static ResourceHandle gLoadedResources[128];
static int gLoadedResourceCount;

typedef struct TextManagerHostState {
    int *manager;
    const unsigned char *banks[17];
    unsigned int bankSizes[17];
    int activeBank;
} TextManagerHostState;

static TextManagerHostState gTextManagerHostStates[4];
static int gFontManagerTextureSlot802e70e8 = -1;

#define MAX_MOVIE_FRAME_CACHES 8

typedef struct MovieFrameCache {
    ResourceHandle *resource;
    int frameCount;
    int uploadedFrame;
    int videoWidth;
    int videoHeight;
    int audioChannels;
    int audioFrequency;
    int audioSamples;
    int audioTracks;
    int playedAudioFrame;
    int *offsets;
    int *sizes;
    int *audioOffsets;
    int *audioSizes;
} MovieFrameCache;

static MovieFrameCache gMovieFrameCaches[MAX_MOVIE_FRAME_CACHES];
static ThpDecodedFrame gMovieDecodedFrame;
static unsigned char gMovieThpWork[THP_WORK_SIZE];
static short *gMoviePcmBuffer;
static int gMoviePcmBufferSamples;

static int ResourcePointerBits(void *pointer) {
    int i;

    if (pointer == 0) {
        return 0;
    }

    for (i = 1; i < (int)(sizeof(gResourceHostPointers) / sizeof(gResourceHostPointers[0])); i++) {
        if (gResourceHostPointers[i] == pointer) {
            return i;
        }
    }

    for (i = 1; i < (int)(sizeof(gResourceHostPointers) / sizeof(gResourceHostPointers[0])); i++) {
        if (gResourceHostPointers[i] == 0) {
            gResourceHostPointers[i] = pointer;
            return i;
        }
    }

    return (int)(uintptr_t)pointer;
}

static void StoreVec4(float *dst, float x, float y, float z, float w) {
    if (dst == 0) {
        return;
    }
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
    dst[3] = w;
}

static void *ResourceHostPointerFromBits(int bits) {
    if (0 < bits && bits < (int)(sizeof(gResourceHostPointers) / sizeof(gResourceHostPointers[0])) &&
        gResourceHostPointers[bits] != 0) {
        return gResourceHostPointers[bits];
    }

    return (void *)(uintptr_t)bits;
}

int ResourceManager_HostPointerBits(void *pointer) {
    return ResourcePointerBits(pointer);
}

static void *ResourceMappedHostPointerFromBits(int bits) {
    if (0 < bits && bits < (int)(sizeof(gResourceHostPointers) / sizeof(gResourceHostPointers[0]))) {
        return gResourceHostPointers[bits];
    }

    return 0;
}

static unsigned int ReadBe32(const unsigned char *data) {
    return ((unsigned int)data[0] << 24) |
           ((unsigned int)data[1] << 16) |
           ((unsigned int)data[2] << 8) |
           (unsigned int)data[3];
}

static float ReadBeFloat32(const unsigned char *data) {
    union {
        unsigned int u;
        float f;
    } value;

    value.u = ReadBe32(data);
    return value.f;
}

static void MovieFrameCache_Clear(MovieFrameCache *cache) {
    if (cache == 0) {
        return;
    }

    free(cache->offsets);
    free(cache->sizes);
    free(cache->audioOffsets);
    free(cache->audioSizes);
    memset(cache, 0, sizeof(*cache));
    cache->uploadedFrame = -1;
    cache->playedAudioFrame = -1;
}

static MovieFrameCache *MovieFrameCache_Get(ResourceHandle *resource) {
    MovieFrameCache *cache = 0;
    const unsigned char *data;
    int declaredFrames;
    int capacity;
    int frameOffset;
    int frameSize;
    int i;
    int componentInfoOffset = 0;

    if (resource == 0 || resource->loaded == 0 || resource->data == 0 || resource->size <= 0) {
        return 0;
    }

    for (i = 0; i < MAX_MOVIE_FRAME_CACHES; i++) {
        if (gMovieFrameCaches[i].resource == resource) {
            return &gMovieFrameCaches[i];
        }
        if (cache == 0 && gMovieFrameCaches[i].resource == 0) {
            cache = &gMovieFrameCaches[i];
        }
    }
    if (cache == 0) {
        cache = &gMovieFrameCaches[0];
        MovieFrameCache_Clear(cache);
    }

    data = (const unsigned char *)resource->data;
    declaredFrames = 1;
    if (resource->size >= 0x18 && memcmp(data, "THP", 3) == 0) {
        declaredFrames = (int)ReadBe32(data + 0x14);
    }
    if (declaredFrames < 1) {
        declaredFrames = 1;
    }
    if (declaredFrames > 10000) {
        declaredFrames = 10000;
    }

    if (resource->size >= 0x34 && memcmp(data, "THP", 3) == 0) {
        int componentHeaderOffset = (int)ReadBe32(data + 0x20);
        if (componentHeaderOffset >= 0 &&
            componentHeaderOffset + 0x14 <= resource->size) {
            int componentCount = (int)ReadBe32(data + componentHeaderOffset);
            componentInfoOffset = componentHeaderOffset + 0x14;
            for (i = 0; i < componentCount && i < 16; i++) {
                unsigned char componentType = data[componentHeaderOffset + 4 + i];
                if (componentType == 0 && componentInfoOffset + 0xc <= resource->size) {
                    cache->videoWidth = (int)ReadBe32(data + componentInfoOffset);
                    cache->videoHeight = (int)ReadBe32(data + componentInfoOffset + 4);
                    componentInfoOffset += 0xc;
                }
                else if (componentType == 1 && componentInfoOffset + 0x10 <= resource->size) {
                    cache->audioChannels = (int)ReadBe32(data + componentInfoOffset);
                    cache->audioFrequency = (int)ReadBe32(data + componentInfoOffset + 4);
                    cache->audioSamples = (int)ReadBe32(data + componentInfoOffset + 8);
                    cache->audioTracks = (int)ReadBe32(data + componentInfoOffset + 0xc);
                    componentInfoOffset += 0x10;
                }
            }
        }
    }

    cache->offsets = (int *)malloc((size_t)declaredFrames * sizeof(int));
    cache->sizes = (int *)malloc((size_t)declaredFrames * sizeof(int));
    cache->audioOffsets = (int *)malloc((size_t)declaredFrames * sizeof(int));
    cache->audioSizes = (int *)malloc((size_t)declaredFrames * sizeof(int));
    if (cache->offsets == 0 || cache->sizes == 0 ||
        cache->audioOffsets == 0 || cache->audioSizes == 0) {
        MovieFrameCache_Clear(cache);
        return 0;
    }
    memset(cache->audioOffsets, 0xff, (size_t)declaredFrames * sizeof(int));
    memset(cache->audioSizes, 0, (size_t)declaredFrames * sizeof(int));

    cache->resource = resource;
    cache->uploadedFrame = -1;
    cache->playedAudioFrame = -1;
    capacity = declaredFrames;
    frameOffset = 0x60;
    frameSize = 0;
    if (resource->size >= 0x2c && memcmp(data, "THP", 3) == 0) {
        frameSize = (int)ReadBe32(data + 0x18);
        frameOffset = (int)ReadBe32(data + 0x28);
    }
    while (cache->frameCount < capacity && frameOffset + 0x10 <= resource->size) {
        int componentHeaderOffset = resource->size >= 0x34 && memcmp(data, "THP", 3) == 0 ?
            (int)ReadBe32(data + 0x20) : -1;
        int componentCount = 1;
        int componentIndex;
        int componentDataOffset;
        int videoOffset = -1;
        int videoSize = 0;
        int audioOffset = -1;
        int audioSize = 0;
        int nextFrameSize = (int)ReadBe32(data + frameOffset);

        if (componentHeaderOffset >= 0 && componentHeaderOffset + 4 <= resource->size) {
            componentCount = (int)ReadBe32(data + componentHeaderOffset);
        }
        if (componentCount < 1 || componentCount > 16 ||
            frameOffset + 8 + componentCount * 4 > resource->size) {
            break;
        }

        componentDataOffset = frameOffset + 8 + componentCount * 4;
        for (componentIndex = 0; componentIndex < componentCount; componentIndex++) {
            int componentSize = (int)ReadBe32(data + frameOffset + 8 + componentIndex * 4);
            unsigned char componentType = componentIndex == 0 ? 0 : (unsigned char)componentIndex;

            if (componentHeaderOffset >= 0 &&
                componentHeaderOffset + 4 + componentIndex < resource->size) {
                componentType = data[componentHeaderOffset + 4 + componentIndex];
            }
            if (componentSize <= 0 ||
                componentDataOffset < 0 ||
                componentDataOffset + componentSize > resource->size) {
                break;
            }
            if (componentType == 0) {
                videoOffset = componentDataOffset;
                videoSize = componentSize;
            }
            else if (componentType == 1) {
                audioOffset = componentDataOffset;
                audioSize = componentSize;
            }
            componentDataOffset += componentSize;
        }
        if (componentIndex != componentCount) {
            break;
        }

        if (frameSize <= 0 || videoSize <= 0 ||
            videoOffset + videoSize > resource->size ||
            data[videoOffset] != 0xff || data[videoOffset + 1] != 0xd8) {
            break;
        }

        cache->offsets[cache->frameCount] = videoOffset;
        cache->sizes[cache->frameCount] = videoSize;
        cache->audioOffsets[cache->frameCount] = audioOffset;
        cache->audioSizes[cache->frameCount] = audioSize;
        cache->frameCount++;

        if (nextFrameSize <= 0) {
            break;
        }
        frameOffset += frameSize;
        frameSize = nextFrameSize;
    }

    RuntimeDebugReport(
        "movie: cached THP components path=%s declared=%d found=%d video=%dx%d audio=%dch/%dHz\n",
        resource->path != 0 ? resource->path : "<unknown>",
        declaredFrames,
        cache->frameCount,
        cache->videoWidth,
        cache->videoHeight,
        cache->audioChannels,
        cache->audioFrequency);

    if (cache->frameCount == 0) {
        MovieFrameCache_Clear(cache);
        return 0;
    }
    return cache;
}

int *Manager802e70e0_Init(int *manager) {
    /* FUN_80099F70 constructs/resets the large manager stored in gManager_802E70E0.
       The new export shows this is not gCharacterAssetManager; that one is
       constructed by FUN_800CC450. */
    if (manager == 0) {
        return 0;
    }

    manager[0] = 0;
    manager[0xa3d5] = 0;
    manager[0xa3d2] = 0;
    manager[0xa3d3] = 0;
    manager[0xa3d4] = 0;
    manager[0xa3cf] = 0;
    manager[0xa3d0] = 0;
    ClearMemory(manager + 1, 0, 0x28f38);
    manager[0xa3d1] = 0;
    return manager;
}

void Manager802e70e0_DestroyNoop(void) {
    /* FUN_8009A050 is an empty destructor/no-op. */
}

static void CharacterAssetSelectSupport_Init(unsigned char *selectSupport);

int *CharacterAssetManager_Init(int *characterAssetManager) {
    /* FUN_800CC450 constructs the actual gCharacterAssetManager object.
       Confirmed fields are initialized here; the nested +0x34 and +0x28168
       subobject constructors are still represented by their cleared host state. */
    if (characterAssetManager == 0) {
        return 0;
    }

    ClearMemory(characterAssetManager, 0, 0x284e0);
    characterAssetManager[0] = -1;
    characterAssetManager[1] = 0;
    characterAssetManager[3] = 0;
    characterAssetManager[4] = -1;
    characterAssetManager[5] = -1;
    characterAssetManager[6] = -1;
    characterAssetManager[7] = 0;
    characterAssetManager[8] = -1;
    characterAssetManager[9] = 0x11;
    characterAssetManager[10] = 0;
    characterAssetManager[0xa059] = 0;
    CharacterAssetSelectSupport_Init((unsigned char *)characterAssetManager + 0x34);
    return characterAssetManager;
}

static void CharacterAssetManager_ResetSelectCommonObjectState(unsigned char *selectSupport) {
    unsigned char *owner;

    if (selectSupport == 0) {
        return;
    }

    owner = selectSupport + 0x27b24;
    CzanModelOwner_Init((int *)owner);
}

static void CharacterAssetManager_ResetSelectCommonCameraState(unsigned char *selectSupport) {
    unsigned char *camera;
    int i;

    if (selectSupport == 0) {
        return;
    }

    camera = selectSupport + 0x27c2c;
    ClearMemory(camera + 0x4a0, 0, 0x28);
    ClearMemory(camera, 0, 0x4a0);
    for (i = 0; i < 8; i++) {
        Matrix34_SetIdentity((float *)(camera + 0x64 + i * 0x94));
    }
    Matrix34_SetIdentity((float *)(camera + 0x4c8));
    ClearMemory(camera + 0x4f8, 0, 0x0c);
}

static void CharacterAssetManager_LoadSelectCommonBlock1(int *characterAssetManager, void *linkData) {
    unsigned char *selectSupport;

    if (characterAssetManager == 0 || linkData == 0) {
        return;
    }

    selectSupport = (unsigned char *)characterAssetManager + 0x34;

    /* 0x800F68FC resets the select-support object owner, resets its camera/
       projection helper, initializes several small vectors, then stores the
       incoming WII link at +0x27B10 for later live object/effect consumers. */
    CharacterAssetManager_ResetSelectCommonObjectState(selectSupport);
    CharacterAssetManager_ResetSelectCommonCameraState(selectSupport);

    *(float *)(selectSupport + 0x27ab8) = 0.0f;
    *(float *)(selectSupport + 0x27abc) = 1.0f;
    *(float *)(selectSupport + 0x27ac0) = 0.0f;
    StoreVec4((float *)(selectSupport + 0x27ac4), 0.0f, 0.0f, 0.0f, 1.0f);
    StoreVec4((float *)(selectSupport + 0x27ad0), 0.0f, 0.0f, 0.0f, 1.0f);
    StoreVec4((float *)(selectSupport + 0x27adc), 0.0f, 0.0f, 0.0f, 1.0f);
    StoreVec4((float *)(selectSupport + 0x27ae8), 0.0f, 0.0f, 0.0f, 1.0f);
    StoreVec4((float *)(selectSupport + 0x27af4), 0.0f, 0.0f, 0.0f, 1.0f);
    StoreVec4((float *)(selectSupport + 0x27b00), 0.0f, 0.0f, 0.0f, 1.0f);
    *(int *)(selectSupport + 0x27ab4) = 0;

    CzanLinkManager_SetLink((int *)(selectSupport + 0x27b10), linkData);
}

static int CharacterAssetSelectSupport_IndexedIdFromLinearIndex(int linearIndex) {
    static const int categoryCounts8026e87c[] = {
        0x0f, 0x0c, 0x0f, 0x0f, 9, 10, 1
    };
    static const int categoryBaseIds8026e898[] = {
        0x00, 0x14, 0x28, 0x3c, 200, 0x50, 100
    };
    int category;

    if (linearIndex < 0 || linearIndex >= 0x4d) {
        return -1;
    }

    for (category = 0; category < (int)(sizeof(categoryCounts8026e87c) / sizeof(categoryCounts8026e87c[0])); category++) {
        if (linearIndex < categoryCounts8026e87c[category]) {
            return categoryBaseIds8026e898[category] + linearIndex;
        }
        linearIndex -= categoryCounts8026e87c[category];
    }

    return -1;
}

static void CharacterAssetSelectSupport_ResetEntryRecord(unsigned char *selectSupport, int entryIndex) {
    unsigned char *entry;
    int i;

    if (selectSupport == 0 || entryIndex < 0 || entryIndex >= 4) {
        return;
    }

    entry = selectSupport + entryIndex * 0xc4;
    *(int *)(entry + 0x00) = -1;
    *(int *)(entry + 0x04) = 0;
    for (i = 0; i < 4; i++) {
        *(int *)(entry + 0x08 + i * 4) = 0;
        ClearMemory(entry + 0x18 + i * 4, 0xff, 4);
    }
    *(int *)(entry + 0x28) = 0;
    *(int *)(entry + 0x2c) = 0;
    *(int *)(entry + 0x30) = 0;
    *(float *)(entry + 0x34) = 0.0f;
    *(int *)(entry + 0x38) = -1;
    *(int *)(entry + 0x3c) = 0;
    *(int *)(entry + 0x40) = 0;
    ClearMemory(entry + 0x44, 0, 0x64);
    *(int *)(entry + 0xa8) = 0;
    *(int *)(entry + 0xac) = entryIndex;
    *(int *)(entry + 0xb4) = 0;
    *(int *)(entry + 0xb8) = 1;
    *(float *)(entry + 0xc0) = 1.0f;
}

static void CharacterAssetSelectSupport_Init(unsigned char *selectSupport) {
    int linearIndex;
    int partIndex;

    if (selectSupport == 0) {
        return;
    }

    CzanLinkManager_Init((int *)(selectSupport + 0x27b10));
    CharacterAssetManager_ResetSelectCommonObjectState(selectSupport);
    CharacterAssetManager_ResetSelectCommonCameraState(selectSupport);
    ClearMemory(selectSupport, 0, 0x310);
    ClearMemory(selectSupport + 0x310, 0, 0x25e60);
    ClearMemory(selectSupport + 0x26170, 0, 0x1944);
    ClearMemory(selectSupport + 0x27ab8, 0, 0x54);
    *(int *)(selectSupport + 0x27ab4) = 0;
    *(int *)(selectSupport + 0x27b0c) = 0;

    for (linearIndex = 0; linearIndex < 0x4d; linearIndex++) {
        int indexedId = CharacterAssetSelectSupport_IndexedIdFromLinearIndex(linearIndex);
        unsigned char *perPlayerBase = selectSupport + 0x310 + linearIndex * 0x7e0;
        unsigned char *sharedRecord = selectSupport + 0x26170 + linearIndex * 0x54;

        for (partIndex = 0; partIndex < 0x18; partIndex++) {
            unsigned char *record = perPlayerBase + partIndex * 0x54;
            *(int *)(record + 0x00) = 0;
            *(int *)(record + 0x04) = 0;
            *(int *)(record + 0x08) = 0;
            *(int *)(record + 0x0c) = 0;
            *(int *)(record + 0x50) = 0;
            ClearMemory(record + 0x10, 0, 0x40);
            snprintf((char *)(record + 0x10), 0x40, "/sound/stream/character/CHR%02d%02d", indexedId + 1, partIndex + 1);
        }

        *(int *)(sharedRecord + 0x00) = 0;
        *(int *)(sharedRecord + 0x04) = 0;
        *(int *)(sharedRecord + 0x08) = 0;
        *(int *)(sharedRecord + 0x0c) = 0;
        *(int *)(sharedRecord + 0x50) = 0;
        ClearMemory(sharedRecord + 0x10, 0, 0x40);
        snprintf((char *)(sharedRecord + 0x10), 0x40, "/sound/stream/character/CHR%02d0", indexedId + 1);
    }

    for (linearIndex = 0; linearIndex < 4; linearIndex++) {
        CharacterAssetSelectSupport_ResetEntryRecord(selectSupport, linearIndex);
    }
}

void CharacterAssetManager_ResetSelectCommonState(int *characterAssetManager) {
    /* 0x800CCB34 resets the manager-local select/common state before CSelect
       starts consuming the loaded common assets. */
    if (characterAssetManager == 0) {
        return;
    }

    characterAssetManager[0x10 / 4] = -1;
    characterAssetManager[0x18 / 4] = -1;
    characterAssetManager[0x14 / 4] = -1;
    characterAssetManager[0x1c / 4] = 0;
    characterAssetManager[0x20 / 4] = -1;
}

void CharacterAssetManager_LoadSelectCommon(int *characterAssetManager, void *linkData) {
    /* 0x800CC574 loads select/select_cmn.bin into the character asset manager.
       The original creates a temporary CzanLinkManager, then routes:
       block 0 -> CSelectCommon_LoadResource(characterAssetManager + 0x28168)
       block 1 -> FUN_800F68FC(characterAssetManager + 0x34)
       block 2 -> CzanModelManager_LoadResource(gManager_802E70B8, 4). */
    unsigned int linkSize;
    CzanLinkBlock block;

    if (characterAssetManager == 0 || linkData == 0) {
        return;
    }
    if (*(int *)((unsigned char *)characterAssetManager + 0x28164) != 0) {
        return;
    }

    linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    if (!CzanLinkResource_IsValid(linkData, linkSize)) {
        return;
    }

    *(int *)((unsigned char *)characterAssetManager + 0x28164) = 1;

    if (CzanLinkResource_GetBlock(linkData, linkSize, 0, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CSelectCommon_LoadResource((int *)((unsigned char *)characterAssetManager + 0x28168), (void *)block.data);
    }

    if (CzanLinkResource_GetBlock(linkData, linkSize, 1, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CharacterAssetManager_LoadSelectCommonBlock1(characterAssetManager, (void *)block.data);
    }

    if (CzanLinkResource_GetBlock(linkData, linkSize, 2, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CzanModelManager_LoadResource(GameMain_GetModelEffectManager(), 4, (void *)block.data);
    }
}

static void CharacterAssetSelectSupport_UpdateResourceRecordLoad(unsigned char *selectSupport, int *record) {
    ResourceHandle *handle;
    const char *path;

    if (selectSupport == 0 || record == 0) {
        return;
    }

    if (record[0x14] == 0) {
        if (record[0] == 1) {
            int stillPending = 0;
            handle = (ResourceHandle *)ResourceHostPointerFromBits(record[3]);
            if (handle != 0) {
                if (handle->loaded == 0) {
                    stillPending = 1;
                }
                else {
                    record[3] = 0;
                }
            }

            if (stillPending) {
                record[1] = 1;
            }
            else {
                record[0] = 0;
                if (*(int *)(selectSupport + 0x27ab4) > 0) {
                    *(int *)(selectSupport + 0x27ab4) = *(int *)(selectSupport + 0x27ab4) - 1;
                }
                record[1] = 0;
            }
        }
        return;
    }

    path = (const char *)(record + 4);
    if (path[0] == '\0') {
        return;
    }

    if (record[1] == 1) {
        record[2] = 0;
        if (record[3] == 0) {
            handle = LoadResourceByPath(GlobalRuntimeContext_GetPointerAt(0x260), path, 0);
            record[3] = ResourcePointerBits(handle);
            if (handle == 0) {
                record[2] = 1;
            }
        }
        record[1] = 0;
    }
    else if ((record[0] == 0 || record[2] == 1) && *(int *)(selectSupport + 0x27ab4) < 0x4b) {
        record[2] = 0;
        if (record[3] == 0) {
            handle = LoadResourceByPath(GlobalRuntimeContext_GetPointerAt(0x260), path, 0);
            record[3] = ResourcePointerBits(handle);
            if (handle == 0) {
                record[2] = 1;
            }
        }
        record[0] = 1;
        if (record[2] == 0) {
            *(int *)(selectSupport + 0x27ab4) = *(int *)(selectSupport + 0x27ab4) + 1;
        }
        record[1] = 0;
    }
}

static void CharacterAssetSelectSupport_UpdateResourceRecords(unsigned char *selectSupport) {
    int groupIndex;
    int recordIndex;

    if (selectSupport == 0) {
        return;
    }

    for (groupIndex = 0; groupIndex < 0x4d; groupIndex++) {
        unsigned char *perPlayerRecords = selectSupport + 0x310 + groupIndex * 0x7e0;
        unsigned char *sharedRecord = selectSupport + 0x26170 + groupIndex * 0x54;

        for (recordIndex = 0; recordIndex < 0x18; recordIndex++) {
            CharacterAssetSelectSupport_UpdateResourceRecordLoad(
                selectSupport,
                (int *)(perPlayerRecords + recordIndex * 0x54));
        }
        CharacterAssetSelectSupport_UpdateResourceRecordLoad(selectSupport, (int *)sharedRecord);
    }

    /* The remaining half of FUN_800F6A48 advances per-entry timing and calls
       FUN_800F7428 once resource records are ready. That path creates the 0x7DC
       extended CtsStageObj and is kept separate until the object vtable calls are
       mapped. */
}

static int CharacterAssetSelectSupport_IsValidEntryIndex(int entryIndex) {
    return entryIndex >= 0 && entryIndex < 4;
}

static int CharacterAssetSelectSupport_IsValidPartIndex(int partIndex) {
    return partIndex >= 0 && partIndex < 4;
}

static void CharacterAssetSelectSupport_ClearEntryLiveState(unsigned char *entry) {
    if (entry == 0) {
        return;
    }

    *(int *)(entry + 0x40) = 0;
    ClearMemory(entry + 0x44, 0, 0x60);
}

static int CharacterAssetSelectSupport_AreResourceRecordsIdle(unsigned char *selectSupport) {
    int groupIndex;
    int recordIndex;

    if (selectSupport == 0) {
        return 1;
    }

    /* FUN_800F8348 returns 0 while any of the 77 * 24 per-player records or
       77 shared records still has state 1. */
    for (groupIndex = 0; groupIndex < 0x4d; groupIndex++) {
        unsigned char *perPlayerRecords = selectSupport + 0x310 + groupIndex * 0x7e0;
        unsigned char *sharedRecord = selectSupport + 0x26170 + groupIndex * 0x54;

        for (recordIndex = 0; recordIndex < 0x18; recordIndex++) {
            if (*(int *)(perPlayerRecords + recordIndex * 0x54) == 1) {
                return 0;
            }
        }
        if (*(int *)sharedRecord == 1) {
            return 0;
        }
    }

    return 1;
}

void CharacterAssetManager_UpdateActiveAssets(int *characterAssetManager, int skipSelectCommonUpdate) {
    /* 0x800CC690 drives the active character/select assets once per frame. The
       sound/timer subpaths are still pending; the select-common resource-record
       update is wired so block-1 records can load in the same throttled order as
       the game once their tables are populated. */
    if (characterAssetManager == 0 || skipSelectCommonUpdate == 1) {
        return;
    }

    CharacterAssetSelectSupport_UpdateResourceRecords((unsigned char *)characterAssetManager + 0x34);
}

int CharacterAssetManager_IsSelectCommonIdle(int *characterAssetManager) {
    /* FUN_800CCBD0 is a narrow state predicate: it returns 1 only while
       gCharacterAssetManager +0x24 is still 0x11. */
    if (characterAssetManager == 0) {
        return 0;
    }
    return characterAssetManager[0x24 / 4] == 0x11;
}

int CharacterAssetManager_AreSelectSupportRecordsIdle(int *characterAssetManager) {
    /* FUN_800CD670 is the public wrapper around FUN_800F8348. */
    if (characterAssetManager == 0) {
        return 1;
    }
    return CharacterAssetSelectSupport_AreResourceRecordsIdle((unsigned char *)characterAssetManager + 0x34);
}

void CharacterAssetManager_DestroyLiveSelectSupportObjects(int *characterAssetManager) {
    int entryIndex;
    unsigned char *selectSupport;

    if (characterAssetManager == 0) {
        return;
    }

    selectSupport = (unsigned char *)characterAssetManager + 0x34;
    for (entryIndex = 0; entryIndex < 4; entryIndex++) {
        unsigned char *entry = selectSupport + entryIndex * 0xc4;
        CharacterAssetSelectSupport_ClearEntryLiveState(entry);
        *(int *)(entry + 0xb4) = 0;
        *(int *)(entry + 0xb8) = 1;
        *(float *)(entry + 0xc0) = 1.0f;
    }
}

void CharacterAssetManager_SetSelectSupportEntryId(
    int *characterAssetManager,
    int entryIndex,
    int indexedId,
    int subIndex) {
    unsigned char *selectSupport;
    unsigned char *entry;

    if (characterAssetManager == 0 || !CharacterAssetSelectSupport_IsValidEntryIndex(entryIndex)) {
        return;
    }

    selectSupport = (unsigned char *)characterAssetManager + 0x34;
    entry = selectSupport + entryIndex * 0xc4;

    if (*(int *)(entry + 0x00) != indexedId || *(int *)(entry + 0x04) != subIndex) {
        if (*(int *)(entry + 0x00) != -1) {
            CharacterAssetSelectSupport_ClearEntryLiveState(entry);
            *(float *)(entry + 0x34) = 0.0f;
        }
        *(int *)(entry + 0x00) = indexedId;
        *(int *)(entry + 0x04) = subIndex;
    }

    *(int *)(entry + 0xa8) = 0;
    *(int *)(entry + 0xac) = entryIndex;
    *(int *)(entry + 0xb4) = 0;
}

void CharacterAssetManager_MarkSelectSupportEntryDirty(int *characterAssetManager, int entryIndex) {
    unsigned char *selectSupport;

    if (characterAssetManager == 0 || !CharacterAssetSelectSupport_IsValidEntryIndex(entryIndex)) {
        return;
    }

    selectSupport = (unsigned char *)characterAssetManager + 0x34;
    *(int *)(selectSupport + entryIndex * 0xc4 + 0xa8) = 1;
}

void CharacterAssetManager_SetSelectSupportPartValue(
    int *characterAssetManager,
    int entryIndex,
    int partIndex,
    int value) {
    unsigned char *selectSupport;

    if (characterAssetManager == 0 ||
        !CharacterAssetSelectSupport_IsValidEntryIndex(entryIndex) ||
        !CharacterAssetSelectSupport_IsValidPartIndex(partIndex)) {
        return;
    }

    selectSupport = (unsigned char *)characterAssetManager + 0x34;
    *(int *)(selectSupport + entryIndex * 0xc4 + partIndex * 4 + 0x08) = value;
}

void CharacterAssetManager_SetSelectSupportPartColor(
    int *characterAssetManager,
    int entryIndex,
    int partIndex,
    const unsigned char rgba[4]) {
    unsigned char *selectSupport;
    unsigned char *dst;

    if (characterAssetManager == 0 || rgba == 0 ||
        !CharacterAssetSelectSupport_IsValidEntryIndex(entryIndex) ||
        !CharacterAssetSelectSupport_IsValidPartIndex(partIndex)) {
        return;
    }

    selectSupport = (unsigned char *)characterAssetManager + 0x34;
    dst = selectSupport + entryIndex * 0xc4 + partIndex * 4 + 0x18;
    dst[0] = rgba[0];
    dst[1] = rgba[1];
    dst[2] = rgba[2];
    dst[3] = rgba[3];
}

void CharacterAssetManager_SetSelectSupportEntryAnimation(
    int *characterAssetManager,
    int entryIndex,
    int animationId,
    int animationSubId) {
    unsigned char *selectSupport;
    unsigned char *entry;

    if (characterAssetManager == 0 || !CharacterAssetSelectSupport_IsValidEntryIndex(entryIndex)) {
        return;
    }

    selectSupport = (unsigned char *)characterAssetManager + 0x34;
    entry = selectSupport + entryIndex * 0xc4;
    if (*(int *)(entry + 0x30) != animationId || *(int *)(entry + 0x38) != animationSubId) {
        *(int *)(entry + 0x30) = animationId;
        *(int *)(entry + 0x38) = animationSubId;
        if (*(int *)(entry + 0x40) != 0) {
            CharacterAssetSelectSupport_ClearEntryLiveState(entry);
        }
    }
}

void CharacterAssetManager_ClearSelectSupportEntryAnimation(int *characterAssetManager, int entryIndex) {
    unsigned char *selectSupport;
    unsigned char *entry;

    if (characterAssetManager == 0 || !CharacterAssetSelectSupport_IsValidEntryIndex(entryIndex)) {
        return;
    }

    selectSupport = (unsigned char *)characterAssetManager + 0x34;
    entry = selectSupport + entryIndex * 0xc4;
    if (*(int *)(entry + 0x38) != -1) {
        *(int *)(entry + 0x30) = 0;
        *(int *)(entry + 0x38) = -1;
        if (*(int *)(entry + 0x40) != 0) {
            CharacterAssetSelectSupport_ClearEntryLiveState(entry);
        }
    }
}

int *Manager802e70b0_Init(int *manager) {
    /* FUN_800C0BF8 constructs the 0x50-byte manager stored in gManager_802E70B0.
       It initializes the child controller at +0x48, clears +0x00..+0x43, and sets
       +0x44 to -1. */
    if (manager == 0) {
        return 0;
    }

    ClearMemory(manager, 0, 0x50);
    manager[0x44 / 4] = -1;
    return manager;
}

static TextManagerHostState *TextManager_FindHostState(int *manager, int create) {
    int i;

    if (manager == 0) {
        return 0;
    }
    for (i = 0; i < (int)(sizeof(gTextManagerHostStates) / sizeof(gTextManagerHostStates[0])); i++) {
        if (gTextManagerHostStates[i].manager == manager) {
            return &gTextManagerHostStates[i];
        }
    }
    if (!create) {
        return 0;
    }
    for (i = 0; i < (int)(sizeof(gTextManagerHostStates) / sizeof(gTextManagerHostStates[0])); i++) {
        if (gTextManagerHostStates[i].manager == 0) {
            ClearMemory(&gTextManagerHostStates[i], 0, sizeof(gTextManagerHostStates[i]));
            gTextManagerHostStates[i].manager = manager;
            gTextManagerHostStates[i].activeBank = -1;
            return &gTextManagerHostStates[i];
        }
    }
    return 0;
}

void TextManager_LoadResource(int *manager, void *textLinkData) {
    TextManagerHostState *state;
    unsigned int linkSize;
    int i;

    /* 0x800C0D04 installs up to 17 localized text banks from the boot text WII
       resource into gManager_802E70B0. The retail manager then selects a bank with
       0x800C0FBC and resolves entries with 0x800C101C. */
    if (manager == 0 || textLinkData == 0) {
        return;
    }

    state = TextManager_FindHostState(manager, 1);
    if (state == 0) {
        return;
    }
    ClearMemory(manager, 0, 0x44);
    ClearMemory(state->bankSizes, 0, sizeof(state->bankSizes));
    manager[0x44 / 4] = -1;
    state->activeBank = -1;

    linkSize = HostCzan_GetRegisteredLinkSize(textLinkData);
    for (i = 0; i < 17; i++) {
        CzanLinkBlock block;
        state->banks[i] = 0;
        if (CzanLinkResource_GetBlock(textLinkData, linkSize, (unsigned int)i, &block)) {
            state->banks[i] = block.data;
            state->bankSizes[i] = block.size;
            manager[i] = block.data != 0 ? 1 : 0;
            HostCzan_RegisterLinkSize(block.data, block.size);
        }
        else {
            manager[i] = 0;
        }
    }
    if (state->banks[0] != 0) {
        TextManager_SelectBank(manager, 0);
    }
}

void TextManager_SelectBank(int *manager, int bankIndex) {
    TextManagerHostState *state = TextManager_FindHostState(manager, 0);

    /* 0x800C0FBC selects one installed text bank. */
    if (manager == 0) {
        return;
    }
    if (state == 0 || bankIndex < 0 || bankIndex >= 17 || state->banks[bankIndex] == 0) {
        manager[0x44 / 4] = -1;
        if (state != 0) {
            state->activeBank = -1;
        }
        return;
    }
    manager[0x44 / 4] = bankIndex;
    state->activeBank = bankIndex;
}

const char *TextManager_GetText(int *manager, int textIndex) {
    TextManagerHostState *state = TextManager_FindHostState(manager, 0);
    CzanLinkBlock block;
    int activeBank;

    /* 0x800C101C returns a pointer to the selected bank entry or "ERROR". */
    if (manager == 0 || state == 0) {
        return "ERROR";
    }
    activeBank = manager[0x44 / 4];
    if (activeBank < 0 || activeBank >= 17 || state->banks[activeBank] == 0) {
        return "ERROR";
    }
    if (textIndex < 0 ||
        !CzanLinkResource_GetBlock(
            state->banks[activeBank],
            state->bankSizes[activeBank],
            (unsigned int)textIndex,
            &block) ||
        block.data == 0) {
        return "ERROR";
    }
    return (const char *)block.data;
}

void FontManager_LoadResource(void *fontLinkData) {
    unsigned int linkSize;
    CzanLinkBlock block;
    TextureManagerKnownFields *textureManager;

    /* 0x800B72F8 creates DAT_802E70E8 and loads block 0 of font/font_*.bin as
       a texture through the global texture manager. */
    if (fontLinkData == 0) {
        return;
    }
    if (gFontManagerTextureSlot802e70e8 != -1) {
        return;
    }

    linkSize = HostCzan_GetRegisteredLinkSize(fontLinkData);
    if (!CzanLinkResource_GetBlock(fontLinkData, linkSize, 0, &block)) {
        return;
    }
    textureManager = (TextureManagerKnownFields *)GlobalRuntimeContext_GetPointerAt(0x26c);
    if (textureManager == 0) {
        return;
    }
    gFontManagerTextureSlot802e70e8 = (int)CreateTextureFromTplResource(
        textureManager,
        (void *)block.data,
        block.size,
        0xffffffffu);
}

void UiRootManager_Init(int *uiRootManager) {
    /* FUN_800FE48C constructs the 0x4C-byte gUiRootManager. */
    if (uiRootManager == 0) {
        return;
    }

    ClearMemory(uiRootManager, 0, 0x4c);
    uiRootManager[1] = -1;
    uiRootManager[2] = -1;
    uiRootManager[3] = -1;
    uiRootManager[4] = -1;
    uiRootManager[5] = -1;
    uiRootManager[6] = -1;
    uiRootManager[7] = -1;
    uiRootManager[8] = -1;
    uiRootManager[9] = -1;
}

int *ModelEffectManager_Init(int *manager) {
    /* FUN_80178278 constructs gManager_802E70B8. The nested model/effect/list
       sub-managers are still pending, but the exported scalar defaults and global
       back-pointer are preserved. */
    if (manager == 0) {
        return 0;
    }

    ClearMemory(manager, 0, 0x1b38);
    manager[0] = 0;
    manager[1] = 0;
    manager[0x4b] = -1;
    manager[0x4f] = -1;
    manager[0x53] = -1;
    manager[0x57] = -1;
    manager[0x5b] = -1;
    manager[0x5f] = -1;
    manager[0x63] = -1;
    manager[0x67] = -1;
    manager[0xa9] = 0;
    manager[0x6ad] = 0;
    return manager;
}

void BootTempManager_Init(int *bootTempManager) {
    /* FUN_80021D60 clears the small 0x28-byte boot temp manager. */
    if (bootTempManager == 0) {
        return;
    }

    ClearMemory(bootTempManager, 0, 0x28);
}

void ActiveControllerMovieBindings_SetMovieSlotManagerForHost(int *slotManager) {
    gActiveControllerMovieSlotManager = slotManager;
}

static int ReadFile(const char *path, void **outData, int *outSize) {
    FILE *file = fopen(path, "rb");
    long size;
    void *data;

    if (file == 0) {
        return 0;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return 0;
    }

    data = malloc((size_t)size);
    if (data == 0) {
        fclose(file);
        return 0;
    }

    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return 0;
    }

    fclose(file);
    *outData = data;
    *outSize = (int)size;
    return 1;
}

ResourceHandle *LoadResourceByPath(void *resourceManager, const char *path, int flags) {
    char hostPath[512];
    const char *normalizedPath;
    ResourceHandle *resource;
    (void)resourceManager;

    normalizedPath = path;
    if (normalizedPath == 0) {
        return 0;
    }
    while (*normalizedPath == '/' || *normalizedPath == '\\') {
        normalizedPath++;
    }

    snprintf(hostPath, sizeof(hostPath), "input\\DATA\\%s", normalizedPath);
    {
        char *p;
        for (p = hostPath; *p != '\0'; p++) {
            if (*p == '/') {
                *p = '\\';
            }
        }
    }

    if (gLoadedResourceCount < (int)(sizeof(gLoadedResources) / sizeof(gLoadedResources[0]))) {
        resource = &gLoadedResources[gLoadedResourceCount++];
    }
    else {
        resource = &gLastLoadedResource;
        if (gLastLoadedResourceData != 0) {
            free(gLastLoadedResourceData);
            gLastLoadedResourceData = 0;
        }
    }

    resource->path = path;
    resource->data = 0;
    resource->size = 0;
    resource->loaded = ReadFile(hostPath, &resource->data, &resource->size);
    if (resource == &gLastLoadedResource) {
        gLastLoadedResourceData = (unsigned char *)resource->data;
    }

    printf("resource: LoadResourceByPath path=%s hostPath=%s flags=%d loaded=%d size=%d\n",
           path,
           hostPath,
           flags,
           resource->loaded,
           resource->size);
    fflush(stdout);
    return resource;
}

int ResourceSlotManager_ClaimFreeSlot(int *slotPool) {
    /* 0x80186F4C scans a slot pool for the first free 0x290-byte record.

       Confirmed pool fields:
       slotPool +0x56B8 -> slot record array base
       slotPool +0x56BC -> slot record count
       slot record +0x04 bit 0 -> in-use flag

       When a free slot is found, the original sets bit 0 and returns the slot
       index. If all slots are already in use, it returns -1. */
    int slotCount;
    int index;
    unsigned char *slots;

    if (slotPool == 0) {
        return -1;
    }

    slots = (unsigned char *)ResourceHostPointerFromBits(*(int *)((unsigned char *)slotPool + 0x56b8));
    slotCount = *(int *)((unsigned char *)slotPool + 0x56bc);
    if (slots == 0 || slotCount <= 0) {
        return -1;
    }

    for (index = 0; index < slotCount; index++) {
        unsigned char *slot = slots + index * 0x290;
        unsigned int *flags = (unsigned int *)(slot + 4);
        if ((*flags & 1U) == 0) {
            *flags |= 1U;
            return index;
        }
    }

    return -1;
}

int *ResourceSlotManager_GetClaimedSlot(int *slotPool, int slotIndex) {
    /* 0x801871A8 validates a slot index and returns the claimed 0x290-byte record.

       Confirmed behavior:
       - returns null when slotIndex < 0
       - returns null when slotIndex >= slotPool +0x56BC count
       - computes slot = *(slotPool +0x56B8) + slotIndex * 0x290
       - returns null unless slot +0x04 bit 0 is set
       - otherwise returns slot */
    unsigned char *slots;
    int slotCount;

    if (slotPool == 0) {
        return 0;
    }

    if (slotIndex < 0) {
        return 0;
    }

    slots = (unsigned char *)ResourceHostPointerFromBits(*(int *)((unsigned char *)slotPool + 0x56b8));
    slotCount = *(int *)((unsigned char *)slotPool + 0x56bc);
    if (slots == 0 || slotIndex >= slotCount) {
        return 0;
    }

    if ((*(unsigned int *)(slots + slotIndex * 0x290 + 4) & 1U) == 0) {
        return 0;
    }

    return (int *)(slots + slotIndex * 0x290);
}

void ResourceSlotManager_ReleaseSlot(int *slotPool, int slotIndex) {
    /* 0x80186FB8 releases one claimed 0x290-byte movie/resource slot.

       The original validates the slot index, resets CzanMovieObj state, frees the
       optional movie buffer when inactive, then clears slot record +0x04 bit 0. */
    int *slotObject;

    slotObject = ResourceSlotManager_GetClaimedSlot(slotPool, slotIndex);
    if (slotObject == 0) {
        return;
    }

    CzanMovieObj_Reset(slotObject);
    CzanMovieObj_FreeBuffer(slotObject);
    *(unsigned int *)((unsigned char *)slotObject + 4) &= ~1U;
}

int ResourceSlotManager_AllocateSlot(int *slotManager, int setupData) {
    /* 0x80024EA8 allocates or reserves one slot from gManager_802E70A8.

       Confirmed original flow:
       - return -1 when slotManager[0] is null
       - slotIndex = ResourceSlotManager_ClaimFreeSlot(slotManager[0])
       - return -1 when no slot is available
       - slotObject = ResourceSlotManager_GetClaimedSlot(slotManager[0], slotIndex)
       - if setupData != 0, call FUN_801843CC(slotObject, setupData)
       - return slotIndex

       Callers currently pass setupData=0 from the CzanModel owner resource-group
       path, so this behaves as a plain slot/handle allocator there. */
    int slotIndex;
    int *slotObject;

    if (slotManager == 0 || slotManager[0] == 0) {
        return -1;
    }

    slotIndex = ResourceSlotManager_ClaimFreeSlot((int *)ResourceHostPointerFromBits(slotManager[0]));
    if (slotIndex == -1) {
        return -1;
    }

    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotManager[0]), slotIndex);
    if (slotObject != 0 && setupData != 0) {
        CzanMovieObj_AllocBuffer(slotObject, setupData);
    }

    return slotIndex;
}

int ResourceSlotHandle_IsActivePending(int *slotHandle) {
    /* 0x80024FA4 checks the current slot record for a specific active/pending
       flag state.

       Confirmed original flow:
       - if slotHandle[0] exists, get slotHandle[1] through
         ResourceSlotManager_GetClaimedSlot
       - return 1 when slot +0x230 bit 0 is set and bit 1 is clear
       - otherwise return 0 */
    int *slotObject;

    if (slotHandle == 0 || slotHandle[0] == 0) {
        return 0;
    }

    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotHandle[1]);
    if (slotObject == 0) {
        return 0;
    }

    return ((*(unsigned int *)((unsigned char *)slotObject + 0x230) & 1U) != 0 &&
            (*(unsigned int *)((unsigned char *)slotObject + 0x230) & 2U) == 0);
}

int *ResourceSlotHandle_Init(int *slotHandle) {
    /* FUN_80024C3C constructs a tiny resource-slot handle:
       [0] manager pointer, [1] active slot index, [2] vtable/type pointer.
       The host keeps the vtable placeholder null and preserves the game-facing
       manager/index defaults. */
    if (slotHandle == 0) {
        return 0;
    }

    slotHandle[0] = 0;
    slotHandle[1] = -1;
    slotHandle[2] = 0;
    return slotHandle;
}

void ResourceSlotHandle_CreateSlotPool(int *slotHandle) {
    /* FUN_80024D38 rebuilds gManager_802E70A8:
       release the current slot/pool, allocate a 0x56C8 slot manager, then
       initialize it for 10 CzanMovieObj slots. The original delegates the pool
       construction to FUN_801868BC and FUN_80186B14(pool, 10); the host creates
       equivalent fields consumed by ResourceSlotManager_* directly. */
    int slotIndex;
    int *slotPool;
    unsigned char *slots;

    if (slotHandle == 0) {
        return;
    }

    if (slotHandle[0] != 0) {
        ResourceSlotManager_ReleaseSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotHandle[1]);
    }
    slotHandle[1] = -1;
    slotHandle[0] = 0;

    slotPool = (int *)MemoryPool_AllocateAligned(0, 0x56c8, 0x20);
    if (slotPool == 0) {
        return;
    }
    ClearMemory(slotPool, 0, 0x56c8);

    slots = (unsigned char *)MemoryPool_AllocateAligned(0, 10 * 0x290, 0x20);
    if (slots == 0) {
        slotHandle[0] = ResourcePointerBits(slotPool);
        return;
    }
    ClearMemory(slots, 0, 10 * 0x290);
    for (slotIndex = 0; slotIndex < 10; slotIndex++) {
        CzanMovieObj_InitDefaults((int *)(slots + slotIndex * 0x290));
    }

    *(int *)((unsigned char *)slotPool + 0x56b8) = ResourcePointerBits(slots);
    *(int *)((unsigned char *)slotPool + 0x56bc) = 10;
    slotHandle[0] = ResourcePointerBits(slotPool);
    slotHandle[1] = -1;
}

void ResourceSlotHandle_Release(int *slotHandle) {
    /* 0x800258C4 releases the slot currently referenced by a two-word slot handle
       and resets the index to -1. */
    if (slotHandle == 0) {
        return;
    }
    if (slotHandle[0] != 0) {
        ResourceSlotManager_ReleaseSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotHandle[1]);
    }
    slotHandle[1] = -1;
}

void ResourceSlotHandle_ReleaseIfManagerPresent(int *slotHandle) {
    /* FUN_80024F28 is the light wrapper around ResourceSlotManager_ReleaseSlot:
       if slotHandle[0] is nonzero, release the current slot. Unlike
       ResourceSlotHandle_Release/FUN_800258C4 it does not clear slotHandle[1]. */
    if (slotHandle == 0 || slotHandle[0] == 0) {
        return;
    }

    ResourceSlotManager_ReleaseSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotHandle[1]);
}

void ResourceSlotHandle_Rebind(int *slotHandle, int resourceOrPayload, int setupData) {
    /* 0x80025668 releases an existing slot handle, allocates a replacement slot,
       optionally initializes it, then applies resource/payload data.

       Confirmed original flow:
       - if slotHandle[0] exists, release slotHandle[1] through FUN_80186FB8
       - slotHandle[1] = -1
       - allocate a new slot with ResourceSlotManager_ClaimFreeSlot(slotHandle[0])
       - if setupData != 0, initialize the slot object through FUN_801843CC
       - store the new slot index in slotHandle[1]
       - fetch the slot object through ResourceSlotManager_GetClaimedSlot
       - call CzanMovieObj_Reset(slotObject)
       - call CzanMovieObj_LoadResource(slotObject, resourceOrPayload) */
    int slotIndex;
    int *slotObject;

    if (slotHandle == 0) {
        return;
    }

    ResourceSlotHandle_Release(slotHandle);
    if (slotHandle[0] == 0) {
        return;
    }

    slotIndex = ResourceSlotManager_ClaimFreeSlot((int *)ResourceHostPointerFromBits(slotHandle[0]));
    slotHandle[1] = slotIndex;
    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotIndex);
    if (slotObject != 0) {
        if (setupData != 0) {
            CzanMovieObj_AllocBuffer(slotObject, setupData);
        }
        CzanMovieObj_Reset(slotObject);
        CzanMovieObj_LoadResource(slotObject, resourceOrPayload);
    }

}

void MovieSlotHandle_ResetClaimedSlot(int *slotHandle) {
    /* 0x80025104 resets the claimed CzanMovieObj for a movie slot handle.

       Original flow:
       - if slotHandle[0] exists, fetch the claimed slot object through
         ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotHandle[1])
       - if the slot object exists, call CzanMovieObj_Reset(slotObject)

       This is a movie-slot reset wrapper, not a file/resource loader. */
    int *slotObject;
    if (slotHandle == 0 || slotHandle[0] == 0) {
        return;
    }

    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotHandle[1]);
    if (slotObject != 0) {
        CzanMovieObj_Reset(slotObject);
    }
}

int MovieSlotHandle_IsReadyForDisplay(int *slotHandle, int slotIndex) {
    /* 0x80025148 checks whether a claimed CzanMovieObj slot is ready enough for
       display/playback.

       Original flow:
       - if the current slot is active/pending according to ResourceSlotHandle_IsActivePending,
         return 0
       - fetch the claimed movie slot by slotIndex
       - query stream progress from the sound/video readers at slot +0x08 and +0xE4
       - return 1 only when video progress is at least 60% and audio/progressive data
         is at least 50%

       This is a readiness/progress check, not a reset or load call. */
    int *slotObject;
    unsigned int flags;

    if (slotHandle == 0 || slotHandle[0] == 0) {
        return 0;
    }

    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotIndex);
    if (slotObject == 0) {
        return 0;
    }

    flags = *(unsigned int *)((unsigned char *)slotObject + 0x230);
    return ((flags & 1U) != 0 && (flags & 2U) != 0);
}

void MovieSlotHandle_SetObjectEnabled(int *slotHandle, int slotIndex, int enabled) {
    /* 0x80025248 fetches one claimed CzanMovieObj slot and forwards enabled to the
       movie object's child/object record at +0x114.

       Original flow:
       - if slotHandle[0] exists, fetch ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotIndex)
       - if the slot object exists, call CzanMovieObjChild_SetEnabled(slotObject +0x114, enabled)

       This is the small on/off switch used by bank-5/movie binding mode changes. */
    int *slotObject;

    if (slotHandle == 0 || slotHandle[0] == 0) {
        return;
    }

    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotIndex);
    if (slotObject != 0) {
        CzanMovieObjChild_SetEnabled((int *)((unsigned char *)slotObject + 0x114), enabled);
    }
}

void MovieSlotHandle_LoadResource(int *slotHandle, int slotIndex, int resourceOrPath) {
    /* 0x80024F3C fetches a claimed CzanMovieObj slot, resets it, then binds a THP
       resource/path through CzanMovieObj_LoadResource(slotObject, resourceOrPath).

       This is the path-binding wrapper used by active-controller and select-common
       movie background code after a movie slot has already been allocated. */
    int *slotObject;

    if (slotHandle == 0 || slotHandle[0] == 0) {
        return;
    }

    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotIndex);
    if (slotObject != 0) {
        CzanMovieObj_Reset(slotObject);
        CzanMovieObj_LoadResource(slotObject, resourceOrPath);
    }
}

void MovieSlotHandle_StartPlayback(int *slotHandle, int slotIndex, int enabled) {
    /* 0x8002500C starts/prepares playback on a claimed CzanMovieObj slot.

       Original flow:
       - fetch ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotIndex)
       - call CzanMovieObj_ClearPlaybackState(slotObject)
       - clamp the input scalar to the movie fade/start range and store it at +0x150
       - call CzanMovieObj_StartPlayback(slotObject, enabled, 0)

       The common callers pass 1.0f as the scalar, gManager_802E70A8 as the handle,
       and an enable flag chosen from the active-controller movie category. */
    int *slotObject;

    if (slotHandle == 0 || slotHandle[0] == 0) {
        return;
    }

    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotIndex);
    if (slotObject != 0) {
        int durationFrames = *(int *)((unsigned char *)slotObject + 0x240);
        CzanMovieObj_ClearPlaybackState(slotObject);
        *(int *)((unsigned char *)slotObject + 0x238) = 0;
        *(int *)((unsigned char *)slotObject + 0x240) = durationFrames;
        *(float *)((unsigned char *)slotObject + 0x150) = 1.0f;
        CzanMovieObj_StartPlayback(slotObject, enabled, 0);
    }
}

int MovieSlotHandle_HasPlaybackStarted(int *slotHandle, int slotIndex) {
    /* 0x800250B0 polls bit 0x400 on a claimed CzanMovieObj slot.

       CzanMovieObj_StartPlayback sets this bit after updating playback state, so this
       wrapper is the small movie-background readiness/start poll used by the select
       common movie path. */
    int *slotObject;

    if (slotHandle == 0 || slotHandle[0] == 0) {
        return 0;
    }

    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotIndex);
    if (slotObject == 0) {
        return 0;
    }

    return (int)((*(unsigned int *)((unsigned char *)slotObject + 0x230) >> 10) & 1U);
}

void MovieSlotHandle_DrawMovie(int *slotHandle, int slotIndex, int drawFlags) {
    /* 0x800259C0 resolves the claimed CzanMovieObj, submits its movie frame, then
       draws it through CzanMovieObj_DrawMovieFrame. The real game path decodes
       the THP video component with THPVideoDecode into Y/U/V I8 planes, then
       draws those planes through the YCbCr TEV setup in CzanMovieObj_DrawMovieFrame.
       Do not fall back to host JPEG/RGBA decoding here; that produces the green
       corrupted frames seen with OP.thp. */
    int *slotObject;
    ResourceHandle *resource;
    MovieFrameCache *cache;
    int resourceBits;
    int frameIndex;
    int drawFrameToken;
    int decodeResult;
    static int loggedThpDecodeSuccess;
    static int loggedThpDecodeError;
    static int loggedThpAllocError;
    static int loggedThpAudioSuccess;
    static int loggedThpAudioError;

    (void)drawFlags;

    if (slotHandle == 0 || slotHandle[0] == 0) {
        return;
    }

    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotIndex);
    if (slotObject == 0) {
        return;
    }

    resourceBits = *(int *)((unsigned char *)slotObject + 0x228);
    resource = (ResourceHandle *)ResourceHostPointerFromBits(resourceBits);
    cache = MovieFrameCache_Get(resource);
    if (cache == 0 || cache->frameCount <= 0) {
        return;
    }

    frameIndex = *(int *)((unsigned char *)slotObject + 0x238);
    if (frameIndex < 0) {
        frameIndex = 0;
    }
    if (frameIndex >= cache->frameCount) {
        frameIndex = cache->frameCount - 1;
    }

    if (cache->audioOffsets != 0 && cache->audioSizes != 0) {
        int audioFrame;
        int audioTargetFrame = frameIndex + 3;
        int channelCapacity = cache->audioChannels > 0 ? cache->audioChannels : 2;
        int sampleCapacity = cache->audioSamples > 0 ? cache->audioSamples : 2048;
        int requiredSamples = sampleCapacity * (channelCapacity > 1 ? channelCapacity : 1);
        short *newBuffer;

        if (audioTargetFrame >= cache->frameCount) {
            audioTargetFrame = cache->frameCount - 1;
        }
        if (requiredSamples > gMoviePcmBufferSamples) {
            newBuffer = (short *)realloc(gMoviePcmBuffer, (size_t)requiredSamples * sizeof(short));
            if (newBuffer != 0) {
                gMoviePcmBuffer = newBuffer;
                gMoviePcmBufferSamples = requiredSamples;
            }
        }

        for (audioFrame = cache->playedAudioFrame + 1;
             gMoviePcmBuffer != 0 &&
             gMoviePcmBufferSamples >= requiredSamples &&
             audioFrame <= audioTargetFrame;
             audioFrame++) {
            int channelCount = channelCapacity;
            int decodedSamples;

            if (cache->audioOffsets[audioFrame] < 0 || cache->audioSizes[audioFrame] <= 0) {
                cache->playedAudioFrame = audioFrame;
                continue;
            }

            decodedSamples = THPAudioDecodeHost(
                gMoviePcmBuffer,
                (const unsigned char *)resource->data + cache->audioOffsets[audioFrame],
                cache->audioSizes[audioFrame],
                0,
                &channelCount);
            if (decodedSamples > 0) {
                PlayMoviePcm16(
                    gMoviePcmBuffer,
                    decodedSamples,
                    channelCount,
                    cache->audioFrequency > 0 ? cache->audioFrequency : 44100);
                cache->playedAudioFrame = audioFrame;
                if (!loggedThpAudioSuccess) {
                    RuntimeDebugReport(
                        "movie: THP audio decoded path=%s frame=%d samples=%d channels=%d rate=%d\n",
                        resource->path != 0 ? resource->path : "(unknown)",
                        audioFrame,
                        decodedSamples,
                        channelCount,
                        cache->audioFrequency > 0 ? cache->audioFrequency : 44100);
                    loggedThpAudioSuccess = 1;
                }
            }
            else {
                if (!loggedThpAudioError) {
                    RuntimeDebugReport(
                        "movie: THP audio decode failed path=%s frame=%d componentSize=%d\n",
                        resource->path != 0 ? resource->path : "(unknown)",
                        audioFrame,
                        cache->audioSizes[audioFrame]);
                    loggedThpAudioError = 1;
                }
                break;
            }
        }
    }

    if (!ThpDecodedFrame_Alloc(&gMovieDecodedFrame, cache->videoWidth, cache->videoHeight)) {
        if (!loggedThpAllocError) {
            RuntimeDebugReport(
                "movie: failed to allocate THP Y/U/V planes path=%s size=%dx%d\n",
                resource->path != 0 ? resource->path : "(unknown)",
                cache->videoWidth,
                cache->videoHeight);
            loggedThpAllocError = 1;
        }
        return;
    }

    if (cache->uploadedFrame != frameIndex) {
        THPInit();
        decodeResult = THPVideoDecodeHost(
            (const unsigned char *)resource->data + cache->offsets[frameIndex],
            cache->sizes[frameIndex],
            gMovieDecodedFrame.planeY,
            gMovieDecodedFrame.planeU,
            gMovieDecodedFrame.planeV,
            gMovieThpWork);

        if (decodeResult != THP_DECODE_OK) {
            if (!loggedThpDecodeError) {
                RuntimeDebugReport(
                    "movie: THPVideoDecodeHost returned %d path=%s frame=%d/%d componentSize=%d\n",
                    decodeResult,
                    resource->path != 0 ? resource->path : "(unknown)",
                    frameIndex,
                    cache->frameCount,
                    cache->sizes[frameIndex]);
                loggedThpDecodeError = 1;
            }
            if (cache->uploadedFrame < 0) {
                return;
            }
        }
        else {
            cache->uploadedFrame = frameIndex;
        }
    }
    drawFrameToken = cache->uploadedFrame >= 0 ? cache->uploadedFrame : frameIndex;

    {
        float x = *(float *)((unsigned char *)slotObject + 0x264);
        float y = *(float *)((unsigned char *)slotObject + 0x268);
        float width = *(float *)((unsigned char *)slotObject + 0x26c);
        float height = *(float *)((unsigned char *)slotObject + 0x270);

        if (width <= 0.0f) {
            width = 640.0f;
        }
        if (height <= 0.0f) {
            height = 480.0f;
        }
        DrawMovieYuvFrame(
            gMovieDecodedFrame.planeY,
            gMovieDecodedFrame.planeU,
            gMovieDecodedFrame.planeV,
            gMovieDecodedFrame.width,
            gMovieDecodedFrame.height,
            drawFrameToken,
            (int)x,
            (int)y,
            (int)width,
            (int)height);
        if (!loggedThpDecodeSuccess) {
            RuntimeDebugReport(
                "movie: THP decoded and drew Y/U/V frame path=%s frame=%d/%d size=%dx%d\n",
                resource->path != 0 ? resource->path : "(unknown)",
                drawFrameToken,
                cache->frameCount,
                gMovieDecodedFrame.width,
                gMovieDecodedFrame.height);
            loggedThpDecodeSuccess = 1;
        }
    }
}

int *MovieSlotHandle_GetClaimedObject(int *slotHandle, int slotIndex) {
    /* 0x80025368 is the thin getter for the current claimed CzanMovieObj slot.

       Original behavior:
       if slotHandle[0] exists:
         return ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotIndex)
       return null */
    if (slotHandle == 0 || slotHandle[0] == 0) {
        return 0;
    }

    return ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotIndex);
}

void MovieSlotHandle_SetPlacementRect(
    int *slotHandle,
    int slotIndex,
    double x,
    double y,
    double width,
    double height) {
    /* 0x80025508 writes four float placement/timing values into the claimed movie
       object. The original mirrors x/y into +0x264 and integer copies at +0x244/
       +0x248, then mirrors width/height into +0x26C and integer copies at
       +0x24C/+0x250.

       ActiveControllerMovieBindings_StartCategoryMovie uses it for category byte 1
       after starting the movie playback. */
    int *slotObject;
    float *movieObj;

    if (slotHandle == 0 || slotHandle[0] == 0) {
        return;
    }

    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotIndex);
    if (slotObject == 0) {
        return;
    }

    movieObj = (float *)slotObject;
    movieObj[0x264 / 4] = (float)x;
    movieObj[0x268 / 4] = (float)y;
    *(int *)((unsigned char *)slotObject + 0x244) = (int)(float)x;
    *(int *)((unsigned char *)slotObject + 0x248) = (int)(float)y;
    movieObj[0x26c / 4] = (float)width;
    movieObj[0x270 / 4] = (float)height;
    *(int *)((unsigned char *)slotObject + 0x24c) = (int)(float)width;
    *(int *)((unsigned char *)slotObject + 0x250) = (int)(float)height;
}

void MovieSlotHandle_SetPlaybackFlag278(int *slotHandle, int slotIndex, int value) {
    /* 0x8002561C writes one playback/control value to claimed movie object +0x278.
       The active-controller stage movie path passes zero after applying placement
       data, so keep the field-specific name until the flag meaning is confirmed. */
    int *slotObject;

    if (slotHandle == 0 || slotHandle[0] == 0) {
        return;
    }

    slotObject = ResourceSlotManager_GetClaimedSlot((int *)ResourceHostPointerFromBits(slotHandle[0]), slotIndex);
    if (slotObject != 0) {
        *(int *)((unsigned char *)slotObject + 0x278) = value;
    }
}

int ActiveControllerMovieBindings_HasPendingSlots(int *movieBindings, int mode) {
    /* 0x80055314 checks whether active controller movie/background slots are still
       pending.

       mode 1:
       - if global transition flag movieBindings +0xB36C is clear, checks special
         slot +0xB34C and category slots +0xB340/+0xB344/+0xB348 for active/pending
         movie slots through ResourceSlotHandle_IsActivePending.
       - if no active/pending slot is found, validates that required slots are ready
         through MovieSlotHandle_IsReadyForDisplay.

       mode 3:
       - checks category slots 1..2 and returns pending when any valid slot is not
         ready for display.

       Return value is nonzero while a binding is still pending/not ready. */
    (void)movieBindings;
    (void)mode;
    return 0;
}

void ActiveControllerMovieBindings_Reset(int *movieBindings) {
    /* 0x80055268 clears the active controller's movie/background binding records.

       Confirmed fields:
       movieBindings +0xB360 -> cleared to zero
       movieBindings +0xB34C -> special slot index for category 3
       movieBindings +0xB340/+0xB344/+0xB348 -> slot indices for categories 0..2
       movieBindings +0xB334/+0xB338/+0xB33C -> per-category active flags cleared

       Each valid slot index is released/reset through MovieSlotHandle_ResetClaimedSlot
       against gManager_802E70A8. */
    (void)movieBindings;
}

void ActiveControllerMovieBindings_SetMode(int *movieBindings, int mode) {
    /* 0x80055090 changes the active controller movie/background binding mode.

       Confirmed behavior:
       - stores mode at movieBindings +0xB360
       - mode 1 loads/rebinds categories 0..3 through
         ActiveControllerMovieBindings_LoadCategoryMovie and clears transition flags
         at +0xF068/+0xF06C
       - mode 2 enables the special/category movie slots through
         MovieSlotHandle_SetObjectEnabled, refreshes categories 1..2 through
         ActiveControllerMovieBindings_StartCategoryMovie, and sets +0xF06C = 1
       - mode 4 enables the special/current mode slot so it can be displayed during
         the timed controller transition

       This helper does not parse THP data itself; it controls which claimed movie
       slots are active for the active gameplay controller. */
    (void)movieBindings;
    (void)mode;
}

unsigned char ActiveControllerMovieBindings_GetVisibleModeIndex(int *movieBindings) {
    /* 0x80055008 resolves which bank-5/movie mode should be considered visible.

       Confirmed behavior:
       - starts from movieBindings +0x2D, the current mode byte
       - when transition state +0xB0C4 is 3, may return +0x2E, the previous mode byte
       - uses transition flags +0xE6E8/+0xB37C and timer +0xB0BC/+0xB0C0 to decide
         whether the old mode is still effectively visible */
    int state;

    if (movieBindings == 0) {
        return 0;
    }

    state = *(int *)((unsigned char *)movieBindings + 0xb0c4);
    if (state == 3) {
        if (*(int *)((unsigned char *)movieBindings + 0xe6e8) == 0) {
            if (*(float *)((unsigned char *)movieBindings + 0xb0bc) <
                *(float *)((unsigned char *)movieBindings + 0xb0c0)) {
                return *(unsigned char *)((unsigned char *)movieBindings + 0x2e);
            }
        }
        else if (*(int *)((unsigned char *)movieBindings + 0xb37c) == 0) {
            return *(unsigned char *)((unsigned char *)movieBindings + 0x2e);
        }
    }

    return *(unsigned char *)((unsigned char *)movieBindings + 0x2d);
}

void ActiveControllerMovieBindings_UpdateCategoryVisibility(int *movieBindings, int forceAllVisible) {
    /* 0x80055A94 enables/disables the claimed movie objects for the active
       controller's bank-5 category slots.

       Confirmed behavior:
       - resolves the visible mode through ActiveControllerMovieBindings_GetVisibleModeIndex
       - if forceAllVisible is nonzero, enables every valid category slot
       - otherwise, while movieBindings +0xB36C is clear and mode +0xB360 is 4,
         disables the currently visible category slot and enables the others
       - all toggles go through MovieSlotHandle_SetObjectEnabled(gManager_802E70A8, slot, enabled)

       This is a visibility mux for already claimed THP/movie slots; it does not load
       or start playback. */
    unsigned int visibleMode;
    unsigned int category;

    if (movieBindings == 0 || gActiveControllerMovieSlotManager == 0) {
        return;
    }

    visibleMode = ActiveControllerMovieBindings_GetVisibleModeIndex(movieBindings);
    for (category = 0; category < 3; category++) {
        int slotIndex;
        int enabled = 1;

        if (!forceAllVisible &&
            category == visibleMode &&
            *(int *)((unsigned char *)movieBindings + 0xb36c) == 0 &&
            *(int *)((unsigned char *)movieBindings + 0xb360) == 4) {
            enabled = 0;
        }

        if (category == 3) {
            slotIndex = *(int *)((unsigned char *)movieBindings + 0xb34c);
        }
        else {
            slotIndex = *(int *)((unsigned char *)movieBindings + 0xb340 + category * 4);
        }

        if (*(int *)((unsigned char *)movieBindings + 0xb340 + category * 4) != -1) {
            MovieSlotHandle_SetObjectEnabled(gActiveControllerMovieSlotManager, slotIndex, enabled);
        }
    }
}

void ActiveControllerMovieBindings_SetTransitionFlagAndUpdateVisibility(
    int *movieBindings,
    int forceAllVisible,
    int transitionFlag) {
    /* 0x80055084 stores the global transition/visibility flag at +0xB36C, then calls
       ActiveControllerMovieBindings_UpdateCategoryVisibility.

       The middle argument is passed through to the visibility update path in the
       original calling convention even though Ghidra may lose it around the runtime
       context helper. */
    if (movieBindings == 0) {
        return;
    }
    *(int *)((unsigned char *)movieBindings + 0xb36c) = transitionFlag;
    ActiveControllerMovieBindings_UpdateCategoryVisibility(movieBindings, forceAllVisible);
}

void ActiveControllerMovieBindings_LoadCategoryMovie(int *movieBindings, unsigned int category) {
    /* 0x80055590 chooses and binds the THP movie path for one active-controller
       background category.

       Confirmed path rules:
       - category 3 uses the special slot +0xB34C and binds
         "movie/stage/single01_w.thp"
       - category type byte 0 -> "movie/stage/upt01.thp"
       - category type byte 1 -> "movie/bgv/%s.thp" using the category string at
         owner +0xB328/+0xB32C/+0xB330
       - category type byte 2 -> randomized numbered stage movie path based on
         owner +0xB368:
           1: "movie/stage/upt_%s%02d.thp"
           2: "movie/stage/pop_%s%02d.thp"
           4: "movie/stage/mvo_%s%02d.thp"
           5/default: "movie/stage/fvo_%s%02d.thp"
       - category type byte 3 -> "movie/stage/%s.thp"
       - category type byte 4 -> "movie/zz_pv/ddr%03d.thp"

       After binding the path through MovieSlotHandle_LoadResource, several
       stage-movie paths mark movie object fields +0x284 and +0x288 as enabled. */
    (void)movieBindings;
    (void)category;
}

void ActiveControllerMovieBindings_StartCategoryMovie(int *movieBindings, unsigned int category) {
    /* 0x80055914 starts/enables one already-bound active-controller movie category.

       Confirmed behavior:
       - category 3 uses special slot +0xB34C; other categories use +0xB340 + category*4
       - returns when the slot id is -1 or global transition flag +0xB36C is set
       - non-special categories set a per-category started flag at owner +0xB334
       - category type 4 can suppress the enable flag when owner +0xA0 bit 0x400000 is clear
       - calls MovieSlotHandle_StartPlayback(1.0f, gManager_802E70A8, slot, enableFlag)
       - for owner +0xB324 category byte 1, also applies movie position/scale/timing
         through MovieSlotHandle_SetPlacementRect and clears the field at +0x278
         through MovieSlotHandle_SetPlaybackFlag278 */
    (void)movieBindings;
    (void)category;
}

void CzanMovieObj_AllocBuffer(int *movieObj, int bufferSize) {
    /* 0x801843CC is named by assert strings in zanMovie.cpp as
       CzanMovieObj::AllocBuffer().

       Confirmed fields:
       movieObj +0x220 -> allocated buffer pointer
       movieObj +0x224 -> allocated buffer size
       movieObj +0x230 -> flags; bit 0 means the movie object is active/allocated

       Original behavior:
       - asserts if flag bit 0 at +0x230 is already set
       - frees an existing buffer at +0x220 when inactive
       - if bufferSize != 0, allocates a 0x20-aligned buffer of bufferSize bytes
         and stores pointer/size at +0x220/+0x224 */
    if (movieObj == 0) {
        return;
    }
    if ((*(unsigned int *)((unsigned char *)movieObj + 0x230) & 1U) != 0) {
        return;
    }
    CzanMovieObj_FreeBuffer(movieObj);
    if (bufferSize > 0) {
        void *buffer = malloc((size_t)bufferSize);
        *(int *)((unsigned char *)movieObj + 0x220) = ResourcePointerBits(buffer);
        *(int *)((unsigned char *)movieObj + 0x224) = bufferSize;
    }
}

void CzanMovieObj_FreeBuffer(int *movieObj) {
    /* 0x801843CC's FreeBuffer half frees +0x220 only when the movie object is not
       active. Keep the same guard; active objects report/assert in the original. */
    int bufferBits;
    void *buffer;

    if (movieObj == 0) {
        return;
    }
    if ((*(unsigned int *)((unsigned char *)movieObj + 0x230) & 1U) != 0) {
        return;
    }

    bufferBits = *(int *)((unsigned char *)movieObj + 0x220);
    buffer = ResourceMappedHostPointerFromBits(bufferBits);
    if (buffer != 0) {
        free(buffer);
        *(int *)((unsigned char *)movieObj + 0x220) = 0;
        *(int *)((unsigned char *)movieObj + 0x224) = 0;
    }
}

void CzanMovieObj_InitDefaults(int *movieObj) {
    /* 0x80184108 initializes/defaults a CzanMovieObj slot after reset.

       Confirmed behavior:
       - clears flags/state at +0x228..+0x240
       - clears small blocks at +0x244, +0x254, +0x25C, +0x264, +0x26C
       - stores default scalar at +0x274 and default int 1 at +0x278
       - initializes color/config bytes at +0x27C to 0xFF
       - clears +0x280, +0x284, +0x288
       - reads a local config block from FUN_80166D54
       - clamps config floats into ranges and stores them at +0x14C..+0x180
       - copies config words to +0x170, +0x188, +0x18C, +0x190 */
    if (movieObj == 0) {
        return;
    }
    *(int *)((unsigned char *)movieObj + 0x228) = 0;
    *(int *)((unsigned char *)movieObj + 0x22c) = 0;
    *(unsigned int *)((unsigned char *)movieObj + 0x230) = 0;
    *(int *)((unsigned char *)movieObj + 0x234) = 0;
    *(int *)((unsigned char *)movieObj + 0x238) = 0;
    *(int *)((unsigned char *)movieObj + 0x23c) = 0;
    *(int *)((unsigned char *)movieObj + 0x240) = 0;
    memset((unsigned char *)movieObj + 0x244, 0, 0x10);
    memset((unsigned char *)movieObj + 0x254, 0, 8);
    memset((unsigned char *)movieObj + 0x25c, 0, 8);
    memset((unsigned char *)movieObj + 0x264, 0, 8);
    memset((unsigned char *)movieObj + 0x26c, 0, 8);
    *(float *)((unsigned char *)movieObj + 0x274) = 1.0f;
    *(int *)((unsigned char *)movieObj + 0x278) = 1;
    memset((unsigned char *)movieObj + 0x27c, 0xff, 4);
    *(int *)((unsigned char *)movieObj + 0x280) = 0;
    *(int *)((unsigned char *)movieObj + 0x284) = 0;
    *(int *)((unsigned char *)movieObj + 0x288) = 0;
}

void CzanMovieObj_Reset(int *movieObj) {
    /* 0x80184678 resets a CzanMovieObj slot record.

       Confirmed behavior:
       - when active and flag bit 1 is set, releases subsystems guarded by
         +0x230 bits 0x40000, 0x10000, and 0x20000
       - clears runtime fields +0x234, +0x238, +0x23C, +0x240
       - clears flag range +0x230 bits masked by 0xFFFFC3FF
       - resets child objects at +0x114, +0x08, and +0xE4
       - frees +0x228 when present
       - unregisters/register-clears +0x114 through manager DAT_802E71B8 +0x268
       - calls CzanMovieObj_InitDefaults(movieObj) for base reset/defaults */
    if (movieObj == 0) {
        return;
    }
    CzanMovieObj_ClearPlaybackState(movieObj);
    CzanMovieObj_InitDefaults(movieObj);
}

void CzanMovieObj_ClearPlaybackState(int *movieObj) {
    /* 0x80185150 clears the transient playback/subsystem state on an active
       CzanMovieObj without rebinding a resource or reinitializing defaults.

       It shares the flag-clearing/release block at the start of CzanMovieObj_Reset:
       when +0x230 bit 1 is set, it releases the child object/audio/progressive
       subsystems guarded by bits 0x40000, 0x10000, and 0x20000, then clears
       +0x234/+0x238/+0x23C/+0x240 and masks +0x230 with 0xFFFFC3FF. */
    if (movieObj == 0) {
        return;
    }
    *(int *)((unsigned char *)movieObj + 0x238) = 0;
    *(unsigned int *)((unsigned char *)movieObj + 0x230) &= 0xffffc3ffU;
    *(int *)((unsigned char *)movieObj + 0x23c) = 0;
    *(int *)((unsigned char *)movieObj + 0x240) = 0;
    *(int *)((unsigned char *)movieObj + 0x234) = 0;
}

void CzanMovieObj_StartPlayback(int *movieObj, int enabled, int startParam) {
    /* 0x80184FE8 starts playback on an already-loaded CzanMovieObj.

       Confirmed behavior:
       - requires +0x230 bit 0 to be set
       - clears transient playback state through the same block as
         CzanMovieObj_ClearPlaybackState
       - enters a critical section
       - clears +0x238/+0x23C and stores startParam at +0x234
       - enabled != 0 sets +0x230 bit 0x800; enabled == 0 clears it
       - if +0x230 bit 1 is already set, calls FUN_80185230(movieObj)
       - sets +0x230 bit 0x400 as the playback-started/requested bit */
    if (movieObj == 0) {
        return;
    }
    if (enabled == 0) {
        *(unsigned int *)((unsigned char *)movieObj + 0x230) &= ~0x800U;
    }
    else {
        *(unsigned int *)((unsigned char *)movieObj + 0x230) |= 0x800U;
    }
    *(int *)((unsigned char *)movieObj + 0x238) = 0;
    *(int *)((unsigned char *)movieObj + 0x234) = startParam;
    *(unsigned int *)((unsigned char *)movieObj + 0x230) |= 0x400U;
}

void CzanMovieObj_LoadResource(int *movieObj, int resourceOrPayload) {
    /* 0x801844E8 performs the same reset as CzanMovieObj_Reset, then binds a new
       resource/payload.

       Confirmed post-reset behavior:
       - registers movieObj +0x114 through DAT_802E71B8 +0x268
       - calls CzanSndRead_Open(movieObj +0x08, resourceOrPayload)
       - sets +0x230 bit 0
       - calls FUN_8019CE38(movieObj +0x08) */
    const char *path;
    const char *hostPath;
    ResourceHandle *resource;

    if (movieObj == 0) {
        return;
    }
    CzanMovieObj_Reset(movieObj);

    path = (const char *)ResourceMappedHostPointerFromBits(resourceOrPayload);
    if (path == 0 || path[0] == '\0') {
        *(unsigned int *)((unsigned char *)movieObj + 0x230) |= 1U;
        return;
    }

    hostPath = path;
    if (hostPath[0] == '/') {
        hostPath++;
    }

    resource = LoadResourceByPath(0, hostPath, 0);
    if (resource != 0) {
        *(int *)((unsigned char *)movieObj + 0x228) = ResourcePointerBits(resource);
        *(int *)((unsigned char *)movieObj + 0x22c) = resource->size;
        if (resource->loaded != 0 && resource->data != 0 && resource->size >= 0x18 &&
            memcmp(resource->data, "THP", 3) == 0) {
            *(int *)((unsigned char *)movieObj + 0x240) =
                (int)ReadBe32((const unsigned char *)resource->data + 0x14);
        }
    }

    if (resource != 0 && resource->loaded != 0) {
        *(unsigned int *)((unsigned char *)movieObj + 0x230) |= 3U;
    }
    else {
        *(unsigned int *)((unsigned char *)movieObj + 0x230) |= 1U;
    }
}

float CzanMovieObj_GetPlaybackFps(int *movieObj) {
    ResourceHandle *resource;
    int resourceBits;
    float fps;

    if (movieObj == 0) {
        return 60.0f;
    }

    resourceBits = *(int *)((unsigned char *)movieObj + 0x228);
    resource = (ResourceHandle *)ResourceHostPointerFromBits(resourceBits);
    if (resource == 0 ||
        resource->loaded == 0 ||
        resource->data == 0 ||
        resource->size < 0x18 ||
        memcmp(resource->data, "THP", 3) != 0) {
        return 60.0f;
    }

    fps = ReadBeFloat32((const unsigned char *)resource->data + 0x10);
    if (fps <= 0.0f || fps > 120.0f) {
        return 60.0f;
    }
    return fps;
}

void CzanMovieObjChild_SetEnabled(int *movieChild, int enabled) {
    /* 0x80190440 toggles bit 0x200 on the movie object's child/object record at
       slot +0x114. MovieSlotHandle_SetObjectEnabled reaches it after resolving a
       claimed CzanMovieObj slot.

       Original behavior is protected by Runtime_EnterCriticalSection:
       - enabled == 0 clears bit 0x200 when present
       - enabled != 0 sets bit 0x200 */
    (void)enabled;
    if (movieChild == 0) {
        return;
    }
    if (enabled == 0) {
        *(unsigned int *)((unsigned char *)movieChild + 0x24) &= ~0x200U;
    }
    else {
        *(unsigned int *)((unsigned char *)movieChild + 0x24) |= 0x200U;
    }
}

void LargeResourceManager_ReloadFromDefaultLink(int *largeResourceManager, int linkData) {
    /* 0x80025CA0 reloads the huge global manager allocated at DAT_802E70BC
       with size 0x3010B8. It initializes a CzanLinkManager-like stack object
       through CzanLinkManager_InitAndSetDefaultLink, tears down existing state if
       largeResourceManager[0] is nonzero, loads block 0 into the sub-manager at +0x2FBA7C, initializes
       a common manager at +0x10, initializes seven large banks starting at
       +0x91610 with stride 0x58534, marks the manager active, refreshes state,
       then releases the stack link manager with releaseMode -1.

       Bank setup passes 0x5460 for banks 0..4 and 0 for banks 5..6.
       The listing confirms incoming r4 is passed through as linkData. */
    (void)largeResourceManager;
    (void)linkData;
}

static void LargeResourceStageBank_InitForHost(int *stageBank) {
    int slotCount;
    int *entries;
    int **order;

    /* FUN_800B60A0 is a much larger stage-bank constructor. The slot table at
       stageBank +0x5370 is the part already used by LargeResourceStageBank_*.
       Give it a real host backing so the recovered clear/set calls operate on
       stable entries. */
    if (stageBank == 0) {
        return;
    }

    ClearMemory(stageBank, 0, 0x55f8);
    slotCount = 0x40;
    entries = (int *)MemoryPool_AllocateAligned(0, slotCount * 0x30, 0x20);
    order = (int **)MemoryPool_AllocateAligned(0, slotCount * (int)sizeof(int *), 0x20);
    if (entries == 0 || order == 0) {
        return;
    }

    stageBank[0x5370 / 4] = slotCount;
    stageBank[0x5374 / 4] = ResourcePointerBits(entries);
    stageBank[0x5378 / 4] = ResourcePointerBits(order);
    RuntimeSlotTable_ClearEntry(stageBank + 0x5370 / 4, -1);
}

int *LargeResourceManager_Init(int *largeResourceManager) {
    int bankIndex;

    /* FUN_80025A7C constructs gLargeResourceManager (size 0x3010B8).
       It initializes the common manager at +0x10, ten small records at +0x91590,
       seven large banks at +0x91610 stride +0x58534, the stage bank at +0x2FBA7C,
       then marks the manager inactive and resets the common-manager state. */
    if (largeResourceManager == 0) {
        return 0;
    }

    ClearMemory(largeResourceManager, 0, 0x3010b8);
    largeResourceManager[0xc042d] = 0;
    largeResourceManager[0] = 0;
    largeResourceManager[1] = 0;
    largeResourceManager[3] = 0;

    for (bankIndex = 0; bankIndex < 7; bankIndex++) {
        int *bank = (int *)((unsigned char *)largeResourceManager + 0x91610 + bankIndex * 0x58534);
        ClearMemory(bank, 0, 0x58534);
    }

    LargeResourceStageBank_InitForHost((int *)((unsigned char *)largeResourceManager + 0x2fba7c));
    return largeResourceManager;
}

void LargeResourceManager_ResetLoadedState(int *largeResourceManager) {
    /* FUN_80025DEC tears down an already-active large resource manager.

       Confirmed outer behavior:
       - if manager[0] is zero, do nothing
       - reset manager[1] and manager[3]
       - reset the common sub-manager at manager +0x10
       - clear manager[0]
       - reset seven large banks at byte offsets +0x91610, stride +0x58534
       - reset the stage-bank table at word offset +0xBEE9F (byte +0x2FBA7C)
       - repeat the manager[0]/[1]/[3]/common-sub-manager reset

       The child bodies FUN_80026400, FUN_8002F63C, FUN_80033988,
       FUN_8002F34C, and FUN_800B6864 are still pending, so this host function
       applies the exact known top-level field transitions and keeps those
       reset points documented instead of guessing their internal clears. */
    int bankIndex;

    if (largeResourceManager == 0 || largeResourceManager[0] == 0) {
        return;
    }

    largeResourceManager[1] = 0;
    largeResourceManager[3] = 0;
    largeResourceManager[0] = 0;

    for (bankIndex = 0; bankIndex < 7; bankIndex++) {
        int *bank = (int *)((unsigned char *)largeResourceManager + 0x91610 + bankIndex * 0x58534);
        (void)bank;
    }

    largeResourceManager[0] = 0;
    largeResourceManager[1] = 0;
    largeResourceManager[3] = 0;
}

void CzanSoundManager_ReleaseHandle(int *soundManager, int handle) {
    /* 0x8016A9E4 releases one sound/effect handle when its owner refcount reaches
       zero. The exact pool body is still pending; keep the call visible. */
    (void)soundManager;
    (void)handle;
}

void CzanSoundManager_StopAll(int *soundManager) {
    /* 0x8016A8C0 is called just before loading sound/DDRHP5_SOUND.brsar. */
    (void)soundManager;
}

void CzanSoundManager_SetGlobalPause(int *soundManager, int enabled, int immediate) {
    /* 0x8016AFB8 is used by the transition/effect slot teardown path with
       (soundManager, 1, 0). */
    (void)soundManager;
    (void)enabled;
    (void)immediate;
}

void CzanSoundManager_ClearAuxState(int *soundManager) {
    /* FUN_80169DDC frees the optional pointer at +0x9C0, then clears
       +0x9BC..+0x9CF. The host clears the fields without freeing the stored
       32-bit pointer until the owning sound allocator is mapped. */
    if (soundManager == 0) {
        return;
    }

    ClearMemory((unsigned char *)soundManager + 0x9bc, 0, 0x14);
}

void GlobalCueManager_ResetRuntimeState(int *cueManager) {
    int *globalContext;
    int *soundManager;
    int index;

    /* FUN_80024AF4 resets transient cue-manager state and clears the sound
       manager auxiliary block through FUN_80169DDC(*(DAT_802E71B8 +0x268)). */
    if (cueManager == 0) {
        return;
    }

    globalContext = GlobalRuntimeContext_Get();
    if (globalContext != 0) {
        soundManager = (int *)(uintptr_t)globalContext[0x268 / 4];
        CzanSoundManager_ClearAuxState(soundManager);
    }

    for (index = 0; index < 2; index++) {
        int value = *(int *)((unsigned char *)cueManager + 0x484 + index * 4);
        if (value != 0) {
            /* Original frees this pointer through MemoryPool_Free. Avoid freeing
               a possibly truncated Wii-style pointer in the host for now. */
        }
    }

    *(int *)((unsigned char *)cueManager + 0x480) = 0;
    ClearMemory((unsigned char *)cueManager + 0x484, 0, 8);
    *(int *)((unsigned char *)cueManager + 0x48c) = 0;
    *(int *)((unsigned char *)cueManager + 0x490) = 0;
    *(int *)((unsigned char *)cueManager + 0x494) = 0;
}

int *CzanSoundPlayerBank_FindFirstActiveNode(int *playerBank) {
    int *node;

    /* FUN_8018F508 scans a 0x9C-byte sound-player bank for the first node whose
       vtable predicate at +0x1C/+0x08 returns true. The host does not call Wii
       vtable pointers, so it returns the first linked node when the bank count is
       nonzero. */
    if (playerBank == 0 || playerBank[1] <= 0) {
        return 0;
    }

    node = (int *)(uintptr_t)playerBank[0x18 / 4];
    return node;
}

void CzanSoundPlayerNode_SetStopOrPassive(int *playerNode, int stopParam) {
    unsigned long long token;
    unsigned int flags;

    /* FUN_8018FB20 moves an active zanSndBase node either into hard-stop state
       (stopParam == 0) or passive/fade state (stopParam != 0). The original also
       unlinks/relinks through vtable-dependent helpers when a live owner exists;
       those callbacks stay pending until the sound node vtables are mapped. */
    if (playerNode == 0) {
        return;
    }

    flags = (unsigned int)playerNode[9];
    if ((flags & 0x0fU) == 0) {
        return;
    }

    token = Runtime_EnterCriticalSection();
    flags = (unsigned int)playerNode[9];
    if ((flags & 0x0fU) != 0) {
        *(short *)((unsigned char *)playerNode + 0x28) = 0;
        *(short *)((unsigned char *)playerNode + 0x2a) = (short)stopParam;
        playerNode[0x30 / 4] = 0;
        playerNode[0x2c / 4] = stopParam == 0 ? 0 : 1;
        if (playerNode[0x2c / 4] == 0) {
            playerNode[9] = (int)((flags & 0xfffffff0U) | 5U);
        }
        else if (playerNode[2] < 0) {
            playerNode[2] &= 0x7fffffff;
        }
    }
    Runtime_LeaveCriticalSection(token);
}

void CzanSoundManager_SetPlayerBankStopParam(int *soundManager, int bankIndex, int stopParam) {
    int *playerBanks;
    int *node;
    unsigned long long token;

    /* FUN_8016AFB8 targets one player bank at soundManager +0x9B8, stride 0x9C,
       then applies FUN_8018FB20 to active nodes in that bank. */
    if (soundManager == 0 || bankIndex < 0) {
        return;
    }

    playerBanks = (int *)(uintptr_t)*(int *)((unsigned char *)soundManager + 0x9b8);
    if (playerBanks == 0 || bankIndex >= *(int *)((unsigned char *)soundManager + 0x9b4)) {
        return;
    }

    token = Runtime_EnterCriticalSection();
    node = CzanSoundPlayerBank_FindFirstActiveNode((int *)((unsigned char *)playerBanks + bankIndex * 0x9c));
    while (node != 0) {
        int *next = (int *)(uintptr_t)node[0x10 / 4];
        if (((unsigned int)node[9] & 0x0fU) != 0) {
            CzanSoundPlayerNode_SetStopOrPassive(node, stopParam);
        }
        node = next;
    }
    Runtime_LeaveCriticalSection(token);
}

void GlobalCueManager_SetSoundPlayerBankStopParam(int *cueManager, int bankIndex, int stopParam) {
    int *globalContext;
    int *soundManager;

    /* FUN_8002458C receives gManager_802E70A4 but resolves the target through the
       global runtime context at DAT_802E71B8 +0x268. */
    (void)cueManager;
    globalContext = GlobalRuntimeContext_Get();
    if (globalContext == 0) {
        return;
    }

    soundManager = (int *)(uintptr_t)globalContext[0x268 / 4];
    CzanSoundManager_SetPlayerBankStopParam(soundManager, bankIndex, stopParam);
}

void LargeResourceManager_SetTransitionSoundBanks(int *largeResourceManager, int stopParam) {
    /* FUN_80026AC8 ignores its first argument and applies the value to sound player
       banks 8 and 9 through FUN_8002458C(gManager_802E70A4, bank, value). */
    (void)largeResourceManager;
    GlobalCueManager_SetSoundPlayerBankStopParam(0, 8, stopParam);
    GlobalCueManager_SetSoundPlayerBankStopParam(0, 9, stopParam);
}

static int *RuntimeSlotTable_GetEntry(int *slotTable, int slotIndex) {
    if (slotTable == 0 || slotIndex < 0 || slotIndex >= slotTable[0]) {
        return 0;
    }
    return (int *)((unsigned char *)ResourceHostPointerFromBits(slotTable[1]) + slotIndex * 0x30);
}

static int **RuntimeSlotTable_GetOrderArray(int *slotTable) {
    if (slotTable == 0 || slotTable[2] == 0) {
        return 0;
    }
    return (int **)ResourceHostPointerFromBits(slotTable[2]);
}

void RuntimeSlotTable_SetSortKey(int *slotTable, int slotIndex, int sortKey) {
    int *entry;
    int **order;
    int count;
    int i;
    int j;
    int changed;

    /* FUN_800A85DC writes entry +0x2C and, when the entry is active, sorts the
       pointer order table by sort key and pointer address. */
    entry = RuntimeSlotTable_GetEntry(slotTable, slotIndex);
    if (entry == 0) {
        return;
    }

    changed = (entry[0x2c / 4] != sortKey) && entry[0] != 0;
    entry[0x2c / 4] = sortKey;
    if (!changed) {
        return;
    }

    order = RuntimeSlotTable_GetOrderArray(slotTable);
    if (order == 0) {
        return;
    }

    count = slotTable[0];
    for (i = 0; i < count; i++) {
        int *a = order[i];
        if (a == 0 || a[0] == 0) {
            break;
        }
        for (j = i + 1; j < count; j++) {
            int *b = order[j];
            uintptr_t au;
            uintptr_t bu;
            if (b == 0) {
                continue;
            }
            au = (uintptr_t)a;
            bu = (uintptr_t)b;
            if ((a[0x2c / 4] == b[0x2c / 4] && bu < au) || b[0x2c / 4] < a[0x2c / 4]) {
                order[i] = b;
                order[j] = a;
                a = b;
            }
        }
    }
}

void RuntimeSlotTable_SetPayload(int *slotTable, int slotIndex, int payload) {
    int *entry;
    int **order;
    int count;
    int i;
    int firstEmpty;
    int stateChange;

    /* FUN_800A8428 writes entry payload +0x00 and keeps active entries packed at
       the front of the order table. Transition 0->nonzero bumps the sort key and
       re-sorts through FUN_800A85DC. */
    entry = RuntimeSlotTable_GetEntry(slotTable, slotIndex);
    if (entry == 0) {
        return;
    }

    if (entry[0] == 0 && payload != 0) {
        stateChange = 1;
    }
    else if (entry[0] != 0 && payload == 0) {
        stateChange = 2;
    }
    else {
        stateChange = 0;
    }

    entry[0] = payload;
    if (stateChange != 0) {
        order = RuntimeSlotTable_GetOrderArray(slotTable);
        if (order != 0) {
            count = slotTable[0];
            firstEmpty = count;
            for (i = 0; i < count; i++) {
                int *candidate = order[i];
                if (candidate == 0 || candidate[0] == 0) {
                    if (i < firstEmpty) {
                        firstEmpty = i;
                    }
                }
                else if (firstEmpty < i) {
                    int *tmp = order[firstEmpty];
                    order[firstEmpty] = candidate;
                    order[i] = tmp;
                    i = firstEmpty;
                    firstEmpty = count;
                }
            }
        }
    }

    if (stateChange == 1) {
        entry[0x2c / 4]++;
        RuntimeSlotTable_SetSortKey(slotTable, slotIndex, entry[0x2c / 4]);
    }
}

void RuntimeSlotTable_ClearEntry(int *slotTable, int slotIndex) {
    int *entry;
    int i;

    /* FUN_800A82F4 clears one slot or, when slotIndex == -1, resets the whole
       table. Each 0x30-byte entry gets default floats at +0x1C/+0x20 and bytes
       +0x28..+0x2B set to 0xFF. */
    if (slotTable == 0 || slotTable[1] == 0) {
        return;
    }

    if (slotIndex == -1) {
        int **order = RuntimeSlotTable_GetOrderArray(slotTable);
        for (i = 0; i < slotTable[0]; i++) {
            entry = RuntimeSlotTable_GetEntry(slotTable, i);
            if (entry == 0) {
                continue;
            }
            ClearMemory(entry, 0, 0x30);
            *(float *)((unsigned char *)entry + 0x1c) = 1.0f;
            *(float *)((unsigned char *)entry + 0x20) = 1.0f;
            ClearMemory((unsigned char *)entry + 0x28, 0xff, 4);
            if (order != 0) {
                order[i] = entry;
            }
        }
    }
    else {
        entry = RuntimeSlotTable_GetEntry(slotTable, slotIndex);
        if (entry == 0) {
            return;
        }
        RuntimeSlotTable_SetPayload(slotTable, slotIndex, 0);
        ClearMemory(entry, 0, 0x30);
        *(float *)((unsigned char *)entry + 0x1c) = 1.0f;
        *(float *)((unsigned char *)entry + 0x20) = 1.0f;
        ClearMemory((unsigned char *)entry + 0x28, 0xff, 4);
    }
}

void LargeResourceStageBank_ClearStageSlot(int *stageBank, int slotIndex, int payload, int sortKey) {
    int *slotTable;

    /* FUN_800B6DCC clears slot index +7 in the table at stageBank +0x5370, then
       optionally restores sort key/payload. */
    if (stageBank == 0) {
        return;
    }

    slotTable = (int *)((unsigned char *)stageBank + 0x5370);
    RuntimeSlotTable_ClearEntry(slotTable, slotIndex + 7);
    if (payload != 0) {
        RuntimeSlotTable_SetSortKey(slotTable, slotIndex + 7, sortKey + (int)0x80000000U);
        RuntimeSlotTable_SetPayload(slotTable, slotIndex + 7, payload);
    }
}

void LargeResourceStageBank_ClearMatrixSlot(
    int *stageBank,
    int bankIndex,
    int rowIndex,
    int payload,
    int sortKey) {
    int tableIndex;
    int *slotTable;

    /* FUN_800B6E4C clears table slot rowIndex + bankIndex * 4 + 0x0B at
       stageBank +0x5370, then optionally restores sort key/payload. */
    if (stageBank == 0) {
        return;
    }

    tableIndex = rowIndex + bankIndex * 4 + 0x0b;
    slotTable = (int *)((unsigned char *)stageBank + 0x5370);
    RuntimeSlotTable_ClearEntry(slotTable, tableIndex);
    if (payload != 0) {
        RuntimeSlotTable_SetSortKey(slotTable, tableIndex, sortKey + (int)0x80000000U);
        RuntimeSlotTable_SetPayload(slotTable, tableIndex, payload);
    }
}

void CGameTransitionSlot_Reset(int *transitionSlot) {
    int *globalContext;
    int *textureManager;
    int *modelManager;
    int i;
    int j;

    /* FUN_8012543C tears down/reset the CGame transition subsystem at cgame +0x428.
       The original releases several owned subsystem objects through their vtables.
       The host clears the confirmed fields and calls the safe mapped managers. */
    if (transitionSlot == 0 || transitionSlot[0] == 0 || transitionSlot[1] == 0) {
        return;
    }

    globalContext = GlobalRuntimeContext_Get();
    textureManager = 0;
    modelManager = 0;
    if (globalContext != 0) {
        textureManager = (int *)(uintptr_t)globalContext[0x26c / 4];
        modelManager = (int *)(uintptr_t)globalContext[0x270 / 4];
    }

    for (i = 0; i < 4; i++) {
        LargeResourceStageBank_ClearStageSlot(0, i, 0, 0);
        for (j = 0; j < 7; j++) {
            LargeResourceStageBank_ClearMatrixSlot(0, j, i, 0, 0);
        }
    }

    transitionSlot[0x20] = 0;
    transitionSlot[0x21] = 0;
    transitionSlot[0x22] = 0;
    transitionSlot[0x23] = 0;
    transitionSlot[0x2f] = 0;
    for (i = 0; i < 4; i++) {
        transitionSlot[0x2b + i] = 0;
    }

    if (transitionSlot[8] != -1) {
        TextureManager_DeleteTexture((TextureManagerKnownFields *)textureManager, transitionSlot[8]);
        transitionSlot[8] = -1;
    }

    for (i = 0; i < 7; i++) {
        transitionSlot[0x419 + i] = 0;
        transitionSlot[0x24 + i] = 0;
    }

    RuntimeSlotTable_ClearEntry(transitionSlot + 9, -1);

    for (i = 0; i < 0x14; i++) {
        transitionSlot[0x427 + i] = -1;
    }

    for (i = 0; i < 7; i++) {
        int *rowBase = transitionSlot + i * 0x74;
        int *clearBase = transitionSlot + 0xbb + i * 0x74;
        for (j = 0; j < 0x0c; j++) {
            ClearMemory(clearBase + j * 9, 0, 0x24);
            rowBase[0xbb + j * 9] = -1;
            rowBase[0xbc + j * 9] = -1;
        }
    }

    CzanModelManager_UnloadBank(modelManager, 2);
    transitionSlot[0x445] = 0;
    transitionSlot[0x444] = 0;
    transitionSlot[1] = 0;
}

void CzanSoundManager_LoadArchive(int *soundManager, const char *path) {
    /* 0x8016A638 reloads the sound archive owned by the manager at DAT_802E71B8 +0x268.

       Confirmed behavior:
       - frees any active player list at +0x9B0/+0x9B4/+0x9B8
       - destroys the previous archive object at +0x9AC
       - allocates a 0x158-byte archive object and initializes it
       - opens/binds the archive resource path through FUN_80181F0C */
    ResourceHandle *resource;

    if (soundManager == 0 || path == 0) {
        return;
    }

    if (*(int *)((unsigned char *)soundManager + 0x9b8) != 0) {
        *(int *)((unsigned char *)soundManager + 0x9b8) = 0;
    }
    *(int *)((unsigned char *)soundManager + 0x9b0) = 0;
    *(int *)((unsigned char *)soundManager + 0x9b4) = 0;

    resource = LoadResourceByPath(0, path, 0);
    *(int *)((unsigned char *)soundManager + 0x9ac) = (int)(uintptr_t)resource;
}

void LargeResourceManager_ActivateDefaultAudioReferences(int *largeResourceManager, int *globalContext) {
    /* 0x80023634 is the setup/refcount counterpart to 0x80023030. It first releases
       any active DAT_8027A570 reference set, then marks +0x43C active and increments
       refcounts for the default table entries. */
    static const int defaultHandles[] = {
        -1,
    };
    int index;
    int count = (int)(sizeof(defaultHandles) / sizeof(defaultHandles[0]));
    int *soundManager;

    if (largeResourceManager == 0 || globalContext == 0) {
        return;
    }

    soundManager = (int *)(uintptr_t)globalContext[0x268 / 4];
    if (soundManager != 0 && *(int *)((unsigned char *)largeResourceManager + 0x43c) != 0) {
        for (index = 0; index < count; index++) {
            if (defaultHandles[index] != -1) {
                CzanSoundManager_ReleaseHandle(soundManager, defaultHandles[index]);
            }
        }
        for (index = 0; index < count; index++) {
            int handle = defaultHandles[index];
            if (handle != -1) {
                int *refCount = largeResourceManager + handle;
                if (refCount[1] > 0) {
                    refCount[1]--;
                }
                if (refCount[1] == 0) {
                    CzanSoundManager_StopAll(soundManager);
                }
            }
        }
        *(int *)((unsigned char *)largeResourceManager + 0x43c) = 0;
    }

    *(int *)((unsigned char *)largeResourceManager + 0x43c) = 1;
    for (index = 0; index < count; index++) {
        int handle = defaultHandles[index];
        if (handle == -1) {
            continue;
        }
        if ((unsigned int)handle < 0x10eU) {
            int *refCount = largeResourceManager + handle;
            if (refCount[1] == 0 && soundManager != 0) {
                CzanSoundManager_SetGlobalPause(soundManager, 0, 0);
            }
            refCount[1]++;
        }
        else {
            printf("large resource manager: invalid default audio reference %d\n", handle);
        }
    }
}

void LargeResourceManager_ResetAudioAndLoadDefaultSound(int *largeResourceManager, int *globalContext) {
    /* 0x80023030 clears pending sound/effect references owned by the large resource
       manager, then reloads sound/DDRHP5_SOUND.brsar through DAT_802E71B8 +0x268. */
    int *soundManager;

    if (largeResourceManager == 0 || globalContext == 0) {
        return;
    }

    soundManager = (int *)(uintptr_t)globalContext[0x268 / 4];
    if (soundManager == 0) {
        return;
    }

    if (*(int *)((unsigned char *)largeResourceManager + 0x43c) != 0) {
        *(int *)((unsigned char *)largeResourceManager + 0x43c) = 0;
    }
    if (*(int *)((unsigned char *)largeResourceManager + 0x440) != 0) {
        ClearMemory((unsigned char *)largeResourceManager + 0x440, 0, 8);
        *(int *)((unsigned char *)largeResourceManager + 0x444) = 0xffff;
    }
    if (*(int *)((unsigned char *)largeResourceManager + 0x448) != 0) {
        CzanSoundManager_SetGlobalPause(soundManager, 1, 0);
        ClearMemory((unsigned char *)largeResourceManager + 0x448, 0, 0x30);
        *(int *)((unsigned char *)largeResourceManager + 0x44c) = -1;
        *(int *)((unsigned char *)largeResourceManager + 0x454) = -1;
    }

    CzanSoundManager_StopAll(soundManager);
    CzanSoundManager_LoadArchive(soundManager, "sound/DDRHP5_SOUND.brsar");
}

void CharacterAssetManager_UnloadActiveAssets(int *characterAssetManager) {
    /* 0x800CC630 tears down active character assets when
       characterAssetManager +0x28164 == 1.

       Confirmed original flow:
       - clears the active flag at +0x28164
       - releases/clears the manager-local object at +0x28168
       - unloads CzanModelManager bank 4 from gManager_802E70B8
       - clears special/character part state at characterAssetManager +0x34 */
    if (characterAssetManager == 0) {
        return;
    }
    if (*(int *)((unsigned char *)characterAssetManager + 0x28164) == 1) {
        *(int *)((unsigned char *)characterAssetManager + 0x28164) = 0;
    }
}
