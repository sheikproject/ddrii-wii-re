#include "game/cgame.h"

#include "model/czan_model.h"
#include "render/render_engine.h"
#include "resource/resource_manager.h"
#include "runtime/memory.h"
#include "runtime/module_system.h"
#include "runtime/string_util.h"
#include "ui/czan_ui.h"

#include <stdint.h>
#include <stdio.h>

static unsigned int gRuntimeRandomSeed802e68d8;
static int *gCGameUiRootContext;

static int CGame_GetGlobalUiManager(void) {
    int *globalContext = GlobalRuntimeContext_Get();
    if (globalContext == 0) {
        return 0;
    }
    return globalContext[0x9c];
}

static int *CGame_GetInputManager(void) {
    return GameMain_GetInputOrMenuStateManager();
}

void CGame_InitDefaultGameplaySetup(int *cgame) {
    static const char *const defaultSongNameTable[] = {
        "more than alive",
        0
    };

    /* FUN_8003D180 seeds the default CGame setup block used before player/menu
       choices overwrite the fields. */
    if (cgame == 0) {
        return;
    }

    cgame[0xe0 / 4] = 0;
    cgame[0xe4 / 4] = 0;
    cgame[0xe8 / 4] = (int)(uintptr_t)defaultSongNameTable;
    cgame[0xec / 4] = 0;
    cgame[0xf0 / 4] = 0x12345678;
    cgame[0xf4 / 4] = 0;
    cgame[0xf8 / 4] = 0;
    cgame[0xfc / 4] = -1;
    cgame[0x104 / 4] = 0;
    cgame[0x100 / 4] = 0;
    cgame[0x108 / 4] = 0;
    cgame[0x10c / 4] = 5;
    cgame[0xdc / 4] = 0;
    cgame[0x114 / 4] = 0;
    cgame[0x3ec / 4] = 0;
    cgame[0x118 / 4] = 0;
    cgame[0xd4 / 4] = 0x06880000;
}

int *PlayerDataState_Init(int *state) {
    /* FUN_80020EE4 initializes a 0x10-byte state child:
       +0x00 = 0xFFFF, +0x04 = 0xFFFF, +0x08 = 0, +0x0C = vtable. */
    if (state == 0) {
        return 0;
    }

    state[0] = 0xffff;
    state[1] = 0xffff;
    state[2] = 0;
    state[3] = 0;
    return state;
}

void PlayerDataState_SetInitialValue(int *state, int value) {
    /* FUN_80021030 only writes the first word while +0x04 is still 0xFFFF. */
    if (state == 0 || state[1] != 0xffff) {
        return;
    }
    state[0] = value;
}

int *PlayerDataStateContainer_Init(int *container) {
    int *child;

    /* FUN_80020B68 initializes a small container with an embedded allocated
       PlayerDataState child at +0x0C, then seeds that child with value 0. */
    if (container == 0) {
        return 0;
    }

    container[0] = 0;
    container[1] = 0;
    container[2] = 0;
    container[3] = 0;
    container[4] = 0;

    child = (int *)MemoryPool_AllocateAligned(0, 0x10, 0x20);
    if (child != 0) {
        PlayerDataState_Init(child);
    }
    container[3] = (int)(uintptr_t)child;
    PlayerDataState_SetInitialValue(child, 0);
    return container;
}

static void PlayerDataManager_InitSubBlock(int *playerDataManager, int offset, int size) {
    if (playerDataManager == 0) {
        return;
    }
    ClearMemory((unsigned char *)playerDataManager + offset, 0, size);
}

static int PlayerDataManager_GetDefaultMenuRecordValue(int index) {
    static const int defaultValues8026e8b8[4] = {
        0, 0, 0, 0
    };

    /* DAT_8026E8B8 is a four-word default table. Keep it named here until the
       data table is exported from the binary. */
    return defaultValues8026e8b8[index & 3];
}

void PlayerDataManager_ResetPlayerRecord(int *playerDataManager, int playerIndex) {
    unsigned char *base;
    int i;

    /* FUN_80028330 clears one/all seven 0x19C-byte player records at +0x17C. */
    if (playerDataManager == 0) {
        return;
    }

    base = (unsigned char *)playerDataManager;
    if (playerIndex == -1) {
        for (i = 0; i < 7; i++) {
            PlayerDataManager_ResetPlayerRecord(playerDataManager, i);
        }
        return;
    }

    if (playerIndex < 0 || playerIndex >= 7) {
        return;
    }

    ClearMemory(base + 0x17c + playerIndex * 0x19c, 0, 0x19c);
    *(int *)(void *)(base + 0x17c + playerIndex * 0x19c) = -1;
}

void PlayerDataManager_ResetMenuRecords(int *playerDataManager) {
    unsigned char *base;
    unsigned int i;

    /* FUN_80028500 resets eight 0x44-byte records at +0xCC4, four words at
       +0xEE4, and four 0x12-byte records at +0xEF4. */
    if (playerDataManager == 0) {
        return;
    }

    base = (unsigned char *)playerDataManager;
    *(int *)(void *)(base + 0xcc0) = 0;
    ClearMemory(base + 0xcc4, 0, 0x220);
    ClearMemory(base + 0xee4, 0, 0x10);
    ClearMemory(base + 0xef4, 0, 0x48);

    *(int *)(void *)(base + 0xcc0) = 1;
    for (i = 0; i < 8; i++) {
        unsigned char *record = base + 0xcc4 + i * 0x44;
        *(int *)(void *)(record + 0x00) = -1;
        *(int *)(void *)(record + 0x04) = PlayerDataManager_GetDefaultMenuRecordValue((int)i);
        *(int *)(void *)(record + 0x08) = 0;
        *(int *)(void *)(record + 0x0c) = 0;
        *(int *)(void *)(record + 0x10) = 0;
        *(int *)(void *)(record + 0x14) = -1;
        *(int *)(void *)(record + 0x18) = 0;
        *(int *)(void *)(record + 0x1c) = 0;
        *(int *)(void *)(record + 0x20) = 0;
        *(int *)(void *)(record + 0x24) = 0;
        *(int *)(void *)(record + 0x28) = 0;
        *(int *)(void *)(record + 0x30) = 0;
        *(int *)(void *)(record + 0x34) = 0xd;
        *(int *)(void *)(record + 0x38) = 5;
        *(int *)(void *)(record + 0x3c) = 0;
        *(int *)(void *)(record + 0x40) = 0;

        if (i < 4) {
            *(int *)(void *)(base + 0xee4 + i * 4) = 0;
        }
        if (i >= 4 && i < 8) {
            ClearMemory(base + 0xef4 + (i - 4) * 0x12, 0, 0x12);
        }
    }
}

void PlayerDataManager_SetModeRecordRowValues(
    int *playerDataManager,
    int rowIndex,
    int value0,
    int value1,
    int modeIndex) {
    unsigned char *base;
    int i;

    /* FUN_80028D4C writes row +0x00/+0x04 for one/all six mode records. */
    if (playerDataManager == 0 || rowIndex < 0 || rowIndex >= 5) {
        return;
    }

    if (modeIndex == -1) {
        for (i = 0; i < 6; i++) {
            PlayerDataManager_SetModeRecordRowValues(playerDataManager, rowIndex, value0, value1, i);
        }
        return;
    }

    if (modeIndex < 0 || modeIndex >= 6) {
        return;
    }

    base = (unsigned char *)playerDataManager + 0xf7c + modeIndex * 0xdc + rowIndex * 0x2c;
    *(int *)(void *)(base + 0x00) = value0;
    *(int *)(void *)(base + 0x04) = value1;
}

void PlayerDataManager_ResetModeRecordRowTail(int *playerDataManager, int rowIndex, int modeIndex) {
    unsigned char *base;
    int i;

    /* FUN_80028FA0 resets row +0x0C/+0x10 for one/all six mode records. */
    if (playerDataManager == 0 || rowIndex < 0 || rowIndex >= 5) {
        return;
    }

    if (modeIndex == -1) {
        for (i = 0; i < 6; i++) {
            PlayerDataManager_ResetModeRecordRowTail(playerDataManager, rowIndex, i);
        }
        return;
    }

    if (modeIndex < 0 || modeIndex >= 6) {
        return;
    }

    base = (unsigned char *)playerDataManager + 0xf7c + modeIndex * 0xdc + rowIndex * 0x2c;
    *(int *)(void *)(base + 0x0c) = 0;
    *(int *)(void *)(base + 0x10) = -1;
}

void PlayerDataManager_ResetModeRecord(int *playerDataManager, int modeIndex) {
    unsigned char *base;
    int row;
    int i;

    /* FUN_8002896C clears one/all six 0xDC-byte mode/setup records at +0xF7C. */
    if (playerDataManager == 0) {
        return;
    }

    if (modeIndex == -1) {
        for (i = 0; i < 6; i++) {
            PlayerDataManager_ResetModeRecord(playerDataManager, i);
        }
        return;
    }

    if (modeIndex < 0 || modeIndex >= 6) {
        return;
    }

    base = (unsigned char *)playerDataManager;
    *(int *)(void *)(base + 0xf64 + modeIndex * 4) = 0;
    ClearMemory(base + 0xf7c + modeIndex * 0xdc, 0, 0xdc);

    for (row = 0; row < 5; row++) {
        PlayerDataManager_SetModeRecordRowValues(playerDataManager, row, 0, 0, modeIndex);
        PlayerDataManager_ResetModeRecordRowTail(playerDataManager, row, modeIndex);
    }
}

void PlayerDataManager_ResetSubBlock14A4(int *subBlock) {
    unsigned char *base;

    /* FUN_800F3940 resets the constructed player-data subblock at +0x14A4. */
    if (subBlock == 0) {
        return;
    }

    base = (unsigned char *)subBlock;
    ClearMemory(base, 0, 0x128);
    ClearMemory(base + 0x128, 0, 4);
    ClearMemory(base + 0x154, 0, 0x14);
    *(int *)(void *)(base + 0x15c) = -1;
    ClearMemory(base + 0x168, 0, 0x388);
    *(int *)(void *)(base + 0x17c) = 5;
    *(int *)(void *)(base + 0x4f4) = 0;
    *(int *)(void *)(base + 0x4f8) = 0;
    *(int *)(void *)(base + 0x130) = 0;
    *(int *)(void *)(base + 0x12c) = 0;
    *(int *)(void *)(base + 0x138) = 0;
    *(int *)(void *)(base + 0x134) = 0;
    *(int *)(void *)(base + 0x140) = 0;
    *(int *)(void *)(base + 0x13c) = 0;
    *(int *)(void *)(base + 0x148) = 0;
    *(int *)(void *)(base + 0x144) = 0;
    *(int *)(void *)(base + 0x150) = 0;
    *(int *)(void *)(base + 0x14c) = 0;
    ClearMemory(base + 0x4f0, 0, 4);
    *(int *)(void *)(base + 0x4f0) = 1;
}

void PlayerDataManager_ResetSubBlock1BA8(int *subBlock) {
    int record;

    /* FUN_800F36C4 seeds four 0x20-byte records: {6, index, 0x52, 0, ...}. */
    if (subBlock == 0) {
        return;
    }

    for (record = 0; record < 4; record++) {
        int *entry = subBlock + record * 8;
        entry[0] = 6;
        entry[1] = record;
        entry[2] = 0x52;
        entry[3] = 0;
        entry[4] = 0;
        entry[5] = 0;
        entry[6] = 0;
        entry[7] = 0;
    }
}

void PlayerDataManager_ResetSubBlock1C28(unsigned char *subBlock) {
    /* FUN_800DE50C resets the compact 0x2C-byte player-data subblock at +0x1C28. */
    if (subBlock == 0) {
        return;
    }

    ClearMemory(subBlock, 0, 0x14);
    subBlock[0] = 0xff;
    subBlock[1] = 0xff;
    subBlock[2] = 0xff;
    subBlock[3] = 0xff;
    subBlock[4] = 0xff;
    subBlock[5] = 0xff;
    *(int *)(void *)(subBlock + 0x14) = 1;
    *(int *)(void *)(subBlock + 0x18) = 1;
    *(int *)(void *)(subBlock + 0x1c) = 1;
    *(int *)(void *)(subBlock + 0x20) = 1;
    *(int *)(void *)(subBlock + 0x24) = -1;
    *(int *)(void *)(subBlock + 0x28) = 0;
    subBlock[8] = 0xff;
    subBlock[0x0c] = 0xff;
    subBlock[9] = 0xff;
    subBlock[0x0d] = 0xff;
    subBlock[10] = 0xff;
    subBlock[0x0e] = 0xff;
    subBlock[0x0b] = 0xff;
    subBlock[0x0f] = 0xff;
}

void PlayerDataManager_ResetSubBlock1C54(int *subBlock) {
    unsigned char *base;
    int i;

    /* FUN_800DF6BC resets nine 0x204-byte records, four 0x20-byte records, and
       six tail words in the large player-data subblock at +0x1C54. */
    if (subBlock == 0) {
        return;
    }

    base = (unsigned char *)subBlock;
    for (i = 0; i < 9; i++) {
        unsigned char *record = base + i * 0x204;
        ClearMemory(record, 0, 0x204);
        record[0x50] = 1;
        record[0x51] = 1;
    }

    for (i = 0; i < 4; i++) {
        ClearMemory(base + 0x1224 + i * 0x20, 0, 0x20);
    }

    *(int *)(void *)(base + 0x12b0) = 0;
    *(int *)(void *)(base + 0x12b4) = 0;
    *(int *)(void *)(base + 0x12b8) = 0;
    *(int *)(void *)(base + 0x12bc) = 0;
    *(int *)(void *)(base + 0x12c0) = 0;
    *(int *)(void *)(base + 0x12c4) = 0;
}

void PlayerDataManager_ResetSubBlock2F1C(int *subBlock) {
    int randomValue;
    int quotient;

    /* FUN_801206D8 resets the 0x24-byte random-seeded subblock at +0x2F1C. */
    if (subBlock == 0) {
        return;
    }

    *(int *)(void *)((unsigned char *)subBlock + 0x04) = 0;
    *(int *)(void *)((unsigned char *)subBlock + 0x08) = 1;
    *(int *)(void *)((unsigned char *)subBlock + 0x0c) = -1;
    *(int *)(void *)((unsigned char *)subBlock + 0x10) = 0;
    *(int *)(void *)((unsigned char *)subBlock + 0x14) = -1;
    *((unsigned char *)subBlock + 0x18) = 0;
    randomValue = (int)RuntimeRandom_Next15();
    *((unsigned char *)subBlock + 0x1a) = 0;
    *((unsigned char *)subBlock + 0x1b) = 0;
    *((unsigned char *)subBlock + 0x1c) = 0;
    *((unsigned char *)subBlock + 0x1d) = 0;
    quotient = randomValue / 0xff;
    *(int *)(void *)((unsigned char *)subBlock + 0x20) = -1;
    *((signed char *)subBlock + 0x19) = (signed char)(randomValue - quotient * 0xff); /* rand() % 255 (mulhw/srawi/mulli 0xFF/subf at 0x80120728) */
}

