#include "model/czan_model.h"

#include <stdint.h>
#include <string.h>

int *CzanModel_Init(int *model) {
    /* 0x8014BE78 initializes the 0x2D0-byte model object allocated by
       CtsStageObj_LoadModelBlocks. Its vtable is PTR_PTR_802C0720. The original
       sets many transform/material/render defaults, clears several state blocks,
       initializes color fields to 0xFF, and uses a display-mode check to choose
       one scale/aspect-related float at model[0x67]. */
    if (model == 0) {
        return 0;
    }

    memset(model, 0, 0x2d0);
    model[0] = 0x802C0720;
    model[0x27] = 1;
    model[0x30] = -1;
    model[0x4c] = -1;
    model[0x51] = 0x111;
    model[0x55] = 1;
    model[0x5a] = 1;
    model[0x68] = 1;
    model[0x6b] = 1;
    model[0xa0] = -1;
    memset(model + 0xf, 0xff, 4);
    memset(model + 0x10, 0xff, 4);
    return model;
}

int *CzanModel_Destroy(int *model, short releaseMode) {
    /* 0x8014C0B0 is CzanModel vtable +0x08. It restores the CzanModel vtable,
       frees all runtime arrays allocated by CzanModel_BuildRuntimeData and related
       animation setup paths, then optionally frees the CzanModel object itself. */
    if (model == 0) {
        return 0;
    }

    model[0] = 0x802C0720;
    model[0xd] = 0;
    model[0x2d] = 0;
    model[0x6f] = 0;
    model[0x70] = 0;
    model[0x71] = 0;
    model[0x72] = 0;
    model[0x73] = 0;
    model[0x17] = 0;
    model[0x18] = 0;
    (void)releaseMode;
    return model;
}

int CzanModel_SetPrimaryBlock(int *model, int primaryModelBlock, int primaryModelBlockSize) {
    /* 0x8014C67C stores the primary model resource pointer and size on the
       0x2D0-byte CzanModel instance. */
    if (model == 0) {
        return 0;
    }

    model[1] = primaryModelBlock;
    model[2] = primaryModelBlockSize;
    return 1;
}

void CzanModel_AttachTextureSet(int *model, int textureSet) {
    /* 0x8014E828 stores the texture set at model +0x54. If the primary ZMB/ZAB
       block has a texture/frame-count table at +0x18, the original validates that
       the attached texture set has enough texture frames. */
    if (model == 0) {
        return;
    }

    model[0x15] = textureSet;
}

void CzanModel_SetContinuationCount(int *model, int continuationCount) {
    /* 0x8015C540 stores the continuation/animation block count at model +0x9C.
       CtsStageObj_LoadModelBlocks calls this before allocating the continuation
       handle array when a ZMB/ZAB entry has extra following blocks. */
    if (model == 0) {
        return;
    }

    model[0x27] = continuationCount;
}

int CzanModel_LoadContinuationBlock(int *model, void *continuationBlock, int continuationIndex) {
    /* 0x8014C68C attaches one continuation/ZAB block to a model. It validates
       continuationIndex against model +0x9C, stores the block pointer at model +0x0C,
       then calls the real continuation parser/builder at 0x8014E464(model, index). */
    if (model == 0 || continuationIndex >= model[0x27]) {
        return 0;
    }

    model[3] = (int)(uintptr_t)continuationBlock;
    CzanModel_ParseContinuationAnimationBlock(model, continuationIndex);
    return 1;
}

void CzanModel_ParseContinuationAnimationBlock(int *model, int continuationIndex) {
    /* 0x8014E464 parses the current continuation block pointer at model +0x0C.
       It treats the block as a ZAB animation resource, converts duration/key times
       from ticks through FLOAT_802E9E38, matches each ZAB object/channel name against
       the model object table, and fills the animation record array at model +0x14:

       record base = model[0x14] + (objectIndex + continuationIndex * model[0x98]) * 0x74

       Per matched object:
       +0x00/+0x04 -> translation key count and key pointer
       +0x08/+0x0C -> rotation key count and key pointer
       +0x10/+0x14 -> scale key count and key pointer
       +0x34       -> converted duration

       The original finally marks continuationBlock +0x28 as relocated/parsed. */
    (void)model;
    (void)continuationIndex;
}

void CzanModel_SetFallbackRenderSlot(int *model, int textureSet, int renderMode, unsigned char enabledFlag, unsigned char alpha) {
    unsigned char *bytes;

    /* 0x8015C3CC initializes the fallback/synthetic render slot at model +0x280.
       CtsStageObj_LoadModelBlocks calls it when a fallback texture slot exists:

       CzanModel_SetFallbackRenderSlot(model, textureSet, 6, 1, 0xFF)

       Confirmed fields:
       +0x280 -> renderMode
       +0x288 -> textureSet
       +0x28C..+0x293 -> default/fallback colors
       +0x29C..+0x2A4/+0x2B6/+0x2BC -> control flags and counters. */
    if (model == 0) {
        return;
    }

    bytes = (unsigned char *)model;
    model[0xa0] = renderMode;
    model[0xa2] = textureSet;
    bytes[0x29c] = 1;
    bytes[0x29d] = 0;
    bytes[0x29e] = enabledFlag;
    bytes[0x29f] = (unsigned char)renderMode;
    bytes[0x2a3] = 1;
    model[0xa9] = 0;
    *(unsigned short *)(void *)(bytes + 0x2b6) = 0;
    model[0xaf] = 0;
    bytes[0x2a0] = 0;
    bytes[0x2ab] = 0;
    bytes[0x290] = 0xff;
    bytes[0x291] = 0xff;
    bytes[0x292] = 0xff;
    bytes[0x293] = alpha;
    bytes[0x28c] = 0xff;
    bytes[0x28d] = 0xff;
    bytes[0x28e] = 0xff;
    bytes[0x28f] = 0xff;
}

int CzanModel_BuildRuntimeData(int *model, int enabled) {
    /* 0x8014C6CC is the large CzanModel primary-block relocation/runtime-build
       function. It is called by CzanModel_SetEnabled after normalizing enabled to
       0/1. This is the ZMB runtime builder, not a draw call.

       Confirmed ZMB build phases:
       - validates model[1] / primary block exists.
       - if primaryBlock +0x2C exists, marks model byte +0x6D.
       - relocates primary-block offsets in-place while primaryBlock +0x24 is 0.
       - from primaryBlock +0x18, allocates texture/frame helper arrays:
         model +0x5C and +0x60, each frameCount * 4 bytes.
       - from primaryBlock +0x1C, stores the material/part table at model +0x4C,
         picks part stride model +0xB8 as 0x38 or 0x50 from the table version,
         relocates child pointers, UV/keyframe tables, and nested part records.
       - counts visible/animated parts, allocates model +0x44 as count * 0x50,
         stores count at model +0x48, writes source part pointers at runtime +0x30,
         initializes each runtime part matrix, and calls
         CzanModel_InitVisiblePartUvRuntime.
       - builds per-part runtime animation/cache records at model +0x2C, stride 0xDC.
       - from primaryBlock +0x20, stores object count at model +0x98, object entries
         are 0xA0 bytes, and allocates model +0x1C/+0x20/+0x24/+0x28 as
         objectCount * 0x30 transform arrays.
       - if model +0x9C is nonzero, allocates model +0x14 as
         objectCount * model[0x9C] * 0x74 animation records.
       - allocates model +0x18 as objectCount * 0x10 skip/aux records.
       - scans object names for tags such as trans, ZDRAW, COLLINE, and the object
         name prefix table; writes object bytes +0x27/+0x28/+0x29/+0x2A/+0x2B.
       - initializes local transforms in model +0x1C from object +0x30/+0x34/+0x38
         and translation offsets at object +0x60.
       - relocates object submesh records at object +0x9C and submesh arrays
         +0x20/+0x24/+0x28/+0x2C/+0x30/+0x34.
       - if type-2 objects exist, allocates model +0x34 as objectCount * 0x1C,
         sets model +0x158, and builds per-submesh remap/output buffers used by
         CzanModel_UpdateType2WeightedVectors and type-2 draw submitters.
       - flushes/prepares the relocated primary block and marks primaryBlock +0x24 = 1. */
    if (model == 0 || model[1] == 0) {
        return 0;
    }

    (void)enabled;
    return 1;
}

