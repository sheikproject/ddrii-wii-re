#include "model/zmb_zab.h"

#include <stdint.h>
#include <string.h>

int ZmbZabModelEntry_Init(void *entry) {
    CtsStageObjKnownFields *stageObj = (CtsStageObjKnownFields *)entry;

    /* 0x8004D4E8 initializes one 0x270-byte CtsStageObj/ZMB-ZAB model entry.
       The original calls CtsStageObj_InitBase, sets the CtsStageObj vtable at +0x6C,
       clears the normalized name at +0x74, initializes the OBJSET reference list
       fields at +0xB4/+0xB8, fills two lookup tables with -1, and clears the
       remaining manager/cache fields. */
    if (stageObj == 0) {
        return 0;
    }

    stageObj->vtable = (void *)0x802B9624;
    stageObj->packedFlags = 0;
    memset(stageObj->normalizedName, 0, sizeof(stageObj->normalizedName));
    stageObj->objsetReferenceIndices = 0;
    stageObj->objsetReferenceCount = 0;
    stageObj->manager802e70a4 = 0;
    stageObj->largeResourceManager = 0;
    stageObj->unknown260 = 0;
    stageObj->unknown264 = 0;
    stageObj->state268 = 0;
    stageObj->flags26a = 0;
    stageObj->temporaryObjsetReferenceCounter = 0;
    return 1;
}

int ZmbZabModelEntry_Destroy(void *entry, short releaseMode) {
    CtsStageObjKnownFields *stageObj = (CtsStageObjKnownFields *)entry;

    /* 0x8004D6BC releases the owned OBJSET reference array at +0xB4, clears the
       reference count at +0xB8, calls FUN_80058E18(entry, 0), and frees the whole
       entry only when releaseMode is positive. */
    if (stageObj == 0) {
        return 0;
    }

    stageObj->vtable = (void *)0x802B9624;
    stageObj->objsetReferenceIndices = 0;
    stageObj->objsetReferenceCount = 0;
    CtsStageObj_Destroy(entry, 0);
    (void)releaseMode;
    return 1;
}

int *CtsStageObj_InitBase(int *entry) {
    /* 0x80058D90 initializes the base CtsStageObj fields before a specific
       ZMB/ZAB entry constructor overwrites +0x6C with the derived vtable. */
    if (entry == 0) {
        return 0;
    }

    entry[0x1b] = 0x802B97A8;
    entry[0] = 0;
    entry[1] = -1;
    entry[2] = -1;
    entry[3] = 0;
    entry[4] = 0;
    entry[5] = 0;
    entry[6] = 0;
    memset(entry + 0x13, 0, 0x18);
    entry[0x19] = 0;
    entry[0x1a] = 0;
    return entry;
}

int CtsStageObj_Destroy(void *entry, short releaseMode) {
    CtsStageObjKnownFields *stageObj = (CtsStageObjKnownFields *)entry;

    /* 0x80058E18 switches the object to the CtsStageObj vtable at 0x802B97A8,
       calls CtsStageObj_ResetModelBlocks through vtable +0x14, then frees the
       whole entry only when releaseMode is positive. */
    if (stageObj == 0) {
        return 0;
    }

    stageObj->vtable = (void *)0x802B97A8;
    CtsStageObj_ResetModelBlocks((int *)entry);
    (void)releaseMode;
    return 1;
}

void CtsStageObj_ResetModelBlocks(int *entry) {
    /* 0x80059060 is vtable +0x14. It is called at the start of
       CtsStageObj_LoadModelBlocks and resets any previously loaded model state.

       Confirmed original flow:
       - if entry[4] exists, frees the continuation pointer table
       - if entry[0] exists, destroys the 0x2D0 model object through its vtable
       - if entry[1] != -1, releases that texture-manager slot
       - resets entry[0..4] to empty state
       - sets entry[5] and entry[6] to FLOAT_802E84D8
       - initializes entry +7 through thunk_FUN_801B0120
       - clears 0x18 bytes at entry +0x13
       - clears entry[0x19] and entry[0x1A] */
    if (entry == 0) {
        return;
    }

    entry[0] = 0;
    entry[1] = -1;
    entry[2] = -1;
    entry[3] = 0;
    entry[4] = 0;
    entry[5] = 0;
    entry[6] = 0;
    memset(entry + 0x13, 0, 0x18);
    entry[0x19] = 0;
    entry[0x1a] = 0;
}