void PlayerDataManager_ResetSubBlock2F40(int *subBlock) {
    /* FUN_80123574 clears the final four-word player-data subblock at +0x2F40. */
    if (subBlock == 0) {
        return;
    }
    subBlock[0] = 0;
    subBlock[1] = 0;
    subBlock[2] = 0;
    subBlock[3] = 0;
}

static void PlayerDataManager_ResetRuntimeSubBlocks(int *playerDataManager) {
    unsigned char *base;

    if (playerDataManager == 0) {
        return;
    }

    base = (unsigned char *)playerDataManager;
    PlayerDataManager_ResetSubBlock14A4((int *)(void *)(base + 0x14a4));
    PlayerDataManager_ResetSubBlock1BA8((int *)(void *)(base + 0x1ba8));
    PlayerDataManager_ResetSubBlock1C28(base + 0x1c28);
    PlayerDataManager_ResetSubBlock1C54((int *)(void *)(base + 0x1c54));
    PlayerDataManager_ResetSubBlock2F1C((int *)(void *)(base + 0x2f1c));
    PlayerDataManager_ResetSubBlock2F40((int *)(void *)(base + 0x2f40));
}

void PlayerDataManager_Reset(int *playerDataManager) {
    unsigned char *base;
    static const int timerWords[] = {0x46, 0x4a, 0x4e, 0x52, 0x56, 0x5a};
    int i;

    /* FUN_80027DE4 is the real player-data default-state reset used by
       PlayerDataManager_Init. */
    if (playerDataManager == 0) {
        return;
    }

    base = (unsigned char *)playerDataManager;
    ClearMemory(playerDataManager, 0, 0x14a4);

    playerDataManager[0] = -1;
    playerDataManager[1] = 0;
    playerDataManager[2] = -1;
    playerDataManager[3] = 3;
    playerDataManager[4] = 0;
    playerDataManager[5] = 1;
    *(unsigned short *)(void *)(base + 0x178) = 0;

    ClearMemory(base + 0x18, 0, 0xe0);
    playerDataManager[0x3e] = 0;
    playerDataManager[0x3f] = 0x3c;
    base[0x100] = 1;
    base[0x101] = 1;

    ClearMemory(base + 0x108, 0, 0x70);
    playerDataManager[0x42] = -1;

    for (i = 0; i < (int)(sizeof(timerWords) / sizeof(timerWords[0])); i++) {
        int wordIndex = timerWords[i];
        int byteIndex = (wordIndex + 2) * 4;
        playerDataManager[wordIndex] = 0;
        playerDataManager[wordIndex + 1] = 0x3c;
        base[byteIndex] = 1;
        base[byteIndex + 1] = 1;
    }

    PlayerDataManager_ResetPlayerRecord(playerDataManager, -1);
    PlayerDataManager_ResetMenuRecords(playerDataManager);
    ClearMemory(base + 0xf3c, 0, 0x28);
    playerDataManager[0x3cf] = 0;
    playerDataManager[0x3d0] = 0xd;
    playerDataManager[0x3d1] = 5;
    playerDataManager[0x3d2] = 0;
    playerDataManager[0x3d3] = 0;
    playerDataManager[0x3d4] = 0;
    playerDataManager[0x3d5] = 0xd;
    playerDataManager[0x3d6] = 5;
    playerDataManager[0x3d7] = 0;
    playerDataManager[0x3d8] = 0;

    PlayerDataManager_ResetModeRecord(playerDataManager, -1);
    PlayerDataManager_ResetRuntimeSubBlocks(playerDataManager);
}

int PlayerDataManager_Init(int *playerDataManager) {
    /* FUN_80027CA8 constructs the player-data manager and several large subblocks:
       +0x14A4, +0x1BA8, +0x1C28, +0x1C54, +0x2F1C, +0x2F40, then runs the local
       reset helper at FUN_80027DE4. */
    if (playerDataManager == 0) {
        return 0;
    }

    playerDataManager[0x2f50 / 4] = 0;
    PlayerDataManager_InitSubBlock(playerDataManager, 0x14a4, 0x704);
    PlayerDataManager_InitSubBlock(playerDataManager, 0x1ba8, 0x80);
    PlayerDataManager_InitSubBlock(playerDataManager, 0x1c28, 0x2c);
    PlayerDataManager_InitSubBlock(playerDataManager, 0x1c54, 0x12c8);
    PlayerDataManager_InitSubBlock(playerDataManager, 0x2f1c, 0x24);
    PlayerDataManager_InitSubBlock(playerDataManager, 0x2f40, 0x10);
    PlayerDataManager_Reset(playerDataManager);
    return (int)(uintptr_t)playerDataManager;
}

static void CGameUi_StartObjectGroupAnimation(int objectGroupHandle, int animationIndex, int arg2, int arg3) {
    int uiManager = CGame_GetGlobalUiManager();

    /* 0x80062D58 starts/queues a Czan UI object-group animation:
       - FUN_80174FE0(uiManager, group, arg3) writes object +0x175
       - FUN_80174E2C(0.0f, uiManager, group, animationIndex) starts animation
       - FUN_80174F60(uiManager, group, arg2) writes object +0x174 and clears +0xB1 */
    CzanUiManager_SetObjectGroupAnimationMode(uiManager, objectGroupHandle, (unsigned char)arg3);
    CzanUiManager_StartObjectGroupAnimation(0.0, uiManager, objectGroupHandle, animationIndex);
    CzanUiManager_SetObjectGroupAnimationResetMode(uiManager, objectGroupHandle, (unsigned char)arg2);
}

unsigned int RuntimeRandom_Next15(void) {
    /* 0x80131C0C is the game's simple LCG:
       DAT_802E68D8 = DAT_802E68D8 * 0x41C64E6D + 0x3039
       return DAT_802E68D8 >> 16 & 0x7FFF */
    gRuntimeRandomSeed802e68d8 = gRuntimeRandomSeed802e68d8 * 0x41c64e6dU + 0x3039U;
    return (gRuntimeRandomSeed802e68d8 >> 16) & 0x7fffU;
}

void GlobalCueManager_ResolveCueId(unsigned int cueId, unsigned int *outEffectId, int *outEffectParam) {
    unsigned int variantRange;
    unsigned int randomValue;

    /* 0x800F3158 resolves packed/randomized cue ids. Bits 0x078000 encode a
       random variant count; bit 0x80000 invalidates the cue. */
    if (outEffectId != 0) {
        *outEffectId = 0xffffffffU;
    }
    if (outEffectParam != 0) {
        *outEffectParam = -1;
    }

    if (cueId != 0xffffffffU) {
        if ((cueId & 0x80000U) != 0) {
            cueId = 0xffffffffU;
        }
        if ((cueId & 0xffff8000U) != 0) {
            variantRange = cueId & 0x78000U;
            cueId &= 0x7fffU;
            if (variantRange != 0) {
                variantRange >>= 0xf;
                randomValue = RuntimeRandom_Next15();
                cueId += randomValue - (randomValue / variantRange) * variantRange;
            }
        }
    }

    if (outEffectId != 0) {
        *outEffectId = cueId;
    }
}

static int GlobalCueManager_PlayCue(double startTime, int *cueManager, int cueId, int arg3, int arg4) {
    int *globalContext = GlobalRuntimeContext_Get();
    unsigned int effectHandle = 0xffffffffU;
    int effectParam = -1;

    /* 0x8002425C dispatches a global cue/effect/sound request through
       gManager_802E70A4:
       - initialize a stack effect transform through FUN_80166D54
       - seed local timing floats from the caller and FLOAT_802E7BC8
       - resolve cueId through GlobalCueManager_ResolveCueId(cueId, &effectHandle, &effectParam)
       - if effectHandle is valid, spawn through FUN_8016B270(*(DAT_802E71B8 +0x268), ...)
    */
    (void)cueManager;
    (void)startTime;
    GlobalCueManager_ResolveCueId((unsigned int)cueId, &effectHandle, &effectParam);
    if (globalContext == 0 || effectHandle == 0xffffffffU) {
        return -1;
    }
    return CzanEffectManager_StartEffect(
        (int *)(uintptr_t)(unsigned int)globalContext[0x9a],
        (int)effectHandle,
        effectParam,
        arg4 != 0,
        0,
        arg3,
        -1);
}

int CharacterAssetManager_PlayCue(int *helper, int cueId) {
    int handle;

    /* 0x800CC894: helper is gManager_802E70C4 (the host's characterAssetManager,
       which also owns the save-data flow at +0x24). Plays cueId through
       GlobalCueManager_PlayCue(1.0f, gManager_802E70A4, cueId, 0, 0), then clears
       the helper timer at +0x0C. Returns the cue handle (negative when none). */
    handle = GlobalCueManager_PlayCue(1.0, 0, cueId, 0, 0);
    if (helper != 0) {
        *(float *)(void *)(helper + 3) = 0.0f;
    }
    return handle;
}

void CharacterAssetManager_SetCueMode(int *helper, int mode) {
    /* 0x800CC72C: helper +0x08 = mode, +0x0C = 0.0f. */
    if (helper == 0) {
        return;
    }
    helper[2] = mode;
    *(float *)(void *)(helper + 3) = 0.0f;
}

unsigned int InputOrMenuStateManager_TestPressedMask(int *manager, int controllerIndex, unsigned int mask) {
    unsigned int activeMask;

    /* 0x8002AE28 tests buttons newly pressed this frame (record +0x08). */
    if (manager == 0 || controllerIndex < 0) {
        return 0;
    }
    activeMask = *(unsigned int *)((unsigned char *)manager + controllerIndex * 0x20 + 8);
    mask &= activeMask;
    return ((0U - mask) | mask) >> 31;
}

unsigned int InputOrMenuStateManager_TestActiveMask(int *manager, int controllerIndex, unsigned int mask) {
    unsigned int activeMask;

    /* 0x8002AE08 tests buttons currently held (record +0x04). */
    if (manager == 0 || controllerIndex < 0) {
        return 0;
    }
    activeMask = *(unsigned int *)((unsigned char *)manager + controllerIndex * 0x20 + 4);
    mask &= activeMask;
    return ((0U - mask) | mask) >> 31;
}

unsigned int InputOrMenuStateManager_TestRepeatMask(int *manager, int controllerIndex, unsigned int mask) {
    unsigned int activeMask;

    /* 0x8002AE48 tests press + auto-repeat (0.5 s delay, 0.1 s interval; record +0x10). */
    if (manager == 0 || controllerIndex < 0) {
        return 0;
    }
    activeMask = *(unsigned int *)((unsigned char *)manager + controllerIndex * 0x20 + 0x10);
    mask &= activeMask;
    return ((0U - mask) | mask) >> 31;
}

unsigned int InputOrMenuStateManager_IsConfirmPressed(int *manager, int controllerIndex) {
    /* 0x8002ADE0 returns bit 11 from controller record +0x08. */
    if (manager == 0 || controllerIndex < 0) {
        return 0;
    }
    return (*(unsigned int *)((unsigned char *)manager + controllerIndex * 0x20 + 8) >> 0x0b) & 1U;
}

unsigned int InputOrMenuStateManager_IsBackPressed(int *manager, int controllerIndex) {
    /* 0x8002ADF4 returns bit 10 from controller record +0x08. */
    if (manager == 0 || controllerIndex < 0) {
        return 0;
    }
    return (*(unsigned int *)((unsigned char *)manager + controllerIndex * 0x20 + 8) >> 10) & 1U;
}

static int RuntimeDebugText_GetLineHeight(void) {
    /* FUN_80145070 returns the text row height/spacing used by the debug viewer. */
    return 16;
}

void CGame_PrepareManagersAndResources(int *cgame) {
    /* 0x8003CDC4 is the CGame setup/loading state machine.

       Key fields:
       cgame +0x008 -> next/active module state, set to 5 on error and 2 when ready
       cgame +0x00C -> setup substate
       cgame +0x0B8 -> pending/current game setup mode; -1 means use player data directly
       cgame +0x3F0..+0x428 -> owned CGame subsystem pointers created by CGameFactorySetup

       Substates:
       0 -> reset managers/subsystems and choose direct player-data setup or boot-temp loading
       1 -> wait for gBootTempManager to finish loading
       2 -> apply boot resource bundle and run CGame readiness check
       3 -> wait for external/resource managers, then wire CGame subsystems
       8 -> wait for DAT_802E71F8 +0x0C to clear
       0x0C -> ready; sets cgame +0x08 to module state 2

       The important resource path is:
       if substate 1 and FUN_8002202C(gBootTempManager) != 0:
         BootResourceBundle_ApplyLoadedResources(gBootTempManager)
         substate = 2
    */
    int substate;
    int viewerResult;
    int *bootResourceBundle;
    unsigned int clearColor;

    if (cgame == 0) {
        return;
    }

    substate = cgame[0x0c / 4];
    bootResourceBundle = GameMain_GetBootResourceBundle();

    if (substate == 0) {
        if (*(int *)((unsigned char *)cgame + 0x0b8) == -1) {
            cgame[0x0c / 4] = 3;
        }
        else {
            BootResourceBundle_StartLoading(bootResourceBundle);
            cgame[0x0c / 4] = 1;
        }
        substate = cgame[0x0c / 4];
    }

    if (substate == 1 && bootResourceBundle != 0) {
        BootResourceBundle_ApplyLoadedResources(bootResourceBundle);
        cgame[0x0c / 4] = 2;
        substate = 2;
    }

    if (substate == 2) {
        clearColor = 0x000000ffu;
        RenderBeginFrame();
        ApplyRenderConfig(0, &clearColor);
        viewerResult = CGame_UpdateViewerSetupSelection(cgame);
        RenderEndFrame();

        if (viewerResult == -1) {
            cgame[0x08 / 4] = 5;
        }
        else if (viewerResult == 1) {
            cgame[0x0c / 4] = 3;
        }
    }

    if (cgame[0x0c / 4] == 3) {
        cgame[0x0c / 4] = 8;
    }
    if (cgame[0x0c / 4] == 8) {
        cgame[0x0c / 4] = 0x0c;
    }
    if (cgame[0x0c / 4] == 0x0c) {
        cgame[0x08 / 4] = 2;
    }
}

