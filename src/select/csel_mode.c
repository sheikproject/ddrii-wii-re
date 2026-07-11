#include "select/csel_mode.h"

#include "render/render_engine.h"
#include "model/czan_model.h"
#include "resource/czan_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int gCSelModeHostLinkResourceSize;

#define HOST_CZAN_MAX_GROUPS 64
#define HOST_CZAN_MAX_OBJECTS 512
#define HOST_CZAN_MAX_LINK_SIZES 128
#define HOST_CZAN_TEXTURE_SLOTS 256

typedef struct HostCzanObject {
    int used;
    int groupHandle;
    int childIndex;
    int descriptorType;
    int enabled;
    int drawEnabled;
    int activeByte17d;
    int textureSlot;
    int textureIndex;
    float x;
    float y;
    int width;
    int height;
    unsigned char color[4];
    char name[17];
} HostCzanObject;

typedef struct HostCzanGroup {
    int used;
    int state;
    int childCount;
    int firstObjectIndex;
    unsigned char groupByte08;
    unsigned char groupByte09;
    unsigned char statusFlags24;
    unsigned char byte25;
} HostCzanGroup;

typedef struct HostLinkSizeEntry {
    const void *data;
    unsigned int size;
} HostLinkSizeEntry;

static TextureSlotKnownFields gHostTextureSlots[HOST_CZAN_TEXTURE_SLOTS];
static TextureManagerKnownFields gHostTextureManager = {
    gHostTextureSlots,
    0,
    HOST_CZAN_TEXTURE_SLOTS
};
static HostCzanGroup gHostCzanGroups[HOST_CZAN_MAX_GROUPS];
static HostCzanObject gHostCzanObjects[HOST_CZAN_MAX_OBJECTS];
static HostLinkSizeEntry gHostLinkSizes[HOST_CZAN_MAX_LINK_SIZES];

static void HostCzan_Reset(void) {
    memset(gHostCzanGroups, 0, sizeof(gHostCzanGroups));
    memset(gHostCzanObjects, 0, sizeof(gHostCzanObjects));
    memset(gHostLinkSizes, 0, sizeof(gHostLinkSizes));
    memset(gHostTextureSlots, 0, sizeof(gHostTextureSlots));
    gHostTextureManager.nextTextureSlot = 0;
}

static void HostCzan_RegisterLinkSize(const void *data, unsigned int size) {
    int i;

    if (data == 0 || size == 0) {
        return;
    }

    for (i = 0; i < HOST_CZAN_MAX_LINK_SIZES; i++) {
        if (gHostLinkSizes[i].data == data || gHostLinkSizes[i].data == 0) {
            gHostLinkSizes[i].data = data;
            gHostLinkSizes[i].size = size;
            return;
        }
    }
}

static unsigned int HostCzan_GetRegisteredLinkSize(const void *data) {
    int i;

    for (i = 0; i < HOST_CZAN_MAX_LINK_SIZES; i++) {
        if (gHostLinkSizes[i].data == data) {
            return gHostLinkSizes[i].size;
        }
    }

    return 0;
}

static int HostCzan_FindFreeGroup(void) {
    int i;

    for (i = 0; i < HOST_CZAN_MAX_GROUPS; i++) {
        if (!gHostCzanGroups[i].used) {
            return i;
        }
    }

    return -1;
}

static int HostCzan_FindFreeObject(void) {
    int i;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (!gHostCzanObjects[i].used) {
            return i;
        }
    }

    return -1;
}

static void HostCzan_DefaultObjectPlacement(HostCzanObject *object, int groupHandle, int childIndex) {
    int column = childIndex % 4;
    int row = childIndex / 4;

    object->x = 70.0f + (float)column * 120.0f + (float)(groupHandle % 3) * 16.0f;
    object->y = 74.0f + (float)row * 88.0f + (float)(groupHandle % 4) * 8.0f;
}

static void HostCzan_DrawObject(HostCzanObject *object) {
    RenderQuad quad;
    unsigned int dummyColor;

    if (object == 0 ||
        HostCzan_ShouldSkipObject(object) ||
        !object->used ||
        !object->drawEnabled ||
        object->width <= 0 ||
        object->height <= 0) {
        return;
    }

    quad.x = object->x;
    quad.y = object->y;
    quad.z = 0.0f;
    quad.width = (float)object->width;
    quad.height = (float)object->height;

    if (object->textureSlot >= 0) {
        DrawTexturedQuad(
            &quad,
            object->width,
            object->height,
            object->color,
            (void *)(long)object->textureSlot,
            object->textureIndex);
    }
    else {
        dummyColor = 0xFFFFFF80u;
        DrawFilledRect((int)object->x, (int)object->y, 0, object->width, object->height, &dummyColor, 0);
    }
}

static int HostCzan_IsWindowLikeObject(const HostCzanObject *object) {
    if (object == 0) {
        return 0;
    }

    return strncmp(object->name, "white_window", 12) == 0 ||
           object->width > 360 ||
           object->height > 280;
}

static int HostCzan_ShouldSkipObject(const HostCzanObject *object) {
    if (object == 0) {
        return 1;
    }

    return strncmp(object->name, "cha_sil", 7) == 0 ||
           strncmp(object->name, "@dummy", 6) == 0;
}

static unsigned short ReadBe16(const unsigned char *p) {
    return (unsigned short)(((unsigned int)p[0] << 8) | (unsigned int)p[1]);
}

static unsigned int ReadBe32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) |
           ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) |
           (unsigned int)p[3];
}