void CtsStageObj_LoadModelBlocks(
    int *entry,
    int primaryModelBlock,
    int primaryModelBlockSize,
    int secondaryTextureBlock,
    int secondaryTextureBlockSize,
    int continuationCount,
    int fallbackTextureSlot) {
    /* 0x80058EA8 is the real CtsStageObj primary/secondary model setup method.
       Vtable +0x10 / 0x80059058 is only a tiny wrapper that forwards here.

       Confirmed original flow:
       - calls CtsStageObj_ResetModelBlocks first
       - if secondaryTextureBlock is nonzero, creates a texture slot from it and
         stores the slot at entry[1]
       - allocates a 0x2D0-byte model instance through FUN_8014BE78 and stores it at entry[0]
       - if continuationCount > 0, calls FUN_8015C540(model, continuationCount)
       - if fallbackTextureSlot != -1, binds that texture slot to the model through FUN_8015C3CC
       - loads primary model block data with FUN_8014C67C(model, primaryModelBlock, primaryModelBlockSize)
       - if entry[1] != -1, attaches that texture slot through FUN_8014E828
       - enables/sets model state through FUN_8014E7D0(model, 1)
       - writes model +0x148 = 1 and model +0x1A0 = 2
       - allocates entry[4] as continuationCount * 4 bytes and clears it
       - stores FUN_8014E8E8(model, DAT_80271640) at entry[2] */
    (void)entry;
    (void)primaryModelBlock;
    (void)primaryModelBlockSize;
    (void)secondaryTextureBlock;
    (void)secondaryTextureBlockSize;
    (void)continuationCount;
    (void)fallbackTextureSlot;
}

void CtsStageObj_LoadPrimarySecondaryBlocks(
    void *entry,
    int primaryBlock,
    int primaryBlockSize,
    int secondaryBlock,
    int secondaryBlockSize,
    unsigned int continuationCount) {
    /* Vtable +0x10 / 0x80059058 forwards to FUN_80058EA8. It is called by
       LoadZmbZabModelEntryList after consuming the primary and optional secondary
       ZMB/ZAB blocks for one logical model entry. */
    CtsStageObj_LoadModelBlocks(
        (int *)entry,
        primaryBlock,
        primaryBlockSize,
        secondaryBlock,
        secondaryBlockSize,
        (int)continuationCount,
        -1);
}

void CtsStageObj_LoadContinuationBlock(void *entry, int continuationIndex, int continuationBlock) {
    CtsStageObjKnownFields *stageObj = (CtsStageObjKnownFields *)entry;

    /* Vtable +0x20 / 0x80059158 loads one continuation block when
       continuationIndex < entry[3]. The original calls FUN_8014C68C(*entry, block,
       index), then caches *(entryObject +0x240) into the pointer table at entry[4]. */
    (void)continuationBlock;
    if (stageObj == 0) {
        return;
    }
    (void)continuationIndex;
}

void CtsStageObj_StartAnimation(double frameScale, double startFrame, void *entry, int arg3, int arg4, int arg5) {
    CtsStageObjKnownFields *stageObj = (CtsStageObjKnownFields *)entry;

    /* Vtable +0x24 / 0x800591BC stores (int)startFrame at entry[6], writes
       startFrame * entry[5] to the underlying model object +0x250, then calls
       FUN_8015C548(*entry). */
    (void)frameScale;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    if (stageObj == 0) {
        return;
    }
    (void)startFrame;
}