int CGame_UpdateViewerSetupSelection(int *cgame) {
    static const char *const typeLabels[2] = {
        "SINGLE",
        "BATTLE",
    };
    static const char *const styleLabels[7] = {
        "SINGLE",
        "BATTLE",
        "MULTI",
        "",
        "FRIEND",
        "FAMILY",
        "DOUBLE",
    };
    static const char *const modeLabels[2] = {
        "NORMAL",
        "BOARD",
    };
    char line[256];
    int lineHeight;
    int y;
    int result = 0;

    /* 0x8003C9A0 is the interactive debug/viewer setup selector used by the CGame
       loading path. It updates style/mode fields, draws SSQ/MOTION/VIEWER labels,
       and returns 1 to accept, -1 to cancel, or 0 to keep selecting. */
    if (cgame == 0) {
        return 0;
    }

    lineHeight = RuntimeDebugText_GetLineHeight();
    if (InputOrMenuStateManager_TestRepeatMask(CGame_GetInputManager(), 4, 8)) {
        *(short *)((unsigned char *)cgame + 0x48) = 0;
        (*(int *)((unsigned char *)cgame + 0x14))--;
        if (*(int *)((unsigned char *)cgame + 0x14) < 0) {
            *(int *)((unsigned char *)cgame + 0x14) = 1;
        }
    }
    if (InputOrMenuStateManager_TestRepeatMask(CGame_GetInputManager(), 4, 4)) {
        *(short *)((unsigned char *)cgame + 0x48) = 0;
        (*(int *)((unsigned char *)cgame + 0x14))++;
        if (*(int *)((unsigned char *)cgame + 0x14) > 1) {
            *(int *)((unsigned char *)cgame + 0x14) = 0;
        }
    }

    if (*(int *)((unsigned char *)cgame + 0x14) == 0) {
        if (InputOrMenuStateManager_TestRepeatMask(CGame_GetInputManager(), 4, 1)) {
            do {
                (*(int *)((unsigned char *)cgame + 0xc8))--;
                if (*(int *)((unsigned char *)cgame + 0xc8) < 0) {
                    *(int *)((unsigned char *)cgame + 0xc8) = 6;
                }
            } while (*(int *)((unsigned char *)cgame + 0xc8) != 0 &&
                     *(int *)((unsigned char *)cgame + 0xc8) != 3 &&
                     *(int *)((unsigned char *)cgame + 0xc8) != 4 &&
                     *(int *)((unsigned char *)cgame + 0xc8) != 6);
        }
        if (InputOrMenuStateManager_TestRepeatMask(CGame_GetInputManager(), 4, 2)) {
            do {
                (*(int *)((unsigned char *)cgame + 0xc8))++;
                if (*(int *)((unsigned char *)cgame + 0xc8) > 6) {
                    *(int *)((unsigned char *)cgame + 0xc8) = 0;
                }
            } while (*(int *)((unsigned char *)cgame + 0xc8) != 0 &&
                     *(int *)((unsigned char *)cgame + 0xc8) != 3 &&
                     *(int *)((unsigned char *)cgame + 0xc8) != 4 &&
                     *(int *)((unsigned char *)cgame + 0xc8) != 6);
        }
    }
    else if (*(int *)((unsigned char *)cgame + 0x14) == 1) {
        if (InputOrMenuStateManager_TestRepeatMask(CGame_GetInputManager(), 4, 1)) {
            (*(int *)((unsigned char *)cgame + 0x10))--;
            if (*(int *)((unsigned char *)cgame + 0x10) < 0) {
                *(int *)((unsigned char *)cgame + 0x10) = 1;
            }
        }
        if (InputOrMenuStateManager_TestRepeatMask(CGame_GetInputManager(), 4, 2)) {
            (*(int *)((unsigned char *)cgame + 0x10))++;
            if (*(int *)((unsigned char *)cgame + 0x10) > 1) {
                *(int *)((unsigned char *)cgame + 0x10) = 0;
            }
        }
    }

    if (*(int *)((unsigned char *)cgame + 0xb8) == 0 &&
        *(int *)((unsigned char *)cgame + 0x10) == 0) {
        *(int *)((unsigned char *)cgame + 0xc0) = 5;
    }
    else {
        *(int *)((unsigned char *)cgame + 0xc0) =
            (int)(uintptr_t)typeLabels[*(int *)((unsigned char *)cgame + 0x10) & 1];
    }

    if (InputOrMenuStateManager_IsConfirmPressed(CGame_GetInputManager(), 4)) {
        result = 1;
    }
    else if (InputOrMenuStateManager_IsBackPressed(CGame_GetInputManager(), 4)) {
        result = -1;
    }

    DebugText_SetGlyphSize(lineHeight);
    if (*(int *)((unsigned char *)cgame + 0xb8) == 0) {
        RuntimeString_FormatBuffer(line, "SSQ VIEWER");
    }
    else if (*(int *)((unsigned char *)cgame + 0xb8) == 1) {
        RuntimeString_FormatBuffer(line, "MOTION VIEWER");
    }
    else {
        RuntimeString_FormatBuffer(line, "VIEWER");
    }
    DebugText_Draw(0x20, 0x40, line);

    y = (lineHeight >> 1) + lineHeight + 0x40;
    RuntimeString_FormatBuffer(line,
                               "%cSTYLE : %s",
                               (*(int *)((unsigned char *)cgame + 0x14) == 0 &&
                                (*(unsigned short *)((unsigned char *)cgame + 0x48) & 0x20) == 0) ? '>' : ' ',
                               styleLabels[*(int *)((unsigned char *)cgame + 0xc8) % 7]);
    DebugText_Draw(lineHeight + 0x20, y, line);

    RuntimeString_FormatBuffer(line,
                               "%cMODE : %s",
                               (*(int *)((unsigned char *)cgame + 0x14) == 1 &&
                                (*(unsigned short *)((unsigned char *)cgame + 0x48) & 0x20) == 0) ? '>' : ' ',
                               modeLabels[*(int *)((unsigned char *)cgame + 0x10) & 1]);
    DebugText_Draw(lineHeight + 0x20, y + lineHeight, line);
    DebugText_SetGlyphSize(lineHeight);
    (*(short *)((unsigned char *)cgame + 0x48))++;

    return result;
}

int CGame_TeardownRuntimeState(int *cgame, int nextModuleOrState) {
    /* 0x8003C4F8 tears down/reset CGame runtime state and returns the supplied next
       module/state value.

       Confirmed behavior:
       - mirrors cgame +0xB0 low five bits into DAT_802E71B8 +0x260 records
       - resets gLargeResourceManager, gManager_802E70A4, movie slot cgame +0x414,
         and transition subsystem cgame +0x428
       - destroys the active controller at cgame +0x42C through vtable +0x10 and
         ActiveGameplayControllerBase_Destroy
       - resets/clears CGame subsystems at +0x3F0..+0x410 and +0x428
       - releases owned subsystem objects and nulls +0x40C/+0x410
       - restores UI/input flags from cgame +0xA8/+0xB4 and +0xB0
       - calls the local cleanup helper at 0x8003BF90, clears the UI root, and resets
         render/GX state through FUN_801D7350(0, 0)

       This is the teardown counterpart to the CGame setup/startup path. */
    (void)cgame;
    return nextModuleOrState;
}

void CGame_PrepareSceneFromSelectedSetup(int *cgame) {
    /* 0x8003EE48 is a CGame setup/loading state machine sibling to 0x8003CDC4.

       It resets the same global managers and CGame subsystems at substate 0, then:
       - if cgame +0x0B8 == -1, calls CGame_BuildSceneSetupFromPlayerData and
         configures subsystem +0x3F0 directly, entering substate 3
       - otherwise enters substate 2 and runs CGame_UpdateSceneSetupSelection until
         it returns -1 or 1
       - when ready, waits for the resource managers, then calls
         CGame_LoadSceneResourceManagers(cgame)
       - substate 0x0C sets cgame +0x08 = 3

       Compared with CGame_PrepareManagersAndResources, failure sets cgame +0x08 = 1,
       and ready completion sets cgame +0x08 = 3 instead of 2. */
    (void)cgame;
}

void CGame_BuildSceneSetupFromPlayerData(int *cgame) {
    /* 0x8003D1F0 builds the scene/setup block directly from player/setup data when
       cgame +0x0B8 is -1.

       It is the direct path used by CGame_PrepareSceneFromSelectedSetup before
       subsystem cgame +0x3F0 is configured through FUN_80118724. The sibling
       interactive path is CGame_UpdateSceneSetupSelection. */
    (void)cgame;
}

int CGame_UpdateSceneSetupSelection(int *cgame) {
    /* 0x8003DC74 is the interactive/waiting scene setup path used when CGame cannot
       build directly from player data.

       It resolves selectable/setup state over multiple frames, returns -1 when the
       scene setup should abort back to module state 1, and returns 1 when cgame
       +0x0B8 is ready for FUN_80118724 and CGame_LoadSceneResourceManagers. */
    (void)cgame;
    return 0;
}

void CGame_LoadSceneResourceManagers(int *cgame) {
    /* 0x8003D740 wires the loaded CGame resource bundle into the scene/gameplay
       managers. Ghidra may show this as void(void) because it starts with the
       saved-register helper FUN_8012A164, but the recovered object is the CGame
       instance.

       Confirmed calls/resources:
       - fetches resources from cgame +0x3F0 through FUN_80119300
       - configures gLargeResourceManager through FUN_80025EA8 and related slots
       - loads five position/model resources into cgame +0x408 with
         CzanModelPositionSet_LoadFromLinkList
       - feeds resource lists into subsystem cgame +0x3F8
       - configures subsystem cgame +0x404 with mode/flag-dependent resources
       - when cgame +0x108 is nonzero, loads model-manager bank 1 and optionally
         sets up bank 3 plus five CtsStageObj wrappers
       - fills 0x18 large-resource table entries and finishes by wiring UI/effects

       This is an owner-side scene setup function, not a renderer. */
    (void)cgame;
}

int CGame_UpdateStateMachine(int *cgame, int currentModuleId) {
    int oldState;

    /* 0x8003C8A0 is the CGame top-level state dispatcher. It updates a flag at
       cgame +0xB0 based on DAT_802E71B8 +0x260 +0x34, then dispatches cgame[2]:
       0 -> enter state 1
       1 -> CGame_PrepareManagersAndResources
       2 -> CGame_PrepareSceneFromSelectedSetup
       3 -> CGame_PrepareActiveGameplayState
       4 -> CGame_UpdateActiveGameplayTransitionState
       5 -> return cgame[0] as next module/state

       If the state changes, it resets cgame[3] to zero. */
    if (cgame == 0) {
        return currentModuleId;
    }

    oldState = cgame[2];
    if (cgame[2] == 0) {
        cgame[2] = 1;
    }
    else if (cgame[2] == 1) {
        CGame_PrepareManagersAndResources(cgame);
    }
    else if (cgame[2] == 2) {
        CGame_PrepareSceneFromSelectedSetup(cgame);
    }
    else if (cgame[2] == 4) {
        CGame_UpdateActiveGameplayTransitionState(cgame);
    }
    else if (cgame[2] == 5) {
        currentModuleId = cgame[0];
    }

    if (oldState != cgame[2]) {
        cgame[3] = 0;
    }

    return currentModuleId;
}

void CGame_PrepareActiveGameplayState(int *cgame) {
    /* 0x800418F8 is the state-3 CGame setup/transition state machine.

       It resets a smaller subset of global managers/subsystems, then:
       - if cgame +0x0B8 == -1, calls CGame_BuildActiveGameplaySetupFromPlayerData
         and configures subsystem +0x3F0 through FUN_80118E54, entering substate 3
       - otherwise enters substate 2 and waits for
         CGame_UpdateActiveGameplaySetupSelection to return 1
       - once resource managers are ready, calls FUN_8003FBB0(cgame)
       - substate 0x0C sets cgame +0x08 = 4

       This is still setup/transition logic. FUN_8003FBB0 is the next likely owner-side
       function after setup completes. */
    (void)cgame;
}

void CGame_BuildActiveGameplaySetupFromPlayerData(int *cgame) {
    /* 0x8003F24C builds the active gameplay setup block directly from player/setup
       data when cgame +0x0B8 is -1.

       It is the direct path used by CGame_PrepareActiveGameplayState before
       subsystem cgame +0x3F0 is configured through FUN_80118E54. */
    (void)cgame;
}

int CGame_UpdateActiveGameplaySetupSelection(int *cgame) {
    /* 0x8003FEB0 is the interactive/options waiting path for active gameplay setup.

       It resolves the active gameplay setup across frames, including selection/options
       state and player/setup records. It returns -1 to abort back to scene setup and
       returns 1 once cgame +0x0B8 is ready for FUN_80118E54 and
       CGame_CreateActiveGameplayController. */
    (void)cgame;
    return 0;
}

void CGame_StartActiveGameplayControllerTransition(int *cgame) {
    /* 0x8004205C starts the active gameplay controller transition after CGame has
       created the controller at cgame +0x42C.

       Confirmed behavior:
       - derives a transition duration from cgame +0x11C through gLargeResourceManager
       - calls active controller vtable +0x20, now named
         ActiveGameplayControllerBase_StartTimedTransition, with the recovered tick/id
       - if movie slot cgame +0x414 exists and MovieSlotHandle_HasPlaybackStarted is
         false, starts playback through MovieSlotHandle_StartPlayback
       - starts a manager/audio/UI transition through CGameTransitionManager_Start
         using the table at DAT_8026E828 and cgame +0x10C
       - updates subsystem cgame +0x428 through CGameTransitionSlot_Start
       - sets cgame +0x424 = 1 */
    (void)cgame;
}

void CGame_SetupActiveGameplayTransitionResources(int *cgame) {
    /* 0x80041A9C sets up large-resource/model/movie state before
       CGame_StartActiveGameplayControllerTransition and
       CGame_UpdateActiveGameplayTransitionTiming.

       Confirmed from the attachment:
       - clears cgame +0x418/+0x41C, then sets +0x418 when +0xC0 == 5 or +0xBC == 6
       - reads player-data values through FUN_80028040/FUN_80028048
       - resets transition subsystem cgame +0x428
       - configures gLargeResourceManager from cgame +0xC4/+0xD0/+0x11C/+0x120
       - configures large-resource banks 5..6 from entries at cgame +0x258
       - configures five large-resource banks from entries at cgame +0x12C
       - stores derived bank ids in a five-entry local map
       - applies tempo/timing values from DAT_8026E828
       - wires transition subsystem cgame +0x428 with cgame +0xB8 and cgame +0x3F8
       - calls active controller vtable +0x18
       - sets per-entry state on subsystem cgame +0x400
       - enables the UI root through FUN_801007B8(gUiRootManager, 1)
       - starts manager fade/timing through FUN_800249A8(gManager_802E70A4, FLOAT_802E8308)
       - sets cgame +0x420 = 1

       The large-resource helper set is not fully mapped yet, so this function is a
       named integration point rather than a full host port. */
    (void)cgame;
}

double CGameUiSelectionPanel_GetAnimationDuration(int *panelState) {
    int uiManager;
    int objectGroupHandle;
    double duration;

    /* 0x801054D8 returns the selected panel animation duration divided by
       FLOAT_802E9214. It chooses panel[1] when panel[8] == 1, otherwise panel[0],
       child 0, animation 1. */
    if (panelState == 0) {
        return 0.0;
    }

    uiManager = CGame_GetGlobalUiManager();
    objectGroupHandle = panelState[8] == 1 ? panelState[1] : panelState[0];
    duration = CzanUiManager_GetObjectAnimationDuration(0.0, uiManager, objectGroupHandle, 0, 1);
    return duration / 60.0;
}

unsigned int CGameUiSelectionPanel_IsState5(int *panelState) {
    /* 0x801052F0 returns countLeadingZeros(state - 5) >> 5. */
    if (panelState == 0) {
        return 0;
    }
    return panelState[5] == 5 ? 1U : 0U;
}

unsigned int CGameUiSelectionPanel_IsState6(int *panelState) {
    /* 0x801054A4 returns countLeadingZeros(state - 6) >> 5. */
    if (panelState == 0) {
        return 0;
    }
    return panelState[5] == 6 ? 1U : 0U;
}