static void CSelMode_LogCaeDescriptors(const CzanLinkBlock *objectBlock, unsigned int blockIndex) {
    CzanLinkBlock nestedBlock;
    CzanLinkBlock caeBlock;
    unsigned int descriptorCount;
    unsigned int descriptorOffset;
    unsigned int i;

    if (!CzanLinkResource_GetBlock(objectBlock->data, objectBlock->size, 1, &caeBlock)) {
        return;
    }

    if (caeBlock.size < 0x10 ||
        caeBlock.data[0] != 'C' ||
        caeBlock.data[1] != 'A' ||
        caeBlock.data[2] != 'E' ||
        caeBlock.data[3] != '_') {
        return;
    }

    descriptorCount = ReadBe16(caeBlock.data + 8);
    descriptorOffset = ReadBe32(caeBlock.data + 0x0C);
    if (descriptorOffset >= caeBlock.size) {
        return;
    }

    printf("CSelMode: block %u CAE descriptors=%u\n", blockIndex, descriptorCount);
    for (i = 0; i < descriptorCount && i < 8; i++) {
        const unsigned char *descriptor = caeBlock.data + descriptorOffset + i * 0x20;
        if ((unsigned int)(descriptor - caeBlock.data) + 0x20 > caeBlock.size) {
            break;
        }

        printf("CSelMode:   desc %u name=%.16s tex=%u type=%u animOff=0x%X\n",
               i,
               descriptor,
               ReadBe16(descriptor + 0x10),
               (unsigned int)descriptor[0x14],
               ReadBe32(descriptor + 0x1C));
    }

    if (CzanLinkResource_GetBlock(objectBlock->data, objectBlock->size, 0, &nestedBlock) &&
        CzanLinkResource_IsValid(nestedBlock.data, nestedBlock.size)) {
        printf("CSelMode: block %u texture WII blockCount=%u\n",
               blockIndex,
               CzanLinkResource_GetBlockCount(nestedBlock.data, nestedBlock.size));
    }
}

const CSelModeChoice CSelMode_ChoiceTable[5] = {
    { 0x02, 0x01, 0x01, -1 },
    { 0x02, 0x02, 0x07, -1 },
    { 0x1B, 0x03, 0x05, -1 },
    { 0x15, 0x06, 0x00, -1 },
    { -1, -1, -1, -1 },
};

int CSelMode_ModeIdToSelectedIndex(int modeId) {
    switch (modeId) {
        case 1:
            return 0;
        case 2:
            return 1;
        case 3:
            return 2;
        case 6:
            return 3;
        default:
            return 0;
    }
}

const CSelModeChoice *CSelMode_GetChoice(int selectedModeIndex) {
    if (selectedModeIndex < 0 || selectedModeIndex >= 5) {
        return 0;
    }
    return &CSelMode_ChoiceTable[selectedModeIndex];
}

int CSelMode_MoveSelection(int selectedModeIndex, int direction) {
    selectedModeIndex += direction;

    if (selectedModeIndex < 0) {
        return 4;
    }
    if (selectedModeIndex >= 5) {
        return 0;
    }
    return selectedModeIndex;
}

int CSelMode_Init(void *cselMode) {
    (void)cselMode;

    /* Original initializes a 14-entry controller at +0x160.
       Each entry is 0x50 bytes and uses callbacks at 0x8006309C/0x800630D8. */
    puts("CSelMode: init");
    return 0;
}

void CSelMode_OnEnter(void *cselMode, void *linkData) {
    unsigned int blockCount;
    unsigned int i;
    (void)cselMode;

    /* Original links the Czan resource, builds Czan UI object groups from blocks
       0..6, reuses one shared mode-button object group across entries 7..13,
       applies region-specific position/animation tables to entries 6..13, and
       sets modeState at +0x130 to 1. */
    puts("CSelMode: on enter");
    HostCzan_Reset();

    blockCount = CzanLinkResource_GetBlockCount(linkData, gCSelModeHostLinkResourceSize);
    if (blockCount == 0) {
        puts("CSelMode: linkData is not a valid WII resource");
        return;
    }

    printf("CSelMode: WII link blockCount=%u\n", blockCount);
    for (i = 0; i < blockCount && i < 7; i++) {
        CzanLinkBlock block;
        if (CzanLinkResource_GetBlock(linkData, gCSelModeHostLinkResourceSize, i, &block)) {
            printf("CSelMode: block %u offset=0x%X size=0x%X\n",
                   i,
                   (unsigned int)(block.data - (const unsigned char *)linkData),
                   block.size);
            if (CzanLinkResource_IsValid(block.data, block.size)) {
                printf("CSelMode: block %u nested WII blockCount=%u\n",
                       i,
                       CzanLinkResource_GetBlockCount(block.data, block.size));
                CSelMode_LogCaeDescriptors(&block, i);
            }
        }
    }
}

void CSelMode_Update(void) {
    /* Original reads input, changes selectedModeIndex, plays animations/sounds,
       and commits parentSelectData[0..2] from CSelMode_ChoiceTable. */
    puts("CSelMode: update");
}

void CSelMode_SetInitialSelectedMode(void *cselMode) {
    (void)cselMode;

    /* Original reads **(cselMode + 0x10), maps mode IDs 1/2/3/6 to indices 0..3,
       and writes selectedModeIndex at cselMode + 0x134. */
}

void CSelMode_SetHostLinkResourceSize(unsigned int resourceSize) {
    gCSelModeHostLinkResourceSize = resourceSize;
}