void CtsStageObj_SelectAndApplyModelSlot(
    double x,
    double y,
    double width,
    double height,
    double scaleOrDepth,
    double wrapLimit,
    double currentFrame,
    void *stageObj,
    int modelSlotOrSpecialId,
    float baseFrame,
    int flags) {
    /* 0x8005C540 is called by ActiveGameplayControllerBase_Update-like logic
       after querying a position/model marker from the active gameplay controller
       data. Ghidra shows a strange signature because RuntimeContext_SpillSavedRegisters
       recovers the real object pointer and a count/slot argument.

       Confirmed behavior:
       - sums per-entry counters at stageObj +0x16C.. over the recovered count
       - computes a display/aspect scale from DAT_802E71B8 +0x258 width/height data
       - resolves modelSlotOrSpecialId:
         700 -> random among three configured ranges at stageObj +0x12C/+0x130/+0x138
         800 -> random in range at stageObj +0x128, offset by +0x12C/+0x130
         900 -> random in range at stageObj +0x124
         101..199, 201..299, 301..399, 601..699 -> remap into configured ranges
       - chooses a slot descriptor through FUN_8005A5F4 when the id is below
         stageObj +0x15C, otherwise through the resolved range descriptor
       - calls FUN_8005A9C4 to prepare the slot
       - applies frame/position data through FUN_8005993C and FUN_800599C0
       - stores the final resolved id at stageObj +0x10C

       This is much closer to model/stage-object presentation than the controller
       setup functions, but it still dispatches into lower helpers rather than
       emitting ZMB geometry itself. */
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)scaleOrDepth;
    (void)wrapLimit;
    (void)currentFrame;
    (void)stageObj;
    (void)modelSlotOrSpecialId;
    (void)baseFrame;
    (void)flags;
}

void CtsStageObjDescriptor_SetFrameProgress(double frameProgress, int *descriptor, int useFullDuration) {
    float duration;

    /* 0x8005993C clamps frameProgress to at least FLOAT_802E84E0, reads the
       duration from *(descriptor +0), and writes descriptor[10..12].

       descriptor[10] receives either a wrapped/clamped progress through
       thunk_FUN_80137BB8(frameProgress), or the full duration when useFullDuration
       is nonzero. descriptor[11] mirrors descriptor[10], descriptor[12] is reset
       to FLOAT_802E84E0. */
    if (descriptor == 0) {
        return;
    }

    if (frameProgress < 0.0) {
        frameProgress = 0.0;
    }

    duration = 0.0f;
    if (descriptor[0] != 0) {
        duration = *(float *)(uintptr_t)(unsigned int)descriptor[0];
    }

    if (0.0f < duration) {
        descriptor[10] = useFullDuration == 0 ? (int)(float)frameProgress : (int)duration;
        descriptor[11] = descriptor[10];
        descriptor[12] = 0;
    }
}

void CtsStageObjDescriptor_SetCurrentTime(double currentTime, int descriptor) {
    /* 0x800599C0 writes the current time/frame as a float at descriptor +0x30. */
    if (descriptor == 0) {
        return;
    }
    *(float *)(uintptr_t)(unsigned int)(descriptor + 0x30) = (float)currentTime;
}

int CtsStageObjDescriptor_GetEntryHandle(int *descriptor, int entryIndex) {
    /* 0x8005A5F4 returns -1 when entryIndex is outside descriptor[2], otherwise
       returns *(descriptor[0] + entryIndex * 0xA8 + 0xA4). */
    uintptr_t entries;

    if (descriptor == 0 || descriptor[2] <= entryIndex) {
        return -1;
    }

    entries = (uintptr_t)(unsigned int)descriptor[0];
    if (entries == 0) {
        return -1;
    }

    return *(int *)(entries + entryIndex * 0xa8 + 0xa4);
}

double CtsStageObjDescriptor_GetCurrentDuration(int *descriptor) {
    /* 0x8005ABE8 returns 0.0 when the descriptor has no entry table or active
       entry pointer. Otherwise it returns the first float in the active entry:
       *(float *)(descriptor[0] + descriptor[1] * 0xA8). */
    uintptr_t entries;
    uintptr_t activeEntry;

    if (descriptor == 0 || descriptor[0] == 0) {
        return 0.0;
    }

    entries = (uintptr_t)(unsigned int)descriptor[0];
    activeEntry = entries + descriptor[1] * 0xa8;
    if (activeEntry == 0) {
        return 0.0;
    }

    return (double)*(float *)activeEntry;
}