int CzanModel_SetEnabled(int *model, unsigned int enabled) {
    /* 0x8014E7D0 normalizes any nonzero enabled value to 1, forwards it through
       FUN_8014C6CC, and if that succeeds calls FUN_8015759C(1.0, model). */
    if (model == 0) {
        return 0;
    }

    return CzanModel_BuildRuntimeData(model, enabled != 0);
}

int CzanModel_BuildRuntimeDataAndUpdateTransforms(int *model) {
    /* 0x8014E780 builds runtime data with enabled=0, then updates object
       transforms with a zero/default delta when the build succeeds. */
    if (CzanModel_BuildRuntimeData(model, 0) == 0) {
        return 0;
    }

    CzanModel_UpdateObjectTransforms(0.0, model);
    return 1;
}

void CzanModelOwner_CreateModelFromPrimaryBlock(int *owner, void *primaryBlock, int primaryBlockSize) {
    /* 0x8015EAE0 is the owner-side CzanModel constructor used by select_cmn.
       It destroys any existing model at owner +0x80, allocates a fresh 0x2D0-byte
       CzanModel, initializes it, stores it back at owner +0x80, then calls
       CzanModel_SetPrimaryBlock(model, primaryBlock, primaryBlockSize). */
    int *model;

    if (owner == 0) {
        return;
    }

    model = (int *)(uintptr_t)(unsigned int)owner[0x20];
    if (model != 0) {
        CzanModel_Destroy(model, 1);
        owner[0x20] = 0;
    }

    /* Host builds do not own the original aligned allocator here; keep the export
       as a named reconstruction point instead of fabricating a partial allocation. */
    (void)primaryBlock;
    (void)primaryBlockSize;
}

void CzanModelOwner_BuildRuntimeDataAt80(int *owner) {
    /* 0x8015EBAC builds runtime data for the CzanModel pointer stored at owner
       +0x80. This is a tiny owner-side wrapper around
       CzanModel_BuildRuntimeDataAndUpdateTransforms. */
    if (owner == 0 || owner[0x20] == 0) {
        return;
    }

    CzanModel_BuildRuntimeDataAndUpdateTransforms((int *)(uintptr_t)(unsigned int)owner[0x20]);
}

void CzanModelOwner_SetContinuationCount(int *owner, int continuationCount) {
    /* 0x8015EB98 checks owner +0x80 and forwards to CzanModel_SetContinuationCount.
       In select_cmn the caller passes 10 before attaching blocks 6..0xF. */
    if (owner == 0 || owner[0x20] == 0) {
        return;
    }

    CzanModel_SetContinuationCount((int *)(uintptr_t)(unsigned int)owner[0x20], continuationCount);
}

void CzanModelOwner_LoadContinuationBlock(int *owner, void *continuationBlock, int continuationIndex) {
    /* 0x8015EBC8 checks owner +0x80 and forwards to CzanModel_LoadContinuationBlock.
       The select_cmn loader calls this for blocks 6..0xF with continuation indices
       0..9, which are the ZAB/animation-side blocks paired with the primary model
       block loaded by CzanModelOwner_CreateModelFromPrimaryBlock. */
    if (owner == 0 || owner[0x20] == 0) {
        return;
    }

    CzanModel_LoadContinuationBlock((int *)(uintptr_t)(unsigned int)owner[0x20], continuationBlock, continuationIndex);
}

void CzanModelOwner_SetAnimationStartFrame(int *owner, double startFrame) {
    /* 0x8015EBF4 checks owner +0x80 and writes the frame value to model +0x250.
       select_cmn calls this with 1.0 after attaching the ten continuation blocks. */
    int *model;

    if (owner == 0 || owner[0x20] == 0) {
        return;
    }

    model = (int *)(uintptr_t)(unsigned int)owner[0x20];
    *(float *)(void *)((unsigned char *)model + 0x250) = (float)startFrame;
}

int CzanModelCollection_LoadFromLinkBlocks(int *collection, void *linkData, int modelCount, unsigned int collectionIndex) {
    /* 0x80177CA8 loads a collection/bank of CzanModels from a WII link resource.
       Blocks are consumed in pairs: even block index is the model/ZMB block, odd
       block index is its texture/TPL block. The original allocates modelCount
       CzanModel objects, creates texture slots for each odd block, attaches the
       matching texture set to each model, then builds runtime data immediately. */
    (void)linkData;
    (void)collectionIndex;
    if (collection == 0) {
        return -1;
    }

    (void)modelCount;
    return -1;
}

int CzanModelManager_LoadResource(int *manager, unsigned int bankIndex, void *linkData) {
    /* 0x8017872C loads one manager bank. Block 0 is metadata/header, block 1 is an
       optional TPL texture resource, and block 2 is an optional CzanModel
       collection loaded through CzanModelCollection_LoadFromLinkBlocks. */
    (void)bankIndex;
    (void)linkData;
    if (manager == 0) {
        return 0;
    }

    return 1;
}

int CzanModelManager_UnloadBank(int *manager, unsigned int bankIndex) {
    /* 0x80178B8C releases one model-manager bank. It destroys live objects whose
       owner byte at +0x12C matches the bank index, releases the bank texture slot,
       clears the loaded flag and slot, then refreshes manager state. */
    (void)bankIndex;
    if (manager == 0) {
        return 0;
    }

    return 1;
}

void CzanModelManager_Clear(int *manager) {
    /* 0x80178A68 destroys every live model object, unloads all eight banks, frees
       the live object pointer array, then resets the manager's internal lists. */
    if (manager == 0) {
        return;
    }
}

int CzanModelManager_ClearLiveObjects(int *manager) {
    /* 0x80178C8C destroys every live object in manager[0x69] without unloading
       model banks or resetting the manager's other lists. */
    if (manager == 0) {
        return 0;
    }

    return 1;
}

void CzanModelManager_UpdateVisibleGroup(void *context, void *unused, int removeFinished, char groupId) {
    /* 0x80178DE4 updates live objects whose object byte +0x12D matches groupId.
       It updates manager camera/view matrices, toggles per-object visibility,
       advances matching objects through FUN_8017E720 when they are not finished,
       and optionally destroys finished objects when removeFinished is nonzero. */
    (void)context;
    (void)unused;
    (void)removeFinished;
    (void)groupId;
}