unsigned int CGameUiSelectionPanel_IsState8(int *panelState) {
    /* 0x80105304 returns countLeadingZeros(state - 8) >> 5. */
    if (panelState == 0) {
        return 0;
    }
    return panelState[5] == 8 ? 1U : 0U;
}

unsigned int CGameUiSelectionPanel_IsIdle(int *panelState) {
    unsigned int state;

    /* 0x801054B8 returns countLeadingZeros(panel +0x14) >> 5, which is 1 only
       when the panel state word is zero. */
    if (panelState == 0) {
        return 1;
    }

    state = (unsigned int)panelState[5];
    return state == 0 ? 1U : 0U;
}

int CGameUiSelectionPanel_GetFlag18(int *panelState) {
    /* 0x801054C8 returns the word at panel +0x18. */
    if (panelState == 0) {
        return 0;
    }
    return panelState[6];
}

int CGameUiSelectionPanel_GetFlag1C(int *panelState) {
    /* 0x801054D0 returns the word at panel +0x1C. */
    if (panelState == 0) {
        return 0;
    }
    return panelState[7];
}

void CGameUiSelectionPanel_SetManualAdvanceFlag(int *panelState) {
    /* 0x8010553C sets the word at panel +0x24. */
    if (panelState == 0) {
        return;
    }
    panelState[9] = 1;
}

double UiRootManager_GetSelectionPanelAnimationDuration(int *uiRootManager) {
    /* 0x80100538 forwards to the selection-panel object at uiRootManager +0x34. */
    if (uiRootManager == 0) {
        return 0.0;
    }
    return CGameUiSelectionPanel_GetAnimationDuration((int *)UiRootHostPointerFromBits(uiRootManager[0x0d]));
}

unsigned int UiRootManager_IsSelectionPanelIdle(int *uiRootManager) {
    /* 0x80100520 forwards to FUN_801054B8(*(uiRootManager +0x34)). */
    if (uiRootManager == 0) {
        return 1;
    }
    return CGameUiSelectionPanel_IsIdle((int *)UiRootHostPointerFromBits(uiRootManager[0x0d]));
}

unsigned int UiRootManager_IsBootTransitionControllerIdle(int *uiRootManager) {
    int *controller;

    /* 0x801002A8 returns countLeadingZeros((*(uiRoot +0x30)->0x2C) + 1) >> 5,
       i.e. true only when the boot/title transition controller is idle (-1). */
    if (uiRootManager == 0) {
        return 1;
    }

    controller = (int *)UiRootHostPointerFromBits(uiRootManager[0x0c]);
    if (controller == 0) {
        return 1;
    }

    return (unsigned int)(controller[0x2c / 4] == -1);
}

static int *UiRootManager_GetBootTransitionController(int *uiRootManager) {
    if (uiRootManager == 0) {
        return 0;
    }
    return (int *)UiRootHostPointerFromBits(uiRootManager[0x0c]);
}

void UiRootManager_StartBootTransitionController(
    int *uiRootManager,
    int mode,
    int arg2,
    int arg3,
    int showSecondChild) {
    /* 0x80100280 forwards to 0x801023D8(*(uiRoot +0x30), mode, arg2, arg3, showSecondChild). */
    UiRootBootTransition_Start(UiRootManager_GetBootTransitionController(uiRootManager), mode, arg2, arg3,
                               showSecondChild);
}

void UiRootManager_ConfigureBootTransitionPrompt(int *uiRootManager, int effectSlot, int baseEffectId) {
    int *controller;

    /* 0x80100290 forwards to FUN_8010280C(*(uiRoot +0x30), effectSlot, baseEffectId). */
    controller = UiRootManager_GetBootTransitionController(uiRootManager);
    if (controller == 0) {
        return;
    }

    FontManager_ResetPromptCursor();
    controller[0x1b] = baseEffectId;
    controller[0x11] = -1;
    controller[0x10] = 0;
    controller[0x44 / 4] = -1;
    ClearMemory(controller + 0x12, 0, 0x20);
    UiRootBootTransition_ConfigurePromptHelper(controller, effectSlot, baseEffectId);
}

void UiRootManager_CloseBootTransitionController(int *uiRootManager) {
    int *controller;

    /* 0x801002A0 forwards to FUN_801028BC(*(uiRoot +0x30)). */
    controller = UiRootManager_GetBootTransitionController(uiRootManager);
    if (controller == 0 || (unsigned int)controller[0x0c] >= 2U) {
        return;
    }

    /* 0x801028BC: phase 2, close cue 0x257, window anim 1 (0x80062D58). */
    controller[0x0c] = 2;
    CharacterAssetManager_PlayCue(GameMain_GetCharacterAssetManager(), 0x257);
    CGameUi_StartObjectGroupAnimation(controller[0], 1, 0, 0);
}

void UiRootManager_SetBootTransitionAdvanceLock(int *uiRootManager, int value) {
    int *controller;

    /* 0x80100288 forwards to 0x80102940: controller +0x68 = value. While set, the
       mode-6 page indicator stays hidden (0x80101020). */
    controller = UiRootManager_GetBootTransitionController(uiRootManager);
    if (controller != 0) {
        controller[0x1a] = value;
    }
}

unsigned int UiRootManager_IsBootTransitionPromptReady(int *uiRootManager) {
    int *controller;

    /* 0x801002C0 returns FUN_80102918(*(uiRoot +0x30)). */
    controller = UiRootManager_GetBootTransitionController(uiRootManager);
    if (controller == 0) {
        return 0;
    }
    return (unsigned int)(controller[0x0c] == 1 && controller[0x10] == 0);
}

int UiRootManager_GetBootTransitionResult(int *uiRootManager) {
    int *controller;

    /* 0x801002C8 returns *(uiRoot +0x30)->0x44. */
    controller = UiRootManager_GetBootTransitionController(uiRootManager);
    if (controller == 0) {
        return -1;
    }
    return controller[0x44 / 4];
}

void UiRootManager_SetBootTransitionSelectedOption(int *uiRootManager, int selectedOption) {
    int *controller;
    int previous;

    /* 0x801002D4 forwards to FUN_80102948(*(uiRoot +0x30), selectedOption). */
    controller = UiRootManager_GetBootTransitionController(uiRootManager);
    if (controller == 0) {
        return;
    }

    previous = controller[0x0f];
    if (0 <= previous && previous < 6 && controller[previous + 3] != -1) {
        CzanUiManager_StartObjectGroupAnimation(0.0, 0, controller[previous + 3], 0);
        CzanUiManager_AlignObjectGroupByReferenceEdge(0, controller[previous + 3], controller[0x1d], 0);
    }

    controller[0x0f] = selectedOption;
    FontManager_SetPromptCursor(selectedOption);
    if (0 <= selectedOption && selectedOption < 6 && controller[selectedOption + 3] != -1) {
        CzanUiManager_StartObjectGroupAnimation(0.0, 0, controller[selectedOption + 3], 2);
        CzanUiManager_AlignObjectGroupByReferenceEdge(
            0,
            controller[selectedOption + 3],
            controller[0x1d] - 5,
            0);
    }
    UiRootBootTransition_SetSelectedOptionHelperText(controller, selectedOption);
}

void UiRootManager_SelectDefaultTransition(int *uiRootManager) {
    /* 0x801004E0 forwards to CGameUiSelectionPanel_SelectDefaultTransition
       (*(uiRootManager +0x34)). */
    if (uiRootManager == 0) {
        return;
    }
    CGameUiSelectionPanel_SelectDefaultTransition((int *)UiRootHostPointerFromBits(uiRootManager[0x0d]));
}

void UiRootManager_SelectShortTransition(int *uiRootManager) {
    /* 0x801004E8 forwards to CGameUiSelectionPanel_SelectShortTransition
       (*(uiRootManager +0x34)). */
    if (uiRootManager == 0) {
        return;
    }
    CGameUiSelectionPanel_SelectShortTransition((int *)UiRootHostPointerFromBits(uiRootManager[0x0d]));
}

void UiRootManager_SelectTertiaryPrompt(int *uiRootManager, unsigned int promptIndex, int textureFrame) {
    /* 0x801004F0 forwards to FUN_801051D4(*(uiRootManager +0x34), ...). */
    if (uiRootManager == 0) {
        return;
    }
    CGameUiSelectionPanel_SelectTertiaryPrompt(
        (int *)UiRootHostPointerFromBits(uiRootManager[0x0d]),
        promptIndex,
        textureFrame);
}

void UiRootManager_SelectImmediateTransition(int *uiRootManager) {
    /* 0x801004F8 forwards to CGameUiSelectionPanel_SelectImmediateTransition
       (*(uiRootManager +0x34)). */
    if (uiRootManager == 0) {
        return;
    }
    CGameUiSelectionPanel_SelectImmediateTransition((int *)UiRootHostPointerFromBits(uiRootManager[0x0d]));
}

static void UiRootManager_StartTitleTransitionGroup(
    int groupHandle,
    int animationIndex,
    int priority,
    int forceToEnd,
    const unsigned char *color) {
    /* Shared body of 0x80100550 / 0x80100618 / 0x801006F0:
       0x80062D58(group, anim, 0, 0), optional 0x80175B00 colour blocks,
       0x8017559C priority, and when forced 0x80175F58 + 0x8017501C (jump to the
       animation's last frame). */
    if (groupHandle < 0) {
        return;
    }
    CGameUi_StartObjectGroupAnimation(groupHandle, animationIndex, 0, 0);
    if (color != 0) {
        CzanUiManager_SetObjectGroupColorBlocks(0, groupHandle, color);
    }
    /* 0x8017559C: edge alignment (draw-order key), not the +0x168 priority. */
    CzanUiManager_AlignObjectGroupByReferenceEdge(0, groupHandle, priority, 0);
    if (forceToEnd != 0) {
        CzanUiManager_SeekObjectGroupAnimationToEnd(0, groupHandle);
    }
}

void UiRootManager_StartTitleTransitionA(int *uiRootManager, int animationIndex, int priority, int forceAlpha) {
    /* 0x80100550: full-screen fade quad (uiRoot +0x0C, 'back_white') in white
       (DAT_802E91B0 = 0xFFFFFFFF). Anim 0 fades in, anim 1 fades out. */
    static const unsigned char white[4] = { 0xff, 0xff, 0xff, 0xff };

    if (uiRootManager == 0) {
        return;
    }
    UiRootManager_StartTitleTransitionGroup(uiRootManager[3], animationIndex, priority, forceAlpha, white);
}

void UiRootManager_StartTitleTransitionB(int *uiRootManager, int animationIndex, int priority, int forceAlpha) {
    /* 0x80100618: same fade quad in black (DAT_802E91B4 = 0x000000FF). */
    static const unsigned char black[4] = { 0x00, 0x00, 0x00, 0xff };

    if (uiRootManager == 0) {
        return;
    }
    UiRootManager_StartTitleTransitionGroup(uiRootManager[3], animationIndex, priority, forceAlpha, black);
}

unsigned int UiRootManager_IsTitleTransitionAIdle(int *uiRootManager) {
    /* 0x801006E0 returns CzanUiManager_IsObjectGroupAnimationDone(uiRoot +0x0C). */
    if (uiRootManager == 0 || uiRootManager[3] < 0) {
        return 1;
    }
    return (unsigned int)CzanUiManager_IsObjectGroupAnimationDone(0, uiRootManager[3]);
}

void UiRootManager_StartTitleTransitionC(int *uiRootManager, int animationIndex, int priority, int forceAlpha) {
    /* 0x801006F0: background dimmer (uiRoot +0x10, 'black01'). Anim 0 dims, anim 1
       clears. No colour block. */
    if (uiRootManager == 0) {
        return;
    }
    UiRootManager_StartTitleTransitionGroup(uiRootManager[4], animationIndex, priority, forceAlpha, 0);
}

unsigned int UiRootManager_IsTitleTransitionCIdle(int *uiRootManager) {
    /* 0x80100798 returns CzanUiManager_IsObjectGroupAnimationDone(uiRoot +0x10). */
    if (uiRootManager == 0 || uiRootManager[4] < 0) {
        return 1;
    }
    return (unsigned int)CzanUiManager_IsObjectGroupAnimationDone(0, uiRootManager[4]);
}

void UiRootSubManager44_SetValue(int *subManager, int value) {
    /* 0x8010D8C0 writes one value into the UI-root sub-manager at +0x44 only when
       subManager +0x08 == 1. */
    if (subManager == 0 || subManager[2] != 1) {
        return;
    }
    subManager[5] = -1;
    subManager[3] = value;
}

void UiRootManager_SetSubManager44Value(int *uiRootManager, int value) {
    /* 0x801007B8 forwards to FUN_8010D8C0(*(uiRootManager +0x44), value). */
    if (uiRootManager == 0) {
        return;
    }
    UiRootSubManager44_SetValue((int *)UiRootHostPointerFromBits(uiRootManager[0x11]), value);
}

int PlayerDataManager_GetSetupFieldF8(int *playerDataManager) {
    /* FUN_80028120 returns playerDataManager +0xF8. */
    if (playerDataManager == 0) {
        return 0;
    }
    return playerDataManager[0xf8 / 4];
}

int PlayerDataManager_GetSetupFieldFC(int *playerDataManager) {
    /* FUN_8002813C returns playerDataManager +0xFC. */
    if (playerDataManager == 0) {
        return -1;
    }
    return playerDataManager[0xfc / 4];
}

int PlayerDataManager_GetInactiveOrCpuPlayerCount(int *playerDataManager) {
    /* FUN_800282C4 returns playerDataManager +0x110. */
    if (playerDataManager == 0) {
        return 0;
    }
    return playerDataManager[0x110 / 4];
}

int PlayerDataManager_GetPlayerCount(int *playerDataManager) {
    /* FUN_800282CC returns playerDataManager +0x114. */
    if (playerDataManager == 0) {
        return 1;
    }
    return playerDataManager[0x114 / 4];
}

int PlayerStats_GetField170(int *playerStats) {
    /* FUN_800F3B9C returns the word at +0x170 from a player/stat record. */
    if (playerStats == 0) {
        return 0;
    }
    return playerStats[0x170 / 4];
}

int GameIndexedId_GetCategory(unsigned int indexedId) {
    /* FUN_8002754C maps sparse gameplay/resource ids into seven compact ranges. */
    if (indexedId < 0x0fU) {
        return 0;
    }
    if (indexedId - 0x14U < 0x0cU) {
        return 1;
    }
    if (indexedId - 0x28U < 0x0fU) {
        return 2;
    }
    if (indexedId - 0x3cU < 0x0fU) {
        return 3;
    }
    if (indexedId - 200U < 9U) {
        return 4;
    }
    if (indexedId - 0x50U < 10U) {
        return 5;
    }
    if (indexedId == 100U) {
        return 6;
    }
    return -1;
}

int GameIndexedId_ToLinearIndex(int indexedId) {
    static const int categoryCounts8026e87c[] = {
        0x0f, 0x0c, 0x0f, 0x0f, 9, 10, 1
    };
    static const int categoryBaseIds8026e898[] = {
        0x00, 0x14, 0x28, 0x3c, 200, 0x50, 100
    };
    int category;
    int linearIndex;
    int i;

    /* FUN_800275D8 adds the prefix sum from DAT_8026E87C to the id's offset from
       DAT_8026E898[category]. The table values correspond to the ranges recovered
       in FUN_8002754C. */
    category = GameIndexedId_GetCategory((unsigned int)indexedId);
    if (category == -1) {
        return -1;
    }

    linearIndex = 0;
    for (i = 0; i < category; i++) {
        linearIndex += categoryCounts8026e87c[i];
    }
    return linearIndex + (indexedId - categoryBaseIds8026e898[category]);
}

