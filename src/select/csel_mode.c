#include "select/csel_mode.h"

#include "resource/czan_link.h"

#include <stdio.h>

static unsigned int gCSelModeHostLinkResourceSize;

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
       entry through FUN_80144CF0 when the caller passes a positive flag/count. */
    if (entry != 0 && activeCountOrFlag > 0) {
        return 1;
    }
    return entry != 0;
}

int CSelModeEntry_AddUiObject(void *entry, int linkBlock) {
    CSelModeEntryKnownFields *modeEntry = (CSelModeEntryKnownFields *)entry;
    int slot;

    /* 0x80110524 creates/registers a UI object group from a non-null Czan link block
       via CzanUiManager_CreateObjectGroup / FUN_80173414, stores the returned handle
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

    /* 0x8011058C creates/registers a child or alternate UI object from an object
       ID through FUN_80173D18(uiManager), then stores the returned handle in the
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

void CzanUiObjectInstance_StartAnimation(double startFrame, int objectInstance, int animationIndex) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x80172CC8 selects/starts an animation entry on a Czan UI object instance.
       param_1/startFrame must be >= 0.0. param_2 is the object instance. param_3
       is the animation index stored at +0x16C and used to index descriptor +0x1C. */
    if (instance == 0 || startFrame < 0.0) {
        return;
    }

    instance->initialAnimIndex = animationIndex;
    instance->currentAnimValue = 0;
}

void CzanSpriteObject_SetRenderMode(int spriteObject, int mode) {
    CzanSpriteObjectKnownFields *sprite = (CzanSpriteObjectKnownFields *)spriteObject;

    /* 0x80170DEC maps an animation-entry mode byte to sprite render/blend state.
       param_1 is the 0x1D8 Czan sprite object. param_2 is the mode byte copied
       from animation entry +0x03. The original writes render parameters around
       +0x178..+0x19C, then stores the raw mode byte at +0x1A4. */
    if (sprite == 0) {
        return;
    }

    (void)mode;
}

void CzanUiObjectInstance_RunAnimationScript(int objectInstance, int allowUnknownOpcode) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x80171B98 interprets the Czan animation command stream at object +0x19C.
       param_1 is the 0x1B4 Czan UI object instance, passed through the compiler's
       context helper in the decompile. param_2 controls the invalid-opcode assert
       path: nonzero tolerates unknown/default opcodes, zero asserts. */
    if (instance == 0) {
        return;
    }

    (void)allowUnknownOpcode;
}

void CzanUiObjectInstance_ApplyColorBlocks(int objectInstance) {
    CzanUiObjectInstanceKnownFields *instance = (CzanUiObjectInstanceKnownFields *)objectInstance;

    /* 0x80172FC0 copies four color blocks from the UI object instance into the
       attached sprite object's vertex/RGBA color blocks. param_1 is the 0x1B4
       Czan UI object instance. Flags at +0x182/+0x183 select alternate RGB and
       scaled alpha behavior. */
    if (instance == 0) {
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
    /* 0x80174F04 sets object +0x173 for every child in the group. */
    (void)uiManager;
    (void)objectGroupHandle;
    (void)enabled;
}

void CzanUiManager_SetChildObjectEnabled(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    unsigned char enabled) {
    /* 0x80174F40 sets object +0x173 for one child in the group. */
    (void)uiManager;
    (void)objectGroupHandle;
    (void)childObjectIndex;
    (void)enabled;
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
    /* 0x80174BA8 draws an object list from last to first. It skips when list
       +0x19 is set or +0x1C is null, draws only objects with +0x181 == 1, and
       filters by object +0x18C unless drawLayerFilter is -1. */
    (void)objectList;
    (void)drawLayerFilter;
}

void CzanUiManager_DrawObjectGroupInListOrder(int uiManager, int objectGroupHandle) {
    /* 0x80174C58 draws only children belonging to one object group, while
       preserving the global object-list reverse order from uiManager +0x1C.
       The group is uiManager +0x04 + objectGroupHandle * 0x28. */
    (void)uiManager;
    (void)objectGroupHandle;
}

void CzanUiManager_DrawChildObject(int uiManager, int objectGroupHandle, int childObjectIndex) {
    /* 0x80174CF8 directly draws one child:
       uiManager->groups[objectGroupHandle].children[childObjectIndex]. */
    (void)uiManager;
    (void)objectGroupHandle;
    (void)childObjectIndex;
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
    int param11,
    int param12) {
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
    (void)param11;
    (void)param12;
}