void CzanModelObject_UnregisterManagerEntries(int *object) {
    /* 0x8017E5F8 unregisters an object's manager entries before the object is
       destroyed. Each registration record is 0x10 bytes at object +0x118, and the
       record type byte at *(record[0]) +2 selects which manager/list owns record[3]. */
    if (object == 0) {
        return;
    }
}

void CzanModelObject_Update(double delta, unsigned int *object) {
    /* 0x8017E720 updates one live model-manager object. It waits for registered
       dependencies when flag 4 is set, updates attached registrations while flag 1
       is set, handles fade-in/fade-out timers through flags 0x10/0x20, and marks
       the object finished with flag 8. */
    (void)delta;
    if (object == 0) {
        return;
    }
}

void CzanModelObject_UpdateRegistrationTarget(double delta, unsigned int *object, int *registration) {
    /* 0x8017F7F8 pushes one live object's current fade/value state into a
       registered target, calls the target's vtable +0x0C update method, logs bad
       linked-list pointers, then recursively processes linked child registrations. */
    (void)delta;
    (void)registration;
    if (object == 0) {
        return;
    }
}

void CzanModelManager_LoadBank1Resource(void *unused, void *linkData) {
    /* 0x8004EBA8 is a tiny wrapper that loads bank 1 into gManager_802E70B8. */
    (void)unused;
    (void)linkData;
}

void CzanModelManager_SetupBank3AndStageObjects(
    void *context,
    void *unused,
    unsigned char flagA,
    unsigned char flagB,
    void *bank3LinkData,
    void *stageObjectLinkData) {
    /* 0x8004EC4C clears five existing CtsStageObj pointers, clears manager-local
       state, optionally loads bank 3 into gManager_802E70B8, stores two flags, then
       optionally builds five CtsStageObj instances from grouped model/texture/
       continuation blocks in stageObjectLinkData. */
    (void)context;
    (void)unused;
    (void)flagA;
    (void)flagB;
    (void)bank3LinkData;
    (void)stageObjectLinkData;
}

void CzanModelManager_SwitchBank5ForMode(int *owner) {
    /* 0x80053B90 switches model manager bank 5 when the owner mode byte changes.

       Confirmed owner fields:
       owner +0x002D -> current mode/index byte
       owner +0x002E -> previous mode/index byte
       owner +0xB0B8 -> cached value copied from +0xB378 during switch
       owner +0xB0D4 + mode*4 -> per-mode link/resource pointer
       owner +0xB340 + mode*4 -> per-mode gManager_802E70A8 handle/id
       owner +0xB34C -> alternate handle/id when mode == 3
       owner +0xB36C -> boolean flag passed to FUN_80025248 after reload
       owner +0xB370 -> pending/transition flag
       owner +0xB374 -> requested mode/index byte
       owner +0xB378 -> cached value copied to +0xB0B8
       owner +0xB37C -> transition/lock flag

       Original behavior:
       - if +0xB370 is set but +0xB37C is clear, clear +0xB370
       - when requested mode differs from current mode and no transition is pending:
         unload CzanModelManager bank 5
         disable/clear the old mode handle through FUN_80025248 when present
         copy current mode to previous mode, requested mode to current mode
         if the new mode has a link/resource pointer, load bank 5 and call FUN_80053124(owner)
         enable/update the new mode handle through FUN_80025248 */
    if (owner == 0) {
        return;
    }
}

void CzanModelManager_InitBank5LiveObjectsForMode(int *owner) {
    /* 0x80053124 is called immediately after CzanModelManager_SwitchBank5ForMode
       loads bank 5 for the requested/current mode.

       Ghidra may show void(void) because RuntimeContext_SpillSavedRegisters recovers
       the owner pointer.

       Confirmed owner fields:
       owner +0x002D -> current mode/index byte
       owner +0x8A04 -> group/category byte passed to FUN_8017911C
       owner +0xB0E0 + mode -> count of 0x0C-byte entries for that mode
       owner +0xB0E4 + mode*0xC0 + entry*0x0C -> per-mode entry table
       entry +0x00 -> transform/position id, must not be -1
       entry +0x04 -> live model object handle, created when -1
       entry +0x08 -> signed model/resource id byte, must not be -1
       entry +0x0A -> signed bank/model slot byte, passed to FUN_8017911C

       Original flow for each valid entry:
       - create a live model object with FUN_8017911C(1.0, gManager_802E70B8,
         entry[0x0A], bank 5, owner +0x8A04)
       - store the returned handle at entry +0x04
       - get an owner slot/index through FUN_80054F88(owner)
       - if the selected owner slot has transform data, fetch transform/position data
         through FUN_8005941C(slot +0xAC, stackTransform, entry[0])
       - apply that transform to the live model object with
         FUN_801791F0(gManager_802E70B8, handle, stackTransform) */
    if (owner == 0) {
        return;
    }
}

void CzanModelOwner_LoadStageResourceGroup(int *owner, void *linkData) {
    /* 0x80054CF4 loads one owner-side stage/model resource group from a WII link.

       Confirmed flow:
       - root block 0, when present, is passed to LoadZmbZabModelEntryList(owner, block0)
       - root block 2 is a candidate model-manager bank 5 resource block
       - root block 3, when present, is a nested WII link; nested block 0 carries
         a small metadata/name record, and nested block 1 is forwarded with block 2
         through FUN_80054C00(owner, owner +0x8A04, block2, nestedBlock1)
       - when nested metadata exists, the first byte is stored at
         owner +0xB324 + groupIndex and the metadata string at +4 is copied to a
         newly allocated buffer stored at owner +0xB328 + groupIndex*4
       - if block2 and nested block1 exist for group 0, bank 5 is loaded from block2
       - FUN_800554B0(owner, groupIndex) finalizes the group, then owner +0x2C is
         incremented when any useful block existed

       This is not the draw path; it is the resource grouping path that prepares
       ZMB/ZAB model entries and the bank-5 resources later used by live objects. */
    (void)linkData;
    if (owner == 0) {
        return;
    }
}

void CzanModelOwner_SetupResourceGroupEntries(int *owner, unsigned char groupCategory, void *bank5LinkData, void *entryListLinkData) {
    /* 0x80054C00 stores metadata for the current owner resource group.

       Parameters:
       - owner: model/stage owner object
       - groupCategory: byte copied to owner +0xB0D0 + groupIndex
       - bank5LinkData: pointer copied to owner +0xB0D4 + groupIndex*4
       - entryListLinkData: optional list parsed by FUN_8018CD38/FUN_8018CDBC

       Confirmed fields:
       owner +0x002C -> current group index
       owner +0xB0D0 + group -> group category byte
       owner +0xB0D4 + group*4 -> bank/resource link pointer
       owner +0xB0EC + group*0xC0 + entry*0x0C +0 -> source byte 1
       owner +0xB0EC + group*0xC0 + entry*0x0C +1 -> source byte 2
       owner +0xB0EC + group*0xC0 + entry*0x0C +2 -> source byte 0

       This prepares the compact per-group live-object/resource entry table that
       later code reads at owner +0xB0E4/+0xB0EC ranges. */
    (void)groupCategory;
    (void)bank5LinkData;
    (void)entryListLinkData;
    if (owner == 0) {
        return;
    }
}