static void CGameTransitionSlot_CommitTiming(int *transitionSlot) {
    /* FUN_8012576C commits/arms the transition slot timing. */
    (void)transitionSlot;
}

static void GlobalCueManager_SetTransitionHalfTicks(void *cueManager, unsigned int halfTicks) {
    /* FUN_80024578(gManager_802E70A4, halfTicks). */
    (void)cueManager;
    (void)halfTicks;
}

static void GlobalCueManager_SetTransitionTicks(void *cueManager, unsigned int ticks) {
    /* FUN_8002483C(gManager_802E70A4, ticks). */
    (void)cueManager;
    (void)ticks;
}

void CGame_UpdateActiveGameplayTransitionTiming(int *cgame) {
    unsigned int transitionTicks;
    unsigned int halfTicks;
    int playerCount;
    int inactiveCount;

    /* 0x800421A0 derives transition timing from the UI-root panel animation
       duration, optionally selects one of three UI-root transition branches, then
       pushes half/full tick values into cgame +0x428, gLargeResourceManager, and
       gManager_802E70A4.

       Original formula:
       duration = FUN_80100538(gUiRootManager)
       transitionTicks = FLOAT_802E8304 * (duration / FLOAT_802E831C)

       The host cannot use gUiRootManager/gLargeResourceManager/gManager_802E70A4
       until those globals are exported, so the state decisions and manager calls are
       preserved here as a named integration point. */
    if (cgame == 0) {
        return;
    }

    transitionTicks = 0;
    if (cgame[0x2e] == -1) {
        if (cgame[0x2f] == 8 || cgame[0x25] == 0) {
            playerCount = PlayerDataManager_GetPlayerCount(0);
            inactiveCount = PlayerDataManager_GetInactiveOrCpuPlayerCount(0);
            if ((playerCount - inactiveCount) <= 1) {
                if (cgame[0x2f] == 8 || cgame[0x25] != 0) {
                    UiRootManager_SelectImmediateTransition(0);
                }
                else {
                    UiRootManager_SelectShortTransition(0);
                    transitionTicks = 2000;
                }
            }
            else {
                UiRootManager_SelectDefaultTransition(0);
            }
        }
        else {
            UiRootManager_SelectDefaultTransition(0);
        }
    }

    halfTicks = (transitionTicks >> 1) & 0x7fffU;
    CGameTransitionSlot_CommitTiming((int *)(uintptr_t)(unsigned int)cgame[0x10a]);
    LargeResourceManager_SetTransitionSoundBanks(0, (int)halfTicks);
    GlobalCueManager_SetTransitionHalfTicks(0, halfTicks);
    GlobalCueManager_SetTransitionTicks(0, transitionTicks & 0xffffU);
}

void CGame_UpdateActiveGameplayTransitionState(int *cgame) {
    /* 0x800430CC is CGame state 4, the active gameplay controller transition/update
       state reached after CGame_PrepareActiveGameplayState completes.

       The attached decompile shows this as a large state machine around the active
       gameplay controller, UI root, movie playback readiness, input abort handling,
       and transition completion. It owns the handoff out of active gameplay and is
       the main CGame-level runtime state after the controller has been created. */
    (void)cgame;
}

void CGame_UpdateAndRenderActiveGameplayRuntime(int *cgame, int renderPass) {
    /* 0x800439AC is the heavy active CGame runtime update/render pass.

       Ghidra may show void(void) because RuntimeContext_SpillSavedRegistersR14ToR31
       recovers both the CGame pointer and a pass flag. The attached decompile shows
       the live frame path after the active controller exists at cgame +0x42C.

       Important confirmed order:
       - controller vtable +0x34 runs first when cgame +0x41C is nonzero
       - applies render config based on setup flags
       - toggles gManager_802E70A4 and movie slot cgame +0x414 according to renderPass
       - calls ActiveControllerMovieBindings_SetTransitionFlagAndUpdateVisibility on
         subsystem cgame +0x404
       - updates large-resource manager transition state and controller vtable +0x38
       - calls the helper at 0x800422AC, updates position/model subsystems, and pushes
         controller matrices into cgame +0x3F8/+0x3FC/+0x404
       - controller vtable +0x3C is called before UI/effect controller updates
       - walks cgame +0x2D0 entries, computes stage/object matrices, and submits
         model draws through the cgame +0x404 model manager
       - performs multiple UI/effect/model-manager render phases, then updates
         subsystem cgame +0x428 and final movie-slot state

       This is the render/runtime handoff we were missing; porting it faithfully
       requires the model-manager, CtsStageObj, UI/effect, and movie-slot helpers to
       be real first. */
    (void)cgame;
    (void)renderPass;
}

int PlayerDataState_GetCurrentValue(int *state) {
    /* 0x80027FFC returns the first word of this small player-data/state block. */
    if (state == 0) {
        return 0;
    }
    return state[0];
}

void CGameUiRoot_AdvanceSelectionPanelState(int *uiRoot) {
    /* 0x80100510 is a tiny UI-root wrapper:
       CGameUiSelectionPanel_AdvanceState(*(uiRoot +0x34)). */
    if (uiRoot == 0) {
        return;
    }
    CGameUiSelectionPanel_AdvanceState((int *)UiRootHostPointerFromBits(uiRoot[0x0d]));
}

static void CGameUiSelectionPanel_ResetToIdle(int *panelState) {
    int i;
    int uiManager;

    if (panelState == 0) {
        return;
    }

    uiManager = CGame_GetGlobalUiManager();
    panelState[5] = 0;
    panelState[6] = 0;
    panelState[8] = 0;
    panelState[7] = 0;
    panelState[9] = 0;
    panelState[10] = -1;
    CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[0], 1);
    CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[2], 1);
    CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[1], 1);
    CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[4], 1);
    panelState[0x0d] = 0;
    panelState[0x0e] = 0;
    ((float *)panelState)[0x0c] = 1.0f;
    panelState[0x1f] = 0;
    for (i = 0x0f; i <= 0x1e; i++) {
        panelState[i] = -1;
    }
}

static int *CGameUiRoot_GetMenuPresentationSubManager(int *uiRoot) {
    int *root = uiRoot != 0 ? uiRoot : gCGameUiRootContext;

    if (root == 0) {
        return 0;
    }
    return (int *)UiRootHostPointerFromBits(root[0x0e]);
}

void CGameUiRoot_SetMenuPresentationMode(
    int *uiRoot,
    int mode,
    int highMask,
    int lowMask,
    int bankIndex) {
    /* 0x80100450 forwards to FUN_80106850(*(gUiRootManager +0x38), ...).
       The presentation-grid helper owns the two 0x28-entry texture-frame banks.
       Like the executable, this setter only updates the target mask words; the
       runtime pass consumes them later through UiRootSubManager_UpdateTextureFrameGroupMotion. */
    int *subManager;
    int *bank;
    int i;

    subManager = CGameUiRoot_GetMenuPresentationSubManager(uiRoot);
    if (subManager == 0) {
        return;
    }

    if (bankIndex == 0 && subManager[1] == 1) {
        subManager[1] = 0;
        for (i = 0; i < 0x28; i++) {
            CzanUiManager_SetObjectTextureFrame(CGame_GetGlobalUiManager(), subManager[2 + i], 0, i, 0);
        }
    }

    bank = subManager + bankIndex * (0x3e0 / 4);
    bank[0xa8 / 4] = highMask;
    bank[0xac / 4] = lowMask;
    (void)mode;
}

void CGameUiRoot_SetAlternateMenuPresentationMode(int *uiRoot, int mode, int highMask, int lowMask) {
    /* 0x80100458 forwards to FUN_801068E4. This is the alternate texture-frame
       presentation path: on first use it changes the first fourteen bank-0 groups
       to frames 0x28..0x35, then writes the normal bank-0 target masks. */
    int *subManager;
    int i;

    subManager = CGameUiRoot_GetMenuPresentationSubManager(uiRoot);
    if (subManager == 0) {
        return;
    }

    if (subManager[1] == 0) {
        subManager[1] = 1;
        for (i = 0; i < 0x0e; i++) {
            CzanUiManager_SetObjectTextureFrame(CGame_GetGlobalUiManager(), subManager[2 + i], 0, i + 0x28, 0);
        }
    }

    subManager[0xa8 / 4] = highMask;
    subManager[0xac / 4] = lowMask;
    (void)mode;
}

void CGameUiRoot_SetMenuPresentationBankValue(int *uiRoot, int referenceEdge, int bankIndex) {
    int *subManager;
    int *bank;
    int i;

    subManager = CGameUiRoot_GetMenuPresentationSubManager(uiRoot);
    if (subManager == 0) {
        return;
    }

    bank = subManager + bankIndex * (0x3e0 / 4);
    for (i = 0; i < 0x28; i++) {
        CzanUiManager_SetChildObjectReferenceEdge(CGame_GetGlobalUiManager(), bank[2 + i], 0, referenceEdge);
    }
}

void CGameUiRoot_ResetMenuPresentationBankFlags(int *uiRoot, int bankIndex) {
    int *subManager;
    int *bank;
    int i;

    subManager = CGameUiRoot_GetMenuPresentationSubManager(uiRoot);
    if (subManager == 0) {
        return;
    }

    bank = subManager + bankIndex * (0x3e0 / 4);
    for (i = 0; i < 0x28; i++) {
        CzanUiManager_SetChildObjectReferenceEdgeActive(CGame_GetGlobalUiManager(), bank[2 + i], 0, 0);
    }
}

static void CGameUiSelectionPanel_UpdateSelectionCursor(int *panelState) {
    if (InputOrMenuStateManager_TestRepeatMask(CGame_GetInputManager(), 4, 2) != 0) {
        if (panelState[0x0d] < panelState[0x0e] - 1) {
            panelState[0x0d]++;
        }
        else {
            panelState[0x0d] = 0;
        }
        GlobalCueManager_PlayCue(0.0, 0, 0x24f, 0, 0);
    }
    else if (InputOrMenuStateManager_TestRepeatMask(CGame_GetInputManager(), 4, 1) != 0) {
        if (panelState[0x0d] < 1) {
            panelState[0x0d] = panelState[0x0e] - 1;
        }
        else {
            panelState[0x0d]--;
        }
        GlobalCueManager_PlayCue(0.0, 0, 0x24f, 0, 0);
    }
}

void CGameUiSelectionPanel_Update(int *panelState, int *uiRootManager) {
    int state;
    int mode;
    int uiManager;

    /* 0x8010476C is the real per-frame selection-panel state machine used by
       UiRootManager_UpdateBeforeDraw. It advances states 1..9, waits on Czan
       group animations, applies the prompt/menu presentation mode, and resets the
       four panel object groups back to suppressed when an animation path completes. */
    if (panelState == 0) {
        return;
    }

    uiManager = CGame_GetGlobalUiManager();
    gCGameUiRootContext = uiRootManager;
    state = panelState[5];
    if ((unsigned int)state >= 10U) {
        return;
    }

    mode = state;
    switch (state) {
        case 1:
            CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
            if (CzanUiManager_IsObjectGroupAnimationDone(uiManager, panelState[2]) != 0) {
                panelState[5] = 2;
            }
            break;
        case 2:
            CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
            break;
        case 3:
            CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
            if (CzanUiManager_IsObjectGroupAnimationDone(uiManager, panelState[2]) != 0) {
                CGameUiSelectionPanel_ResetToIdle(panelState);
            }
            break;
        case 4:
            CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
            if (panelState[8] == 0) {
                if (CzanUiManager_IsObjectGroupAnimationDone(uiManager, panelState[0]) != 0) {
                    panelState[5] = 5;
                }
            }
            else if (CzanUiManager_IsObjectGroupAnimationDone(uiManager, panelState[1]) != 0) {
                panelState[5] = 5;
            }
            break;
        case 5:
            mode = panelState[0x0e];
            if (mode < 1) {
                CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
                if (panelState[9] == 1) {
                    CGameUiSelectionPanel_AdvanceState(panelState);
                }
            }
            else if (((float *)panelState)[0x0c] >= 1.0f) {
                if (panelState[9] == 1) {
                    if (panelState[0x1f] == 1) {
                        CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
                        CGameUiSelectionPanel_AdvanceState(panelState);
                    }
                    else if (mode == 1) {
                        CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, 1, 4, 0, 1);
                        if (InputOrMenuStateManager_IsConfirmPressed(CGame_GetInputManager(), 4) != 0) {
                            GlobalCueManager_PlayCue(0.0, 0, 0x249, 0, 0);
                            CGameUiSelectionPanel_AdvanceState(panelState);
                        }
                    }
                    else {
                        CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 4, 0x20, 1);
                        if (InputOrMenuStateManager_IsConfirmPressed(CGame_GetInputManager(), 4) != 0) {
                            GlobalCueManager_PlayCue(0.0, 0, 0x249, 0, 0);
                            CGameUiSelectionPanel_AdvanceState(panelState);
                        }
                        else {
                            CGameUiSelectionPanel_UpdateSelectionCursor(panelState);
                        }
                    }
                }
                else if (mode < 2 || panelState[0x1f] == 1) {
                    CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
                }
                else {
                    CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0x20, 1);
                    CGameUiSelectionPanel_UpdateSelectionCursor(panelState);
                }
            }
            else {
                CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
                ((float *)panelState)[0x0c] += 1.0f / 60.0f;
                if (((float *)panelState)[0x0c] > 1.0f) {
                    ((float *)panelState)[0x0c] = 1.0f;
                }
            }
            break;
        case 6:
            CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
            if (panelState[8] == 0) {
                if (CzanUiManager_IsObjectGroupAnimationDone(uiManager, panelState[0]) != 0) {
                    CGameUiSelectionPanel_ResetToIdle(panelState);
                }
            }
            else if (CzanUiManager_IsObjectGroupAnimationDone(uiManager, panelState[1]) != 0) {
                CGameUiSelectionPanel_ResetToIdle(panelState);
            }
            break;
        case 7:
            CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
            if (CzanUiManager_IsObjectGroupAnimationDone(uiManager, panelState[3]) != 0) {
                panelState[5] = 8;
                if (panelState[10] != -1) {
                    GlobalCueManager_PlayCue(0.0, 0, panelState[10], 0, 0);
                }
            }
            break;
        case 8:
            CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
            break;
        case 9:
            CGameUiRoot_SetMenuPresentationMode(gCGameUiRootContext, mode, 0, 0, 1);
            if (CzanUiManager_IsObjectGroupAnimationDone(uiManager, panelState[3]) != 0) {
                CGameUiSelectionPanel_ResetToIdle(panelState);
            }
            break;
        default:
            break;
    }
}