void CSelectCommon_LoadResource(int *selectCommon, void *linkData) {
    /* 0x800982A8 is the select_cmn resource loader for the common background
       scene. It uses CzanLinkManager_GetBlockInfo for block pointer+size pairs.

       Confirmed block map:
       - blocks 0/1: primary model/texture pair for the CtsStageObj at +0x48.
       - block 2: continuation block attached to that +0x48 stage object.
       - blocks 3/4: primary model/texture pair for the CtsStageObj at +0xB8.
       - block 5: primary CzanModel block for the owner at +0x128.
       - blocks 6..0xF: ten continuation/ZAB blocks loaded into owner +0x128.
       - block 0x10: shared CSelModeEntry UI object group used by three entries. */
    int i;
    int blockSize;
    void *block;
    int *modelOwner;

    if (selectCommon == 0 || linkData == 0) {
        return;
    }

    modelOwner = (int *)((unsigned char *)selectCommon + 0x128);
    if (CzanLinkManager_GetBlockInfo((int *)linkData, 5, &block, &blockSize)) {
        CzanModelOwner_CreateModelFromPrimaryBlock(modelOwner, block, blockSize);
        CzanModelOwner_SetContinuationCount(modelOwner, 10);
        CzanModelOwner_BuildRuntimeDataAt80(modelOwner);
    }

    for (i = 0; i < 10; i++) {
        if (CzanLinkManager_GetBlockInfo((int *)linkData, 6 + i, &block, &blockSize)) {
            (void)blockSize;
            CzanModelOwner_LoadContinuationBlock(modelOwner, block, i);
        }
    }

    CzanModelOwner_SetAnimationStartFrame(modelOwner, 1.0);
}

void CSelectCommon_UpdateMovieBackground(int *selectCommon, int skipInitialUpdate, int allowMovieStart, int forceInitialBind) {
    /* 0x80098810 updates the select-common THP/movie-backed background layer.

       The original is entered through a saved-register helper, so the decompiler
       shows an unused first parameter. The real object is the selectCommon pointer
       recovered from that helper.

       Confirmed responsibilities:
       - pauses/resets the CzanModelOwner at selectCommon +0x128 through
         CzanModelOwner_SetAnimationStartFrame-like helpers before rebinding video.
       - resets the two CtsStageObj layers at +0x48 and +0xB8 through their vtables.
       - when allowMovieStart is nonzero and selectCommon +0x358 is 1, selects a
         movie path from /sound/stream/mu_bgm_999/movie/b_* based on
         selectCommon +0x34C.
       - uses movie manager gManager_802E70A8 and handle selectCommon +0x344.
       - once the movie is ready, binds the movie object/texture to the two
         CSelModeEntry objects at +0x254 and +0x2A4 through the Czan UI manager
         stored at selectCommon +0x250.
       - selectCommon +0x350/+0x354 track requested/active movie binding.
       - selectCommon +0x358/+0x35C/+0x360/+0x364/+0x368 are the loader/start/bind
         state flags controlling the movie background and menu-entry reveal path.

       Important follow-up callees from the original:
       - MovieSlotHandle_LoadResource: load/assign THP movie path.
       - MovieSlotHandle_HasPlaybackStarted: poll movie playback-started bit.
       - MovieSlotHandle_StartPlayback: start/fade movie playback.
       - MovieSlotHandle_GetClaimedObject: get active movie object/state.
       - FUN_80025104: release/stop movie binding.
       - CSelectCommon_RevealMovieEntriesPrimary /
         CSelectCommon_RevealMovieEntriesAlternate: reveal/transition the two menu entry objects
         after the movie object has been attached. */
    (void)skipInitialUpdate;
    (void)allowMovieStart;
    (void)forceInitialBind;
    if (selectCommon == 0) {
        return;
    }
}

void CSelectCommon_RevealMovieEntriesPrimary(int *selectCommon, int useImmediateTiming) {
    /* 0x80098FA0 reveals/transitions the two movie-backed CSelModeEntry objects
       after CSelectCommon_UpdateMovieBackground has attached the movie object.

       Confirmed behavior:
       - if selectCommon +0x350 is set, disables object 0 in entry +0x254, applies
         timing/layout through CSelModeEntry_ResetObjectAnimation and
         CSelModeEntry_StartObjectAnimation, runs the entry vtable method at +0x14,
         then binds/updates the UI object through the Czan UI manager at +0x250 and
         applies a 10-frame alpha/color transition
       - repeats the same flow for selectCommon +0x354 and entry +0x2A4
       - useImmediateTiming selects between the shorter FLOAT_802E883C timing and
         the alternate FLOAT_802E8850 timing/layout mode */
    (void)selectCommon;
    (void)useImmediateTiming;
}

void CSelectCommon_RevealMovieEntriesAlternate(int *selectCommon, int useImmediateTiming) {
    /* 0x800991C4 is the alternate movie-entry reveal path.

       It mirrors CSelectCommon_RevealMovieEntriesPrimary but uses the alternate
       layout/animation ids for the two entries:
       - entry +0x254 uses animation/layout id 1
       - entry +0x2A4 uses animation/layout id 3

       This is selected by CSelectCommon_UpdateMovieBackground when +0x368 is not
       the primary reveal mode. */
    (void)selectCommon;
    (void)useImmediateTiming;
}

int CSelModeEntry_Init(void *entry) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;

    /* Original calls the shared UI-entry base initializer at 0x801102DC and sets
       the entry vtable at +0x40 to PTR_PTR_802BEA38. */
    if (modeEntry != 0) {
        modeEntry->vtable = (void *)0x802BEA38;
    }
    return entry != 0;
}