int CtsStageObjDescriptor_GetCurrentEntryActiveFlag(int *descriptor) {
    /* 0x8005AC14 returns the active flag at descriptor current entry +0x20.

       Original flow:
       - return 1 when descriptor[0] is null
       - otherwise return *(descriptor[0] + descriptor[1] * 0xA8 +0x20)

       This is used by CtsStageObjSlot_IsBusy to decide whether an animation/model
       slot entry has completed. */
    (void)descriptor;
    return 1;
}

int CtsStageObjSlot_IsBusy(float *slot) {
    /* 0x8005CA9C checks whether a CtsStageObj slot/descriptor is still active.

       The original chooses one of several descriptor records depending on slot state:
       - a special sentinel at slot +0x150 selects the descriptor at +0x1FC
       - when slot +0x114 is zero, it indexes a descriptor table through +0x164/+0x168
       - otherwise it uses the main descriptor at +0x180 and may defer completion
         based on current frame, total frame count, and entry count

       It returns nonzero when the selected descriptor is complete/idle, or when the
       slot has no positive duration/current frame to wait on. */
    (void)slot;
    return 1;
}

void CtsStageObjSlot_SetState(int *slot, int state) {
    /* 0x8005D0E8 changes the CtsStageObj slot state stored at slot +0x80.

       Confirmed states:
       - 0: clear slot +0x80
       - 1: run FUN_8005CB94 transition/setup helper
       - 2: if current state is 0 or 4, run FUN_8005CB94, then set +0x80 = 2
       - 3: run FUN_8005CB94, then set +0x80 = 3
       - 4: set +0x80 = 4, activate descriptor table at +0x164, clear +0x64,
            set +0x114 = 1
       - 5: ensure helper/state at +0x118 exists through FUN_8005CE7C, set +0x80 = 5,
            set +0x11C = 1
       - 6: set +0x80 = 6

       ActiveGameplayControllerBase_UpdateTimelineMarker uses state 6 as a forced
       disabled/sentinel state. */
    (void)slot;
    (void)state;
}

void CtsStageObjSlot_ResetDescriptorFrames(double frameProgress, int *slot) {
    /* 0x8005C9F4 iterates the descriptor table owned by a CtsStageObj slot and
       rewinds/activates each descriptor entry.

       Original flow:
       - for each descriptor counted by slot +0x15C with stride 0x7C at slot +0x164:
         - CtsStageObjDescriptor_ActivateEntry(1.0f, descriptor, 0, 0)
         - CtsStageObjDescriptor_SetFrameProgress(frameProgress,
           descriptor[0] + descriptor[1] * 0xA8, 0)

       ActiveGameplayControllerBase_StopStageModelSlot calls this with 0.0f before
       switching the movie/background bindings into mode 3. */
    (void)frameProgress;
    (void)slot;
}

void CtsStageObjDescriptor_ActivateEntry(double blendDuration, int *descriptor, int entryIndex, int entryHandle) {
    uintptr_t activeEntry;

    /* 0x8005A9C4 switches/activates one descriptor entry.

       If blendDuration <= 0.0, it clears descriptor blend timers at +0x0C/+0x10.
       Otherwise it snapshots either the currently active entry transform block
       (+0x4C..+0x7C) or the descriptor's previous cached block (+0x48..+0x78),
       then stores blendDuration in descriptor +0x0C/+0x10.

       It then resets the selected entry at:
         descriptor[0] + entryIndex * 0xA8

       Entry fields reset:
         +0x20 = 0
         +0x24 = entryHandle
         +0x28/+0x2C/+0x30 = 0.0
         +0x4C identity/default transform
         +0x58 identity/default matrix/vector
         +0x68 = 0.0
         +0x6C identity/default transform
         +0x78/+0x7C default floats
         +0x34..+0x48 = 0

       Finally descriptor[1] becomes entryIndex. */
    if (descriptor == 0 || descriptor[0] == 0 || descriptor[2] <= entryIndex) {
        return;
    }

    if (blendDuration <= 0.0) {
        descriptor[3] = 0;
        descriptor[4] = 0;
    }
    else {
        descriptor[3] = (int)(float)blendDuration;
        descriptor[4] = (int)(float)blendDuration;
    }

    activeEntry = (uintptr_t)(unsigned int)descriptor[0] + entryIndex * 0xa8;
    *(int *)(activeEntry + 0x20) = 0;
    *(int *)(activeEntry + 0x24) = entryHandle;
    *(float *)(activeEntry + 0x28) = 0.0f;
    *(float *)(activeEntry + 0x2c) = 0.0f;
    *(float *)(activeEntry + 0x30) = 0.0f;
    *(float *)(activeEntry + 0x68) = 0.0f;
    *(float *)(activeEntry + 0x78) = 1.0f;
    *(float *)(activeEntry + 0x7c) = 1.0f;
    memset((void *)(activeEntry + 0x34), 0, 0x18);
    descriptor[1] = entryIndex;
}