void CGameUiSelectionPanel_AdvanceState(int *panelState) {
    int state;
    int uiManager;

    /* 0x80105318 advances one of three CGame/menu UI states and starts the matching
       object-group animation. The state lives at panelState +0x14.

       5 -> 6: primary selection panel closes, cue 0x259
       2 -> 3: secondary object group closes, cue 0x25F
       8 -> 9: tertiary object group closes, cue 0x25D

       panelState +0x20 supplies the next animation index for the primary group, and
       panelState +0x08 selects whether the primary or alternate object group is used. */
    if (panelState == 0) {
        return;
    }

    uiManager = CGame_GetGlobalUiManager();
    state = panelState[5];
    if (state == 5) {
        panelState[5] = 6;
        if (panelState[8] == 0) {
            CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[0], 0);
            CzanUiManager_ResetObjectGroupAnimationTime(0.0, uiManager, panelState[0]);
            CGameUi_StartObjectGroupAnimation(panelState[0], panelState[0x20] + 1, 0, 0);
            GlobalCueManager_PlayCue(0.0, 0, 0x259, 0, 0);
        }
        else {
            CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[1], 0);
            CGameUi_StartObjectGroupAnimation(panelState[1], 1, 0, 0);
        }
        CGameUiRoot_ResetMenuPresentationGrid(0);
    }
    else if (state == 2) {
        panelState[5] = 3;
        CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[2], 0);
        CGameUi_StartObjectGroupAnimation(panelState[2], 1, 0, 0);
        GlobalCueManager_PlayCue(0.0, 0, 0x25f, 0, 0);
        CGameUiRoot_ResetMenuPresentationGrid(0);
    }
    else if (state == 8) {
        panelState[5] = 9;
        CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[3], 0);
        CGameUi_StartObjectGroupAnimation(panelState[3], 1, 0, 0);
        GlobalCueManager_PlayCue(0.0, 0, 0x25d, 0, 0);
        CGameUiRoot_ResetMenuPresentationGrid(0);
    }
}

void CGameUiSelectionPanel_SelectDefaultTransition(int *panelState) {
    int uiManager;

    /* 0x8010502C is the default branch selected by UiRootManager_SelectDefaultTransition.
       It only fires when panel state +0x14 is zero, then moves to state 4, disables
       the primary object group, rewinds its animation, starts animation panel[0x20],
       and plays cue 0x25A. */
    if (panelState == 0 || panelState[5] != 0) {
        return;
    }

    uiManager = CGame_GetGlobalUiManager();
    panelState[5] = 4;
    CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[0], 0);
    CzanUiManager_ResetObjectGroupAnimationTime(0.0, uiManager, panelState[0]);
    CGameUi_StartObjectGroupAnimation(panelState[0], panelState[0x20], 0, 0);
    GlobalCueManager_PlayCue(0.0, 0, 0x25a, 0, 0);
}

void CGameUiSelectionPanel_SelectShortTransition(int *panelState) {
    int uiManager;

    /* 0x801050BC mirrors the default branch but starts animation panel[0x20] + 2
       and plays cue 0x261. */
    if (panelState == 0 || panelState[5] != 0) {
        return;
    }

    uiManager = CGame_GetGlobalUiManager();
    panelState[5] = 4;
    CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[0], 0);
    CzanUiManager_ResetObjectGroupAnimationTime(0.0, uiManager, panelState[0]);
    CGameUi_StartObjectGroupAnimation(panelState[0], panelState[0x20] + 2, 0, 0);
    GlobalCueManager_PlayCue(0.0, 0, 0x261, 0, 0);
}

void CGameUiSelectionPanel_SelectTertiaryPrompt(int *panelState, unsigned int promptIndex, int textureFrame) {
    int uiManager;
    int parityFrame;

    /* 0x801051D4 starts the tertiary prompt branch. It switches to state 7,
       enables panel[3], starts animation 0, writes textureFrame to children 0/1,
       writes promptIndex parity to children 2/3, marks panel[7], and plays cue
       0x25E. */
    if (panelState == 0 || panelState[5] != 0) {
        return;
    }

    uiManager = CGame_GetGlobalUiManager();
    panelState[5] = 7;
    CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[3], 0);
    CGameUi_StartObjectGroupAnimation(panelState[3], 0, 0, 0);
    CzanUiManager_SetObjectTextureFrame(uiManager, panelState[3], 0, textureFrame, 0);
    CzanUiManager_SetObjectTextureFrame(uiManager, panelState[3], 1, textureFrame, 0);
    parityFrame = (int)(promptIndex & 1U);
    CzanUiManager_SetObjectTextureFrame(uiManager, panelState[3], 2, parityFrame, 0);
    CzanUiManager_SetObjectTextureFrame(uiManager, panelState[3], 3, parityFrame, 0);
    panelState[7] = 1;
    GlobalCueManager_PlayCue(0.0, 0, 0x25e, 0, 0);
}

void CGameUiSelectionPanel_SelectImmediateTransition(int *panelState) {
    int uiManager;

    /* 0x80105150 uses the alternate object group at panel[1], starts animation 0,
       sets panel[8] = 1, and plays cue 0x274. */
    if (panelState == 0 || panelState[5] != 0) {
        return;
    }

    uiManager = CGame_GetGlobalUiManager();
    panelState[5] = 4;
    CzanUiManager_SetObjectGroupEnabled(uiManager, panelState[1], 0);
    CGameUi_StartObjectGroupAnimation(panelState[1], 0, 0, 0);
    panelState[8] = 1;
    GlobalCueManager_PlayCue(0.0, 0, 0x274, 0, 0);
}

void CGameUiReferenceState_Init(
    int *state,
    int enabled,
    int objectGroupHandle,
    int childObjectIndex,
    int trackedChildIndex,
    int linkedHandle) {
    int i;
    float width;
    float height;
    int uiManager;

    /* 0x800DA01C initializes one menu/UI reference-state record.

       Layout confirmed by the decompile:
       +0x00 objectGroupHandle
       +0x04 childObjectIndex
       +0x08 trackedChildIndex
       +0x0C linkedHandle
       +0x10/+0x18/+0x1C cleared runtime fields
       +0x14 enabled flag
       +0x20 float zero/default
       +0x24/+0x28 child dimensions from CzanUiManager_GetChildObjectDimensions
       +0x30..+0x84 0x16 rgba/color words initialized to 0xFFFFFFFF */
    if (state == 0) {
        return;
    }

    state[5] = enabled;
    if (enabled == 0) {
        return;
    }

    uiManager = CGame_GetGlobalUiManager();
    state[2] = trackedChildIndex;
    state[0] = objectGroupHandle;
    state[1] = childObjectIndex;
    state[3] = linkedHandle;
    state[4] = 0;
    state[6] = 0;
    state[7] = 0;
    ((float *)state)[8] = 0.0f;

    width = 0.0f;
    height = 0.0f;
    CzanUiManager_GetChildObjectDimensions(uiManager, objectGroupHandle, childObjectIndex, &width, &height);
    if (width < 0.0f) {
        width *= -1.0f;
    }
    if (height < 0.0f) {
        height *= -1.0f;
    }
    ((float *)state)[9] = width;
    ((float *)state)[10] = height;

    for (i = 0; i < 0x16; i++) {
        state[0x0c + i] = 0xffffffff;
    }
}

void CGameUiSubManager_InitSelectionReferenceGroups(int *subManager) {
    /* 0x800CF044 initializes a menu/selection UI sub-manager built around the main
       object group stored at subManager +0x96C.

       Confirmed flow from the attached decompile:
       - builds a small offset vector through FUN_801455C0
       - runs local reset/setup helper FUN_800D67B8(subManager)
       - initializes the primary CGameUiReferenceState at subManager +0x00 with:
         enabled = (subManager +0xA90 == 0), group = +0x96C,
         child = 0x0C, trackedChild = 0x40, linkedHandle = -1
       - if enabled, stores +0x9B0 = +0x96C, +0x9B4 = 10, and creates +0x9B8
         through UiRootManager_CreateReferenceObjectGroup(gUiRootManager, +0x96C, 10, 0x1F)
       - clears six following CGameUiReferenceState records at +0x8C strides and
         clears the companion +0x9D8-style records
       - in mode +0x4E8 == 2, creates an additional reference group at +0xA7C from
         child 2 and marks +0xA80 active
       - links object groups +0x970 and +0x98C to children of +0x96C. Widescreen flag
         +0x550 bit 0x2000 uses children 0x3A/0x3E and applies position offsets;
         otherwise it uses children 0x38/0x3C.
       - enables +0x970 and +0x98C
       - writes child object +0x188 on many children of +0x96C:
         disabled set: 4, 6, 0x42, 0x0C..0x21, 0x40, 8, 10, 0x44, 0x4A, 0x4B,
                       0x38, 0x3A, 0x3C, 0x3E
         enabled set:  5, 7, 0x43, 0x22..0x37, 0x41, 9, 0x0B, 0x45, 0x4C, 0x4D,
                       0x39, 0x3B, 0x3D, 0x3F

       The host exposes this name now, but the full implementation waits on a real
       gUiRootManager pointer/export so UiRootManager_CreateReferenceObjectGroup can
       be called with the same owner object as the game. */
    (void)subManager;
}

void CGameUiRoot_ResetMenuPresentationGrid(int *uiRoot) {
    /* 0x80100460 forwards to the menu-presentation grid object at uiRoot +0x38.
       Some recovered callers only have gUiRootManager in global state; passing null
       is therefore accepted by the host stub. */
    if (uiRoot == 0) {
        uiRoot = gCGameUiRootContext;
    }
    if (uiRoot == 0) {
        return;
    }
    ActiveGameplayControllerBase_ResetMenuPresentationGrid((int *)UiRootHostPointerFromBits(uiRoot[0x0e]));
}

static int CGameUiPresentationMaskContains(const int *bank, int childIndex) {
    unsigned int bit = 1u << (childIndex & 0x1f);

    if (childIndex < 0x20) {
        return ((unsigned int)bank[0xac / 4] & bit) != 0;
    }
    return ((unsigned int)bank[0xa8 / 4] & bit) != 0;
}

static float *CGameUiPresentationEntry(int *bank, int childIndex) {
    return (float *)((unsigned char *)bank + 0xc4 + childIndex * 0x14);
}

void ActiveGameplayControllerBase_ResetMenuPresentationGrid(int *controller) {
    int bankIndex;
    int childIndex;
    int uiManager;

    /* 0x80106964 resets the two-bank, 0x28-entry menu presentation grid used by
       active/menu controller UI state.

       Confirmed decompile shape:
       - first-time sentinel at +0x04 triggers SetObjectTextureFrame(group, 0, i, 0)
         for 0x28 group handles starting at +0x08
       - clears aggregate/timer floats at +0xA8/+0xAC/+0x488/+0x48C
       - for two banks, walks 0x28 entries, uses FUN_8012A5DC(0,1,index) to test a
         two-word mask at bank +0xA8/+0xAC, enables visible groups, and seeds per-entry
         animation bounds from constants FLOAT_802E923C/FLOAT_802E9240.

       The host currently applies only the harmless, confirmed texture-frame reset and
       group-enable side effect; the packed float/int bank layout needs one more data
       sample before we mutate it aggressively. */
    if (controller == 0) {
        return;
    }

    uiManager = CGame_GetGlobalUiManager();
    if (controller[1] == 1) {
        controller[1] = 0;
        for (childIndex = 0; childIndex < 0x28; childIndex++) {
            CzanUiManager_SetObjectTextureFrame(uiManager, controller[2 + childIndex], 0, childIndex, 0);
        }
    }

    for (bankIndex = 0; bankIndex < 2; bankIndex++) {
        int *bank = controller + bankIndex * 0xf8;
        for (childIndex = 0; childIndex < 0x28; childIndex++) {
            int objectGroupHandle = bank[2 + childIndex];
            if (objectGroupHandle != -1) {
                CzanUiManager_SetObjectGroupEnabled(
                    uiManager,
                    objectGroupHandle,
                    CGameUiPresentationMaskContains(bank, childIndex) ? 0 : 1);
            }
        }
    }
}

static unsigned char *CGameUiPresentationBank(int *controller, int bankIndex) {
    /* Bank layout (relative to controller + bankIndex * 0x3E0): +0x08 group handles
       [0x28], +0xA8/+0xAC target mask (high/low word), +0xB0/+0xB4 current mask,
       +0xB8 state (-1 idle, 0 shown, 1 moving), +0xBC laid-out count, +0xC0 timer,
       +0xC4 entries [0x28] x 0x14 {x, y, targetX, targetY, active}. Controller +0 is
       the right edge the rows are aligned to (666, or 850 in 16:9). */
    return (unsigned char *)controller + bankIndex * 0x3e0;
}

#define PRESENTATION_I32(bank, offset) (*(int *)(void *)((bank) + (offset)))
#define PRESENTATION_F32(bank, offset) (*(float *)(void *)((bank) + (offset)))

static void CGameUiPresentation_LayoutBank(int *controller, int bankIndex) {
    /* 0x801064F8: every masked item becomes active and visible; the first three
       (and any in the first half) go on the row at y 434, the rest at y 451, each
       right-aligned to controller +0 with a 3-pixel gap and 100-pixel width.
       Unmasked items keep their position with target y 488 (off screen). */
    unsigned char *bank = CGameUiPresentationBank(controller, bankIndex);
    float baseX = *(float *)(void *)controller;
    int order[0x28];
    float widths[0x28];
    float rowSum[2];
    int count = 0;
    int placed = 0;
    int i;

    PRESENTATION_I32(bank, 0xbc) = 0;
    for (i = 0; i < 0x28; i++) {
        if (CGameUiPresentationMaskContains((const int *)bank, i)) {
            float *entry = CGameUiPresentationEntry((int *)bank, i);

            *(int *)(void *)(entry + 4) = 1;
            order[i] = count;
            widths[count] = 100.0f;
            count++;
            if (PRESENTATION_I32(bank, 0x08 + i * 4) != -1) {
                CzanUiManager_SetObjectGroupEnabled(CGame_GetGlobalUiManager(), PRESENTATION_I32(bank, 0x08 + i * 4), 0);
            }
        }
        else {
            order[i] = -1;
        }
    }
    PRESENTATION_I32(bank, 0xbc) = count;

    rowSum[0] = -3.0f;
    rowSum[1] = -3.0f;
    for (i = 0; i < count; i++) {
        int row = (i * 2 < count || i < 3) ? 0 : 1;
        rowSum[row] += 3.0f + widths[i];
    }
    for (i = 0; i < 0x28; i++) {
        float *entry = CGameUiPresentationEntry((int *)bank, i);

        if (order[i] != -1) {
            int row = (placed * 2 < count || placed < 3) ? 0 : 1;

            entry[2] = baseX - rowSum[row];
            entry[3] = row == 0 ? 434.0f : 451.0f;
            rowSum[row] -= 3.0f + widths[placed];
            placed++;
        }
        else {
            entry[2] = entry[0];
            entry[3] = 488.0f;
        }
    }
}