int CSelModeEntry_Update(void *entry, short activeCountOrFlag) {
    /* Original updates the shared UI-entry base state, then frees/releases the
       entry through MemoryPool_Free when the caller passes a positive flag/count. */
    if (entry != 0 && activeCountOrFlag > 0) {
        return 1;
    }
    return entry != 0;
}

int CSelModeEntry_AddUiObject(void *entry, int linkBlock) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;
    int slot;

    /* 0x80110524 creates/registers a UI object group from a non-null Czan link block
       via CzanUiManager_CreateObjectGroup, stores the returned handle
       at +0x14+n*4, and increments +0x34. */
    if (modeEntry == 0 || linkBlock == 0 || modeEntry->objectHandleCount >= 8) {
        return -1;
    }

    slot = modeEntry->objectHandleCount;
    modeEntry->objectHandles[slot] = -1;
    modeEntry->objectHandleCount++;
    return modeEntry->objectHandles[slot];
}

int CSelModeEntry_AddChildUiObject(void *entry, int objectId) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;
    int slot;

    /* 0x8011058C creates/registers a cloned/alternate UI object group through
       CzanUiManager_CloneObjectGroup, then stores the returned handle in the
       same +0x14 handle array. */
    if (modeEntry == 0 || objectId == -1 || modeEntry->objectHandleCount >= 8) {
        return -1;
    }

    slot = modeEntry->objectHandleCount;
    modeEntry->objectHandles[slot] = -1;
    modeEntry->objectHandleCount++;
    return modeEntry->objectHandles[slot];
}

void CSelModeEntry_SetAnimationOrLayout(void *entry, int objectSlot, int animationId, int animationData) {
    (void)entry;
    (void)objectSlot;
    (void)animationId;
    (void)animationData;

    /* 0x80110754 applies animation/layout data to one stored object handle.
       animationId == -1 calls FUN_801751B8(uiManager, handle, animationData);
       otherwise it calls FUN_80175240(uiManager, handle). */
}

void CSelModeEntry_ResetObjectAnimation(void *entry, int objectSlot) {
    /* 0x801106C4 forwards the selected CSelModeEntry object handle to the Czan UI
       manager reset/clear-animation helper.

       Original behavior:
       CzanUiManager_ResetObjectAnimation(entry +0x3C, entry->objectHandles[objectSlot]) */
    (void)entry;
    (void)objectSlot;
}

void CSelModeEntry_StartObjectAnimation(
    double startFrame,
    void *entry,
    int objectSlot,
    int animationId,
    unsigned char mode,
    int playbackMode) {
    /* 0x801105FC starts/configures one CSelModeEntry object animation when the
       object handle is valid.

       Confirmed behavior:
       - object handle comes from entry +0x14 + objectSlot*4
       - calls UI-manager helpers at 0x80174F60 and 0x80174FE0 with mode/playbackMode
       - calls 0x80174E2C(startFrame, uiManager, objectHandle, animationId)
       - caches animationId at entry +0x24 + objectSlot*4 */
    (void)startFrame;
    (void)entry;
    (void)objectSlot;
    (void)animationId;
    (void)mode;
    (void)playbackMode;
}

void CSelModeEntry_PlayObject(void *entry, int objectSlot) {
    (void)entry;
    (void)objectSlot;

    /* 0x80110B80 forwards the selected object handle to FUN_80175448(uiManager, handle). */
}

void CSelModeEntry_SetTransformTriplet(void *entry, const int *values) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;

    /* 0x80110BF4 copies three 32-bit values into +0x44, +0x48, +0x4C.
       In CSelMode_OnEnter these values are built from local_48/local_44/local_40
       before applying layout to each mode-entry object. */
    if (modeEntry == 0 || values == 0) {
        return;
    }

    modeEntry->transformOrState0 = values[0];
    modeEntry->transformOrState1 = values[1];
    modeEntry->transformOrState2 = values[2];
}

int CzanUiObjectInstance_Init(void *objectInstance) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x801711DC initializes the 0x1B4-byte UI animation/object instance created
       by CzanUiManager_CreateObjectGroup. The original clears transform/matrix
       state, initializes two color/parameter blocks, sets playback/visibility
       flags, stores default animation fields, and sets the bottom bound from the
       current screen height. */
    if (instance == 0) {
        return 0;
    }

    instance->manager = 0;
    instance->spriteObject = 0;
    instance->animationCounterOrTimer = 0;
    instance->currentAnimationId = -2;
    instance->activeAnimationEntry = 0;
    instance->initialAnimIndex = 0;
    instance->playbackRate = 1.0f;
    instance->unknownHandle188 = -1;
    instance->descriptor = 0;
    instance->currentAnimValue = 0;
    return 1;
}