int CzanModelOwner_EnsureResourceGroupHandle(int *owner, unsigned int groupIndex) {
    /* 0x800554B0 creates the per-resource-group handle through
       ResourceSlotManager_AllocateSlot / FUN_80024EA8 when the owner is not locked
       by +0xB36C.

       Confirmed fields:
       owner +0xB324 + group -> metadata byte set by CzanModelOwner_LoadStageResourceGroup
       owner +0xB340 + group*4 -> per-group handle/id, initialized to -1
       owner +0xB34C -> special handle/id used for group 3
       owner +0xB354 + group*4 -> per-group resource/link presence test
       owner +0xB36C -> lock/disable flag; when nonzero no handle is created

       Return value is 1 when a new handle is created, otherwise 0. Group 3 stores
       its handle in +0xB34C instead of the normal +0xB340 table. */
    (void)groupIndex;
    if (owner == 0) {
        return 0;
    }

    return 0;
}

void CzanModelLiveObject_Init(int *object) {
    /* 0x8017911C is reached by the live-object creation path used by
       CzanModelManager_InitBank5LiveObjectsForMode. The caller-facing decompile
       shows extra arguments and a return handle; this tiny decompile is likely the
       constructor/init body after allocation.

       Confirmed body:
       - object +0x120 = -1
       - calls FUN_80179330(object/remaining recovered args)

       Keep the name conservative until the allocator/registration wrapper around
       this init body is fully mapped. */
    if (object == 0) {
        return;
    }
    object[0x48] = -1;
}

void CzanModelManager_SetLiveObjectMatrix(int *manager, int liveObjectHandle, const void *matrix) {
    /* 0x801791F0 applies matrix/transform data to one live object handle.

       Original flow:
       - if liveObjectHandle < 0, return
       - object = *(manager +0x1A4)[liveObjectHandle]
       - if object is null, log "CzanEffMng::set_mtx(): NULL!!!!"
       - otherwise call FUN_8017DA24(object +4, matrix). The pasted decompile of
         FUN_8017DA24 only recovered one parameter, but this call site passes the
         matrix/transform argument as well. */
    (void)matrix;
    if (manager == 0 || liveObjectHandle < 0) {
        return;
    }
}

void CzanModelLiveObject_ApplyGlobalScaleToMatrix(int *liveObjectTransform) {
    /* 0x8017DA24 normalizes/copies the live object's matrix at transform +0xDC,
       then multiplies the 3x3 basis values by the CzanModelManager global scale
       at manager +0x2A4, retrieved through FUN_80178270().

       Confirmed scaled float offsets relative to liveObjectTransform:
       +0xDC, +0xE0, +0xE4,
       +0xEC, +0xF0, +0xF4,
       +0xFC, +0x100, +0x104. */
    if (liveObjectTransform == 0) {
        return;
    }
}

unsigned char CzanModelOwner_SelectModeSlot(int *owner) {
    unsigned char currentMode;

    /* 0x80054F88 chooses which owner mode slot should supply transform data.

       It normally returns owner +0x2D (current mode). During mode-switch/blend
       state, when owner +0xB0C4 == 2, it can return owner +0x2E (previous mode)
       based on the blend timer fields at +0xB0BC/+0xB0C0, active flag +0xE6E8,
       and lock flag +0xB37C. */
    if (owner == 0) {
        return 0;
    }

    currentMode = *(unsigned char *)((unsigned char *)owner + 0x2d);
    return currentMode;
}

void CzanModelPositionSet_Clear(int *positionSet) {
    /* 0x80117A9C clears/releases the model position-set structure before
       FUN_801175B8 repopulates it. The exact owned fields still need mapping. */
    if (positionSet == 0) {
        return;
    }
}

void CzanModelPositionSet_LoadFromLinkList(int *positionSet, void **linkDataList, int linkDataCount) {
    /* 0x801175B8 loads a list of WII model resources into 0x38-byte entries.
       For each link resource it:
       - clears the old position set through FUN_80117A9C
       - stores linkDataCount at positionSet[0]
       - allocates positionSet[1] as linkDataCount * 0x38
       - treats every block in each WII resource as one CzanModel primary block
       - allocates blockCount CzanModels and calls CzanModel_SetPrimaryBlock +
         CzanModel_SetEnabled(model, 1) for each one
       - scans every model object's name for strings such as s1_pos...
       - builds six lookup/count tables from those named objects

       This is a model/position lookup loader, not the draw function. */
    (void)linkDataList;
    if (positionSet == 0) {
        return;
    }

    CzanModelPositionSet_Clear(positionSet);
    positionSet[0] = linkDataCount;
}

int CzanModel_FindObjectIndexByName(int *model, int objectName) {
    /* 0x8014E8E8 scans the object table in the primary model block at
       *(model +0x04) +0x20. Each entry is 0xA0 bytes, and FUN_801332F0 compares
       the entry name/key against objectName. Returns the matching index or -1. */
    if (model == 0 || model[1] == 0) {
        return -1;
    }

    (void)objectName;
    return -1;
}

int CzanModel_GetObjectTransform(int *model, int objectIndex) {
    /* 0x801568D4 returns the runtime object transform pointer:

       *(model +0x20) + objectIndex * 0x30

       The object transform array is allocated/populated by CzanModel_BuildRuntimeData
       and consumed by draw/effect code when composing model object matrices. */
    if (model == 0 || objectIndex < 0 || model[8] == 0) {
        return 0;
    }

    return model[8] + objectIndex * 0x30;
}

static int CzanModel_FindKeyIndex(double frame, const float *keys, int keyCount, int keyStrideFloats, int cachedKeyIndex) {
    int keyIndex;

    if (keyCount <= 0) {
        return 0;
    }

    if (cachedKeyIndex < 0 || cachedKeyIndex >= keyCount) {
        cachedKeyIndex = 0;
    }

    keyIndex = cachedKeyIndex;
    while (keyIndex < keyCount && frame >= (double)keys[keyIndex * keyStrideFloats]) {
        if (keyIndex + 1 >= keyCount || frame < (double)keys[(keyIndex + 1) * keyStrideFloats]) {
            break;
        }
        keyIndex++;
    }

    return keyIndex;
}

int CzanModel_EvaluateTranslationKeys(double frame, double duration, float *outVec3, const void *keys, int keyCount, int loop, int cachedKeyIndex) {
    const float *keyFloats;
    int keyIndex;
    int nextIndex;
    float t;

    /* 0x8015D9C4 evaluates translation keyframes. Each key is 0x10 bytes:
       time, x, y, z. It returns the key index to cache for the next call. */
    if (outVec3 == 0 || keys == 0 || keyCount <= 0) {
        return 0;
    }

    keyFloats = (const float *)keys;
    keyIndex = CzanModel_FindKeyIndex(frame, keyFloats, keyCount, 4, cachedKeyIndex);
    nextIndex = keyIndex + 1;
    if (keyCount < 2 || nextIndex >= keyCount) {
        nextIndex = (loop != 0 && keyCount > 1) ? 0 : keyIndex;
    }

    if (nextIndex == keyIndex) {
        outVec3[0] = keyFloats[keyIndex * 4 + 1];
        outVec3[1] = keyFloats[keyIndex * 4 + 2];
        outVec3[2] = keyFloats[keyIndex * 4 + 3];
        return keyIndex;
    }

    t = (float)((frame - (double)keyFloats[keyIndex * 4]) /
        ((nextIndex == 0 ? duration : (double)keyFloats[nextIndex * 4]) - (double)keyFloats[keyIndex * 4]));
    outVec3[0] = keyFloats[keyIndex * 4 + 1] + (keyFloats[nextIndex * 4 + 1] - keyFloats[keyIndex * 4 + 1]) * t;
    outVec3[1] = keyFloats[keyIndex * 4 + 2] + (keyFloats[nextIndex * 4 + 2] - keyFloats[keyIndex * 4 + 2]) * t;
    outVec3[2] = keyFloats[keyIndex * 4 + 3] + (keyFloats[nextIndex * 4 + 3] - keyFloats[keyIndex * 4 + 3]) * t;
    return keyIndex;
}