void UiRootSubManager_UpdateTextureFrameGroupMotion(int *controller) {
    /* 0x801061B4 + 0x80106724 */
    float step = 1.0f;   /* 60 / (refresh 60 / (frame skip 0 + 1)) */
    int bankIndex;
    int i;

    if (controller == 0) {
        return;
    }
    for (bankIndex = 0; bankIndex < 2; bankIndex++) {
        unsigned char *bank = CGameUiPresentationBank(controller, bankIndex);
        int state = PRESENTATION_I32(bank, 0xb8);

        if (state == -1 || state == 0) {
            if (PRESENTATION_I32(bank, 0xa8) != PRESENTATION_I32(bank, 0xb0) ||
                PRESENTATION_I32(bank, 0xac) != PRESENTATION_I32(bank, 0xb4)) {
                CGameUiPresentation_LayoutBank(controller, bankIndex);
                PRESENTATION_I32(bank, 0xb0) = PRESENTATION_I32(bank, 0xa8);
                PRESENTATION_I32(bank, 0xb4) = PRESENTATION_I32(bank, 0xac);
                PRESENTATION_I32(bank, 0xb8) = 1;
                PRESENTATION_F32(bank, 0xc0) = 0.0f;
            }
        }
        else if (state == 1) {
            PRESENTATION_F32(bank, 0xc0) += step;
            for (i = 0; i < 0x28; i++) {
                float *entry = CGameUiPresentationEntry((int *)bank, i);

                if (*(int *)(void *)(entry + 4) != 0) {
                    entry[0] = (entry[0] + entry[2]) * 0.5f;
                    entry[1] = (entry[1] + entry[3]) * 0.5f;
                }
            }
            if (PRESENTATION_F32(bank, 0xc0) >= 3.0f) {
                float baseX = *(float *)(void *)controller;

                for (i = 0; i < 0x28; i++) {
                    float *entry = CGameUiPresentationEntry((int *)bank, i);

                    if (CGameUiPresentationMaskContains((const int *)bank, i)) {
                        entry[0] = entry[2];
                        entry[1] = entry[3];
                    }
                    else {
                        *(int *)(void *)(entry + 4) = 0;
                        entry[0] = baseX - 100.0f;
                        entry[1] = 488.0f;
                        entry[2] = entry[0];
                        entry[3] = 488.0f;
                        if (PRESENTATION_I32(bank, 0x08 + i * 4) != -1) {
                            CzanUiManager_SetObjectGroupEnabled(CGame_GetGlobalUiManager(),
                                                                PRESENTATION_I32(bank, 0x08 + i * 4), 1);
                        }
                    }
                }
                PRESENTATION_F32(bank, 0xc0) = 3.0f;
                PRESENTATION_I32(bank, 0xb8) =
                    (PRESENTATION_I32(bank, 0xa8) | PRESENTATION_I32(bank, 0xac)) != 0 ? 0 : -1;
            }
        }
    }

    /* 0x80106724: place the active items. */
    for (bankIndex = 0; bankIndex < 2; bankIndex++) {
        unsigned char *bank = CGameUiPresentationBank(controller, bankIndex);

        if (PRESENTATION_I32(bank, 0xb8) == -1) {
            continue;
        }
        for (i = 0; i < 0x28; i++) {
            float *entry = CGameUiPresentationEntry((int *)bank, i);

            if (*(int *)(void *)(entry + 4) != 0 && PRESENTATION_I32(bank, 0x08 + i * 4) != -1) {
                float xyOffset[3];

                xyOffset[0] = entry[0];
                xyOffset[1] = entry[1];
                xyOffset[2] = 0.0f;
                CzanUiManager_ApplyObjectGroupPositionLayout(CGame_GetGlobalUiManager(),
                                                            PRESENTATION_I32(bank, 0x08 + i * 4), xyOffset);
            }
        }
    }
}

void UiRootSubManager_InitPresentationState(int *controller, int widescreen) {
    /* 0x80105DE4 (constructor part): idle banks, entries parked off screen, hidden;
       controller +0 = 850 in 16:9, else 666. */
    int bankIndex;
    int i;

    *(float *)(void *)controller = widescreen ? 850.0f : 666.0f;
    controller[1] = 0;
    for (bankIndex = 0; bankIndex < 2; bankIndex++) {
        unsigned char *bank = CGameUiPresentationBank(controller, bankIndex);

        PRESENTATION_I32(bank, 0xa8) = 0;
        PRESENTATION_I32(bank, 0xac) = 0;
        PRESENTATION_I32(bank, 0xb0) = 0;
        PRESENTATION_I32(bank, 0xb4) = 0;
        PRESENTATION_I32(bank, 0xb8) = -1;
        PRESENTATION_F32(bank, 0xc0) = 0.0f;
        for (i = 0; i < 0x28; i++) {
            float *entry = CGameUiPresentationEntry((int *)bank, i);

            *(int *)(void *)(entry + 4) = 0;
            entry[0] = *(float *)(void *)controller - 100.0f;
            entry[1] = 488.0f;
            entry[2] = entry[0];
            entry[3] = 488.0f;
            if (PRESENTATION_I32(bank, 0x08 + i * 4) != -1) {
                CzanUiManager_SetObjectGroupEnabled(CGame_GetGlobalUiManager(), PRESENTATION_I32(bank, 0x08 + i * 4), 1);
            }
        }
    }
}

void CGameTransitionManager_Start(double duration, double speed, int *manager, int cueOrDelay, int spawnEffect) {
    /* 0x800245A4 starts the manager-side transition used by
       CGame_StartActiveGameplayControllerTransition.

       Confirmed behavior:
       - requires manager +0x448 to be nonzero
       - looks up a transition record through FUN_800F3068(manager +0x44C, +0x450)
       - enters a critical section and initializes timer/state fields at
         +0x45C..+0x474
       - converts duration/speed through FUN_8012A03C
       - when spawnEffect is nonzero, starts a manager effect through FUN_8016B270
         and stores the returned handle at +0x454 plus the duration at +0x458 */
    (void)duration;
    (void)speed;
    (void)manager;
    (void)cueOrDelay;
    (void)spawnEffect;
}

void CGameTransitionSlot_Start(int *transitionSlot) {
    /* 0x80125728 starts/arms the transition subsystem stored at cgame +0x428.

       If transitionSlot +0x80 exists, it calls FUN_800F8DB0 on that object and marks
       transitionSlot +0x84 = 1. This is the final small kick in
       CGame_StartActiveGameplayControllerTransition. */
    (void)transitionSlot;
}

void CGame_CreateActiveGameplayController(int *cgame) {
    /* 0x8003FBB0 wires active gameplay resources after state 3 setup is ready.

       Confirmed behavior:
       - configures subsystem cgame +0x400 from resources in cgame +0x3F0
       - iterates cgame +0x2D0 entries, each 0x38 bytes at cgame +0x2D4
       - builds per-entry controller/resource lists from resource flags and ids
       - commits those entries into subsystem cgame +0x400 through FUN_800612A4,
         FUN_80061430, and FUN_800611B4
       - passes a handle from cgame +0x400 into subsystem cgame +0x404
       - creates/stores the active gameplay controller at cgame +0x42C through
         thunk_FUN_80115E10(cgame +0xFC)
       - builds a context struct containing cgame +0x408, +0x3F8, +0x3FC, +0x404,
         +0x400, +0x410, +0x40C, +0x3F4, and cgame +0xB8
       - calls controller vtable +0x0C with that context

       This function does not draw directly. The next likely render/update target is
       the active gameplay controller object stored at cgame +0x42C. */
    (void)cgame;
}

int ActiveGameplayControllerBase_Init(int *controller) {
    /* 0x801120D0 initializes the common 0x2BF0-byte active gameplay controller.
       Ghidra may show void(void) because the function uses FUN_8012A130/FUN_8012A17C
       to recover/return the object pointer.

       Confirmed fields:
       controller +0x2BEC -> vtable PTR_PTR_802BEF00
       controller +0x0068 -> subobject initialized by FUN_8012927C
       controller +0x1428..+0x2A80 -> repeated 0x478-byte entry/controllers initialized
                                      through FUN_80110D30
       controller +0x1300..+0x13FC -> eight repeated 0x20-byte default vectors copied
                                      from DAT_8027D118..DAT_8027D134
       controller +0x0080..+0x0110, then repeated at +0x4A0 strides -> 0x94-byte
                                      default records copied from DAT_8027D07C table
       controller +0x2800..+0x2824 -> transient ids/state cleared or set to -1
       controller +0x2A80..+0x2A9C -> timing/default floats and ids
       controller +0x2AA0..+0x2ACB -> cleared trailing state
       controller +0x2ACC -> initialized to 1

       This constructor is mostly ownership/default-state setup. It does not load the
       menu background, model files, THP movies, or ZMB/ZAB data directly. Those enter
       later through SetupContext and the vtable update/render methods.
    */
    if (controller == 0) {
        return 0;
    }
    return (int)(uintptr_t)controller;
}

int ActiveGameplayControllerBase_Destroy(int *controller, short releaseMode) {
    /* 0x801125E8 destroys the base active gameplay controller.

       Confirmed behavior:
       - if controller is non-null, destroys the embedded subobject at controller
         +0x68 through ActiveGameplayControllerSubobject_Destroy(..., -1)
       - if releaseMode > 0, frees the controller from MemoryPool 0
       - returns the original controller pointer */
    if (controller == 0) {
        return 0;
    }
    (void)releaseMode;
    return (int)(uintptr_t)controller;
}

int ActiveGameplayControllerSpecial_Init(int *controller) {
    /* 0x80116360 initializes the larger 0x2C08-byte special active gameplay
       controller used by selected character/setup IDs. */
    if (controller == 0) {
        return 0;
    }
    return (int)(uintptr_t)controller;
}

void ActiveGameplayControllerBase_SetupContext(int *controller, int *context) {
    uintptr_t subsystem410;

    /* 0x80112670 is PTR_PTR_802BEF00 vtable +0x0C.
       Ghidra may show void(void) because FUN_8012A140 recovers both arguments.

       This is the setup method called by CGame_CreateActiveGameplayController after
       the active gameplay controller is created at cgame +0x42C. It copies the
       CGame-owned resource/subsystem context into the controller, clears transient
       setup state, initializes the repeated per-player/visual slots, and applies
       setup-block flags from context[8].

       Confirmed side effects from the original:
       - calls FUN_801160A8(controller, context[8]) before copying fields
       - clears controller +0x0C size 0x40
       - calls FUN_800FCD10()
       - clears controller +0x2AA0 size 0x2C
       - calls FUN_801292EC(controller +0x68)
       - initializes eight repeated visual/color/state slots from DAT_802E9410 and
         randomized entries at controller +0x1440
       - mirrors selected default records into the repeated controller slots
       - derives controller[0x1D] and [0x1E] from context[5] +0x74/+0x80
       - uses setup/player data at context[8] to decide [0x1F], [0x507], [0x508],
         and [0xAB3]
       - calls FUN_800626B8(controller[0x18], setupBlock +0x70 < 3)
       - clears controller +0x2AD4 and +0x2B60, each size 0x8C

       It is still not the draw function. */
    if (controller == 0 || context == 0) {
        return;
    }

    controller[0x14] = context[0];
    controller[0x00] = context[1];
    controller[0x15] = context[2];
    controller[0x16] = context[3];
    controller[0x17] = context[4];
    controller[0x18] = context[5];
    controller[0x01] = context[6];
    controller[0x02] = context[7];
    controller[0x13] = context[8];
    controller[0x19] = context[9];

    subsystem410 = (uintptr_t)(unsigned int)controller[0x18];
    controller[0x1D] = subsystem410 != 0 ? *(int *)(subsystem410 + 0x74) : 0;
    controller[0x1E] = subsystem410 != 0 ? *(int *)(subsystem410 + 0x80) : 0;
    controller[0x500] = 0;
    controller[0x501] = -1;
    controller[0x502] = -1;
    controller[0x503] = -1;
    controller[0x504] = -1;
    controller[0x507] = -1;
    controller[0x508] = -1;
}

int ActiveGameplayControllerBase_GetEmbeddedSubobject(int *controller) {
    /* 0x80112D94 is PTR_PTR_802BEF00 vtable +0x10.

       It returns the embedded controller subobject initialized at controller +0x68 by
       ActiveGameplayControllerBase_Init and reset by ActiveGameplayControllerBase_SetupContext.
       This is an accessor, not a teardown/reset method. */
    if (controller == 0) {
        return 0;
    }
    return (int)(uintptr_t)(controller + 0x1a);
}

int ActiveGameplayControllerEventData_SampleCurrentEvent(int *eventData, int *outEvent, unsigned int category) {
    /* 0x8011AB4C samples the current event record for one runtime category from
       the controller event data object used at controller +0x64.

       Confirmed behavior:
       - uses eventData[3] as the current event/frame index
       - dispatches category 0..10 through the parser table at PTR_LAB_802918B8
       - succeeds only when the parser fills a nonnegative event id
       - writes a 9-word result:
         [0] range value from ActiveGameplayControllerEventData_ResolveRangeValue
         [1] secondary range value from
             ActiveGameplayControllerEventData_ResolveSecondaryRangeValue
         [2] parsed event id
         [3..7] parsed event payload/timing floats/words
         [8] whether the current index matches one of the range boundary markers

       This is the direct/current sample used by the active controller runtime
       channel code. */
    (void)eventData;
    (void)outEvent;
    (void)category;
    return 0;
}

int ActiveGameplayControllerEventData_FindNearbyEvent(
    int *eventData,
    int *outEvent,
    unsigned int category,
    int direction,
    int skipCurrent) {
    /* 0x8011AD00 searches forward or backward from the current event index until
       the parser for category finds a valid event.

       Confirmed behavior:
       - direction < 0 searches backward, otherwise forward
       - skipCurrent starts from currentIndex +/- step
       - decrements skipCurrent while valid events are found, allowing callers to
         request the next/previous matching event
       - emits the same 9-word result layout as
         ActiveGameplayControllerEventData_SampleCurrentEvent */
    (void)eventData;
    (void)outEvent;
    (void)category;
    (void)direction;
    (void)skipCurrent;
    return 0;
}

int ActiveGameplayControllerEventData_ResolveRangeValue(int *eventData, int frameOrIndex) {
    /* 0x8011AF30 resolves a value from the event data's 0x11 range table.

       When frameOrIndex is -1, it uses eventData[3] as the frame/index. It then
       scans paired ushort ranges at +0x10/+0x12 and +0x18/+0x1A, returning the
       corresponding word from +0x14/+0x1C. If no range contains the frame/index,
       it returns zero. */
    (void)eventData;
    (void)frameOrIndex;
    return 0;
}

int ActiveGameplayControllerEventData_ResolveSecondaryRangeValue(int *eventData, int frameOrIndex) {
    /* 0x8011B030 resolves the secondary value from the same 0x11 paired range table
       used by ActiveGameplayControllerEventData_ResolveRangeValue.

       When frameOrIndex is -1, it uses eventData[3]. It scans paired ushort ranges
       at +0x10/+0x12 and +0x18/+0x1A, computes the matching range slot index, then
       returns the corresponding word from eventData +0x120 + slotIndex*4. If no
       range matches, the original falls back to slot index zero. */
    (void)eventData;
    (void)frameOrIndex;
    return 0;
}