int CzanSpriteObject_Init(void *spriteObject) {
    CzanSpriteObjectKnownFields *sprite = (CzanSpriteObjectKnownFields *)spriteObject;

    /* 0x8016BA8C initializes the 0x1D8-byte sprite/texture object paired with a
       CzanUiObjectInstance. The original clears texture references, sets anchor
       modes, initializes transform floats/colors/render defaults, and clears the
       draw callbacks later filled by CzanUiManager_CreateObjectGroup. */
    if (sprite == 0) {
        return 0;
    }

    sprite->enabled = 0;
    sprite->textureSlot = 0;
    sprite->textureHeader = 0;
    sprite->textureResourceHandle = 0;
    sprite->textureIndex = 0;
    sprite->uvOrFrameIndex = 0;
    sprite->xAnchorMode = 4;
    sprite->yAnchorMode = 0;
    sprite->ownsTexture = 1;
    sprite->textureReady = 0;
    sprite->color0[0] = 0xFF;
    sprite->color0[1] = 0xFF;
    sprite->color0[2] = 0xFF;
    sprite->color0[3] = 0xFF;
    sprite->color1[0] = 0xFF;
    sprite->color1[1] = 0xFF;
    sprite->color1[2] = 0xFF;
    sprite->color1[3] = 0xFF;
    sprite->color2[0] = 0xFF;
    sprite->color2[1] = 0xFF;
    sprite->color2[2] = 0xFF;
    sprite->color2[3] = 0xFF;
    sprite->color3[0] = 0xFF;
    sprite->color3[1] = 0xFF;
    sprite->color3[2] = 0xFF;
    sprite->color3[3] = 0xFF;
    sprite->visibleFlag = 0;
    sprite->renderMode174 = 1;
    return 1;
}

int CzanUiManager_CreateObjectGroup(
    int uiManager,
    void *linkData,
    unsigned int flags,
    int initialAnimIndex
) {
    unsigned int linkSize;
    CzanLinkBlock textureContainer;
    CzanLinkBlock metadataBlock;
    unsigned int descriptorCount;
    unsigned int descriptorOffset;
    int groupHandle;
    HostCzanGroup *group;
    int i;
    int activeCount;

    /* 0x80173414 creates a new Czan object group from a WII/Czan link resource.
       It finds a free group slot, links the resource, validates/relocates block 1 as
       the group metadata, uses block 0 as a nested texture/TPL resource container,
       then creates one CzanUiObjectInstance and CzanSpriteObject per 0x20-byte child
       descriptor.

       Descriptor type 0 loads a TPL block and creates a texture slot. Type 2 creates
       an 8x8 dummy sprite. Other descriptor types copy/reuse texture state from a
       previously-created child named by descriptor +0x12. The low byte of flags and
       descriptor +0x1A flags drive the same initial animation/visibility setup used
       by CzanUiManager_CloneObjectGroup. Flags 3/4 preplay the selected animation
       through CzanUiObjectInstance_PreplayInitialAnimation.

       After every child is created, the original sets uiManager +0x18 and +0x19 to 1,
       sets group +0x24 bit 0, and computes group +0x24 bit 3 from child +0x17D
       activity. Missing those manager/group flags means the later draw traversal has
       no drawable object list even if textures loaded correctly. Returns the new
       group handle, -1 when no group slot is free, or -2 when metadata validation fails. */
    (void)uiManager;
    (void)initialAnimIndex;

    linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    if (!CzanLinkResource_IsValid(linkData, linkSize)) {
        return -2;
    }

    if (!CzanLinkResource_GetBlock(linkData, linkSize, 1, &metadataBlock) ||
        !CzanLinkResource_GetBlock(linkData, linkSize, 0, &textureContainer) ||
        !CzanLinkResource_IsValid(textureContainer.data, textureContainer.size)) {
        return -2;
    }

    if (!CzanUiManager_ValidateAndRelocateObjectGroupMetadata(0, (char *)metadataBlock.data)) {
        return -2;
    }

    descriptorCount = ReadBe16(metadataBlock.data + 8);
    descriptorOffset = ReadBe32(metadataBlock.data + 0x0C);
    if (descriptorOffset >= metadataBlock.size ||
        descriptorCount > HOST_CZAN_MAX_OBJECTS ||
        descriptorOffset + descriptorCount * 0x20u > metadataBlock.size) {
        return -2;
    }

    groupHandle = HostCzan_FindFreeGroup();
    if (groupHandle < 0) {
        return -1;
    }

    group = &gHostCzanGroups[groupHandle];
    memset(group, 0, sizeof(*group));
    group->used = 1;
    group->state = -1;
    group->childCount = (int)descriptorCount;
    group->firstObjectIndex = -1;
    group->byte25 = 0;

    activeCount = 0;
    for (i = 0; i < (int)descriptorCount; i++) {
        const unsigned char *descriptor = metadataBlock.data + descriptorOffset + (unsigned int)i * 0x20u;
        unsigned int descriptorFlags = ReadBe16(descriptor + 0x1A);
        unsigned int descriptorType = descriptor[0x14];
        int objectIndex = HostCzan_FindFreeObject();
        HostCzanObject *object;

        if (objectIndex < 0) {
            break;
        }

        if (group->firstObjectIndex < 0) {
            group->firstObjectIndex = objectIndex;
        }

        object = &gHostCzanObjects[objectIndex];
        memset(object, 0, sizeof(*object));
        object->used = 1;
        object->groupHandle = groupHandle;
        object->childIndex = i;
        object->descriptorType = (int)descriptorType;
        object->enabled = (descriptorFlags & 0x001u) != 0;
        object->drawEnabled = 1;
        object->activeByte17d = (descriptorFlags & 0x040u) == 0;
        object->textureSlot = -1;
        object->textureIndex = 0;
        object->width = 8;
        object->height = 8;
        object->color[0] = 0xFF;
        object->color[1] = 0xFF;
        object->color[2] = 0xFF;
        object->color[3] = 0xFF;
        memcpy(object->name, descriptor, 16);
        object->name[16] = '\0';
        HostCzan_DefaultObjectPlacement(object, groupHandle, i);

        if (descriptorType == 0) {
            unsigned int textureBlockIndex = ReadBe16(descriptor + 0x10);
            CzanLinkBlock textureBlock;

            if (CzanLinkResource_GetBlock(textureContainer.data, textureContainer.size, textureBlockIndex, &textureBlock)) {
                unsigned int slot = CreateTextureFromTplResource(
                    &gHostTextureManager,
                    (void *)textureBlock.data,
                    (int)textureBlock.size,
                    0xFFFFFFFFu);
                object->textureSlot = (int)slot;
                GetTextureDimensions((void *)(long)object->textureSlot, 0, &object->width, &object->height);
                if (strncmp(object->name, "white_window", 12) == 0) {
                    object->color[3] = 0x78;
                }
            }
        }
        else if (descriptorType != 2) {
            unsigned int sourceIndex = ReadBe16(descriptor + 0x12);
            int j;

            for (j = 0; j < HOST_CZAN_MAX_OBJECTS; j++) {
                if (gHostCzanObjects[j].used &&
                    gHostCzanObjects[j].groupHandle == groupHandle &&
                    gHostCzanObjects[j].childIndex == (int)sourceIndex) {
                    object->textureSlot = gHostCzanObjects[j].textureSlot;
                    object->textureIndex = gHostCzanObjects[j].textureIndex;
                    object->width = gHostCzanObjects[j].width;
                    object->height = gHostCzanObjects[j].height;
                    object->color[3] = gHostCzanObjects[j].color[3];
                    break;
                }
            }
        }

        if ((descriptorFlags & 0x080u) != 0 && group->groupByte08 == 0) {
            group->groupByte08 = 0xFF;
        }
        else if (group->groupByte08 == 0) {
            if ((descriptorFlags & 0x100u) != 0) {
                group->groupByte08 |= 1;
            }
            if ((descriptorFlags & 0x200u) != 0) {
                group->groupByte08 |= 2;
            }
        }

        activeCount += object->activeByte17d != 0;
    }

    group->statusFlags24 |= 1;
    if (activeCount == 0 && group->state == -1) {
        group->statusFlags24 |= 8;
    }
    else {
        group->statusFlags24 &= (unsigned char)~8u;
    }

    printf("CzanUiManager: created group %d children=%d firstObject=%d flags=0x%02X\n",
           groupHandle,
           group->childCount,
           group->firstObjectIndex,
           group->statusFlags24);
    return groupHandle;
}