int CzanModel_EvaluateRotationKeys(double frame, double duration, float *outQuat, const void *keys, int keyCount, int loop, int cachedKeyIndex) {
    const float *keyFloats;
    int keyIndex;
    int nextIndex;
    float t;
    int i;

    /* 0x8015DACC evaluates rotation keyframes. Each key is 0x14 bytes:
       time plus four rotation/quaternion floats. The original interpolation helper
       is FUN_80145FDC; the host placeholder stores component-interpolated values. */
    if (outQuat == 0 || keys == 0 || keyCount <= 0) {
        return 0;
    }

    keyFloats = (const float *)keys;
    keyIndex = CzanModel_FindKeyIndex(frame, keyFloats, keyCount, 5, cachedKeyIndex);
    nextIndex = keyIndex + 1;
    if (keyCount < 2 || nextIndex >= keyCount) {
        nextIndex = (loop != 0 && keyCount > 1) ? 0 : keyIndex;
    }

    if (nextIndex == keyIndex) {
        for (i = 0; i < 4; i++) {
            outQuat[i] = keyFloats[keyIndex * 5 + 1 + i];
        }
        return keyIndex;
    }

    t = (float)((frame - (double)keyFloats[keyIndex * 5]) /
        ((nextIndex == 0 ? duration : (double)keyFloats[nextIndex * 5]) - (double)keyFloats[keyIndex * 5]));
    for (i = 0; i < 4; i++) {
        outQuat[i] = keyFloats[keyIndex * 5 + 1 + i] +
            (keyFloats[nextIndex * 5 + 1 + i] - keyFloats[keyIndex * 5 + 1 + i]) * t;
    }
    return keyIndex;
}

int CzanModel_EvaluateScaleKeys(double frame, double duration, float *outVec3, const void *keys, int keyCount, int loop, int cachedKeyIndex) {
    /* 0x8015DBD8 is structurally identical to the translation evaluator: 0x10-byte
       keys holding time, x, y, z. */
    return CzanModel_EvaluateTranslationKeys(frame, duration, outVec3, keys, keyCount, loop, cachedKeyIndex);
}

void CzanModel_UpdateAnimationChannel(int *model, int holdFrame, int channelIndex) {
    /* 0x8015627C updates one animation channel at model +0x230 + channel*0x24.
       Channel 0 drives the main transform update. The original advances the
       channel timer, handles loop/end flags, updates animation blending through
       FUN_801564BC/FUN_8015601C, and calls CzanModel_UpdateObjectTransforms for
       channel 0. */
    (void)holdFrame;
    (void)channelIndex;
    if (model == 0) {
        return;
    }
}

void CzanModel_ApplyAnimationChannelFrame(double frame, int *model, int channelIndex) {
    /* 0x8015601C applies one animation channel's current frame to every object
       local transform. It reads channel data at model +0x230 + channel*0x24,
       per-object animation records from model +0x14, and updates local transforms
       at model +0x1C using translation, rotation, and scale keyframe helpers. */
    (void)frame;
    (void)channelIndex;
    if (model == 0 || model[1] == 0) {
        return;
    }
}

void CzanModel_BlendAnimationChannelFrame(double deltaOrScale, int *model, float *channel, int channelIndex) {
    /* 0x801564BC blends one animation channel frame into existing per-object
       animation transforms. It evaluates translation/rotation/scale keyframes into
       temporary matrices, blends them into the per-object animation record at
       model +0x14 using channel[7] as weight unless the object has a forced weight
       flag, then updates object transforms for channel 0. */
    (void)deltaOrScale;
    (void)channel;
    (void)channelIndex;
    if (model == 0 || model[1] == 0) {
        return;
    }
}

void CzanModel_SolveObjectAnimationTransform(void *outMatrix, void *objectWorkspace, int *objectAnimState, int *objectAnimConfig) {
    /* 0x8018B4BC solves the secondary per-object animation transform used by
       CzanModel_UpdateObjectAnimation / 0x801574B0.

       The caller passes:
       - outMatrix: stack transform later copied into model +0x1C + objectIndex*0x30
       - objectWorkspace: model +0x1CC + objectIndex*0xB4
       - objectAnimState: model +0x1C0 + objectIndex*0x24
       - objectAnimConfig: objectEntry +0x9C

       Confirmed behavior:
       - composes matrices from objectAnimState[0..2]
       - uses objectAnimConfig flags/fields to correct or blend the solved vector
       - maintains previous/current matrices in the object workspace
       - writes the solved matrix back to outMatrix and updates workspace state

       This is downstream from raw ZAB key evaluation: the keyframes have already
       produced runtime object animation state, and this helper turns that state into
       the final local object transform. */
    (void)outMatrix;
    (void)objectWorkspace;
    (void)objectAnimState;
    (void)objectAnimConfig;
}

void CzanModel_UpdateObjectAnimation(double deltaOrScale, int *model, int objectIndex) {
    /* 0x801574B0 updates one object's animation transform when model +0x1BC is
       enabled and the object table entry byte +0x98 is nonzero. It writes the
       current delta/scale into model +0x1C0 object state, evaluates animation data
       through CzanModel_SolveObjectAnimationTransform, converts it into the object's
       local transform at model +0x1C, then recomposes the world transform at
       model +0x20. */
    (void)deltaOrScale;
    (void)objectIndex;
    if (model == 0 || model[1] == 0) {
        return;
    }
}

void CzanModel_UpdateType2WeightedVectors(int *model, int *objectEntry, int *drawContext) {
    /* 0x8014F7D0 updates the per-submesh weighted vector buffer used by type-2
       object entries before the type-2 part tree draw path.

       Ghidra recovers model/objectEntry through the saved-register helper
       FUN_8012A134. The explicit drawContext argument is the model +0x34 runtime
       entry passed by CzanModel_UpdateObjectTransforms.

       Confirmed behavior:
       - objectEntry +0x9A is the submesh count, and +0x9C points at 0x40-byte
         per-submesh records.
       - each drawContext submesh record is 0x10 bytes; drawContext +0x04 points at
         output buffers used by later type-2 draw submitters.
       - source transforms come from model +0x28, stride 0x30.
       - for each weighted source record, it copies a matrix/vector, applies a
         weight at source +0x3C, accumulates into the output float3 buffer, then
         flushes the buffer with FlushDataCacheRange.

       This is not final drawing; it prepares the type-2 generated vector arrays that
       CzanModel_DrawType2PartTree and the type-2 primitive submitters consume. */
    if (model == 0 || objectEntry == 0 || drawContext == 0) {
        return;
    }
}

void CzanModel_UpdateObjectTransforms(double deltaOrScale, int *model) {
    /* 0x8015759C updates runtime transform arrays for every object entry in the
       primary model block. It walks the 0xA0-byte object table, composes parent
       transforms from model +0x1C/+0x20, optionally writes +0x24/+0x28 skinning or
       alternate transform arrays, updates type-2 weighted vectors through
       CzanModel_UpdateType2WeightedVectors, updates object animation data through
       CzanModel_UpdateObjectAnimation, then finalizes through FUN_801576FC. */
    (void)deltaOrScale;
    if (model == 0 || model[1] == 0) {
        return;
    }
}

