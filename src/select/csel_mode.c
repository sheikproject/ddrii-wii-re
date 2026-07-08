#include "select/csel_mode.h"

#include <stdio.h>

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
    (void)cselMode;
    (void)linkData;

    /* Original links the Czan resource, builds Czan UI object groups from blocks
       0..6, reuses one shared mode-button object group across entries 7..13,
       applies region-specific position/animation tables to entries 6..13, and
       sets modeState at +0x130 to 1. */
    puts("CSelMode: on enter");
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