int CzanUiManager_ValidateAndRelocateObjectGroupMetadata(int uiManager, char *metadataBlock) {
    /* 0x80174D14 validates that metadataBlock starts with "CAE_WII\0". If the
       relocation byte at +0x0B is already nonzero, the block is accepted as already
       relocated. Otherwise it sets +0x0B to 1, converts the descriptor-table offset
       at +0x0C into an absolute pointer, then walks every 0x20-byte descriptor and
       converts each descriptor animation-table offset at +0x1C into a pointer.

       For every 0x10-byte animation entry, an entry with +0x04 == 0 has its +0x0C
       pointer/value cleared to 0; otherwise +0x0C is relocated relative to the
       metadata block base. The uiManager argument is present in the signature but
       is not used by the decompiled body. */
    (void)uiManager;

    if (metadataBlock == 0) {
        return 0;
    }

    return metadataBlock[0] == 'C' &&
           metadataBlock[1] == 'A' &&
           metadataBlock[2] == 'E' &&
           metadataBlock[3] == '_' &&
           metadataBlock[4] == 'W' &&
           metadataBlock[5] == 'I' &&
           metadataBlock[6] == 'I' &&
           metadataBlock[7] == '\0';
}

int CzanUiManager_CloneObjectGroup(
    int uiManager,
    int sourceObjectGroupHandle,
    unsigned int cloneFlags,
    int initialAnimIndex
) {
    /* 0x80173D18 finds a free object-group slot, copies the source group's
       descriptor pointer and child count, allocates a new child-object handle array,
       then creates a fresh CzanUiObjectInstance and CzanSpriteObject for every source
       child. Real sprite descriptors copy texture information from the source child;
       descriptor type 2 creates an 8x8 dummy sprite.

       The low byte of cloneFlags controls initial animation behavior:
       1/2 start and run the selected animation, 3/4 preplay the selected animation
       through CzanUiObjectInstance_PreplayInitialAnimation.
       Descriptor flags also set enabled/draw/animation fields on the cloned children.
       The function returns the new group handle, or -1 if no free group slot exists. */
    (void)uiManager;
    (void)sourceObjectGroupHandle;
    (void)cloneFlags;
    (void)initialAnimIndex;
    return -1;
}

void CzanUiObjectInstance_StartAnimation(double startFrame, int objectInstance, int animationIndex) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x80172CC8 selects/starts an animation entry on a Czan UI object instance.
       startFrame must be >= 0.0. objectInstance is the 0x1B4 Czan UI object instance.
       animationIndex is stored at +0x16C and used to index descriptor +0x1C. */
    if (instance == 0 || startFrame < 0.0) {
        return;
    }

    instance->initialAnimIndex = animationIndex;
    instance->currentAnimValue = 0;
}

double CzanUiManager_GetObjectAnimationDuration(double fallbackDuration, int uiManager, int objectGroupHandle, int childObjectIndex, int animationIndex) {
    /* 0x80175F58 returns the duration/tick count for one Czan UI object's animation.

       Original flow:
       - object = *(uiManager +4 + objectGroupHandle * 0x28 +0x20)[childObjectIndex]
       - if animationIndex == -1, use object +0x16C
       - return animation entry duration at *(object +0x198 +0x1C) + animationIndex * 0x10 +8

       The fallbackDuration argument is the decompiler-visible FPR argument used by
       callers when the UI object/group is unavailable. */
    (void)uiManager;
    (void)objectGroupHandle;
    (void)childObjectIndex;
    (void)animationIndex;
    return fallbackDuration;
}