void CzanModel_DrawVisibleObjects(int *model, int arg1, const void *baseMatrix, int arg2) {
    /* 0x8014F420 is called by CtsStageObj_ApplyModelTransform / FUN_800594B8 as:

         CzanModel_DrawVisibleObjects(stageObj[0], arg1, stageObj +7, arg2)

       Confirmed behavior from the full decompile:
       - clears model +0x150 and returns unless a primary model block exists and
         model alpha/visibility at +0x7C is nonzero
       - copies/composes the caller base matrix, stores arg2 at model +0x128, and
         stores the caller matrix pointer at model +0x15C
       - when model +0x140 is set and a caller matrix exists, builds an alternate
         transform through FUN_8014E96C
       - walks the primary model object's 0xA0-byte table
       - only draws objects with submesh count +0x9A, visible bytes +0x28/+0x2A clear,
         and no runtime skip entry in model +0x18
       - for each object, composes matrices and dispatches each submesh to either
         CzanModel_DrawType2PartTree when object type +0x2C is 2, or
         CzanModel_DrawStandardPartTree otherwise
       - increments model +0x1A4 modulo model +0x1A0 and marks model +0x164 = 1

       This is the CzanModel visible-object draw traversal. The part-tree walkers
       then dispatch to the actual leaf draw routines at FUN_8015BD68/FUN_8015BEFC
       and FUN_801595BC/FUN_80159730. */
    (void)arg1;
    (void)baseMatrix;
    (void)arg2;
    if (model == 0) {
        return;
    }
}

void CzanModel_DrawType2PartTree(
    int *model,
    int *partIndexSource,
    int submeshIndex,
    int partTableBase,
    const void *objectMatrix,
    int childIndex,
    int *drawContext,
    int objectIndex) {
    /* 0x80151E90 recursively walks a model part/material tree for visible objects
       whose object entry type field (+0x2C) is 2.

       Recovered behavior:
       - selects a part entry at partTableBase + model[0x2E] * *partIndexSource
       - if childIndex is nonzero, switches to that child part through parent +0x2C
       - byte part +0x13 controls leaf rendering:
         - 0: container node; recurse into children
         - 1: call CzanModel_UpdateType2PartTexcoords(model, partIndexSource,
                                                      drawContext, submeshIndex,
                                                      drawContext[3] + submeshIndex * 0x10)
         - other: call CzanModel_UpdateType2SpecialPartTexcoords(model, partIndexSource,
                                                                 partEntry, drawContext,
                                                                 submeshIndex,
                                                                 drawContext[3] + submeshIndex * 0x10)
       - child count is short part +0x2A, child table pointer is part +0x2C
       - child stride is model[0x2E] (model +0xB8)

       objectMatrix and objectIndex are passed through by the caller but are not used
       directly in this wrapper; the leaf routines use drawContext/submesh metadata. */
    (void)objectMatrix;
    (void)objectIndex;
    if (model == 0 || partIndexSource == 0 || drawContext == 0) {
        return;
    }

    (void)submeshIndex;
    (void)partTableBase;
    (void)childIndex;
}

void CzanModel_DrawStandardPartTree(
    int *model,
    int *partIndexSource,
    int partTableBase,
    int childPass,
    const void *objectMatrix,
    int *drawContext,
    int submeshIndex,
    int objectIndex) {
    /* 0x80155484 recursively walks the standard/non-type-2 part tree.

       Recovered behavior:
       - selects a part entry at partTableBase + model[0x2E] * *partIndexSource
       - on childPass, switches to the first child at part +0x2C
       - byte part +0x13 controls leaf rendering:
         - 0: container node; recurse once into the first child when childPass == 0
         - 1: call CzanModel_UpdateStandardPartTexcoords(model, partIndexSource,
                                                         objectMatrix,
                                                         drawContext[3] + submeshIndex * 0x10)
         - other: call CzanModel_UpdateStandardSpecialPartTexcoords(model, partIndexSource,
                                                                    partEntry, objectMatrix,
                                                                    drawContext[3] + submeshIndex * 0x10)

       Unlike CzanModel_DrawType2PartTree, this path only follows the first child
       pointer directly in the pasted decompile. */
    (void)objectIndex;
    if (model == 0 || partIndexSource == 0 || drawContext == 0) {
        return;
    }

    (void)partTableBase;
    (void)childPass;
    (void)objectMatrix;
    (void)submeshIndex;
}

void CzanModel_UpdateStandardPartTexcoords(int *model, int *partIndexSource, const void *objectMatrix, unsigned short *submesh) {
    /* 0x801595BC is the standard-path leaf for part byte +0x13 == 1.

       Confirmed behavior:
       - recovers model, object vertex position table at caller state +0x24, and
         normal/vector table at caller state +0x2C from the saved-register helper
       - selects model +0xC8 as the base matrix unless model +0x12C is nonzero, in
         which case it uses that matrix pointer
       - updates only a slice of vertices based on model +0x1A0/+0x1A4
       - reads index pairs from submesh +0x0C, using each pair to select position and
         normal/vector records
       - transforms/project vectors through matrix helpers
       - writes two floats per vertex to *(submesh +0x04)
       - flushes *(submesh +0x04), vertexCount * 8 bytes

       This is a CPU texcoord/projection update, not the final draw submit. */
    if (model == 0 || partIndexSource == 0 || objectMatrix == 0 || submesh == 0) {
        return;
    }
}

void CzanModel_UpdateStandardSpecialPartTexcoords(
    int *model,
    int *partIndexSource,
    int partEntry,
    const void *objectMatrix,
    unsigned short *submesh) {
    /* 0x80159730 is the standard-path leaf for part byte +0x13 values other than 1.

       Confirmed behavior:
       - starts from the same model/objectMatrix/submesh inputs as
         CzanModel_UpdateStandardPartTexcoords
       - if part byte +0x13 is not 4 and model +0x128 is nonzero, overrides the
         projection vector from *(model +0x128)
       - chooses matrix source from objectMatrix or model +0x16C depending on
         model +0x160
       - kind 3 uses a direct transformed normal/vector projection
       - other kinds normalize a transformed vector and compute the output texcoords
         from the x/y components
       - writes two floats per vertex to *(submesh +0x04), then flushes the buffer

       Part kind 4 zeroes the projection vector before the non-kind-3 calculation. */
    if (model == 0 || partIndexSource == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)objectMatrix;
}