int CtsStageObj_CopyObjectTransform(void *stageObjOrSlot, void *outMatrix, int objectIndex) {
    /* 0x8005941C copies transform data from a CtsStageObj/slot into outMatrix.

       Confirmed behavior:
       - if objectIndex != -1, calls FUN_801568D4(*(slot +0), objectIndex)
       - if that returns null, copies the default/base transform at slot +0x1C
         into outMatrix through FUN_801459EC
       - if a transform is found, composes/copies slot +0x1C with that transform
         into outMatrix through FUN_80145A0C and returns 1
       - returns 0 when only the base transform was copied

       This provides the matrix later passed into CzanModelManager_SetLiveObjectMatrix. */
    if (stageObjOrSlot == 0) {
        return 0;
    }
    (void)outMatrix;
    (void)objectIndex;
    return 0;
}

void CtsStageObj_ApplyModelTransform(int *stageObj, int arg1, int arg2) {
    /* 0x800594B8 is a tiny CtsStageObj method. It does nothing when no CzanModel
       is loaded at stageObj[0]; otherwise it forwards to FUN_8014F420:

         CzanModel_DrawVisibleObjects(stageObj[0], arg1, stageObj +7, arg2)

       stageObj +7 is the base 3x4 transform initialized by Matrix34_SetIdentity.
       The callee walks visible model objects and dispatches submesh draw routines. */
    if (stageObj == 0 || stageObj[0] == 0) {
        return;
    }

    (void)arg1;
    (void)arg2;
}

void CtsStageObj_DrawModelWithFlags(int *stageObj, int arg1, int arg2, unsigned int drawFlags) {
    /* 0x800594DC is the flag-aware CtsStageObj draw wrapper. It does nothing when
       no CzanModel is loaded at stageObj[0], maps drawFlags to a small draw mode,
       then forwards to FUN_8014ED4C:

         drawMode = 1 when drawFlags bit 0 is set
         drawMode = 2 when bit 0 is clear and bit 1 is set
         drawMode = 0 otherwise

         FUN_8014ED4C(stageObj[0], arg1, stageObj +7, arg2, drawMode)

       This is the companion path to CtsStageObj_ApplyModelTransform. The next
       target is FUN_8014ED4C, because it should explain what the mode changes in
       the CzanModel draw traversal. */
    if (stageObj == 0 || stageObj[0] == 0) {
        return;
    }

    (void)arg1;
    (void)arg2;
    (void)drawFlags;
}

void ZmbZabModelEntry_UpdatePresentation(int *entry, int arg1, int arg2, int arg3) {
    /* 0x8004E220 is vtable +0x40 on the ZMB/ZAB CtsStageObj-derived entry.

       Confirmed behavior:
       - returns unless entry +0x254 is nonzero
       - if entry +0x70 has bit 0x10000 set:
         - FUN_80059380(entry, stackMatrix)
         - FUN_800259C0(gManager_802E70A8, arg1, stackMatrix)
       - always calls CtsStageObj_DrawModelWithFlags(entry, arg1, arg2, arg3) afterward

       This is a higher-level presentation/update method for ZMB/ZAB entries. The
       important remaining target is FUN_8014ED4C, because FUN_800594DC only maps
       flag bits into the CzanModel draw mode. */
    (void)arg1;
    (void)arg2;
    (void)arg3;
    if (entry == 0) {
        return;
    }
}