void CzanUiManager_ResetObjectGroupAnimationTime(double frame, int uiManager, int objectGroupHandle) {
    /* 0x80174FA4 writes frame to +0x178 on every child CzanUiObjectInstance in an
       object group. CSelModeEntry_ResetObjectAnimation uses this before starting
       the movie-backed entry reveal animation. */
    (void)frame;
    (void)uiManager;
    (void)objectGroupHandle;
}

int CzanUiManager_GetChildObjectInstance(int uiManager, int objectGroupHandle, int childObjectIndex) {
    /* 0x801761C4 resolves one child CzanUiObjectInstance from an object group.

       Original behavior:
       return *( *( *(uiManager +4) + objectGroupHandle*0x28 +0x20 ) + childObjectIndex*4 )

       It is used by the select-common movie reveal path after getting a
       CSelModeEntry object handle, then the returned child instance receives color
       transition data. */
    (void)uiManager;
    (void)objectGroupHandle;
    (void)childObjectIndex;
    return 0;
}

void CzanUiObjectInstance_PreplayInitialAnimation(int objectInstance) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x801728E4 temporarily forces object +0x175 to mode 3, starts the selected
       animation at frame 0, sets +0xB4 to 0, then runs the animation script the old
       +0xB4 value number of times. It restores +0x175 and resets the script pointer
       and playback state back to the selected animation entry. */
    if (instance == 0) {
        return;
    }
}

void CzanSpriteObject_SetRenderMode(int spriteObject, int mode) {
    CzanSpriteObjectKnownFields *sprite = (CzanSpriteObjectKnownFields *)spriteObject;

    /* 0x80170DEC maps an animation-entry mode byte to sprite render/blend state.
       spriteObject is the 0x1D8 Czan sprite object. mode is the byte copied from
       animation entry +0x03. The original writes render parameters around
       +0x178..+0x19C, then stores the raw mode byte at +0x1A4. */
    if (sprite == 0) {
        return;
    }

    (void)mode;
}

void CzanUiObjectInstance_RunAnimationScript(int objectInstance, int allowUnknownOpcode) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x80171B98 interprets the Czan animation command stream at object +0x19C.
       objectInstance is the 0x1B4 Czan UI object instance, passed through the compiler's
       context helper in the decompile. allowUnknownOpcode controls the invalid-opcode assert
       path: nonzero tolerates unknown/default opcodes, zero asserts.

       Confirmed opcodes update playback/end state, texture frames, sprite position,
       size, scale, render mode, color bytes, dynamic value lists, and wait timers.
       The interpreter loops until it reaches a wait/end condition. */
    if (instance == 0) {
        return;
    }

    (void)allowUnknownOpcode;
}

void CzanUiObjectInstance_ApplyColorBlocks(int objectInstance) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x80172FC0 copies four color blocks from the UI object instance into the
       attached sprite object's vertex/RGBA color blocks. objectInstance is the 0x1B4
       Czan UI object instance. Flags at +0x182/+0x183 select alternate RGB and
       scaled alpha behavior. */
    if (instance == 0) {
        return;
    }
}

void CzanUiObjectInstance_SetColorBlocks(int objectInstance, int colorSlot, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    /* 0x80172F30 writes RGBA color bytes to one or all four color blocks on a
       CzanUiObjectInstance, marks color state dirty at +0x182/+0x183, then calls
       CzanUiObjectInstance_ApplyColorBlocks.

       colorSlot == -1 writes the same RGBA to all four blocks at +0x134..+0x143.
       Otherwise it writes only the selected 4-byte block. */
    (void)colorSlot;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
    if (objectInstance == 0) {
        return;
    }
}

void CzanUiManager_SetObjectTextureFrame(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    int textureFrameOrAuto,
    int updateSpriteDimensions) {
    /* 0x80175448 selects/rebinds a texture frame on one child object.
       It stores the requested frame at object +0x148, resolves -2 through
       object +0xA8/+0x14C, updates sprite +0x34, and optionally refreshes
       sprite dimensions at +0x100/+0x108 and half sizes at +0x78/+0x7C. */
    (void)uiManager;
    (void)objectGroupHandle;
    (void)childObjectIndex;
    (void)textureFrameOrAuto;
    (void)updateSpriteDimensions;
}

void CzanUiManager_ApplyObjectGroupPositionLayout(int uiManager, int objectGroupHandle, float *xyOffset) {
    /* 0x801750E4 applies xyOffset to every child in a group:
       object +0xD0/+0xD4 = offset, sprite +0x3C/+0x40 = object +0x30/+0x34 + offset. */
    (void)uiManager;
    (void)objectGroupHandle;
    (void)xyOffset;
}

void CzanUiManager_ApplyChildObjectPositionLayout(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    float *xyOffset) {
    /* 0x8017515C is the single-child version of CzanUiManager_ApplyObjectGroupPositionLayout. */
    (void)uiManager;
    (void)objectGroupHandle;
    (void)childObjectIndex;
    (void)xyOffset;
}

void CzanUiManager_ApplyObjectGroupAnimationOffset(int uiManager, int objectGroupHandle, float *xyOffset) {
    /* 0x801751B8 applies xyOffset to every child in a group:
       object +0xE8/+0xEC = offset, sprite +0x90/+0x94 =
       object scale +0xF4/+0xF8 * (object +0x48/+0x4C + offset). */
    (void)uiManager;
    (void)objectGroupHandle;
    (void)xyOffset;
}