void CzanModel_UpdateType2PartTexcoords(
    int *model,
    int *partIndexSource,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh) {
    /* 0x8015BD68 is the type-2 path leaf for part byte +0x13 == 1.

       Confirmed behavior:
       - uses model +0xC8 or model +0x12C as the base matrix source
       - uses drawContext[1] + submeshIndex * 0x10 +4 to locate a per-submesh vector
         table
       - updates only the current model +0x1A4 slice out of model +0x1A0 slices
       - reads position indices from submesh +0x0C
       - computes a normalized vector from base matrix translation to the source
         position, projects it through the per-submesh vector table, then writes two
         floats per vertex to *(submesh +0x04)
       - flushes *(submesh +0x04), vertexCount * 8 bytes

       The paired type-2 special leaf is CzanModel_UpdateType2SpecialPartTexcoords. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0 || submesh == 0) {
        return;
    }

    (void)submeshIndex;
}

void CzanModel_UpdateType2SpecialPartTexcoords(
    int *model,
    int *partIndexSource,
    int partEntry,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh) {
    /* 0x8015BEFC is the type-2 special texcoord/projection leaf for part byte
       +0x13 values other than 1.

       Confirmed behavior:
       - uses model +0xC8/model +0x12C as the base matrix/vector source, optionally
         overridden by model +0x128 when part kind is not 4
       - uses drawContext[1] + submeshIndex * 0x10 +4 for a per-submesh vector table
       - updates only the current model +0x1A4 slice out of model +0x1A0 slices
       - has separate formulas for part kinds 2, 3, 4, and the external-matrix path
         when model +0x15C is present and model +0x160 is clear
       - writes two floats per vertex to *(submesh +0x04), then flushes the buffer

       This completes the recovered CPU texcoord generation leaves. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)submeshIndex;
}

void CzanModel_ApplyMaterialCullMode(int *partMaterial, int forceCullBack) {
    /* 0x80177150 selects the GX cull mode via FUN_801D40E0.

       Original rules:
       - if partMaterial exists and byte +0x11 is nonzero, call FUN_801D40E0(0)
       - else if forceCullBack is nonzero, call FUN_801D40E0(1)
       - otherwise call FUN_801D40E0(2)

       This is likely material-local culling/visibility state, with the caller able
       to force the middle mode. */
    (void)partMaterial;
    (void)forceCullBack;
}

void CzanModel_ApplyMaterialBlendMode(int *partMaterial, int forceAlphaCompare, int forceBlendEnabled) {
    /* 0x80177184 applies material blend/alpha state.

       Original behavior:
       - starts with FUN_801D7200(1, 3, 1)
       - materialMode = partMaterial byte +0x12 & 0x7F
       - highBit = partMaterial byte +0x12 >> 7
       - forceBlendEnabled overrides highBit to 1
       - materialMode 3 -> RenderSetBlendMode(1, 4, 5, 5)
       - materialMode 2 -> RenderSetBlendMode(1, 0, 5, 5)
       - materialMode 1 -> RenderSetBlendMode(1, 4, 1, 5), then FUN_801D7200(1, 3, 0)
       - default        -> RenderSetBlendMode(1, 4, 5, 5)
       - if materialMode == 0 or forceAlphaCompare:
           RenderSetAlphaUpdate(1)
           RenderSetAlphaCompare(7, 0, 1, 7, 0)
         else:
           RenderSetAlphaUpdate(0)
           highBit clear -> RenderSetAlphaCompare(4, 0xA0, 0, 3, 0xFF)
           highBit set   -> RenderSetAlphaCompare(4, 0,    0, 3, 0xFF)

       This is the blend/alpha compare half of the material state used by
       CzanModel_SetupPartRenderState and the material tree walkers. */
    (void)partMaterial;
    (void)forceAlphaCompare;
    (void)forceBlendEnabled;
}

void CzanModel_SetupMaterialVertexAttributes(
    int *submesh,
    int *partMaterial,
    int forceNormalAttr,
    int useGeneratedColorAttr,
    int useTexcoordAttr) {
    /* 0x801772E8 configures the GX vertex attribute layout for one material/submesh.

       Confirmed source offsets:
       - short submesh +0x06 controls position attr source mode.
       - submesh +0x24 is position-array base when short +0x06 != 1.
       - submesh +0x34 is color array base.
       - submesh +0x30 is generated texcoord array base.
       - submesh +0x2C is normal/vector array base.
       - partMaterial byte +0x10 enables normal/vector attr 10.

       Confirmed attr setup:
       - always clears attrs through RenderClearVertexDescriptors / FUN_801D2B80.
       - attr 9 is position:
         short +0x06 == 1 -> direct/indexed mode 1
         otherwise        -> mode 3 with array base submesh +0x24, stride 0x0C
       - attr 11 is color when submesh +0x34 exists:
         useGeneratedColorAttr == 0 -> mode 3, array base submesh +0x34, stride 4
         otherwise                  -> direct/indexed mode 1
       - attr 13 is texcoord when submesh +0x30 exists and useTexcoordAttr != 0:
         mode 3, array base submesh +0x30, stride 8
       - attr 10 is normal/vector when material byte +0x10 or forceNormalAttr is set
         and submesh +0x2C exists:
         short +0x06 == 1 -> direct/indexed mode 1
         otherwise        -> mode 3, array base submesh +0x2C, stride 0x0C

       The PC renderer can map this directly into a vertex declaration once the ZMB
       runtime arrays are decoded. */
    (void)submesh;
    (void)partMaterial;
    (void)forceNormalAttr;
    (void)useGeneratedColorAttr;
    (void)useTexcoordAttr;
}

int CzanModel_SetupPartRenderState(int *model, void *outState, int *drawArgs) {
    /* 0x80157FE4 is the shared material/render-state setup helper used by the
       primitive submit leaves.

       Confirmed behavior:
       - drawArgs[0] points at the part/material entry.
       - drawArgs[2] selects the texture/runtime slot; negative values use the
         default texture object at model +0x288.
       - drawArgs[3] enables the display/config path through FUN_80143858 or
         FUN_801438A4.
       - drawArgs[4] enables an extra render-state bit through FUN_801D7200.
       - drawArgs[5]/drawArgs[6] feed the material/color mask setup.
       - binds the resolved texture with GXLoadTexObj_wrapper.
       - calls CzanModel_ApplyMaterialBlendMode / FUN_80177184 and configures TEV,
         blend, alpha, texgen, and raster state through the render/GX wrapper layer.
       - returns success when a usable texture/render state was resolved; returns
         zero when the part cannot bind its required texture.

       The original also writes two small state values through the saved-register
       helper context. The host renderer should eventually turn this into a real
       RenderState object instead of a boolean stub. */
    if (model == 0 || drawArgs == 0 || drawArgs[0] == 0) {
        return 0;
    }

    (void)outState;
    return 1;
}

void CzanModel_SubmitPartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    const void *objectMatrix,
    unsigned short *submesh) {
    /* 0x801586C0 is a real GX/display-list submit leaf.

       Confirmed behavior:
       - recovers model plus draw-state fields from a saved-register helper
       - calls FUN_80157FE4 to validate/setup the part and render state
       - configures vertex attribute arrays with FUN_801D2B80 plus
         RenderSetVertexAttrDescriptor, RenderSetVertexAttrFormat, and
         RenderSetVertexArray
       - binds/generated texcoords at attr 0x0D from *(submesh +0x04)
       - optionally regenerates texcoords through the same slice logic when
         model +0x164 == 0 and model +0x168 != 0
       - emits GX primitive 0x98 batches through RenderBeginPrimitiveBatch and writes
         indices, colors, and texcoords to the write-gather pipe at 0xCC008000

       This is one of the PC renderer's key targets: it tells us which arrays feed
       the final draw call. */
    if (model == 0 || partIndexSource == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)childIndex;
    (void)objectMatrix;
}