int ActiveGameplayControllerSubobject_ResolveModeTransitionSlot(
    int *subobject,
    int forceImmediate,
    int targetMode,
    int transitionKind,
    int tableIndex) {
    /* 0x80129308 resolves the next presentation/mode transition slot from the
       embedded controller subobject at controller +0x68.

       Confirmed behavior:
       - clamps transitionKind to 3
       - returns 0 when targetMode is zero
       - returns -1 when the table entry at subobject[0] + transitionKind*0x14 +
         tableIndex*4 is zero and forceImmediate is zero
       - otherwise advances subobject[1] through a 1,2 ring and returns it

       This helper chooses a transition slot/index; it does not load ZMB/ZAB data. */
    (void)subobject;
    (void)forceImmediate;
    (void)targetMode;
    (void)transitionKind;
    (void)tableIndex;
    return 0;
}

void ActiveGameplayControllerBase_ResetEmbeddedSubobject(int *controller) {
    /* 0x80113C24 is PTR_PTR_802BEF00 vtable +0x14.

       It resets/destroys the embedded controller +0x68 subobject, then switches the
       movie/background binding object at controller +0x5C to mode 1. */
    (void)controller;
}

int ActiveGameplayControllerBase_AreMovieBindingsReady(int *controller) {
    /* 0x80113C60 checks the active controller movie/background binding readiness.

       Original behavior:
       return ActiveControllerMovieBindings_HasPendingSlots(*(controller +0x5C), 1) == 0

       The helper returns nonzero while a slot is still active/pending or not far
       enough through its movie/audio streams, so this wrapper returns true when the
       bindings are ready. */
    (void)controller;
    return 1;
}

int ActiveGameplayControllerBase_AreMode3MovieBindingsReady(int *controller) {
    /* 0x801140E8 checks mode-3 movie/background binding readiness.

       Original behavior:
       return ActiveControllerMovieBindings_HasPendingSlots(*(controller +0x5C), 3) == 0 */
    (void)controller;
    return 1;
}

void ActiveGameplayControllerBase_ResetRuntimeState(int *controller) {
    /* 0x80113CA4 is PTR_PTR_802BEF00 vtable +0x18.

       This refreshes active gameplay/controller runtime state after setup:
       - clears controller +0x0C size 0x40
       - resets/rewires the subsystem at controller +0x04 using controller +0x4C
       - resets stage/resource subsystem controller +0x60
       - when controller +0x64 exists, samples timing/input/resource data and applies
         it through controller +0x60 stage model slots
       - calls ActiveGameplayControllerBase_UpdateTimelineMarker(controller)
       - sets controller +0x3C from setup block flags at controller +0x4C +0x50 bit 0x200
       - resets the vector at controller +0x40
       - clears all model slots counted by *(controller +0x4C +0x218)
       - may select/apply a model slot through controller +0x54
       - refreshes subsystem controller +0x08
       - switches controller +0x5C movie/background bindings to mode 2

       This is lifecycle/resource-state work. It is closer to runtime boot wiring than
       rendering, but still not the main draw method. */
    (void)controller;
}

void ActiveGameplayControllerBase_UpdateTimelineMarker(int *controller) {
    /* 0x80114A90 updates the active controller timeline/frame marker after runtime
       state has been refreshed.

       Confirmed behavior:
       - when setup flags permit, asks the controller data at +0x64 for category-5
         marker data through ActiveGameplayControllerEventData_SampleCurrentEvent /
         ActiveGameplayControllerEventData_FindNearbyEvent
       - stores the selected marker id at controller +0x2AB4
       - falls back to the current base timeline value at *(controller[0] +0x0C)
       - in forced/disabled cases uses a large sentinel value 100000 and may force
         the stage slot object at controller +0x54 into state 6 through
         CtsStageObjSlot_SetState
       - writes baseTime + markerId * 1000 either to *(controller[0] +0x10) when
         an alternate timeline is active, or to *(controller[0] +0x0C) otherwise
       - refreshes *(controller[0] +0x08) from the controller data helper when
         controller +0x64 is present

       This is timing/state selection, not rendering. */
    (void)controller;
}

void ActiveGameplayControllerBase_ApplyRuntimeEventChannels(int *controller, int frameContext) {
    /* 0x8011399C consumes event/channel records from the controller data pointer at
       controller +0x64 and mirrors them into active runtime state.

       Confirmed channel categories:
       - 7: applies ActiveGameplayControllerBase_ApplyRuntimeVisibilityMask and
            ActiveGameplayControllerBase_TriggerRuntimeCue, then refreshes +0x2ABC
       - 6: applies ActiveGameplayControllerBase_ApplyRuntimeVisualState and
            refreshes controller +0x2AB8
       - 2: updates controller +0x74 and the subsystem at controller +0x60
       - 3: updates controller +0x78 and the subsystem at controller +0x60
       - 8: updates stage slot flag controller +0x7C and, unless preset mode 4 is
            active, mirrors it into *(controller +0x54 +0x5C)
       - 9/10: applies ActiveGameplayControllerBase_ApplyModeTransitionEvent

       If the currently selected event group changes and controller +0x5C exists,
       the original also calls CzanModelManager_StopBank5ModeEffects(0.0f, ...). */
    (void)controller;
    (void)frameContext;
}

void ActiveGameplayControllerBase_ApplyRuntimeVisualState(int *controller, int laneIndex, unsigned int enabledMask) {
    /* 0x80113098 applies category-6 visual/runtime state to one of the controller's
       eight 0x94-byte source records and the eight 0x478-byte presentation records.

       Confirmed behavior:
       - ignores laneIndex >= 8
       - requires controller +0x1418 to have the lane bit set
       - skips when source record +0xCC is nonzero and controller +0x50 bit 2 is set
       - converts enabledMask to a boolean and uses source +0x80 as a blend duration
       - blends/copies two global controller values at +0x1880/+0x1890 and their
         transition timers at +0x1884..+0x189C
       - loops eight 0x478-byte presentation records at controller +0x1428, updating
         value/color transition records from the selected source record
       - when a source mask bit is clear, it chooses a randomized palette/color from
         controller +0x1440 and source-local color tables
       - when a source mask bit is set, it uses explicit source colors/timing fields
       - if source +0xCC differs from controller +0x2A80, calls one of the stage-slot
         helpers at controller +0x54, then stores the new +0x2A80 state

       This is the missing category-6 runtime visual/presentation updater, not a
       geometry loader. */
    (void)controller;
    (void)laneIndex;
    (void)enabledMask;
}

void ActiveGameplayControllerBase_ApplyRuntimeVisibilityMask(int *controller, int laneIndex, unsigned int visibilityMask) {
    /* 0x80112D9C applies a category-7 runtime visibility/enable mask to one of the
       controller's eight 0x20-byte lane tables at controller +0x1300.

       Confirmed behavior:
       - ignores laneIndex >= 8 and controller states where controller +0x50 low bits
         are nonzero
       - walks signed ids in the selected lane until sentinel 0x104
       - converts negative ids back into the same 0..0x17 range
       - enables the id when visibilityMask is nonzero and the normalized id is in
         0..0x17, otherwise disables it
       - negative ids route through FUN_80052ED8; nonnegative ids route through
         FUN_80052E1C unless the model/setup flag path says the id should be kept
       - records the processed lane in a four-entry ring at controller +0x1404,
         indexed by controller +0x1400

       The helper affects controller-managed presentation state, not geometry decode. */
    (void)controller;
    (void)laneIndex;
    (void)visibilityMask;
}

void ActiveGameplayControllerBase_TriggerRuntimeCue(int *controller, int cueId) {
    /* 0x80112F4C handles category-7 cue ids in the 100..199 range.

       Confirmed behavior:
       - maps cueId to a manager cue id with cueId +0x7FF9D
       - for cue ids 100..105, compares controller buffers +0x2AD4 and +0x2B60
         as five 0x1C-byte records and copies +0x2AD4 into +0x2B60
       - suppresses the cue unless the compared records changed and setup flags at
         controller +0x4C allow it
       - dispatches valid cues through FUN_800246E8(gManager_802E70A4, mappedCueId)

       This looks like a runtime cue/sound/manager trigger driven by controller event
       data rather than a model or movie loader. */
    (void)controller;
    (void)cueId;
}

void ActiveGameplayControllerBase_ApplyModeTransitionEvent(
    double transitionSeconds,
    int *controller,
    int targetMode,
    int transitionKind,
    int forceImmediate,
    int eventArg0,
    int eventArg1) {
    /* 0x80113838 applies the category-9/10 runtime event payload that can switch the
       active bank-5 mode.

       Confirmed behavior:
       - asks the embedded controller subobject at controller +0x68 to resolve a
         target mode through ActiveGameplayControllerSubobject_ResolveModeTransitionSlot
       - ignores modes not enabled by controller +0x141C
       - ignores the transition when setup data at controller +0x4C +0x50 has bit
         0x1000 set
       - writes controller +0x70 to the resolved mode
       - unless preset mode 4 is active, compares against the current movie/model
         binding mode from controller +0x5C and calls
         CzanModelManager_RequestBank5ModeTransition
       - clamps controller +0x1424 to 0..3
       - when a transition is already active and transitionSeconds > 0, stores a
         blend reference from the previous 0x478-byte mode record into the new one

       This is one of the important menu/runtime transition functions. */
    (void)transitionSeconds;
    (void)controller;
    (void)targetMode;
    (void)transitionKind;
    (void)forceImmediate;
    (void)eventArg0;
    (void)eventArg1;
}

void ActiveGameplayControllerBase_StopStageModelSlot(int *controller) {
    /* 0x801140A8 is PTR_PTR_802BEF00 vtable +0x1C.

       It resets/fades the stage model slot object at controller +0x54 to zero time,
       then switches controller +0x5C movie/background bindings to mode 3. */
    (void)controller;
}

int ActiveGameplayControllerBase_IsStageModelSlotBusy(int *controller) {
    /* 0x80114044 returns whether the controller is currently in a transition or its
       stage model slot object at controller +0x54 is still busy.

       Original behavior:
       return controller +0x0C != 0 || CtsStageObjSlot_IsBusy(*(controller +0x54)) != 0 */
    (void)controller;
    return 0;
}

void ActiveGameplayControllerBase_StartTimedTransition(int *controller, unsigned int transitionTicks) {
    /* 0x8011412C is PTR_PTR_802BEF00 vtable +0x20.

       It converts transitionTicks through gLargeResourceManager timing, marks
       controller +0x0C active, stores the resulting duration at +0x28, updates the
       controller +0x5C handle with mode 4, resets subsystem +0x60, clears the two
       0x8C-byte runtime buffers, and optionally kicks UI/fade state through
       controller +0x08. */
    (void)controller;
    (void)transitionTicks;
}

void ActiveGameplayControllerBase_ResetTransitionMovieBindings(int *controller) {
    /* 0x80114268 is PTR_PTR_802BEF00 vtable +0x24.

       It clears controller +0x0C size 0x40, then resets the active controller movie
       bindings object stored at controller +0x5C through FUN_80055268. */
    (void)controller;
}

void ActiveGameplayControllerBase_TickRuntime(int *controller, int transitionPass) {
    /* 0x801142C4 is the active gameplay controller runtime tick after setup.

       Ghidra may show void(void) because FUN_8012A150 recovers arguments. The second
       recovered value acts like a pass/transition flag; the main update work runs for
       pass zero while controller +0x0C is active and the transition duration at +0x28
       has elapsed.

       Confirmed behavior:
       - copies controller +0x2AA0 size 0x2C as previous event state
       - advances controller time through gLargeResourceManager and writes +0x10/+0x14
       - advances controller event data at +0x64
       - samples category 4 events to select/apply CtsStageObj model slots at
         controller +0x54
       - samples categories 0 and 1 to update subsystem controller +0x60 through
         FUN_8006153C / FUN_80061848
       - applies runtime event channels when setup flags allow
       - finishes by calling ActiveGameplayControllerBase_UpdateTimelineMarker */
    (void)controller;
    (void)transitionPass;
}

void ActiveGameplayControllerBase_LateUpdateTransforms(int *controller, int skipRuntimeAdvance) {
    /* 0x80114D18 is a later active controller update pass that pushes matrices and
       advances presentation/preset state.

       Confirmed behavior:
       - copies the current model-owner matrix from controller +0x54
       - asks the controller +0x54 UI/model subobject for another matrix through a
         vtable +0x18 call
       - pushes those matrices to the bank-5/current-mode model owner at controller
         +0x5C through CzanModelManager_UpdateCurrentModeMatrices
       - when skipRuntimeAdvance is zero and setup flags allow, advances five
         0x478-byte presentation records at controller +0x1428
       - updates the currently selected 0x478-byte mode record through FUN_80114FA4
       - when controller +0x1424 == 4, advances the visual preset table at +0x2A94
         and mirrors selected ids into subsystem controller +0x60
       - calls FUN_80114BF0 and may restart UI/effect fades when large-resource state
         and controller +0x08 conditions match */
    (void)controller;
    (void)skipRuntimeAdvance;
}

void ActiveGameplayControllerBase_ApplyVisualPreset(int *controller, int presetA, int presetB, int presetGroup) {
    /* 0x80115B80 applies one controller visual/stage preset selected by two preset
       indices and a group.

       It stores a selected pointer-table entry at controller +0x2A94, resets the
       preset transition state at +0x2A98/+0x2A9C, writes default vector/color records
       from DAT_802BEA80 into controller +0x2A70/+0x2A60/+0x267C ranges, optionally
       writes a selected id into subsystem controller +0x60 at +0x74/+0x80, clears the
       stage model slot flag at *(controller +0x54 +0x5C), sets controller +0x1424 = 4,
       and optionally resets/starts the UI/effect controller at controller +0x08. */
    (void)controller;
    (void)presetA;
    (void)presetB;
    (void)presetGroup;
}

void ActiveGameplayControllerBase_CommitVisualPreset(int *controller) {
    /* 0x80115CE8 commits the controller visual/stage preset currently stored in
       controller +0x70/+0x74/+0x78/+0x7C.

       It copies the chosen ids into subsystem controller +0x60, clamps controller
       +0x1424 from +0x70, copies +0x7C into the stage model slot at controller +0x54,
       requests a bank-5 mode transition through controller +0x5C, then optionally
       resets/starts the UI/effect controller at controller +0x08. */
    (void)controller;
}

int ActiveGameplayController_Create(unsigned int characterOrSetupId) {
    /* 0x80115E10 creates the active gameplay controller stored at cgame +0x42C.
       It chooses between:
       - 0x2BF0-byte base controller initialized by FUN_801120D0
       - 0x2C08-byte special controller initialized by FUN_80116360

       After construction it calls FUN_801157CC(controller, configTable, 0).
       Known special branch: IDs 200, 0xCC, 0xCD use the larger controller and
       config table DAT_80290B50. IDs 0xC9 and 0xCB use the base controller with
       DAT_80290B80. */
    (void)characterOrSetupId;
    return 0;
}

int ActiveGameplayControllerSubobject_Destroy(int *subobject, short releaseMode) {
    /* 0x80129280 destroys the small embedded subobject used at controller +0x68.

       The function only frees the subobject when releaseMode > 0, which is why
       ActiveGameplayControllerBase_Destroy calls it with -1 for the embedded case. */
    if (subobject == 0) {
        return 0;
    }
    (void)releaseMode;
    return (int)(uintptr_t)subobject;
}