void CzanUiManager_ApplyChildObjectAnimationOffset(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    float *xyOffset) {
    /* 0x80175240 is the single-child version of CzanUiManager_ApplyObjectGroupAnimationOffset. */
    (void)uiManager;
    (void)objectGroupHandle;
    (void)childObjectIndex;
    (void)xyOffset;
}

void CzanUiManager_SetObjectGroupEnabled(int uiManager, int objectGroupHandle, unsigned char enabled) {
    int i;

    /* 0x80174F04 sets object +0x173 for every child in the group. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].enabled = enabled != 0;
        }
    }
}

void CzanUiManager_SetChildObjectEnabled(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    unsigned char enabled) {
    int i;

    /* 0x80174F40 sets object +0x173 for one child in the group. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            gHostCzanObjects[i].enabled = enabled != 0;
            return;
        }
    }
}

void CzanUiManager_SetObjectGroupDrawEnabled(int uiManager, int objectGroupHandle, unsigned char drawEnabled) {
    int i;

    /* 0x801750A8 sets object +0x181 for every child in the group. This is the
       draw-list participation flag checked by CzanUiManager_DrawObjectListReverse. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used && gHostCzanObjects[i].groupHandle == objectGroupHandle) {
            gHostCzanObjects[i].drawEnabled = drawEnabled != 0;
        }
    }
}

void CzanUiManager_LinkObjectGroupToReferenceObject(
    int uiManager,
    int targetObjectGroupHandle,
    int referenceObjectGroupHandle,
    int referenceChildIndex,
    unsigned char linkMode) {
    /* 0x80176D68 links every target child to one reference object:
       target object +0x190 = reference object, target sprite +0x1B0 =
       reference sprite, target sprite +0x1B4 = linkMode. */
    (void)uiManager;
    (void)targetObjectGroupHandle;
    (void)referenceObjectGroupHandle;
    (void)referenceChildIndex;
    (void)linkMode;
}

void CzanSpriteObject_Draw(int spriteObject, int parentTransform, int externalTransform, int drawMode) {
    /* 0x80170354 is the high-level Czan sprite draw dispatcher. It checks sprite
       active/texture/dimension state, applies render state, selects a draw mode,
       then calls a low-level quad emitter such as CzanDrawTexturedOrColoredQuad. */
    (void)spriteObject;
    (void)parentTransform;
    (void)externalTransform;
    (void)drawMode;
}

void CzanUiObjectInstance_Draw(int objectInstance) {
    /* 0x801729B4 is the per-object Czan draw wrapper. It can run up to eight
       child/pre-post callbacks, resolves linked sprite state at object +0x28,
       writes sprite +0x1A8, and calls CzanSpriteObject_Draw(object +0x24, ...).
       It uses object +0x173 as enabled/visibility, +0x198 as descriptor,
       +0x16C as animation index, +0x148 as texture-frame override, and +0x1A4
       as the owning object-group handle. */
    (void)objectInstance;
}

void CzanUiManager_DrawObjectListReverse(int objectList, int drawLayerFilter) {
    int i;
    int pass;

    /* 0x80174BA8 draws an object list from last to first. It skips when list
       +0x19 is set or +0x1C is null, draws only objects with +0x181 == 1, and
       filters by object +0x18C unless drawLayerFilter is -1. */
    (void)objectList;
    (void)drawLayerFilter;

    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
            if (gHostCzanObjects[i].used &&
                gHostCzanObjects[i].drawEnabled &&
                HostCzan_IsWindowLikeObject(&gHostCzanObjects[i]) == (pass == 0)) {
                HostCzan_DrawObject(&gHostCzanObjects[i]);
            }
        }
    }
}

void CzanUiManager_DrawObjectGroupInListOrder(int uiManager, int objectGroupHandle) {
    int i;

    /* 0x80174C58 draws only children belonging to one object group, while
       preserving the global object-list reverse order from uiManager +0x1C.
       The group is uiManager +0x04 + objectGroupHandle * 0x28. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].drawEnabled) {
            HostCzan_DrawObject(&gHostCzanObjects[i]);
        }
    }
}

void CzanUiManager_DrawChildObject(int uiManager, int objectGroupHandle, int childObjectIndex) {
    int i;

    /* 0x80174CF8 directly draws one child:
       uiManager->groups[objectGroupHandle].children[childObjectIndex]. */
    (void)uiManager;

    for (i = 0; i < HOST_CZAN_MAX_OBJECTS; i++) {
        if (gHostCzanObjects[i].used &&
            gHostCzanObjects[i].groupHandle == objectGroupHandle &&
            gHostCzanObjects[i].childIndex == childObjectIndex) {
            HostCzan_DrawObject(&gHostCzanObjects[i]);
            return;
        }
    }
}

void CzanDrawTexturedOrColoredQuad(
    double u0,
    double v0,
    double u1,
    double v1,
    int spriteObject,
    void *quadData,
    int width,
    unsigned int height,
    unsigned char *vertexColors,
    int textureObject,
    int unknownArg11,
    int unknownArg12) {
    /* 0x8016BE18 is the low-level GX quad emitter. textureObject == 0 emits a
       colored quad; nonzero loads a GX texture object and emits textured vertices. */
    (void)u0;
    (void)v0;
    (void)u1;
    (void)v1;
    (void)spriteObject;
    (void)quadData;
    (void)width;
    (void)height;
    (void)vertexColors;
    (void)textureObject;
    (void)unknownArg11;
    (void)unknownArg12;
}