void CzanModel_SubmitSpecialPartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    const void *objectMatrix,
    unsigned short *submesh) {
    /* 0x80158DB8 is the non-kind-1 companion to CzanModel_SubmitPartPrimitive.

       Confirmed behavior:
       - validates part/render state through FUN_80157FE4
       - configures position, normal/vector, color, and generated texcoord attrs
         through the same render/GX wrapper layer as 0x801586C0
       - for part kind 3, directly projects the normal/vector table into generated
         texcoords; for other kinds it normalizes/project vectors
       - binds attr 0x0D to *(submesh +0x04), optionally regenerating that buffer
         when model +0x164 == 0 and model +0x168 != 0
       - emits primitive 0x98 batches to the GX write-gather pipe

       This is the special/non-kind-1 primitive submit path reached by the material
       part-tree renderers. */
    if (model == 0 || partIndexSource == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)childIndex;
    (void)objectMatrix;
}

void CzanModel_SubmitType2PartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh) {
    /* 0x8015ABC8 is the type-2 object-path companion to
       CzanModel_SubmitPartPrimitive for part kind 1.

       Confirmed behavior:
       - calls CzanModel_SetupPartRenderState / FUN_80157FE4 first.
       - pulls a per-submesh vector table from drawContext[1] + submeshIndex * 0x10
         + 4.
       - configures GX vertex attrs with FUN_801D2B80 plus
         RenderSetVertexAttrDescriptor, RenderSetVertexAttrFormat, and
         RenderSetVertexArray.
       - when model +0x164 is clear and model +0x168 is set, regenerates generated
         texcoords for the active model +0x1A4 slice.
       - emits primitive 0x98 batches through RenderBeginPrimitiveBatch and writes
         the position index, per-submesh vector index/data, color index, and
         generated texcoord index to the write-gather pipe.

       This is one of the exact select_cmn model submitters we need for the PC
       renderer once ZMB runtime arrays are populated. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)childIndex;
    (void)submeshIndex;
}

void CzanModel_SubmitType2SpecialPartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh) {
    /* 0x8015B2D8 is the type-2 object-path companion to
       CzanModel_SubmitSpecialPartPrimitive for non-kind-1 parts.

       Confirmed behavior:
       - calls CzanModel_SetupPartRenderState / FUN_80157FE4 first.
       - uses drawContext[1] + submeshIndex * 0x10 + 4 for the per-submesh vector
         table.
       - configures the same GX attr/primitive stream as the other submit leaves.
       - contains the type-2 special texcoord formulas for part kinds 2, 3, 4, and
         the external-matrix path.
       - emits primitive 0x98 batches to 0xCC008000.

       The important distinction from 0x80158DB8 is that this path uses the type-2
       drawContext/submesh vector table instead of the standard object matrix path. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0 || submesh == 0) {
        return;
    }

    (void)partEntry;
    (void)childIndex;
    (void)submeshIndex;
}

void CzanModel_DrawBaseMaterialPartTree(
    int *model,
    int *partIndexSource,
    int partTableBase,
    int childIndex,
    const void *objectMatrix,
    int *drawContext,
    int submeshIndex,
    int objectIndex) {
    /* 0x80153F44 is the base/fallback material part draw traversal.

       Confirmed behavior:
       - accepts a null partTableBase and handles default model material state
       - applies part/material state with CzanModel_ApplyMaterialCullMode,
         CzanModel_SetupMaterialVertexAttributes, and
         CzanModel_ApplyMaterialBlendMode
       - configures texture/color/alpha/TEV state through the render/GX wrapper layer
       - when a texture is available, binds texture-set entries and emits primitive
         batches to 0xCC008000
       - recurses into child parts, using CzanModel_DrawMaterialPartTree when a child
         has byte +0x17 set and CzanModel_DrawBaseMaterialPartTree otherwise
       - when model +0x168 is enabled, delegates leaves to
         CzanModel_SubmitPartPrimitive or CzanModel_SubmitSpecialPartPrimitive

       Together with CzanModel_DrawMaterialPartTree, this is the material-state and
       GX-submit layer above the primitive leaves. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0) {
        return;
    }

    (void)partTableBase;
    (void)childIndex;
    (void)objectMatrix;
    (void)submeshIndex;
    (void)objectIndex;
}

void CzanModel_DrawMaterialPartTree(
    int *model,
    int *partIndexSource,
    int partTableBase,
    int childIndex,
    const void *objectMatrix,
    int *drawContext,
    int submeshIndex,
    int objectIndex) {
    /* 0x801528B4 is a full material/part draw traversal with render-state setup.

       Confirmed behavior:
       - selects the current part from partTableBase + model[0x2E] * *partIndexSource,
         or from the child table when childIndex is nonzero
       - calls CzanModel_ApplyMaterialCullMode and
         CzanModel_SetupMaterialVertexAttributes to apply part/material state
       - handles special model modes at model +0x280 by dispatching to FUN_80159A04
         or FUN_8015A2B8
       - binds texture set slots through BindTextureFromTextureSet/GXLoadTexObj_wrapper
       - configures TEV/color/alpha state through the render/GX wrapper layer
       - emits primitive batches to the GX write-gather pipe
       - when model +0x168 is enabled, delegates leaf submissions to
         CzanModel_SubmitPartPrimitive for kind 1 or
         CzanModel_SubmitSpecialPartPrimitive for other kinds
       - recurses through child parts and handles extra synthetic part model +0x28C

       This is closer to the final renderer than the earlier texcoord leaves. */
    if (model == 0 || partIndexSource == 0 || drawContext == 0) {
        return;
    }

    (void)partTableBase;
    (void)childIndex;
    (void)objectMatrix;
    (void)submeshIndex;
    (void)objectIndex;
}

void CzanModel_FinalizeTransformUpdate(double deltaOrScale, int *model) {
    /* 0x801576FC runs once after all object transforms are updated. It advances
       material/part frame timers from the primary block's +0x1C table, clears the
       dirty byte at model +0x50, optionally rescales delta by model +0x94 and
       +0x19C, calls FUN_80156B70, optionally calls FUN_801579A8 when model +0x1B4
       is set, then marks model +0x150 = 1. */
    (void)deltaOrScale;
    if (model == 0 || model[1] == 0) {
        return;
    }
}

void CzanModel_InitVisiblePartUvRuntime(int *model) {
    /* 0x801568FC / CzanModel_InitVisiblePartUvRuntime initializes/resets UV
       animation runtime fields for visible parts.

       Ghidra may show void(void) because the function begins with a saved-register
       helper. The recovered object is the CzanModel pointer.

       Confirmed behavior:
       - reads the material/part table at *(model +0x04) +0x1C
       - uses model +0x44 as the visible-part runtime array and model +0x48 as count
       - each runtime part entry is 0x50 bytes
       - clears runtime timers/offsets at +0x0C, +0x1C, +0x40, +0x44
       - clears bytes +0x4A/+0x4B
       - when the source part at runtime +0x30 has a UV/key table at +0x3C, reads
         count at source +0x3A and initializes current key indices +0x4A/+0x4B
       - computes starting UV offsets into runtime +0x0C/+0x1C and duration/range
         into +0x34/+0x38/+0x3C */
    if (model == 0) {
        return;
    }
}

void CzanModel_UpdatePartUvAnimation(double deltaOrScale, int *model) {
    /* 0x80156B70 advances UV/scroll animation for renderable parts. It walks the
       primary block's part table at +0x1C, updates runtime part records at model
       +0x44 with scrolling values, then applies keyframed UV animation curves from
       part +0x3C when available. */
    (void)deltaOrScale;
    if (model == 0 || model[1] == 0) {
        return;
    }
}
