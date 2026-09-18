#include "runtime/module_system.h"
#include "game/cgame.h"
#include "runtime/boot_logo.h"
#include "model/czan_model.h"
#include "platform/render_backend.h"
#include "render/render_engine.h"
#include "resource/czan_link.h"
#include "resource/resource_manager.h"
#include "runtime/math.h"
#include "runtime/memory.h"
#include "runtime/string_util.h"
#include "select/csel_mode.h"
#include "ui/czan_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int *gGlobalRuntimeContext;
static int gResourceManager260SharedInitialized;
static int gResourceManager260LastId = -1;
static int gResourceManager260VTable;
static int gResourceManager260ModeTableDefault;
static int gResourceManager260ModeTableTwo;
static int gEffectSceneManager268VTable;
static int gEffectSceneManager268GlobalParam;
static int gSoundArchiveReloadGuard;
static int gRuntimeMemoryCriticalFlag;
static int gRuntimeMemoryCriticalValue;
static void *gModuleSystemHostPointers[256];
static int gCSelectHostBootPhase;
static int gCSelectHostBootPhaseTicks;

#define GLOBAL_TEXTURE_MANAGER_SLOTS 512
#define CSELECT_MODULE_SIZE 0x1c18

static TextureSlotKnownFields gGlobalTextureSlots[GLOBAL_TEXTURE_MANAGER_SLOTS];
static TextureManagerKnownFields gGlobalTextureManager = {
    gGlobalTextureSlots,
    0,
    GLOBAL_TEXTURE_MANAGER_SLOTS
};

typedef struct CSelectBootTitleFlow {
    unsigned char storage[0x138];
} CSelectBootTitleFlow;

typedef struct CSelectTitleFlow {
    unsigned char storage[0x264];
} CSelectTitleFlow;

typedef struct CSelectPlayerCountFlow {
    unsigned char storage[0x360];
} CSelectPlayerCountFlow;

typedef struct CSelectTitleModelFocus {
    unsigned char storage[0x1b8];
} CSelectTitleModelFocus;

typedef enum CSelectHostBootPhase {
    CSELECT_HOST_BOOT_PHASE_NONE = 0,
    CSELECT_HOST_BOOT_PHASE_WAIT_SELECT_BIN = 1,
    CSELECT_HOST_BOOT_PHASE_BOOT_FLOW = 2,
    CSELECT_HOST_BOOT_PHASE_WAIT_SEL_TITLE = 3,
    CSELECT_HOST_BOOT_PHASE_TITLE_READY = 4
} CSelectHostBootPhase;

typedef struct RuntimeLowLevelMemoryPoolState {
    int initialized;
    int interruptsOrDebugFlags;
    void *arenaLow;
    void *arenaHigh;
    void *poolBase[2];
    int globalLockInitialized;
} RuntimeLowLevelMemoryPoolState;

typedef struct RuntimeLowLevelCoreState {
    int initialized;
    unsigned long long bootTime;
    int bootInfo;
    int consoleType;
    int arenaLo;
    int arenaHi;
    unsigned int h4aRegister;
    int firmwareChecked;
    int sdkInitialized;
    int interruptSystemInitialized;
    int dvdSystemInitialized;
    int exceptionHandlersInitialized;
} RuntimeLowLevelCoreState;

typedef struct RuntimeLowLevelVideoState {
    int initialized;
    int viMode;
    int scanMode;
    int displayWidth;
    int framebufferWidth;
    int framebufferHeight;
    int viewportX;
    int viewportY;
    int viewportWidth;
    int viewportHeight;
    int retraceA;
    int retraceB;
    int retraceC;
    int progressiveFlag;
} RuntimeLowLevelVideoState;

static RuntimeLowLevelCoreState gRuntimeLowLevelCore;
static RuntimeLowLevelMemoryPoolState gRuntimeLowLevelMemoryPool;
static RuntimeLowLevelVideoState gRuntimeLowLevelVideo;
static CSelectTitleModelFocus gCSelectTitleModelFocus;

typedef struct MainLoopManagerKnownFields {
    int frameCounter;
    int resetFrameCounter;
    int shutdownFrameCounter;
    ModuleControllerKnownFields *moduleController;
} MainLoopManagerKnownFields;

typedef struct GameMainManagerChain {
    int *mainLoopManager;
    int *playerDataManager;
    int *manager802e70e0;
    int *manager802e70a4;
    int *manager802e70a8;
    int *inputOrMenuStateManager;
    int *manager802e70b0;
    int *manager802e70b4;
    int *uiRootManager;
    int *characterAssetManager;
    int *manager802e70b8;
    int *largeResourceManager;
    int *bootTempManager;
} GameMainManagerChain;

static GameMainManagerChain gGameMainManagers;
static MainLoopManagerKnownFields *gActiveMainLoopManager;

static int RuntimeFloatBits(float value) {
    union {
        float f;
        int i;
    } bits;

    bits.f = value;
    return bits.i;
}

static int RuntimePointerBits(void *pointer) {
    int i;

    if (pointer == 0) {
        return 0;
    }

    for (i = 1; i < (int)(sizeof(gModuleSystemHostPointers) / sizeof(gModuleSystemHostPointers[0])); i++) {
        if (gModuleSystemHostPointers[i] == pointer) {
            return i;
        }
    }

    for (i = 1; i < (int)(sizeof(gModuleSystemHostPointers) / sizeof(gModuleSystemHostPointers[0])); i++) {
        if (gModuleSystemHostPointers[i] == 0) {
            gModuleSystemHostPointers[i] = pointer;
            return i;
        }
    }

    return (int)(uintptr_t)pointer;
}

static void *RuntimePointerFromBits(int bits) {
    if (0 < bits && bits < (int)(sizeof(gModuleSystemHostPointers) / sizeof(gModuleSystemHostPointers[0])) &&
        gModuleSystemHostPointers[bits] != 0) {
        return gModuleSystemHostPointers[bits];
    }
    return (void *)(uintptr_t)bits;
}

int RuntimeHostPointerBits(void *pointer) {
    return RuntimePointerBits(pointer);
}

void *RuntimeHostPointerFromBits(int bits) {
    return RuntimePointerFromBits(bits);
}

int *GlobalRuntimeContext_GetPointerAt(int byteOffset) {
    if (gGlobalRuntimeContext == 0) {
        return 0;
    }
    return (int *)RuntimePointerFromBits(gGlobalRuntimeContext[byteOffset / 4]);
}

static void RuntimeLowLevel_SetH4AFlag(void) {
    /* 0x801A2130 reads a low-level OS register and sets bit 0x200. */
    gRuntimeLowLevelCore.h4aRegister |= 0x200U;
}

static void RuntimeLowLevel_ReportKernelInfo(void) {
    /* 0x801A29D0 prints the Revolution OS banner, console type, firmware, memory,
       and arena ranges during OS startup. */
    RuntimeDebugReport("Revolution OS\n");
    RuntimeDebugReport("Kernel built : %s %s\n", "Aug 23 2010", "17:33:06");
    RuntimeDebugReport("Console Type : ");
    if ((gRuntimeLowLevelCore.consoleType & 0xf0000000) == 0x10000000) {
        RuntimeDebugReport("Emulation platform (%08x)\n", gRuntimeLowLevelCore.consoleType);
    }
    else if ((gRuntimeLowLevelCore.consoleType & 0xf0000000) == 0x20000000) {
        RuntimeDebugReport("TDEV-based emulation HW%d\n",
                           (gRuntimeLowLevelCore.consoleType & 0x0fffffff) - 3);
    }
    else {
        RuntimeDebugReport("Retail %d\n", gRuntimeLowLevelCore.consoleType);
    }
    RuntimeDebugReport("Firmware : %d.%d.%d", 0, 0, 0);
    RuntimeDebugReport(" (%d/%d/%d)\n", 1, 1, 2000);
    RuntimeDebugReport("Memory %d MB\n", 88);
    RuntimeDebugReport("MEM1 Arena : 0x%x - 0x%x\n",
                       gRuntimeLowLevelCore.arenaLo,
                       gRuntimeLowLevelCore.arenaHi);
    RuntimeDebugReport("MEM2 Arena : 0x%x - 0x%x\n", 0, 0);
}

static void RuntimeLowLevel_InitCoreLibraries(void) {
    /* 0x801A2C80 is the one-time Revolution SDK / OS core initializer. The original
       configures OS globals, arenas, interrupt handlers, DVD/device state, exception
       handling, and performs firmware/apploader checks before game managers exist. */
    if (gRuntimeLowLevelCore.initialized != 0) {
        return;
    }

    ClearMemory(&gRuntimeLowLevelCore, 0, sizeof(gRuntimeLowLevelCore));
    gRuntimeLowLevelCore.initialized = 1;
    gRuntimeLowLevelCore.bootTime = Runtime_GetBootTime();
    gRuntimeLowLevelCore.bootInfo = 0;
    gRuntimeLowLevelCore.consoleType = 0;
    gRuntimeLowLevelCore.arenaLo = 0;
    gRuntimeLowLevelCore.arenaHi = 0;
    gRuntimeLowLevelCore.firmwareChecked = 1;
    gRuntimeLowLevelCore.sdkInitialized = 1;
    gRuntimeLowLevelCore.interruptSystemInitialized = 1;
    gRuntimeLowLevelCore.dvdSystemInitialized = 1;
    gRuntimeLowLevelCore.exceptionHandlersInitialized = 1;
    RuntimeLowLevel_SetH4AFlag();
    RuntimeLowLevel_ReportKernelInfo();
}

static void RuntimeLowLevel_InitHeapOrDebugFlags(int flags) {
    int poolIndex;

    /* 0x80144B50 initializes the global allocator/pool records at DAT_802EE158 and
       the global memory mutex before DAT_802E71B8 is allocated. */
    ClearMemory(&gRuntimeLowLevelMemoryPool, 0, sizeof(gRuntimeLowLevelMemoryPool));
    gRuntimeLowLevelMemoryPool.initialized = 1;
    gRuntimeLowLevelMemoryPool.interruptsOrDebugFlags = flags;
    gRuntimeLowLevelMemoryPool.arenaLow = MemoryPool_AllocateAligned(0, 0x20, 0x20);
    gRuntimeLowLevelMemoryPool.arenaHigh = MemoryPool_AllocateAligned(0, 0x20, 0x20);
    for (poolIndex = 0; poolIndex < 2; poolIndex++) {
        gRuntimeLowLevelMemoryPool.poolBase[poolIndex] =
            MemoryPool_AllocateAligned(0, 0x20, 0x20);
    }
    gRuntimeLowLevelMemoryPool.globalLockInitialized = 1;
}

static void RuntimeLowLevel_InitVideoInterface(void) {
    /* 0x801BACA0 initializes VI/video timing and interrupt callbacks once. The host
       preserves the game-facing state instead of touching Wii hardware registers. */
    if (gRuntimeLowLevelVideo.initialized != 0) {
        return;
    }

    ClearMemory(&gRuntimeLowLevelVideo, 0, sizeof(gRuntimeLowLevelVideo));
    gRuntimeLowLevelVideo.initialized = 1;
    gRuntimeLowLevelVideo.viMode = 5;
    gRuntimeLowLevelVideo.scanMode = 0;
    gRuntimeLowLevelVideo.displayWidth = 0x280;
    gRuntimeLowLevelVideo.framebufferWidth = 0x280;
    gRuntimeLowLevelVideo.framebufferHeight = 480;
    gRuntimeLowLevelVideo.viewportX = 0x28;
    gRuntimeLowLevelVideo.viewportY = 0;
    gRuntimeLowLevelVideo.viewportWidth = 0x280;
    gRuntimeLowLevelVideo.viewportHeight = 480;
    gRuntimeLowLevelVideo.retraceA = 18000;
    gRuntimeLowLevelVideo.retraceB = 18000;
    gRuntimeLowLevelVideo.retraceC = 0x1a5e0;
    gRuntimeLowLevelVideo.progressiveFlag = 1;
}

int RuntimeVideo_GetFramebufferWidth(void) {
    RuntimeLowLevel_InitVideoInterface();
    return gRuntimeLowLevelVideo.framebufferWidth;
}

int RuntimeVideo_GetFramebufferHeight(void) {
    RuntimeLowLevel_InitVideoInterface();
    return gRuntimeLowLevelVideo.framebufferHeight;
}

int RuntimeVideo_GetViewportX(void) {
    RuntimeLowLevel_InitVideoInterface();
    return gRuntimeLowLevelVideo.viewportX;
}

int RuntimeVideo_GetViewportY(void) {
    RuntimeLowLevel_InitVideoInterface();
    return gRuntimeLowLevelVideo.viewportY;
}

int RuntimeVideo_GetViewportWidth(void) {
    RuntimeLowLevel_InitVideoInterface();
    return gRuntimeLowLevelVideo.viewportWidth;
}

int RuntimeVideo_GetViewportHeight(void) {
    RuntimeLowLevel_InitVideoInterface();
    return gRuntimeLowLevelVideo.viewportHeight;
}

int Runtime_GetMainLoopFrameCounter(void) {
    MainLoopManagerKnownFields *manager = gActiveMainLoopManager != 0 ?
        gActiveMainLoopManager :
        (MainLoopManagerKnownFields *)gGameMainManagers.mainLoopManager;

    return manager != 0 ? manager->frameCounter : 0;
}

static void ResourceManager260_InitRecord(int *record) {
    /* LAB_80143DEC / FUN_80143E10 initialize one 0x4C-byte resource-manager record.
       The constructor loop in 0x80143E7C confirms these fields. */
    ClearMemory(record, 0, 0x4c);
    record[0] = -1;
    record[1] = 0;
    record[0x11] = 0;
}

static int *RuntimeObjectArray_Construct(
    int *allocation,
    void (*constructRecord)(int *record, short initMode),
    void (*destroyRecord)(int *record, short releaseMode),
    int recordSize,
    unsigned int count) {
    unsigned int index;
    int *record;
    int constructed;

    /* 0x80129C64 stores a small header before a contiguous object array and returns
       the first record at allocation +0x10. */
    if (allocation == 0) {
        return 0;
    }

    allocation[0] = recordSize;
    allocation[1] = (int)count;
    record = allocation + 4;
    if (constructRecord != 0) {
        constructed = 0;
        for (index = 0; index < count; index++) {
            constructRecord((int *)((unsigned char *)record + index * recordSize), 1);
            constructed++;
        }
        if (index < count && destroyRecord != 0) {
            while (constructed != 0) {
                constructed--;
                destroyRecord((int *)((unsigned char *)record + constructed * recordSize), -1);
            }
        }
    }
    return record;
}

static void ResourceManager260_ConstructRecord(int *record, short initMode) {
    (void)initMode;
    ResourceManager260_InitRecord(record);
}

static void ResourceManager260_DestroyRecord(int *record, short releaseMode) {
    /* 0x80143E10 only frees the record pointer when releaseMode > 0. Records inside
       a RuntimeObjectArray allocation are destroyed with -1 in rollback paths. */
    (void)record;
    (void)releaseMode;
}

static int *ResourceManager260_AllocateRecordTable(int capacity) {
    int *allocation;

    allocation = (int *)MemoryPool_AllocateAligned(0, capacity * 0x4c + 0x10, 0x20);
    if (allocation == 0) {
        return 0;
    }
    ClearMemory(allocation, 0, capacity * 0x4c + 0x10);
    return RuntimeObjectArray_Construct(
        allocation,
        ResourceManager260_ConstructRecord,
        ResourceManager260_DestroyRecord,
        0x4c,
        (unsigned int)capacity);
}

static void ResourceManager260_Init(int *manager, int capacity, int modeSelector) {
    int *records;

    /* 0x80143E7C constructs the 0x54-byte manager stored at DAT_802E71B8 +0x260.
       The second argument is the record capacity; GlobalRuntimeContext_Init passes
       0x100. */
    ClearMemory(manager, 0, 0x54);
    manager[0x14] = RuntimePointerBits(&gResourceManager260VTable);
    gResourceManager260LastId = -1;
    if (gResourceManager260SharedInitialized == 0) {
        /* 0x801B1BF0 runs only once before 0x801B7550(1). */
        gResourceManager260SharedInitialized = 1;
    }

    records = ResourceManager260_AllocateRecordTable(capacity);
    manager[4] = RuntimePointerBits(records);
    manager[0] = 0;
    manager[1] = RuntimeFloatBits(0.0f);
    manager[2] = 0;
    manager[3] = capacity;
    manager[5] = 0;
    manager[6] = 0;
    manager[7] = 1;
    manager[8] = 1;
    manager[9] = 1;
    manager[10] = 1;
    manager[0x0b] = 1;
    manager[0x0c] = 0;
    manager[0x0d] = -1;
    manager[0x0e] = 0;
    manager[0x0f] = 0;
    manager[0x10] = 0;
    manager[0x11] = 0;
    manager[0x12] = 0;
    manager[0x13] = RuntimePointerBits(
        modeSelector == 2 ? &gResourceManager260ModeTableTwo : &gResourceManager260ModeTableDefault);
}

int GlobalResourceManager260_GetRecordPayloadSize(int *payload) {
    int kind;

    /* 0x801B1AC0 resolves the size/weight field for one payload record. */
    if (payload == 0) {
        return 0;
    }

    kind = payload[3];
    if (kind == 2) {
        return 0;
    }
    if (kind < 2) {
        if (kind > 0) {
            return payload[8];
        }
        if (kind < -1) {
            return RuntimePointerBits(payload);
        }
    }
    else if (kind < 10) {
        if (kind > 7) {
            return RuntimePointerBits(payload);
        }
    }
    else if (kind > 0x0c) {
        return RuntimePointerBits(payload);
    }
    return payload[8];
}

int GlobalResourceManager260_UpdateProgress(int *manager) {
    int index;
    int offset;
    int pendingSize;
    int hasNoActiveRecords;
    int done;
    int *records;
    float progress;

    /* 0x80144294 walks the DAT_802E71B8 +0x260 resource-manager records and updates
       manager +0x04 as a 0..1 progress float. It returns true when the manager has
       no active work or when all active records have completed. */
    if (manager == 0) {
        return 1;
    }

    pendingSize = 0;
    hasNoActiveRecords = 1;
    records = (int *)RuntimePointerFromBits(manager[4]);
    offset = 0;
    for (index = 0; records != 0 && index < manager[3]; index++) {
        int *record = (int *)((unsigned char *)records + offset);
        if (record[1] == 1) {
            pendingSize += GlobalResourceManager260_GetRecordPayloadSize(record + 2);
            hasNoActiveRecords = 0;
        }
        offset += 0x4c;
    }

    done = manager[0] == 0;
    if (manager[6] != 0) {
        progress = (float)(manager[5] + pendingSize) / (float)manager[6];
    }
    else {
        progress = 0.0f;
    }
    manager[1] = RuntimeFloatBits(progress);

    if (done) {
        manager[0] = 0;
        manager[5] = 0;
        manager[6] = 0;
        manager[1] = RuntimeFloatBits(0.0f);
    }
    if (hasNoActiveRecords && progress == 1.0f) {
        manager[0] = 0;
        done = 1;
        manager[5] = 0;
        manager[6] = 0;
        manager[1] = RuntimeFloatBits(0.0f);
    }

    return done;
}

static void UiObjectManager270_Init(int *manager) {
    /* 0x80173084 constructs the 0x38-byte manager stored at DAT_802E71B8 +0x270. */
    ClearMemory(manager, 0, 0x38);
    manager[0] = 0;
    manager[1] = 0;
    manager[3] = 0;
    *(unsigned char *)((unsigned char *)manager + 0x18) = 1;
    *(unsigned char *)((unsigned char *)manager + 0x19) = 0;
    *(unsigned char *)((unsigned char *)manager + 0x1a) = 0;
    manager[5] = 0;
    manager[7] = 0;
    manager[9] = RuntimeFloatBits(1.0f);
    manager[10] = RuntimeFloatBits(1.0f);
    *(unsigned char *)((unsigned char *)manager + 0x2c) = 0;
    manager[0x0c] = 1;
    *(unsigned char *)((unsigned char *)manager + 0x34) = 0x0c;
    *(unsigned char *)((unsigned char *)manager + 0x35) = 1;
}

static void EffectSceneManager268_ConstructRecord(int *record, short initMode) {
    (void)initMode;
    /* The original passes pool-specific constructors for these records. Until those
       bodies are recovered, preserve the cleared allocation behavior and global-index
       write performed by the owner after construction. */
}

static void EffectSceneManager268_DestroyRecord(int *record, short releaseMode) {
    (void)record;
    (void)releaseMode;
}

static int *EffectSceneManager268_AllocateRecords(int count, int recordSize) {
    int *allocation;
    int index;
    int *records;

    if (count <= 0) {
        return 0;
    }

    allocation = (int *)MemoryPool_AllocateAligned(0, count * recordSize + 0x10, 0x20);
    if (allocation == 0) {
        return 0;
    }

    ClearMemory(allocation, 0, count * recordSize + 0x10);
    records = RuntimeObjectArray_Construct(
        allocation,
        EffectSceneManager268_ConstructRecord,
        EffectSceneManager268_DestroyRecord,
        recordSize,
        (unsigned int)count);
    for (index = 0; index < count; index++) {
        int *record = (int *)((unsigned char *)records + index * recordSize);
        if (recordSize > 0xc0) {
            *(int *)((unsigned char *)record + 0xc0) = index;
        }
    }
    return records;
}

static void EffectSceneManager268_Init(
    int *manager,
    int group0Count,
    int group1Count,
    int group2Count,
    int globalParam,
    unsigned int flags) {
    int totalCount;
    int *recordMap;
    int *group0;
    int *group1;
    int *group2;
    int index;

    /* 0x80166DF0 constructs the large 0x4248-byte manager stored at
       DAT_802E71B8 +0x268. It owns three typed record pools and a pointer map over
       their combined index range. */
    ClearMemory(manager, 0, 0x4248);
    manager[0x4244 / 4] = RuntimePointerBits(&gEffectSceneManager268VTable);
    gEffectSceneManager268GlobalParam = globalParam;

    manager[0x80 / 4] = 0;
    manager[0x84 / 4] = -1;
    manager[0x88 / 4] = -1;
    manager[0x8c / 4] = 1;
    manager[0xbc / 4] = RuntimeFloatBits(1.0f);
    manager[0xc0 / 4] = RuntimeFloatBits(1.0f);
    manager[0xc4 / 4] = RuntimeFloatBits(1.0f);
    manager[0xc8 / 4] = 0;
    manager[0xcc / 4] = 0;
    manager[0xd0 / 4] = 0;
    manager[0xd4 / 4] = 0;
    manager[0xd8 / 4] = 0;
    manager[0xdc / 4] = 0;
    manager[0xe0 / 4] = 0;
    manager[0xe4 / 4] = 0;

    manager[0x108 / 4] = 0;
    manager[0x10c / 4] = group0Count;
    manager[0x110 / 4] = group0Count;
    manager[0x114 / 4] = group0Count + group1Count;
    manager[0x118 / 4] = group0Count + group1Count;
    manager[0x11c / 4] = group0Count + group1Count + group2Count;
    manager[0x120 / 4] = group0Count + group1Count + group2Count;
    manager[0x124 / 4] = 0;

    ClearMemory((unsigned char *)manager + 0x128, 0, 0xfc);
    ClearMemory((unsigned char *)manager + 0x224, 0, 0x160);
    ClearMemory((unsigned char *)manager + 0x384, 0, 0xac);
    ClearMemory((unsigned char *)manager + 0x430, 0, 100);
    ClearMemory((unsigned char *)manager + 0x494, 0, 0x118);
    ClearMemory((unsigned char *)manager + 0x5ac, 0, 400);
    ClearMemory((unsigned char *)manager + 0x73c, 0, 0xc0);
    ClearMemory((unsigned char *)manager + 0x7fc, 0, 0x84);
    manager[0x888 / 4] = -1;
    manager[0x88c / 4] = -1;
    manager[0x890 / 4] = -1;
    ClearMemory((unsigned char *)manager + 0x8b8, 0, 0x20);
    ClearMemory((unsigned char *)manager + 0x8d8, 0, 0xa0);
    ClearMemory((unsigned char *)manager + 0x978, 0, 0x30);
    ClearMemory((unsigned char *)manager + 0x9bc, 0, 0x14);
    manager[0x9d0 / 4] = 1;
    ClearMemory((unsigned char *)manager + 0x9d8, 0, 8);
    ClearMemory((unsigned char *)manager + 0x9e0, 0, 0x3800);
    ClearMemory((unsigned char *)manager + 0x41e8, 0, 8);
    ClearMemory((unsigned char *)manager + 0x41f0, 0xff, 8);
    ClearMemory((unsigned char *)manager + 0x41f8, 0, 8);
    ClearMemory((unsigned char *)manager + 0x4200, 0, 8);
    ClearMemory((unsigned char *)manager + 0x4208, 0, 8);
    ClearMemory((unsigned char *)manager + 0x4210, 0, 8);
    ClearMemory((unsigned char *)manager + 0x4218, 0, 8);
    manager[0x4220 / 4] = 0;
    ClearMemory((unsigned char *)manager + 0x4224, 0, 0x20);

    if ((flags & 1U) != 0) {
        manager[0x124 / 4] = group2Count;
    }

    totalCount = manager[0x120 / 4];
    recordMap = (int *)MemoryPool_AllocateAligned(0, totalCount << 2, 0x20);
    manager[0xf8 / 4] = RuntimePointerBits(recordMap);
    if (recordMap != 0) {
        ClearMemory(recordMap, 0, totalCount << 2);
    }

    group0 = EffectSceneManager268_AllocateRecords(group0Count, 0xd0);
    group1 = EffectSceneManager268_AllocateRecords(group1Count, 0x220);
    group2 = EffectSceneManager268_AllocateRecords(group2Count, 0xaa4);
    manager[0xfc / 4] = RuntimePointerBits(group0);
    manager[0x100 / 4] = RuntimePointerBits(group1);
    manager[0x104 / 4] = RuntimePointerBits(group2);

    if (recordMap != 0) {
        for (index = 0; index < totalCount; index++) {
            int *record = 0;
            if (index >= manager[0x108 / 4] && index < manager[0x10c / 4] && group0 != 0) {
                record = (int *)((unsigned char *)group0 + (index - manager[0x108 / 4]) * 0xd0);
            }
            else if (index >= manager[0x110 / 4] && index < manager[0x114 / 4] && group1 != 0) {
                record = (int *)((unsigned char *)group1 + (index - manager[0x110 / 4]) * 0x220);
            }
            else if (index >= manager[0x118 / 4] && index < manager[0x11c / 4] && group2 != 0) {
                record = (int *)((unsigned char *)group2 + (index - manager[0x118 / 4]) * 0xaa4);
            }
            recordMap[index] = RuntimePointerBits(record);
            if (record != 0) {
                *(int *)((unsigned char *)record + 0xc0) = index;
            }
        }
    }

    for (index = 0; index < 4; index++) {
        int *slot = (int *)MemoryPool_AllocateAligned(0, 0x50, 0x20);
        if (slot != 0) {
            ClearMemory(slot, 0, 0x50);
        }
        *(int *)((unsigned char *)manager + 0x8d8 + index * 0x28) = RuntimePointerBits(slot);
    }

    manager[0xb0 / 4] = 1;
    manager[0x84 / 4] = 0;
    manager[0x88 / 4] = 0;
    manager[0x898 / 4] = 4;
    manager[0x894 / 4] = 1;

    manager[0x218 / 4] = RuntimeFloatBits(0.0f);
    manager[0x220 / 4] = RuntimeFloatBits(1.0f);
    manager[0x374 / 4] = RuntimeFloatBits(0.0f);
    manager[0x37c / 4] = RuntimeFloatBits(1.0f);
    manager[0x424 / 4] = 0x0f;
    manager[0x42c / 4] = 500;
    manager[0x470 / 4] = 500;
    manager[0x47c / 4] = 0x32;
    manager[0x488 / 4] = 100;
    manager[0x474 / 4] = 500;
    manager[0x480 / 4] = 0x32;
    manager[0x48c / 4] = 100;
    manager[0x478 / 4] = 500;
    manager[0x484 / 4] = 0x32;
    manager[0x490 / 4] = 100;
    manager[0x5a0 / 4] = RuntimeFloatBits(0.0f);
    manager[0x5a8 / 4] = RuntimeFloatBits(1.0f);
    manager[0x72c / 4] = RuntimeFloatBits(0.0f);
    manager[0x734 / 4] = RuntimeFloatBits(1.0f);
    manager[0x7f0 / 4] = 0x0f;
    manager[0x7f8 / 4] = 500;
    manager[0x850 / 4] = 500;
    manager[0x860 / 4] = 0x32;
    manager[0x870 / 4] = 100;
    manager[0x854 / 4] = 500;
    manager[0x864 / 4] = 0x32;
    manager[0x874 / 4] = 100;
    manager[0x858 / 4] = 500;
    manager[0x868 / 4] = 0x32;
    manager[0x878 / 4] = 100;
    manager[0x85c / 4] = 500;
    manager[0x86c / 4] = 0x32;
    manager[0x87c / 4] = 100;
    manager[0x9a8 / 4] = 1;
}

static void GlobalRuntimeContext_Init(
    int *context,
    int arg0,
    int arg1,
    int arg2,
    int arg3,
    int arg4,
    int arg5,
    int arg6,
    int arg7,
    int arg8,
    int arg9) {
    void *submanager258;
    void *resourceManager260;
    void *submanager25c;
    void *submanager264;
    void *effectManager268;
    void *uiManager270;
    void *submanager274;

    /* 0x80142BF0 constructs the 0x278-byte global runtime context stored at
       DAT_802E71B8. */
    ClearMemory(context, 0, 0x278);
    context[1] = -1;
    context[0x91] = 0;
    context[0x92] = 2;
    context[0x93] = RuntimeFloatBits(640.0f);
    context[0x94] = RuntimeFloatBits(480.0f);
    context[0x95] = -1;
    context[0x2f] = arg0;
    context[0x30] = 0;

    submanager258 = MemoryPool_AllocateAligned(0, 0x70, 0x20);
    if (submanager258 != 0) {
        /* 0x801410F8 constructs this 0x70-byte context submanager. */
        ClearMemory(submanager258, 0, 0x70);
    }
    context[0x96] = RuntimePointerBits(submanager258);

    resourceManager260 = MemoryPool_AllocateAligned(0, 0x54, 0x20);
    if (resourceManager260 != 0) {
        /* 0x80143E7C(resourceManager260, 0x100, context[0x2F]) */
        ResourceManager260_Init((int *)resourceManager260, 0x100, context[0x2f]);
    }
    context[0x98] = RuntimePointerBits(resourceManager260);

    submanager25c = MemoryPool_AllocateAligned(0, 0x504, 0x20);
    if (submanager25c != 0) {
        /* 0x801499F4 constructs the 0x504-byte manager, then 0x80149C80 applies arg8. */
        ClearMemory(submanager25c, 0, 0x504);
        ((int *)submanager25c)[0] = arg8;
    }
    context[0x97] = RuntimePointerBits(submanager25c);

    submanager264 = MemoryPool_AllocateAligned(0, 0x874, 0x20);
    if (submanager264 != 0) {
        /* 0x801644D0(submanager264, arg2, arg3) */
        ClearMemory(submanager264, 0, 0x874);
        ((int *)submanager264)[0] = arg2;
        ((int *)submanager264)[1] = arg3;
    }
    context[0x99] = RuntimePointerBits(submanager264);

    effectManager268 = MemoryPool_AllocateAligned(0, 0x4248, 0x20);
    if (effectManager268 != 0) {
        /* 0x80166DF0(effectManager268, arg4, arg5, arg6, arg7, 0) */
        EffectSceneManager268_Init((int *)effectManager268, arg4, arg5, arg6, arg7, 0);
    }
    context[0x9a] = RuntimePointerBits(effectManager268);

    ClearMemory(gGlobalTextureSlots, 0, sizeof(gGlobalTextureSlots));
    gGlobalTextureManager.nextTextureSlot = 0;
    gGlobalTextureManager.slotCount = GLOBAL_TEXTURE_MANAGER_SLOTS;
    context[0x9b] = RuntimePointerBits(&gGlobalTextureManager);

    uiManager270 = MemoryPool_AllocateAligned(0, 0x38, 0x20);
    if (uiManager270 != 0) {
        /* 0x80173084 constructs the 0x38-byte UI/object manager referenced at +0x270. */
        UiObjectManager270_Init((int *)uiManager270);
    }
    context[0x9c] = RuntimePointerBits(uiManager270);

    submanager274 = MemoryPool_AllocateAligned(0, 0xb4, 0x20);
    if (submanager274 != 0) {
        /* 0x80141AC8(submanager274, arg9) */
        ClearMemory(submanager274, 0, 0xb4);
        ((int *)submanager274)[0] = arg9;
    }
    context[0x9d] = RuntimePointerBits(submanager274);

    context[0x23] = 4;
    context[0x2e] = -1;
    context[0x24] = 1;
    context[0x25] = 1;
    context[0x26] = 1;
    context[0x27] = 1;
    context[0x28] = 1;
    context[0x29] = 1;
    context[0x2a] = 1;
    context[0x2b] = 1;
    context[0x2c] = 1;
    context[0x2d] = 1;

    /* 0x801ADD70(&LAB_80143A88), 0x801E6790, 0x801AD410/0x801AD510 entropy,
       FUN_80131C2C, FUN_8018D144, FUN_8015F4AC, and the region string setup are
       still startup/environment hooks to port once their bodies are known. */
}

int *GlobalRuntimeContext_Get(void) {
    return gGlobalRuntimeContext;
}

int GlobalRuntimeContext_SelectRegionVariant(int *globalContext) {
    int regionIndex;

    /* 0x80143830 chooses the active region/language resource table. It uses the
       low byte stored at globalContext +0x8C, unless the corresponding +0x90 table
       entry is zero, in which case it falls back to globalContext +0xB8. */
    if (globalContext == 0) {
        return 0;
    }

    regionIndex = globalContext[0x8c / 4];
    if (globalContext[0x90 / 4 + regionIndex] == 0) {
        return globalContext[0xb8 / 4];
    }
    return regionIndex;
}

void GlobalRuntimeContext_SetRegionVariantEntry(int *globalContext, int variantIndex, int value) {
    /* FUN_80143818 writes globalContext +0x90 + variantIndex * 4. */
    if (globalContext == 0) {
        return;
    }
    globalContext[0x90 / 4 + variantIndex] = value;
}

void GlobalRuntimeContext_SetFallbackRegionVariant(int *globalContext, int value) {
    /* FUN_80143828 writes globalContext +0xB8. */
    if (globalContext == 0) {
        return;
    }
    globalContext[0xb8 / 4] = value;
}

void GlobalResourceManager260_SetModeTable(int *manager, int modeTable) {
    /* FUN_80144B40 writes manager +0x4C. */
    if (manager == 0) {
        return;
    }
    manager[0x4c / 4] = modeTable;
}

void RuntimeMemory_SetCriticalFlag(int value) {
    /* 0x80144EA0 locks DAT_802EE1C0, sets DAT_802E71C8, stores uRam802E71CC, and
       unlocks. This is the paired set helper for the low-level memory/global state. */
    gRuntimeMemoryCriticalFlag = 1;
    gRuntimeMemoryCriticalValue = value;
}

void RuntimeMemory_ClearCriticalFlag(void) {
    /* 0x80144EF4 clears DAT_802E71C8 under the same memory mutex. */
    gRuntimeMemoryCriticalFlag = 0;
}

void Runtime_SetSoundArchiveReloadGuard(int enabled) {
    /* 0x80168060 stores DAT_802E71E0. That global gates the archive reload path
       around CzanSoundManager_LoadArchive. */
    gSoundArchiveReloadGuard = enabled;
}

int *GameMain_GetBootResourceBundle(void) {
    return gGameMainManagers.manager802e70a4;
}

int *GameMain_GetTextManager(void) {
    return gGameMainManagers.manager802e70b0;
}

int *GameMain_GetUiRootManager(void) {
    return gGameMainManagers.uiRootManager;
}

int *GameMain_GetCharacterAssetManager(void) {
    return gGameMainManagers.characterAssetManager;
}

int *GameMain_GetModelEffectManager(void) {
    return gGameMainManagers.manager802e70b8;
}

int *GameMain_GetInputOrMenuStateManager(void) {
    return gGameMainManagers.inputOrMenuStateManager;
}

void GlobalSubManager274_CopyRgba48(int *manager, unsigned char *outColor) {
    unsigned char *bytes;

    /* 0x80141AA4 copies four bytes from manager +0x48..+0x4B into the caller's
       output color buffer. */
    if (outColor == 0) {
        return;
    }

    if (manager == 0) {
        outColor[0] = 0;
        outColor[1] = 0;
        outColor[2] = 0;
        outColor[3] = 0;
        return;
    }

    bytes = (unsigned char *)manager;
    outColor[0] = bytes[0x48];
    outColor[1] = bytes[0x49];
    outColor[2] = bytes[0x4a];
    outColor[3] = bytes[0x4b];
}

void GlobalRuntimeContext_CreateOnce(
    int arg0,
    int arg1,
    int arg2,
    int arg3,
    int arg4,
    int arg5,
    int arg6,
    int arg7,
    int arg8,
    int arg9) {
    int *context;

    /* 0x801438F4 lazily creates DAT_802E71B8. The original returns void and writes
       the global; the host exposes GlobalRuntimeContext_Get for callers that need it. */
    if (gGlobalRuntimeContext != 0) {
        return;
    }

    RuntimeLowLevel_InitCoreLibraries();
    RuntimeLowLevel_InitHeapOrDebugFlags(0x70007);
    RuntimeLowLevel_InitVideoInterface();

    context = (int *)MemoryPool_AllocateAligned(0, 0x278, 0x20);
    if (context != 0) {
        GlobalRuntimeContext_Init(
            context,
            arg0,
            arg1,
            arg2,
            arg3,
            arg4,
            arg5,
            arg6,
            arg7,
            arg8,
            arg9);
    }

    gGlobalRuntimeContext = context;
}

void RuntimeEntry(void) {
    /* DOL runtime entry / __start.
       Performs low-level runtime setup, relocates boot info pointers,
       initializes OS/C runtime, runs constructors, then calls GameMain. */
    (void)GameMain();
}

static void GameMain_InitStartupUiAndDebug(void);

static void GameMain_InitRuntimeManagers(void) {
    int regionIndex;

    /* GameMain reaches the global runtime context creator before entering the module
       loop. This is the recovered 0x801438F4 path, not a host-only skeleton setup. */
    GlobalRuntimeContext_CreateOnce(1, 0x800, 4, 0x10, 0x32, 3, 0x46, 1, 0xf00403, 1);
    GameMain_InitStartupUiAndDebug();

    for (regionIndex = 0; regionIndex < 10; regionIndex++) {
        GlobalRuntimeContext_SetRegionVariantEntry(gGlobalRuntimeContext, regionIndex, 0);
    }
    GlobalRuntimeContext_SetRegionVariantEntry(gGlobalRuntimeContext, 1, 1);
    GlobalRuntimeContext_SetRegionVariantEntry(gGlobalRuntimeContext, 3, 1);
    GlobalRuntimeContext_SetRegionVariantEntry(gGlobalRuntimeContext, 4, 1);
    GlobalRuntimeContext_SetFallbackRegionVariant(gGlobalRuntimeContext, 1);

    if (gGameMainManagers.mainLoopManager == 0) {
        gGameMainManagers.mainLoopManager = (int *)MemoryPool_AllocateAligned(0, 0x14, 0x20);
        if (gGameMainManagers.mainLoopManager != 0) {
            PlayerDataStateContainer_Init(gGameMainManagers.mainLoopManager);
        }
    }
    if (gGameMainManagers.playerDataManager == 0) {
        gGameMainManagers.playerDataManager = (int *)MemoryPool_AllocateAligned(0, 0x2f54, 0x20);
        if (gGameMainManagers.playerDataManager != 0) {
            PlayerDataManager_Init(gGameMainManagers.playerDataManager);
        }
    }
    if (gGameMainManagers.manager802e70e0 == 0) {
        gGameMainManagers.manager802e70e0 = (int *)MemoryPool_AllocateAligned(0, 0x28f58, 0x20);
        Manager802e70e0_Init(gGameMainManagers.manager802e70e0);
        Manager802e70e0_DestroyNoop();
    }
    if (gGameMainManagers.manager802e70a4 == 0) {
        gGameMainManagers.manager802e70a4 = (int *)MemoryPool_AllocateAligned(0, 0x49c, 0x20);
        BootResourceBundle_Init(gGameMainManagers.manager802e70a4);
    }
    if (gGameMainManagers.manager802e70a8 == 0) {
        gGameMainManagers.manager802e70a8 = (int *)MemoryPool_AllocateAligned(0, 0x0c, 0x20);
        ResourceSlotHandle_Init(gGameMainManagers.manager802e70a8);
        ResourceSlotHandle_CreateSlotPool(gGameMainManagers.manager802e70a8);
        ActiveControllerMovieBindings_SetMovieSlotManagerForHost(gGameMainManagers.manager802e70a8);
    }
    if (gGameMainManagers.inputOrMenuStateManager == 0) {
        gGameMainManagers.inputOrMenuStateManager = (int *)MemoryPool_AllocateAligned(0, 0xef0, 0x20);
        if (gGameMainManagers.inputOrMenuStateManager != 0) {
            ClearMemory(gGameMainManagers.inputOrMenuStateManager, 0, 0xef0);
        }
    }
    if (gGameMainManagers.manager802e70b0 == 0) {
        gGameMainManagers.manager802e70b0 = (int *)MemoryPool_AllocateAligned(0, 0x50, 0x20);
        Manager802e70b0_Init(gGameMainManagers.manager802e70b0);
    }
    if (gGameMainManagers.manager802e70b4 == 0) {
        gGameMainManagers.manager802e70b4 = (int *)MemoryPool_AllocateAligned(0, 0xbf0, 0x20);
        if (gGameMainManagers.manager802e70b4 != 0) {
            ClearMemory(gGameMainManagers.manager802e70b4, 0, 0xbf0);
        }
    }
    if (gGameMainManagers.uiRootManager == 0) {
        gGameMainManagers.uiRootManager = (int *)MemoryPool_AllocateAligned(0, 0x4c, 0x20);
        UiRootManager_Init(gGameMainManagers.uiRootManager);
    }
    if (gGameMainManagers.characterAssetManager == 0) {
        gGameMainManagers.characterAssetManager = (int *)MemoryPool_AllocateAligned(0, 0x284e0, 0x20);
        CharacterAssetManager_Init(gGameMainManagers.characterAssetManager);
    }
    if (gGameMainManagers.manager802e70b8 == 0) {
        gGameMainManagers.manager802e70b8 = (int *)MemoryPool_AllocateAligned(0, 0x1b38, 0x20);
        ModelEffectManager_Init(gGameMainManagers.manager802e70b8);
    }
    if (gGameMainManagers.largeResourceManager == 0) {
        gGameMainManagers.largeResourceManager = (int *)MemoryPool_AllocateAligned(0, 0x3010b8, 0x20);
        LargeResourceManager_Init(gGameMainManagers.largeResourceManager);
    }
    if (gGameMainManagers.bootTempManager == 0) {
        gGameMainManagers.bootTempManager = (int *)MemoryPool_AllocateAligned(0, 0x28, 0x20);
        BootTempManager_Init(gGameMainManagers.bootTempManager);
    }
}

static void GameMain_InitStartupUiAndDebug(void) {
    int *uiObjectManager;
    int *resourceManager260;
    static int modeTablePlaceholder;

    /* GameMain calls these immediately after GlobalRuntimeContext_CreateOnce and
       before global manager allocation. They are idempotent in the host, so keep
       them adjacent to the recovered manager setup path. */
    DebugText_InitFontBacking();
    GlobalUiFrameState_CreateOnce();

    uiObjectManager = GlobalRuntimeContext_GetPointerAt(0x270);
    CzanUiManager_AllocateObjectGroupStorage(uiObjectManager, 300, 0x800);

    resourceManager260 = GlobalRuntimeContext_GetPointerAt(0x260);
    GlobalResourceManager260_SetModeTable(resourceManager260, RuntimePointerBits(&modeTablePlaceholder));
}

static void MainLoopManager_SetInitialFrameStep(int ticksPerSecond) {
    /* FUN_8016BA28(0x3C) configures a 60 Hz frame/timer step before the main loop. */
    (void)ticksPerSecond;
}

static void MainLoop_PrepareDebugTextFrame(void) {
    /* FUN_8014505C is paired with the debug text globals before each main-loop tick.
       The host debug renderer keeps its state internally. */
}

static void GlobalRuntimeContext_PreFrame(int *globalContext) {
    /* FUN_801432AC prepares per-frame render/global context state. The Wii body
       touches GX/VI state; host rendering starts frames in the module draw paths. */
    (void)globalContext;
}

static int GlobalResourceManager260_Tick(int *manager) {
    /* FUN_80144940 services the global resource manager and returns nonzero while
       it consumed the frame for file/resource work. The host has no async DVD queue
       yet, but keep the progress helper live for recovered resource records. */
    if (manager != 0) {
        manager[0x34 / 4] = -1;
        manager[0x30 / 4] = 0;
        (void)GlobalResourceManager260_UpdateProgress(manager);
    }
    return 0;
}

static int EffectSceneManager264_IsBusy(int *manager) {
    /* FUN_80166A34 gates module updates while the global effect/scene manager is busy. */
    (void)manager;
    return 0;
}

static void UiRootManager_UpdateBeforeDraw(int *uiRootManager, int skipModuleFrame) {
    int *transitionSubManager;

    /* FUN_801004B0 advances the UI-root selection-panel submanager when the active
       frame is not skipped and the transition submanager at +0x44 is not busy. */
    if (skipModuleFrame == 1 || uiRootManager == 0 || uiRootManager[0] == 0) {
        return;
    }

    transitionSubManager = (int *)UiRootHostPointerFromBits(uiRootManager[0x11]);
    if (transitionSubManager != 0 && transitionSubManager[4] == 1) {
        return;
    }

    CGameUiSelectionPanel_Update((int *)UiRootHostPointerFromBits(uiRootManager[0x0d]), uiRootManager);
}

static void UiRootManager_UpdateRuntimeBeforeDraw(int *uiRootManager, int skipModuleFrame) {
    int *transitionSubManager;

    /* FUN_800FEA60 is the UI-root runtime/update pass. The complete Wii order is:
       FUN_8010D484(uiRoot[0x11]), skip if +0x10 == 1, then update submanagers
       +0x0C, +0x0E, +0x10, +0x0F, +0x12 and mark the global Czan list dirty.

       The currently critical piece is FUN_801061B4(uiRoot[0x0E]), the two-bank
       menu-presentation/texture-frame update. */
    if (skipModuleFrame == 1 || uiRootManager == 0 || uiRootManager[0] == 0) {
        return;
    }

    transitionSubManager = (int *)UiRootHostPointerFromBits(uiRootManager[0x11]);
    if (transitionSubManager != 0 && transitionSubManager[4] == 1) {
        return;
    }

    UiRootSubManager_UpdateTextureFrameGroupMotion((int *)UiRootHostPointerFromBits(uiRootManager[0x0e]));
    UiRootBootTransition_Update((int *)UiRootHostPointerFromBits(uiRootManager[0x0c]));
    UiRootManager_UpdateGlobalCzanListOnce(uiRootManager, 0);
    uiRootManager[0x28 / 4] = 0;
}

static void ResourceSlotHandle_UpdateActiveSlot(int *slotHandle) {
    /* FUN_80024E94 updates the claimed movie slot if gManager_802E70A8 has one. */
    static LARGE_INTEGER lastMovieClock;
    static int *lastMovieObject;
    static double movieFrameAccumulator;
    int *movieObj;
    int elapsedFrames;
    int durationFrames;
    int framesToAdvance;
    float movieFps;
    unsigned int *flags;
    LARGE_INTEGER now;
    LARGE_INTEGER frequency;

    if (slotHandle == 0 || slotHandle[0] == 0 || slotHandle[1] < 0) {
        lastMovieClock.QuadPart = 0;
        lastMovieObject = 0;
        movieFrameAccumulator = 0.0;
        return;
    }

    movieObj = MovieSlotHandle_GetClaimedObject(slotHandle, slotHandle[1]);
    if (movieObj == 0) {
        lastMovieClock.QuadPart = 0;
        lastMovieObject = 0;
        movieFrameAccumulator = 0.0;
        return;
    }

    flags = (unsigned int *)((unsigned char *)movieObj + 0x230);
    if ((*flags & 0x400U) == 0) {
        lastMovieClock.QuadPart = 0;
        lastMovieObject = 0;
        movieFrameAccumulator = 0.0;
        return;
    }

    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&now)) {
        framesToAdvance = 1;
    }
    else {
        if (lastMovieObject != movieObj || lastMovieClock.QuadPart == 0) {
            lastMovieObject = movieObj;
            lastMovieClock = now;
            movieFrameAccumulator = 0.0;
            return;
        }

        movieFps = CzanMovieObj_GetPlaybackFps(movieObj);
        movieFrameAccumulator +=
            ((double)(now.QuadPart - lastMovieClock.QuadPart) / (double)frequency.QuadPart) *
            (double)movieFps;
        lastMovieClock = now;
        if (movieFrameAccumulator < 1.0) {
            return;
        }
        framesToAdvance = movieFrameAccumulator >= 2.0 ? 2 : 1;
        movieFrameAccumulator -= (double)framesToAdvance;
    }

    elapsedFrames = *(int *)((unsigned char *)movieObj + 0x238) + framesToAdvance;
    durationFrames = *(int *)((unsigned char *)movieObj + 0x240);
    *(int *)((unsigned char *)movieObj + 0x238) = elapsedFrames;
    if (durationFrames > 0 && elapsedFrames >= durationFrames) {
        *flags &= ~0x400U;
    }
}

static void GlobalRuntimeContext_PostFrame(int *globalContext) {
    /* FUN_80143430 flushes the global context's per-frame render state. */
    (void)globalContext;
}

static void UiFrameState_Update(int *uiFrameState) {
    /* FUN_80189298 ticks DAT_802E71F8, the lazily-created UI frame state. */
    (void)uiFrameState;
}

static void InputOrMenuStateManager_WriteControllerRecord(
    int *manager,
    int controllerIndex,
    unsigned int activeMask,
    unsigned int heldMask,
    unsigned int triggeredMask) {
    unsigned char *record;

    if (manager == 0 || controllerIndex < 0) {
        return;
    }

    record = (unsigned char *)manager + controllerIndex * 0x20;
    *(unsigned int *)(void *)(record + 0x04) = activeMask;
    *(unsigned int *)(void *)(record + 0x08) = heldMask;
    *(unsigned int *)(void *)(record + 0x10) = triggeredMask;
}

static void InputOrMenuStateManager_Update(int *manager, int flags) {
    unsigned int heldMask = 0;
    unsigned int triggeredMask = 0;

    /* 0x8002A33C updates the controller/menu records consumed by the DOL helper
       functions. The boot/select path reads logical controller slot 4; gameplay
       setup checks also probe slot 0 for start/confirm-style masks. */
    (void)flags;
    Platform_PollMenuInput(&heldMask, &triggeredMask);
    InputOrMenuStateManager_WriteControllerRecord(manager, 0, heldMask, heldMask, triggeredMask);
    InputOrMenuStateManager_WriteControllerRecord(manager, 4, heldMask, heldMask, triggeredMask);
}

static void Manager802e70b4_Update(int *manager, int skipModuleFrame) {
    /* FUN_8010E9DC updates the 0xBF0 manager allocated as gManager_802E70B4. */
    (void)manager;
    (void)skipModuleFrame;
}

static void GlobalCueManager_Update(int *manager) {
    /* FUN_80023EBC is the per-frame global cue/sound manager update. */
    (void)manager;
}

static int GlobalRuntimeContext_ShouldResetOrShutdown(int *globalContext) {
    /* FUN_8014323C checks OS reset requests and submanager +0x274 request state.
       Preserve the visible global flags; Wii OS reset button polling is pending. */
    if (globalContext == 0) {
        return 0;
    }
    return globalContext[0];
}

static int EffectSceneManager264_HasPendingReset(int *manager) {
    /* FUN_80166CB8 blocks reset/shutdown while effect scene work is pending. */
    (void)manager;
    return 0;
}

static void Runtime_RequestRestartMode(int mode) {
    /* FUN_801AAD70/FUN_801AB0E0/FUN_801AB120 eventually reset/restart the process.
       The host keeps running unless the platform quit flag is set. */
    (void)mode;
}

int GameMain(void) {
    MainLoopManagerKnownFields mainLoopManager;
    ModuleControllerKnownFields moduleController;
    const char *startModule = getenv("DDRII_HOST_START");

    setvbuf(stdout, 0, _IONBF, 0);

    ClearMemory(&mainLoopManager, 0, sizeof(mainLoopManager));
    ClearMemory(&moduleController, 0, sizeof(moduleController));
    moduleController.pendingModuleId = MODULE_ID_BOOT_LOGO;
    moduleController.activeModuleId = -1;
    moduleController.activeModule = 0;
    mainLoopManager.moduleController = &moduleController;
    gActiveMainLoopManager = &mainLoopManager;

    GameMain_InitRuntimeManagers();

    if (startModule != 0 && strcmp(startModule, "select") == 0) {
        moduleController.pendingModuleId = MODULE_ID_CSELECT;
    }

    MainLoopManager_SetInitialFrameStep(0x3c);

    {
        LARGE_INTEGER frequency;
        LARGE_INTEGER nextFrameTime;
        int useFramePacing = QueryPerformanceFrequency(&frequency) &&
            QueryPerformanceCounter(&nextFrameTime);
        long long frameTicks = useFramePacing ? frequency.QuadPart / 60 : 0;

        while (MainLoopManager_Tick((int *)&mainLoopManager) == 0) {
            if (useFramePacing && frameTicks > 0) {
                LARGE_INTEGER now;
                nextFrameTime.QuadPart += frameTicks;
                while (QueryPerformanceCounter(&now) &&
                       nextFrameTime.QuadPart > now.QuadPart) {
                    long long remainingTicks = nextFrameTime.QuadPart - now.QuadPart;
                    DWORD sleepMs = (DWORD)((remainingTicks * 1000) / frequency.QuadPart);
                    if (sleepMs > 2) {
                        Sleep(1);
                    }
                    else {
                        SwitchToThread();
                    }
                }
                if (!QueryPerformanceCounter(&now) ||
                    now.QuadPart > nextFrameTime.QuadPart + frameTicks * 4) {
                    QueryPerformanceCounter(&nextFrameTime);
                }
            }
            else {
                Sleep(1);
            }
        }
    }

    return 0;
}

int MainLoopManager_Tick(int *mainLoopManager) {
    MainLoopManagerKnownFields *manager = (MainLoopManagerKnownFields *)mainLoopManager;
    ModuleControllerKnownFields *moduleController;
    int *globalContext;
    int *submanager274;
    int skipModuleFrame;
    int status;

    /* 0x80020CC8 runs one frame of the real main loop. It updates global resource,
       UI, input/menu, scene/module, sound/cue, and reset/shutdown state. */
    if (manager == 0) {
        return 1;
    }
    if (Platform_ShouldQuit()) {
        return 1;
    }

    globalContext = GlobalRuntimeContext_Get();
    moduleController = manager->moduleController;
    skipModuleFrame = 1;

    MainLoop_PrepareDebugTextFrame();
    submanager274 = GlobalRuntimeContext_GetPointerAt(0x274);
    if (submanager274 == 0 || submanager274[1] != 2) {
        ModuleController_ApplyPendingModule((int *)moduleController);
    }
    GlobalRuntimeContext_PreFrame(globalContext);

    status = GlobalResourceManager260_Tick(GlobalRuntimeContext_GetPointerAt(0x260));
    if (status == 0 &&
        EffectSceneManager264_IsBusy(GlobalRuntimeContext_GetPointerAt(0x264)) == 0 &&
        (submanager274 == 0 || submanager274[1] != 2) &&
        manager->resetFrameCounter == 0 &&
        manager->shutdownFrameCounter == 0 &&
        (globalContext == 0 || globalContext[2] == 0)) {
        skipModuleFrame = 0;
    }

    if (gGameMainManagers.uiRootManager != 0) {
        /* FUN_801004B0(gUiRootManager, skipModuleFrame) is still pending. */
    }
    if (skipModuleFrame == 0) {
        (void)ModuleController_Update(moduleController);
    }
    if (gGameMainManagers.uiRootManager != 0) {
        UiRootManager_UpdateBeforeDraw(gGameMainManagers.uiRootManager, skipModuleFrame);
        UiRootManager_UpdateRuntimeBeforeDraw(gGameMainManagers.uiRootManager, skipModuleFrame);
        if (skipModuleFrame == 0 && moduleController->activeModuleId != MODULE_ID_CSELECT) {
            UiRootManager_DrawFrame(gGameMainManagers.uiRootManager);
        }
    }

    ResourceSlotHandle_UpdateActiveSlot(gGameMainManagers.manager802e70a8);
    GlobalRuntimeContext_PostFrame(globalContext);
    RenderFlushPendingState();
    UiFrameState_Update(GlobalUiFrameState_CreateOnce());
    InputOrMenuStateManager_Update(gGameMainManagers.inputOrMenuStateManager, 0);
    Manager802e70b4_Update(gGameMainManagers.manager802e70b4, skipModuleFrame);
    GlobalCueManager_Update(gGameMainManagers.manager802e70a4);

    if (GlobalRuntimeContext_ShouldResetOrShutdown(globalContext) == 1) {
        if (GlobalRuntimeContext_GetPointerAt(0x258) != 0) {
            GlobalRuntimeContext_GetPointerAt(0x258)[0x44 / 4] = 1;
        }
        status = manager->resetFrameCounter;
        manager->resetFrameCounter = status + 1;
        if (status + 1 > 5 && EffectSceneManager264_HasPendingReset(GlobalRuntimeContext_GetPointerAt(0x264)) == 0) {
            if (globalContext != 0 && globalContext[1] == 1) {
                Runtime_RequestRestartMode(1);
            }
            else if (globalContext != 0 && globalContext[1] == 2) {
                Runtime_RequestRestartMode(2);
            }
            else {
                Runtime_RequestRestartMode(0);
            }
        }
    }
    if (globalContext != 0 && globalContext[2] == 1) {
        if (GlobalRuntimeContext_GetPointerAt(0x258) != 0) {
            GlobalRuntimeContext_GetPointerAt(0x258)[0x44 / 4] = 1;
        }
        status = manager->shutdownFrameCounter;
        manager->shutdownFrameCounter = status + 1;
        if (status + 1 > 5 && EffectSceneManager264_HasPendingReset(GlobalRuntimeContext_GetPointerAt(0x264)) == 0) {
            return 1;
        }
    }

    manager->frameCounter++;
    return 0;
}

int *BootResourceBundle_Init(int *resourceBundle) {
    /* FUN_800221BC constructs the boot-temp resource bundle object used by
       BootResourceBundle_StartLoading / ApplyLoadedResources / Release. */
    if (resourceBundle == 0) {
        return 0;
    }

    resourceBundle[0x126] = 0;
    resourceBundle[0] = -1;
    ClearMemory(resourceBundle + 1, 0, 0x438);
    resourceBundle[0x10f] = 0;
    ClearMemory(resourceBundle + 0x110, 0, 8);
    resourceBundle[0x111] = 0xffff;
    ClearMemory(resourceBundle + 0x112, 0, 0x30);
    resourceBundle[0x113] = -1;
    resourceBundle[0x115] = -1;
    resourceBundle[0x11e] = 0;
    resourceBundle[0x11f] = 0;
    resourceBundle[0x120] = 0;
    ClearMemory(resourceBundle + 0x121, 0, 8);
    resourceBundle[0x123] = 0;
    resourceBundle[0x124] = 0;
    resourceBundle[0x125] = 0;
    return resourceBundle;
}

static const char *const BootResourceBundlePathTable[6][8] = {
    {
        "banner/banner_US.bin",
        "mii/RFLRes01.arc",
        "text/text_eng.bin",
        "font/font_us.bin",
        "select/select_cmn.bin",
        "ssq/SSQ_CMN.bin",
        "2Dcommon/comAF_US.bin",
        "Pointer/Pointer.bin",
    },
    {
        "banner/banner_US.bin",
        "mii/RFLRes01.arc",
        "text/text_eng.bin",
        "font/font_us.bin",
        "select/select_cmn.bin",
        "ssq/SSQ_CMN.bin",
        "2Dcommon/comAF_US.bin",
        "Pointer/Pointer.bin",
    },
    {
        "banner/banner_US.bin",
        "mii/RFLRes01.arc",
        "text/text_eng.bin",
        "font/font_us.bin",
        "select/select_cmn.bin",
        "ssq/SSQ_CMN.bin",
        "2Dcommon/comAF_US.bin",
        "Pointer/Pointer.bin",
    },
    {
        "banner/banner_US.bin",
        "mii/RFLRes01.arc",
        "text/text_fra.bin",
        "font/font_fr.bin",
        "select/select_cmn.bin",
        "ssq/SSQ_CMN_FR.bin",
        "2Dcommon/comAF_FR.bin",
        "Pointer/Pointer.bin",
    },
    {
        "banner/banner_US.bin",
        "mii/RFLRes01.arc",
        "text/text_spa.bin",
        "font/font_sp.bin",
        "select/select_cmn.bin",
        "ssq/SSQ_CMN_SP.bin",
        "2Dcommon/comAF_SP.bin",
        "Pointer/Pointer.bin",
    },
    {
        "banner/banner_US.bin",
        "mii/RFLRes01.arc",
        "text/text_eng.bin",
        "font/font_us.bin",
        "select/select_cmn.bin",
        "ssq/SSQ_CMN.bin",
        "2Dcommon/comAF_US.bin",
        "Pointer/Pointer.bin",
    },
};

static ResourceHandle *BootResourceBundle_GetHandle(int *resourceBundle, int slot) {
    if (resourceBundle == 0 || slot < 0 || slot >= 8) {
        return 0;
    }
    return (ResourceHandle *)RuntimePointerFromBits(resourceBundle[slot + 2]);
}

static void *BootResourceBundle_GetLoadedData(int *resourceBundle, int slot) {
    ResourceHandle *handle = BootResourceBundle_GetHandle(resourceBundle, slot);

    if (handle == 0 || handle->loaded == 0) {
        return 0;
    }
    HostCzan_RegisterLinkSize(handle->data, (unsigned int)handle->size);
    return handle->data;
}

void BootResourceBundle_ApplyLoadedResources(int *resourceBundle) {
    /* 0x80021F58 applies a loaded boot/resource bundle to the global managers.
       It runs once when resourceBundle[0] == 1 and resourceBundle[1] == 0, then
       marks resourceBundle[1] = 1.

       Confirmed resource handle slots:
       [3] -> DAT_802E71F8 / system manager, link data at handle +0x10
       [4] -> gManager_802E70B0
       [5] -> gLargeResourceManager-related sub-manager setup
       [6] -> gCharacterAssetManager
       [7] -> gLargeResourceManager via LargeResourceManager_ReloadFromDefaultLink
       [8] -> gUiRootManager via UiRootManager_LoadResource
       [9] -> gManager_802E70B4

       The original wraps the manager setup calls with FUN_80144EA0(1) / FUN_80144EF4(). */
    void *miiLinkData;
    void *textLinkData;
    void *fontLinkData;
    void *selectCommonLinkData;
    void *ssqCommonLinkData;
    void *uiRootLinkData;
    void *pointerLinkData;

    if (resourceBundle == 0 || resourceBundle[0] != 1) {
        return;
    }
    if (resourceBundle[1] != 0) {
        return;
    }

    RuntimeMemory_SetCriticalFlag(1);
    miiLinkData = BootResourceBundle_GetLoadedData(resourceBundle, 1);
    textLinkData = BootResourceBundle_GetLoadedData(resourceBundle, 2);
    fontLinkData = BootResourceBundle_GetLoadedData(resourceBundle, 3);
    selectCommonLinkData = BootResourceBundle_GetLoadedData(resourceBundle, 4);
    ssqCommonLinkData = BootResourceBundle_GetLoadedData(resourceBundle, 5);
    uiRootLinkData = BootResourceBundle_GetLoadedData(resourceBundle, 6);
    pointerLinkData = BootResourceBundle_GetLoadedData(resourceBundle, 7);

    /* FUN_8009A6A8(gManager_802E70E0), FUN_80188EFC(DAT_802E71F8, mii),
       and FUN_8010E770(gManager_802E70B4, Pointer) still need full bodies. Keep
       the exact data flow visible and call the recovered managers we do have. */
    (void)miiLinkData;
    (void)pointerLinkData;

    if (textLinkData != 0) {
        TextManager_LoadResource(gGameMainManagers.manager802e70b0, textLinkData);
    }
    if (fontLinkData != 0) {
        FontManager_LoadResource(fontLinkData);
    }
    if (selectCommonLinkData != 0) {
        CharacterAssetManager_LoadSelectCommon(gGameMainManagers.characterAssetManager, selectCommonLinkData);
    }
    if (uiRootLinkData != 0) {
        UiRootManager_RegisterResource(gGameMainManagers.uiRootManager, uiRootLinkData);
    }
    if (ssqCommonLinkData != 0) {
        LargeResourceManager_ReloadFromDefaultLink(
            gGameMainManagers.largeResourceManager,
            (int)(uintptr_t)ssqCommonLinkData);
    }
    RuntimeMemory_ClearCriticalFlag();
    resourceBundle[1] = 1;
}

void BootResourceBundle_StartLoading(int *resourceBundle) {
    /* 0x80021E98 starts loading the boot/CGame resource bundle into gBootTempManager.
       If resourceBundle[0] is zero, it selects a region/layout path table using
       FUN_80143830(DAT_802E71B8), then loads eight resources with LoadResourceByPath.

       Slot layout is one int per resourceBundle slot:
       resourceBundle[slot + 2] = ResourceHandle*

       The path table starts at PTR_s_/banner/banner_US.bin_802A4390 + regionIndex * 8.
       After queuing/loading resources, it calls FUN_80023634(gManager_802E70A4) and
       marks resourceBundle[0] = 1. */
    int regionIndex;
    int slot;

    if (resourceBundle == 0 || resourceBundle[0] != 0) {
        return;
    }

    RuntimeMemory_SetCriticalFlag(1);
    regionIndex = GlobalRuntimeContext_SelectRegionVariant(GlobalRuntimeContext_Get());
    if (regionIndex < 0 ||
        regionIndex >= (int)(sizeof(BootResourceBundlePathTable) / sizeof(BootResourceBundlePathTable[0]))) {
        regionIndex = 1;
    }

    for (slot = 0; slot < 8; slot++) {
        if (resourceBundle[slot + 2] == 0) {
            ResourceHandle *handle = LoadResourceByPath(
                GlobalRuntimeContext_GetPointerAt(0x260),
                BootResourceBundlePathTable[regionIndex][slot],
                0);
            resourceBundle[slot + 2] = RuntimePointerBits(handle);
        }
    }

    LargeResourceManager_ActivateDefaultAudioReferences(
        gGameMainManagers.largeResourceManager,
        GlobalRuntimeContext_Get());
    RuntimeMemory_ClearCriticalFlag();
    resourceBundle[0] = 1;
}

int *BootResourceBundle_Release(int *resourceBundle, short releaseMode) {
    /* 0x80021D90 releases the boot/CGame resource bundle loaded by
       BootResourceBundle_StartLoading and applied by BootResourceBundle_ApplyLoadedResources.

       Confirmed behavior:
       - if resourceBundle[1] == 1, tears down the global managers that consumed the
         loaded bundle, then clears resourceBundle[1]
       - if resourceBundle[0] == 1, releases any nonzero resource handles in
         resourceBundle[2..9], calls FUN_80023794(gManager_802E70A4), and clears
         resourceBundle[0]
       - frees the resourceBundle object only when releaseMode is positive */
    if (resourceBundle == 0) {
        return 0;
    }

    if (resourceBundle[1] == 1) {
        LargeResourceManager_ResetLoadedState(gGameMainManagers.largeResourceManager);
        resourceBundle[1] = 0;
    }
    if (resourceBundle[0] == 1) {
        resourceBundle[0] = 0;
    }
    (void)releaseMode;
    return resourceBundle;
}

void ModuleController_ApplyPendingModule(int *moduleController) {
    ModuleControllerKnownFields *controller = (ModuleControllerKnownFields *)moduleController;
    int previousModuleId;

    /* Original destroys the active module when pendingModuleId != activeModuleId,
       creates the pending module, calls its enter/setup method, then stores activeModuleId. */
    if (controller->pendingModuleId == controller->activeModuleId) {
        return;
    }

    previousModuleId = controller->activeModuleId;
    ModuleController_CreatePendingModule(moduleController);
    if (controller->pendingModuleId == MODULE_ID_BOOT_LOGO && controller->activeModule != 0) {
        (void)BootLogoModule_OnEnter(controller->activeModule, controller->pendingModuleId);
    }
    else if (controller->pendingModuleId == MODULE_ID_CSELECT && controller->activeModule != 0) {
        ((int *)controller->activeModule)[0] = previousModuleId;
        (void)CSelect_OnEnter((int *)controller->activeModule, controller->pendingModuleId);
    }
    controller->activeModuleId = controller->pendingModuleId;
}

void ModuleController_CreatePendingModule(int *moduleController) {
    static BootLogoModuleKnownFields bootLogoModule;
    static unsigned char cSelectModule[CSELECT_MODULE_SIZE];
    static unsigned char cGameModule[0x430];
    ModuleControllerKnownFields *controller = (ModuleControllerKnownFields *)moduleController;

    /* Original allocates a module object based on pendingModuleId:
       0 small boot object, 1 BootLogoModule, 2 CSelect, 3/5/6 CGame. */
    switch (controller->pendingModuleId) {
        case MODULE_ID_BOOT_LOGO:
            BootLogoModule_Init(&bootLogoModule);
            controller->activeModule = &bootLogoModule;
            break;
        case MODULE_ID_CSELECT:
            CSelect_Init(&cSelectModule);
            controller->activeModule = &cSelectModule;
            break;
        case MODULE_ID_CGAME:
        case MODULE_ID_CGAME_VARIANT_5:
        case MODULE_ID_CGAME_VARIANT_6:
            ClearMemory(cGameModule, 0, sizeof(cGameModule));
            ((int *)cGameModule)[0] = MODULE_ID_CSELECT;
            ((int *)cGameModule)[2] = 0;
            ((int *)cGameModule)[3] = 0;
            *(int *)(void *)(cGameModule + 0xb8) = controller->pendingModuleId == MODULE_ID_CGAME ? 0 :
                                                   controller->pendingModuleId == MODULE_ID_CGAME_VARIANT_5 ? 1 :
                                                   2;
            CGame_InitDefaultGameplaySetup((int *)(void *)cGameModule);
            controller->activeModule = &cGameModule;
            break;
        default:
            printf("ModuleController: unsupported module %d\n", controller->pendingModuleId);
            controller->activeModule = 0;
            break;
    }
}

int ModuleController_Update(ModuleControllerKnownFields *moduleController) {
    int nextModuleId;

    ModuleController_ApplyPendingModule((int *)moduleController);

    switch (moduleController->activeModuleId) {
        case MODULE_ID_BOOT_LOGO:
            nextModuleId = BootLogoModule_Tick(moduleController->activeModule, MODULE_ID_BOOT_LOGO);
            RenderBeginFrame();
            BootLogoModule_Draw(moduleController->activeModule);
            RenderEndFrame();
            if (nextModuleId != MODULE_ID_BOOT_LOGO) {
                moduleController->pendingModuleId = nextModuleId;
            }
            return 0;
        case MODULE_ID_CSELECT:
            return CSelect_Tick((int *)moduleController->activeModule);
        case MODULE_ID_CGAME:
        case MODULE_ID_CGAME_VARIANT_5:
        case MODULE_ID_CGAME_VARIANT_6:
            nextModuleId = CGame_UpdateStateMachine((int *)moduleController->activeModule, moduleController->activeModuleId);
            if (nextModuleId != moduleController->activeModuleId) {
                moduleController->pendingModuleId = nextModuleId;
            }
            return 0;
        default:
            return 1;
    }
}

static const char *const CSelectResourcePaths[6][5] = {
    {
        "select/selMusic_US.bin",
        "select/select_bin_us.bin",
        "select/selTitle_US.bin",
        "select/selResult_US.bin",
        "select/cmnAccMdl.bin",
    },
    {
        "select/selMusic_US.bin",
        "select/select_bin_us.bin",
        "select/selTitle_US.bin",
        "select/selResult_US.bin",
        "select/cmnAccMdl.bin",
    },
    {
        "select/selMusic_US.bin",
        "select/select_bin_us.bin",
        "select/selTitle_US.bin",
        "select/selResult_US.bin",
        "select/cmnAccMdl.bin",
    },
    {
        "select/selMusic_FR.bin",
        "select/select_bin_fr.bin",
        "select/selTitle_FR.bin",
        "select/selResult_FR.bin",
        "select/cmnAccMdl.bin",
    },
    {
        "select/selMusic_SP.bin",
        "select/select_bin_sp.bin",
        "select/selTitle_SP.bin",
        "select/selResult_SP.bin",
        "select/cmnAccMdl.bin",
    },
    {
        "select/selMusic_US.bin",
        "select/select_bin_us.bin",
        "select/selTitle_US.bin",
        "select/selResult_US.bin",
        "select/cmnAccMdl.bin",
    },
};

static int CSelect_GetRegionIndex(void) {
    int regionIndex = GlobalRuntimeContext_SelectRegionVariant(GlobalRuntimeContext_Get());
    if (regionIndex < 0 || regionIndex >= (int)(sizeof(CSelectResourcePaths) / sizeof(CSelectResourcePaths[0]))) {
        regionIndex = 1;
    }
    return regionIndex;
}

static int CSelect_StoreLoadedResource(int *cSelect, int wordIndex, const char *path) {
    ResourceHandle *handle = LoadResourceByPath(GlobalRuntimeContext_GetPointerAt(0x260), path, 0);
    cSelect[wordIndex] = RuntimePointerBits(handle);
    return handle != 0 && handle->loaded;
}

static int CSelectBootTitleFlow_Init(CSelectBootTitleFlow *flow) {
    if (flow == 0) {
        return 0;
    }

    /* 0x800DDF98: constructor for CSelect state 0. The real object inherits the
       common select-screen base at 0x8008CF54, then installs vtable
       PTR_PTR_802BD2F8 and seeds the boot/save/title substate. */
    ClearMemory(flow, 0, sizeof(*flow));
    *(int *)(void *)(flow->storage + 0x12c) = RuntimePointerBits((void *)0x802bd2f8);
    *(int *)(void *)(flow->storage + 0x130) = 0;
    *(float *)(void *)(flow->storage + 0x134) = 0.0f;
    return (int)(intptr_t)flow;
}

static int CSelectBootTitleFlow_Tick(CSelectBootTitleFlow *flow, int forceIdle) {
    int state;
    int *uiRootManager;
    int *playerDataManager;

    if (flow == 0 || forceIdle == 1) {
        return 0;
    }

    /* 0x800DE0DC. This is intentionally still a partial port: every branch below
       is from the recovered state machine, but several UI/save helpers are not
       implemented yet and are called only when a named host equivalent exists. */
    state = *(int *)(void *)(flow->storage + 0x130);
    uiRootManager = gGameMainManagers.uiRootManager;
    playerDataManager = gGameMainManagers.playerDataManager;

    switch (state) {
        case 0:
            if (UiRootManager_IsTitleTransitionAIdle(uiRootManager) != 0 &&
                UiRootManager_IsTitleTransitionCIdle(uiRootManager) != 0) {
                *(int *)(void *)(flow->storage + 0x130) = 1;
            }
            break;
        case 1:
            /* DAT_802E71F8[0] == 1 skips the save prompt path. The host UI-frame
               state object is not mapped yet, so keep the default save-check path. */
            UiRootManager_StartBootTransitionController(uiRootManager, 6, 1, 0, 1);
            UiRootManager_ConfigureBootTransitionPrompt(uiRootManager, 9, 0);
            *(int *)(void *)(flow->storage + 0x130) = 2;
            UiRootManager_SetBootTransitionSelectedOption(uiRootManager, 1);
            *(float *)(void *)(flow->storage + 0x134) = 0.0f;
            break;
        case 2:
            if (UiRootManager_GetBootTransitionResult(uiRootManager) == 0) {
                UiRootManager_ConfigureBootTransitionPrompt(uiRootManager, 9, 2);
                *(int *)(void *)(flow->storage + 0x130) = 3;
                UiRootManager_SetBootTransitionSelectedOption(uiRootManager, 1);
            }
            else if (UiRootManager_GetBootTransitionResult(uiRootManager) == 1) {
                UiRootManager_CloseBootTransitionController(uiRootManager);
                *(int *)(void *)(flow->storage + 0x130) = 6;
            }
            break;
        case 3:
            if (UiRootManager_GetBootTransitionResult(uiRootManager) == 0) {
                UiRootManager_CloseBootTransitionController(uiRootManager);
                *(int *)(void *)(flow->storage + 0x130) = 4;
            }
            else if (UiRootManager_GetBootTransitionResult(uiRootManager) == 1) {
                UiRootManager_ConfigureBootTransitionPrompt(uiRootManager, 9, 0);
                *(int *)(void *)(flow->storage + 0x130) = 2;
                UiRootManager_SetBootTransitionSelectedOption(uiRootManager, 1);
            }
            break;
        case 4:
            if (UiRootManager_IsBootTransitionControllerIdle(uiRootManager) != 0) {
                *(int *)(void *)(flow->storage + 0x130) = 5;
            }
            break;
        case 5:
            if (CharacterAssetManager_IsSelectCommonIdle(gGameMainManagers.characterAssetManager) == 0) {
                break;
            }
            if (playerDataManager == 0 || PlayerDataManager_GetSetupFieldF8(playerDataManager) == 0) {
                *(int *)(void *)(flow->storage + 0x130) = 7;
                UiRootManager_StartBootTransitionController(uiRootManager, 6, 1, 0, 1);
                UiRootManager_ConfigureBootTransitionPrompt(uiRootManager, 9, 4);
            }
            else {
                *(int *)(void *)(flow->storage + 0x130) = 10;
                return 0x0c;
            }
            break;
        case 6:
            if (UiRootManager_IsSelectionPanelIdle(uiRootManager) != 0) {
                *(int *)(void *)(flow->storage + 0x130) = 10;
            }
            break;
        case 7:
            /* The real game waits for confirm on the create-save prompt. Without
               the exact prompt UI loaded yet, advance only when host input says A. */
            if (UiRootManager_IsBootTransitionPromptReady(uiRootManager) != 0 &&
                InputOrMenuStateManager_IsConfirmPressed(gGameMainManagers.inputOrMenuStateManager, 4) != 0) {
                *(int *)(void *)(flow->storage + 0x130) = 8;
                UiRootManager_CloseBootTransitionController(uiRootManager);
            }
            break;
        case 8:
            if (UiRootManager_IsBootTransitionControllerIdle(uiRootManager) != 0) {
                if (playerDataManager == 0 || PlayerDataManager_GetSetupFieldF8(playerDataManager) == 0) {
                    *(int *)(void *)(flow->storage + 0x130) = 10;
                    return 0x0c;
                }
                *(int *)(void *)(flow->storage + 0x130) = 9;
            }
            break;
        case 9:
            *(int *)(void *)(flow->storage + 0x130) = 10;
            return 0x0c;
        default:
            break;
    }

    return 0;
}

static int CSelectTitleFlow_Init(CSelectTitleFlow *flow) {
    int i;

    if (flow == 0) {
        return 0;
    }

    /* 0x800E12D0: state 0x0C title/request flow constructor. The real object
       installs vtable PTR_PTR_802BD3F0, initializes a 3-entry CSelModeEntry list
       at +0x16C, and seeds title-flow state at +0x144. */
    ClearMemory(flow, 0, sizeof(*flow));
    *(int *)(void *)(flow->storage + 0x12c) = RuntimePointerBits((void *)0x802bd3f0);
    *(int *)(void *)(flow->storage + 0x144) = 0;
    *(float *)(void *)(flow->storage + 0x148) = 0.0f;
    *(int *)(void *)(flow->storage + 0x14c) = 0;
    *(int *)(void *)(flow->storage + 0x150) = 1;
    *(int *)(void *)(flow->storage + 0x154) = 0;
    *(int *)(void *)(flow->storage + 0x158) = 0;
    *(int *)(void *)(flow->storage + 0x15c) = 0;
    *(int *)(void *)(flow->storage + 0x1c) = -1;
    *(int *)(void *)(flow->storage + 0x20) = -1;
    *(int *)(void *)(flow->storage + 0x24) = -1;
    *(int *)(void *)(flow->storage + 0x128) = -1;
    *(int *)(void *)(flow->storage + 0x25c) = RuntimePointerBits(GlobalRuntimeContext_GetPointerAt(0x270));
    for (i = 0; i < 3; i++) {
        CSelModeEntry_Init(flow->storage + 0x16c + i * CSEL_MODE_ENTRY_SIZE);
    }
    return (int)(intptr_t)flow;
}

static void CSelectTitleModelFocus_Init(CSelectTitleModelFocus *focus) {
    unsigned char *bytes;

    if (focus == 0) {
        return;
    }

    bytes = focus->storage;
    ClearMemory(bytes, 0, sizeof(focus->storage));
    bytes[0] = 0;
    bytes[1] = 1;
    CzanModelOwner_Init((int *)(void *)(bytes + 0xb0));
    Matrix34_SetIdentity((float *)(void *)(bytes + 0x3c));
    Matrix34_SetIdentity((float *)(void *)(bytes + 0x6c));
}

static void CSelectTitleModelFocus_BindObject(
    CSelectTitleModelFocus *focus,
    int objectGroupHandle,
    int childIndex,
    unsigned char groupId,
    int bindMode) {
    unsigned char *bytes;

    if (focus == 0) {
        return;
    }

    bytes = focus->storage;
    bytes[0xac] = groupId;
    *(int *)(void *)(bytes + 0x10) = bindMode;
    bytes[1] = 1;
    *(int *)(void *)(bytes + 0x08) = objectGroupHandle;
    *(int *)(void *)(bytes + 0x0c) = childIndex;
}

static void CSelectTitleModelFocus_SetVectors(
    CSelectTitleModelFocus *focus,
    double duration,
    const float *forwardVector,
    const float *upVector,
    const float *translation) {
    unsigned char *bytes;

    if (focus == 0) {
        return;
    }

    bytes = focus->storage;
    if (forwardVector != 0) {
        memcpy(bytes + 0x14, forwardVector, sizeof(float) * 3);
    }
    if (upVector != 0) {
        memcpy(bytes + 0x2c, upVector, sizeof(float) * 3);
    }
    if (translation != 0) {
        memcpy(bytes + 0x20, translation, sizeof(float) * 3);
    }
    *(float *)(void *)(bytes + 0x38) = (float)duration;
}

static void CSelectTitleModelFocus_BuildInitialMatrix(CSelectTitleModelFocus *focus) {
    unsigned char *bytes;
    int *owner;

    if (focus == 0) {
        return;
    }

    bytes = focus->storage;
    owner = (int *)(void *)(bytes + 0xb0);
    CzanModelOwner_Reset(owner);
    CzanModelOwner_SetBaseTransformVectors(
        owner,
        (const float *)(const void *)(bytes + 0x14),
        (const float *)(const void *)(bytes + 0x2c),
        (const float *)(const void *)(bytes + 0x20));
    CzanModelOwner_UpdateCurrentMatrix(owner, 0);
    CzanModelOwner_CopyCurrentModelMatrix(owner, bytes + 0x3c);
    Matrix34_Copy((float *)(void *)(bytes + 0x6c), (const float *)(const void *)(bytes + 0x3c));
}

static void CSelectTitleCoordinator_Create(
    CSelectTitleFlow *flow,
    int objectGroupHandle,
    int childIndex,
    unsigned char groupId,
    int bindMode) {
    if (flow == 0 || *(int *)(void *)(flow->storage + 0x04) != 0) {
        return;
    }

    CSelectTitleModelFocus_Init(&gCSelectTitleModelFocus);
    *(int *)(void *)(flow->storage + 0x04) = RuntimePointerBits(&gCSelectTitleModelFocus);
    CSelectTitleModelFocus_BindObject(
        &gCSelectTitleModelFocus,
        objectGroupHandle,
        childIndex,
        groupId,
        bindMode);
}

static void CSelectTitleCoordinator_ApplyVectors(
    CSelectTitleFlow *flow,
    double duration,
    const float *forwardVector,
    const float *upVector,
    const float *translation) {
    CSelectTitleModelFocus *focus;

    if (flow == 0 || *(int *)(void *)(flow->storage + 0x04) == 0) {
        return;
    }

    focus = (CSelectTitleModelFocus *)RuntimePointerFromBits(*(int *)(void *)(flow->storage + 0x04));
    CSelectTitleModelFocus_SetVectors(focus, duration, forwardVector, upVector, translation);
    CSelectTitleModelFocus_BuildInitialMatrix(focus);
}

static void CSelectTitleCoordinator_ClearModelGroup(CSelectTitleFlow *flow) {
    if (flow == 0) {
        return;
    }

    if (*(int *)(void *)(flow->storage + 0x128) != -1) {
        CzanModelManager_ClearLiveObjects(gGameMainManagers.manager802e70b8);
        *(int *)(void *)(flow->storage + 0x128) = -1;
    }
}

static void CSelectTitleCoordinator_TickDraw(CSelectTitleFlow *flow) {
    CSelectTitleModelFocus *focus;
    unsigned char *bytes;
    int *owner;

    if (flow == 0 || *(int *)(void *)(flow->storage + 0x04) == 0) {
        return;
    }

    focus = (CSelectTitleModelFocus *)RuntimePointerFromBits(*(int *)(void *)(flow->storage + 0x04));
    if (focus == 0) {
        return;
    }

    bytes = focus->storage;
    owner = (int *)(void *)(bytes + 0xb0);
    CzanModelOwner_UpdateCurrentMatrix(owner, 0);
    CzanModelOwner_CopyCurrentModelMatrix(owner, bytes + 0x3c);
    Matrix34_Copy((float *)(void *)(bytes + 0x6c), (const float *)(const void *)(bytes + 0x3c));
    CzanModelManager_UpdateVisibleGroup(
        gGameMainManagers.manager802e70b8,
        0,
        1,
        (char)bytes[0xac]);
}

static int CSelect_HostConfirmPressed(void) {
    return InputOrMenuStateManager_IsConfirmPressed(gGameMainManagers.inputOrMenuStateManager, 4) != 0 ||
           Platform_ConsumeConfirmPressed() != 0;
}

static void CSelect_StartObjectGroupAnimation(int objectGroupHandle, int animationIndex, int arg2, int arg3) {
    int uiManager = RuntimePointerBits(GlobalRuntimeContext_GetPointerAt(0x270));

    CzanUiManager_SetObjectGroupAnimationMode(uiManager, objectGroupHandle, (unsigned char)arg3);
    CzanUiManager_StartObjectGroupAnimation(0.0, uiManager, objectGroupHandle, animationIndex);
    CzanUiManager_SetObjectGroupAnimationResetMode(uiManager, objectGroupHandle, (unsigned char)arg2);
}

static void CSelectTitleFlow_LoadSelTitle(CSelectTitleFlow *flow, void *linkData, unsigned int linkSize) {
    CzanLinkBlock block;
    int group0;
    int group1;
    int group2;
    CSelModeEntryKnownFields *entry0;
    CSelModeEntryKnownFields *entry1;
    CSelModeEntryKnownFields *entry2;

    if (flow == 0 || linkData == 0 || !CzanLinkResource_IsValid(linkData, linkSize)) {
        return;
    }

    /* 0x800E140C consumes selTitle +0x10. The first three blocks become Czan UI
       object groups stored at +0x1C/+0x20/+0x24. Blocks 3..5 are the title call
       CSelModeEntry objects at +0x16C/+0x1BC/+0x20C; without those, the host drew
       raw group textures instead of the game's title state objects. */
    entry0 = (CSelModeEntryKnownFields *)(void *)(flow->storage + 0x16c);
    entry1 = (CSelModeEntryKnownFields *)(void *)(flow->storage + 0x1bc);
    entry2 = (CSelModeEntryKnownFields *)(void *)(flow->storage + 0x20c);

    if (CzanLinkResource_GetBlock(linkData, linkSize, 3, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CSelModeEntry_AddUiObject(entry0, (void *)block.data);
    }
    if (CzanLinkResource_GetBlock(linkData, linkSize, 4, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CSelModeEntry_AddUiObject(entry1, (void *)block.data);
    }
    if (CzanLinkResource_GetBlock(linkData, linkSize, 5, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        CSelModeEntry_AddUiObject(entry2, (void *)block.data);
    }
    CSelModeEntry_SetObjectEnabled(entry1, 0, -1, 1);
    CSelModeEntry_SetObjectFlags(entry1, 0, 0xffffffc4);
    CSelModeEntry_ActivateObject(entry1, 0);
    CSelModeEntry_SetObjectFlags(entry0, 0, 0xffffffe2);
    CSelModeEntry_ActivateObject(entry0, 0);

    if (CzanLinkResource_GetBlock(linkData, linkSize, 0, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        group0 = CzanUiManager_CreateObjectGroup(0, (void *)block.data, 2, 0);
        *(int *)(void *)(flow->storage + 0x1c) = group0;
    }
    if (CzanLinkResource_GetBlock(linkData, linkSize, 1, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        group1 = CzanUiManager_CreateObjectGroup(0, (void *)block.data, 0, 0);
        *(int *)(void *)(flow->storage + 0x20) = group1;
    }
    if (CzanLinkResource_GetBlock(linkData, linkSize, 2, &block)) {
        HostCzan_RegisterLinkSize(block.data, block.size);
        group2 = CzanUiManager_CreateObjectGroup(0, (void *)block.data, 0, 0);
        *(int *)(void *)(flow->storage + 0x24) = group2;
    }

    if (entry0->objectHandles[0] >= 0) {
        static const float titleFocusForward[3] = { 0.0f, 0.0f, 1.0f };
        static const float titleFocusUp[3] = { 0.0f, 1.0f, 0.0f };
        static const float titleFocusTranslation[3] = { 0.0f, 0.0f, 0.0f };

        CSelectTitleCoordinator_Create(flow, entry0->objectHandles[0], 0, 0, 0);
        CSelectTitleCoordinator_ApplyVectors(
            flow,
            0.0,
            titleFocusForward,
            titleFocusUp,
            titleFocusTranslation);
    }

    /* With the constructor's +0x0C defaulting to 0, the original enters state 3
       and starts animation 0 on the group stored at +0x20. */
    *(int *)(void *)(flow->storage + 0x144) = 3;
    group0 = *(int *)(void *)(flow->storage + 0x1c);
    group2 = *(int *)(void *)(flow->storage + 0x24);
    if (group0 >= 0) {
        CzanUiManager_SetObjectGroupDisplayFlags(0, group0, 1, 1);
    }
    if (group2 >= 0) {
        CzanUiManager_SetObjectGroupDisplayFlags(0, group2, 1, 1);
    }
    group1 = *(int *)(void *)(flow->storage + 0x20);
    if (group1 >= 0) {
        CSelect_StartObjectGroupAnimation(group1, 0, 0, 0);
    }
    if (entry0->objectHandles[0] >= 0 && entry2->objectHandles[0] >= 0) {
        CzanUiManager_LinkObjectGroupToReferenceObject(0, entry2->objectHandles[0], entry0->objectHandles[0], 0, 0x1f);
        CzanUiManager_SetObjectGroupPriority(0, entry2->objectHandles[0], 0x14);
        CzanUiManager_SetObjectGroupAnimationResetMode(0, entry2->objectHandles[0], 1);
    }
    RuntimeDebugReport(
        "CSelect selTitle: groups=%d,%d,%d entries=%d,%d,%d\n",
        *(int *)(void *)(flow->storage + 0x1c),
        *(int *)(void *)(flow->storage + 0x20),
        *(int *)(void *)(flow->storage + 0x24),
        entry0->objectHandles[0],
        entry1->objectHandles[0],
        entry2->objectHandles[0]);
}

static void CSelectTitleFlow_Draw(CSelectTitleFlow *flow) {
    int state;
    int movieDrawState;
    int groupHandles[15];
    int groupCount = 0;
    int i;

    if (flow == 0) {
        return;
    }

    state = *(int *)(void *)(flow->storage + 0x144);
    movieDrawState = *(int *)(void *)(flow->storage + 0x130);

    if (gGameMainManagers.uiRootManager != 0) {
        CSelModeEntryKnownFields *entry0 = (CSelModeEntryKnownFields *)(void *)(flow->storage + 0x16c);
        CSelModeEntryKnownFields *entry1 = (CSelModeEntryKnownFields *)(void *)(flow->storage + 0x1bc);
        CSelModeEntryKnownFields *entry2 = (CSelModeEntryKnownFields *)(void *)(flow->storage + 0x20c);
        int rawGroups[3] = {
            *(int *)(void *)(flow->storage + 0x1c),
            *(int *)(void *)(flow->storage + 0x20),
            *(int *)(void *)(flow->storage + 0x24),
        };
        CSelModeEntryKnownFields *entries[3] = {
            entry0,
            entry1,
            entry2,
        };

        for (i = 0; i < 3; i++) {
            if (rawGroups[i] >= 0) {
                groupHandles[groupCount++] = rawGroups[i];
            }
        }
        for (i = 0; i < 3; i++) {
            int slot;
            for (slot = 0; slot < entries[i]->objectHandleCount && slot < 4; slot++) {
                if (entries[i]->objectHandles[slot] >= 0 &&
                    groupCount < (int)(sizeof(groupHandles) / sizeof(groupHandles[0]))) {
                    groupHandles[groupCount++] = entries[i]->objectHandles[slot];
                }
            }
        }
        CzanUiManager_DrawObjectGroupsReverse(0, groupHandles, groupCount);
    }

    if ((state == 10 || state == 0x0b) && movieDrawState != 0 && movieDrawState != 2 &&
        gGameMainManagers.manager802e70a8 != 0) {
        MovieSlotHandle_DrawMovie(
            gGameMainManagers.manager802e70a8,
            gGameMainManagers.manager802e70a8[1],
            0);
    }
}

static double CSelectTitleFlow_GetGroupAnimationDuration(int objectGroupHandle, int animationIndex, double fallbackDuration) {
    return CzanUiManager_GetObjectAnimationDuration(
        fallbackDuration,
        RuntimePointerBits(GlobalRuntimeContext_GetPointerAt(0x270)),
        objectGroupHandle,
        0,
        animationIndex);
}

static void CSelectTitleFlow_StartOpeningMovie(CSelectTitleFlow *flow, int *cSelect) {
    int moviePath;
    int slotIndex;

    if (flow == 0 || cSelect == 0 || *(int *)(void *)(flow->storage + 0x154) != 0) {
        return;
    }

    moviePath = ResourceManager_HostPointerBits("movie/select/OP.thp");
    ResourceSlotHandle_Rebind(gGameMainManagers.manager802e70a8, moviePath, 0);
    slotIndex = gGameMainManagers.manager802e70a8 != 0 ? gGameMainManagers.manager802e70a8[1] : -1;
    cSelect[0x1c10 / 4] = moviePath;
    *(int *)(void *)(flow->storage + 0x154) = 1;
    *(int *)(void *)(flow->storage + 0x130) = 0;
    *(float *)(void *)(flow->storage + 0x148) = 0.0f;
    *(float *)(void *)(flow->storage + 0x134) = 0.0f;
    *(float *)(void *)(flow->storage + 0x138) = 0.0f;
    *(float *)(void *)(flow->storage + 0x13c) = 0.0f;
    *(unsigned int *)(void *)(flow->storage + 0x140) = 0x00000000u;
    RuntimeDebugReport(
        "CSelect title: rebound OP movie slot=%d pending=%d\n",
        slotIndex,
        ResourceSlotHandle_IsActivePending(gGameMainManagers.manager802e70a8));
}

static int CSelectTitleFlow_Tick(CSelectTitleFlow *flow, int *cSelect, int forceIdle) {
    int state;
    int group1;
    float timer;
    double threshold;

    if (flow == 0 || forceIdle == 1) {
        return 0x0c;
    }

    state = *(int *)(void *)(flow->storage + 0x144);
    group1 = *(int *)(void *)(flow->storage + 0x20);

    switch (state) {
        case 3:
            if (group1 >= 0 && CzanUiManager_IsObjectGroupAnimationDone(0, group1) != 0) {
                *(int *)(void *)(flow->storage + 0x144) = 4;
                *(float *)(void *)(flow->storage + 0x148) = 0.0f;
                RuntimeDebugReport("CSelect title: state 3 -> 4\n");
            }
            break;
        case 4:
            timer = *(float *)(void *)(flow->storage + 0x148) + 1.0f;
            *(float *)(void *)(flow->storage + 0x148) = timer;
            threshold = CSelectTitleFlow_GetGroupAnimationDuration(group1, 0, 120.0);
            if (threshold < 1.0) {
                threshold = 120.0;
            }
            if ((double)timer >= threshold) {
                *(int *)(void *)(flow->storage + 0x144) = 5;
                *(int *)(void *)(flow->storage + 0x14c) = 0;
                if (group1 >= 0) {
                    CSelect_StartObjectGroupAnimation(group1, 1, 0, 0);
                }
                RuntimeDebugReport("CSelect title: state 4 -> 5\n");
            }
            break;
        case 5:
            if (group1 >= 0 && CzanUiManager_IsObjectGroupAnimationDone(0, group1) != 0) {
                CzanUiManager_SetObjectGroupDisplayFlags(0, group1, 1, 1);
                *(int *)(void *)(flow->storage + 0x144) = 0x0c;
                *(float *)(void *)(flow->storage + 0x148) = 0.0f;
                UiRootManager_StartTitleTransitionC(gGameMainManagers.uiRootManager, 1, 100, 0);
                UiRootManager_StartTitleTransitionA(gGameMainManagers.uiRootManager, 0, 0x32, 0);
                RuntimeDebugReport("CSelect title: state 5 -> 12\n");
            }
            break;
        case 0x0c:
            if (UiRootManager_IsTitleTransitionAIdle(gGameMainManagers.uiRootManager) != 0 &&
                UiRootManager_IsTitleTransitionCIdle(gGameMainManagers.uiRootManager) != 0) {
                *(int *)(void *)(flow->storage + 0x144) = 10;
                CSelectTitleFlow_StartOpeningMovie(flow, cSelect);
                RuntimeDebugReport("CSelect title: state 12 -> 10\n");
            }
            break;
        case 10:
            timer = *(float *)(void *)(flow->storage + 0x148) + 1.0f;
            *(float *)(void *)(flow->storage + 0x148) = timer;
            if (ResourceSlotHandle_IsActivePending(gGameMainManagers.manager802e70a8) == 0) {
                *(int *)(void *)(flow->storage + 0x144) = 0x0b;
                *(int *)(void *)(flow->storage + 0x130) = -1;
                *(float *)(void *)(flow->storage + 0x134) = 0.0f;
                *(float *)(void *)(flow->storage + 0x138) = 1.0f;
                *(float *)(void *)(flow->storage + 0x13c) = 1.0f;
                if (gGameMainManagers.manager802e70a8 != 0) {
                    MovieSlotHandle_StartPlayback(
                        gGameMainManagers.manager802e70a8,
                        gGameMainManagers.manager802e70a8[1],
                        0);
                }
                RuntimeDebugReport("CSelect title: state 10 -> 11 OP movie playback requested\n");
            }
            else if (CSelect_HostConfirmPressed() != 0) {
                *(int *)(void *)(flow->storage + 0x144) = 6;
                UiRootManager_StartTitleTransitionC(gGameMainManagers.uiRootManager, 1, 100, 0);
                UiRootManager_StartTitleTransitionB(gGameMainManagers.uiRootManager, 1, 0x32, 0);
                RuntimeDebugReport("CSelect title: state 10 -> 6\n");
            }
            break;
        case 0x0b:
            if ((gGameMainManagers.manager802e70a8 != 0 &&
                 MovieSlotHandle_HasPlaybackStarted(
                     gGameMainManagers.manager802e70a8,
                     gGameMainManagers.manager802e70a8[1]) == 0) ||
                CSelect_HostConfirmPressed() != 0) {
                ResourceSlotHandle_Release(gGameMainManagers.manager802e70a8);
                *(int *)(void *)(flow->storage + 0x144) = 6;
                UiRootManager_StartTitleTransitionC(gGameMainManagers.uiRootManager, 1, 100, 0);
                UiRootManager_StartTitleTransitionB(gGameMainManagers.uiRootManager, 1, 0x32, 0);
                RuntimeDebugReport("CSelect title: state 11 -> 6 OP movie complete/skip\n");
            }
            break;
        case 6:
            if (UiRootManager_IsTitleTransitionAIdle(gGameMainManagers.uiRootManager) != 0) {
                int entry2Handle;
                CSelModeEntryKnownFields *entry1;

                entry1 = (CSelModeEntryKnownFields *)(void *)(flow->storage + 0x1bc);
                *(int *)(void *)(flow->storage + 0x144) = 0x0d;
                CSelModeEntry_StartObjectAnimation(0.0, flow->storage + 0x16c, 0, 0, 0, 0);
                CSelModeEntry_StartObjectAnimation(0.0, flow->storage + 0x20c, 0, 0, 0, 0);
                *(float *)(void *)(flow->storage + 0x148) = 0.0f;
                entry2Handle = CSelModeEntry_GetObjectHandle(flow->storage + 0x20c, 0);
                if (entry2Handle >= 0) {
                    CzanUiManager_SetObjectGroupAnimationResetMode(0, entry2Handle, 1);
                }
                if (entry1->objectHandleCount > 0 && entry1->objectHandles[0] >= 0) {
                    CSelModeEntry_SetObjectEnabled(entry1, 0, -1, 1);
                }
                *(int *)(void *)(flow->storage + 0x260) = 0;
                RuntimeDebugReport("CSelect title: state 6 -> 13 title call\n");
            }
            break;
        case 0x0d:
            if (CSelect_HostConfirmPressed() != 0) {
                RuntimeDebugReport("CSelect title: state 13 -> 1 mode select\n");
                return 1;
            }
            break;
        default:
            break;
    }

    UiRootManager_UpdateGlobalCzanListOnce(gGameMainManagers.uiRootManager, 0);
    if (gGameMainManagers.uiRootManager != 0) {
        gGameMainManagers.uiRootManager[0x28 / 4] = 0;
    }
    CSelectTitleCoordinator_TickDraw(flow);
    return 0x0c;
}

static int CSelect_IsResourceReady(int resourceBits) {
    ResourceHandle *handle = (ResourceHandle *)RuntimePointerFromBits(resourceBits);
    return handle != 0 && handle->loaded;
}

static int CSelect_RequestResourceIfMissing(int *cSelect, int wordIndex, const char *path) {
    if (cSelect[wordIndex] != 0) {
        return CSelect_IsResourceReady(cSelect[wordIndex]);
    }
    return CSelect_StoreLoadedResource(cSelect, wordIndex, path);
}

static void *CSelect_GetLoadedResourcePayload(int resourceBits, unsigned int *outSize) {
    ResourceHandle *handle = (ResourceHandle *)RuntimePointerFromBits(resourceBits);
    unsigned char *data;

    if (outSize != 0) {
        *outSize = 0;
    }
    if (handle == 0 || handle->loaded == 0 || handle->data == 0 || handle->size <= 0xa0) {
        return 0;
    }

    data = (unsigned char *)handle->data + 0xa0;
    if (outSize != 0) {
        *outSize = (unsigned int)handle->size - 0xa0u;
    }
    return data;
}

static void *CSelect_GetLoadedResourceData(int resourceBits, unsigned int *outSize) {
    ResourceHandle *handle = (ResourceHandle *)RuntimePointerFromBits(resourceBits);

    if (outSize != 0) {
        *outSize = 0;
    }
    if (handle == 0 || handle->loaded == 0 || handle->data == 0 || handle->size <= 0) {
        return 0;
    }

    if (outSize != 0) {
        *outSize = (unsigned int)handle->size;
    }
    return handle->data;
}

static void CSelect_ActivateSelectBinBridge(int *cSelect, int regionIndex) {
    void *linkData;
    unsigned int linkSize;
    unsigned int blockCount;
    unsigned int blockIndex;
    CzanLinkBlock block;

    if (cSelect == 0 || cSelect[0x10 / 4] != 0) {
        return;
    }

    linkData = CSelect_GetLoadedResourceData(cSelect[0x1bfc / 4], &linkSize);
    if (linkData == 0 || !CzanLinkResource_IsValid(linkData, linkSize)) {
        return;
    }

    blockCount = CzanLinkResource_GetBlockCount(linkData, linkSize);
    for (blockIndex = 0; blockIndex + 0x11 < blockCount; blockIndex++) {
        if (CzanLinkResource_GetBlock(linkData, linkSize, blockIndex, &block)) {
            cSelect[(0x44 / 4) + (int)blockIndex] = (int)CreateTextureFromTplResource(
                (TextureManagerKnownFields *)GlobalRuntimeContext_GetPointerAt(0x26c),
                (void *)block.data,
                (int)block.size,
                0xFFFFFFFFu);
        }
    }

    cSelect[0x1bf8 / 4] = -1;
    cSelect[0x14 / 4] = 0;
    cSelect[0x10 / 4] = 1;
    if (gCSelectHostBootPhase == CSELECT_HOST_BOOT_PHASE_WAIT_SELECT_BIN) {
        cSelect[0x1bf0 / 4] = 0;
        cSelect[0x3c / 4] = 0;
        gCSelectHostBootPhase = CSELECT_HOST_BOOT_PHASE_BOOT_FLOW;
        gCSelectHostBootPhaseTicks = 0;
        RuntimeDebugReport("CSelect boot: select_bin bridge active, entering boot title flow\n");
    }
}

static void CSelect_TickHostBootSequence(int *cSelect, int regionIndex) {
    if (cSelect == 0 || gCSelectHostBootPhase == CSELECT_HOST_BOOT_PHASE_NONE) {
        return;
    }

    (void)regionIndex;
    gCSelectHostBootPhaseTicks++;

    if (gCSelectHostBootPhase == CSELECT_HOST_BOOT_PHASE_WAIT_SEL_TITLE &&
        cSelect[0x18 / 4] != 0) {
        gCSelectHostBootPhase = CSELECT_HOST_BOOT_PHASE_TITLE_READY;
        gCSelectHostBootPhaseTicks = 0;
        cSelect[0x1bf0 / 4] = 0;
        cSelect[0x3c / 4] = 0x0c;
        RuntimeDebugReport("CSelect boot: selTitle ready, entering title flow\n");
    }
}

static void CSelect_DrawSelectCommonBackground(void) {
    if (gGameMainManagers.characterAssetManager == 0) {
        return;
    }
    CSelectCommon_DrawHostBackground(
        (int *)((unsigned char *)gGameMainManagers.characterAssetManager + 0x28168));
}

static void CSelect_UpdateSelectCommonBackground(int *cSelect) {
    int forceInitialBind;

    if (gGameMainManagers.characterAssetManager == 0) {
        return;
    }

    /* Retail CSelect_Tick updates select_cmn before ticking/drawing the active
       sub-screen. The draw helper should only render the prepared background. */
    forceInitialBind = cSelect != 0 && cSelect[0x38 / 4] == 0x11;
    CSelectCommon_UpdateMovieBackground(
        (int *)((unsigned char *)gGameMainManagers.characterAssetManager + 0x28168),
        0,
        1,
        forceInitialBind);
}

static int CSelectPlayerCountFlow_Init(CSelectPlayerCountFlow *flow) {
    int i;
    int *words;

    if (flow == 0) {
        return 0;
    }

    /* 0x8008C11C constructs the state-2 select screen. It starts from the shared
       CSelect screen base at 0x8008CF54, then initializes a six-entry
       CSelModeEntry list at +0x150. */
    ClearMemory(flow->storage, 0, sizeof(flow->storage));
    words = (int *)(void *)flow->storage;
    words[0x4b] = RuntimePointerBits((void *)0x802b9ee8);
    words[2] = -1;
    words[3] = -1;
    words[5] = -1;
    words[6] = -1;
    for (i = 7; i <= 0x46; i++) {
        words[i] = -1;
    }
    words[0x49] = -1;
    words[0x4a] = -1;
    *(int *)(void *)(flow->storage + 300) = RuntimePointerBits((void *)0x802b9eb8);
    for (i = 0; i < 6; i++) {
        CSelModeEntry_Init(flow->storage + 0x150 + i * CSEL_MODE_ENTRY_SIZE);
    }
    *(int *)(void *)(flow->storage + 0x13c) = RuntimePointerBits(GlobalRuntimeContext_GetPointerAt(0x270));
    *(int *)(void *)(flow->storage + 0x140) = RuntimePointerBits(gGameMainManagers.manager802e70b4);
    *(int *)(void *)(flow->storage + 0x130) =
        RuntimePointerBits((unsigned char *)gGameMainManagers.characterAssetManager + 0x28168);
    *(int *)(void *)(flow->storage + 0x134) = -1;
    *(int *)(void *)(flow->storage + 0x144) = 0;
    *(int *)(void *)(flow->storage + 0x34c) = 0;
    return 1;
}

static int CSelectPlayerCountFlow_ReadMaxPlayers(CSelectPlayerCountFlow *flow) {
    int *parentSelectData;
    int maxPlayers;

    if (flow == 0) {
        return 1;
    }
    parentSelectData = (int *)RuntimePointerFromBits(*(int *)(void *)(flow->storage + 0x10));
    maxPlayers = 4;
    if (parentSelectData != 0 && parentSelectData[0] == 3) {
        maxPlayers = 1;
    }
    if (maxPlayers < 1) {
        maxPlayers = 1;
    }
    if (maxPlayers > 4) {
        maxPlayers = 4;
    }
    return maxPlayers;
}

static void CSelectPlayerCountFlow_OnEnter(CSelectPlayerCountFlow *flow, void *linkData, unsigned int linkSize) {
    CzanLinkBlock block;
    int sharedGroup;
    int referenceHandle;
    int i;
    int maxPlayers;
    int selectedPlayers;
    int *parentSelectData;

    if (flow == 0 || linkData == 0 || !CzanLinkResource_IsValid(linkData, linkSize)) {
        return;
    }

    /* 0x8008C254 consumes select_bin block 1. Block 0 is the shared header;
       block 1 is cloned into the five player-count/options entries. */
    if (CzanLinkResource_GetBlock(linkData, linkSize, 0, &block)) {
        CSelModeEntry_AddUiObject(flow->storage + 0x150, (void *)block.data);
    }
    sharedGroup = -1;
    if (CzanLinkResource_GetBlock(linkData, linkSize, 1, &block)) {
        sharedGroup = CSelModeEntry_AddUiObject(flow->storage + 0x1a0, (void *)block.data);
    }
    for (i = 1; i < 5; i++) {
        CSelModeEntry_AddChildUiObject(flow->storage + 0x150 + (i + 1) * CSEL_MODE_ENTRY_SIZE, sharedGroup);
    }

    referenceHandle = CSelModeEntry_GetObjectHandle(flow->storage + 0x150, 0);
    for (i = 0; i < 5; i++) {
        int entryHandle = CSelModeEntry_GetObjectHandle(flow->storage + 0x150 + (i + 1) * CSEL_MODE_ENTRY_SIZE, 0);
        CzanUiManager_LinkObjectGroupToReferenceObject(0, entryHandle, referenceHandle, i + 5, 0x1f);
        CSelModeEntry_PlayObject(flow->storage + 0x150 + (i + 1) * CSEL_MODE_ENTRY_SIZE, 0, 0, i, 0);
        {
            int triplet[3] = { 0, i, 0 };
            CSelModeEntry_SetTransformTriplet(flow->storage + 0x150 + (i + 1) * CSEL_MODE_ENTRY_SIZE, triplet);
        }
    }

    parentSelectData = (int *)RuntimePointerFromBits(*(int *)(void *)(flow->storage + 0x10));
    maxPlayers = CSelectPlayerCountFlow_ReadMaxPlayers(flow);
    selectedPlayers = parentSelectData != 0 ? parentSelectData[3] : 1;
    if (selectedPlayers < 1) {
        selectedPlayers = 1;
    }
    if (selectedPlayers > maxPlayers) {
        selectedPlayers = maxPlayers;
    }
    *(int *)(void *)(flow->storage + 0x148) = maxPlayers;
    *(int *)(void *)(flow->storage + 0x14c) = selectedPlayers;
    *(int *)(void *)(flow->storage + 0x138) = selectedPlayers - 1;

    for (i = 0; i < 4; i++) {
        *(int *)(void *)(flow->storage + 0x330 + i * 4) = i < maxPlayers ? 1 : 0;
    }
    *(int *)(void *)(flow->storage + 0x340) = 1;
    *(int *)(void *)(flow->storage + 0x344) = 0;
    RuntimeDebugReport(
        "CSelect state2: player-count screen max=%d selected=%d\n",
        maxPlayers,
        selectedPlayers);
}

static int CSelectPlayerCountFlow_Update(CSelectPlayerCountFlow *flow, int firstFrame) {
    int state;
    int selectedIndex;
    int oldSelectedIndex;
    int maxPlayers;
    int confirmPressed;
    int backPressed;
    int i;
    int *inputManager;
    int *parentSelectData;

    if (flow == 0) {
        return 2;
    }

    inputManager = gGameMainManagers.inputOrMenuStateManager;
    state = *(int *)(void *)(flow->storage + 0x134);
    selectedIndex = *(int *)(void *)(flow->storage + 0x138);
    maxPlayers = CSelectPlayerCountFlow_ReadMaxPlayers(flow);
    *(int *)(void *)(flow->storage + 0x148) = maxPlayers;

    if (firstFrame != 0) {
        return 2;
    }

    if (state == -1) {
        *(int *)(void *)(flow->storage + 0x134) = 0;
        return 2;
    }
    if (state == 0) {
        CSelModeEntry_StartObjectAnimation(0.0, flow->storage + 0x150, 0, 0, 0, 0);
        for (i = 0; i < 5; i++) {
            CSelModeEntry_StartObjectAnimation(
                0.0,
                flow->storage + 0x150 + (i + 1) * CSEL_MODE_ENTRY_SIZE,
                0,
                0,
                0,
                0);
        }
        *(int *)(void *)(flow->storage + 0x134) = 1;
        return 2;
    }
    if (state == 1) {
        if (CSelModeEntry_IsObjectAnimationDone(flow->storage + 0x150, 0) != 0) {
            *(int *)(void *)(flow->storage + 0x134) = 2;
        }
        return 2;
    }
    if (state == 2) {
        oldSelectedIndex = selectedIndex;
        if (InputOrMenuStateManager_TestTriggeredMask(inputManager, 4, 2) != 0) {
            selectedIndex = (selectedIndex + 1) % 5;
        }
        else if (InputOrMenuStateManager_TestTriggeredMask(inputManager, 4, 1) != 0) {
            selectedIndex = (selectedIndex + 4) % 5;
        }
        if (selectedIndex != oldSelectedIndex) {
            *(int *)(void *)(flow->storage + 0x138) = selectedIndex;
            CSelModeEntry_StartObjectAnimation(
                0.0,
                flow->storage + 0x150 + (oldSelectedIndex + 1) * CSEL_MODE_ENTRY_SIZE,
                0,
                0,
                0,
                0);
            CSelModeEntry_StartObjectAnimation(
                0.0,
                flow->storage + 0x150 + (selectedIndex + 1) * CSEL_MODE_ENTRY_SIZE,
                0,
                1,
                0,
                0);
        }
        for (i = 0; i < 5; i++) {
            int anim = i == selectedIndex ? 1 : 0;
            if (CSelModeEntry_GetCachedAnimationId(flow->storage + 0x150 + (i + 1) * CSEL_MODE_ENTRY_SIZE, 0) != anim) {
                CSelModeEntry_StartObjectAnimation(
                    0.0,
                    flow->storage + 0x150 + (i + 1) * CSEL_MODE_ENTRY_SIZE,
                    0,
                    anim,
                    0,
                    0);
            }
        }

        confirmPressed = (int)InputOrMenuStateManager_IsConfirmPressed(inputManager, 4);
        backPressed = (int)InputOrMenuStateManager_IsBackPressed(inputManager, 4);
        if (confirmPressed != 0) {
            if (selectedIndex < maxPlayers || selectedIndex == 4) {
                CSelModeEntry_StartObjectAnimation(
                    0.0,
                    flow->storage + 0x150 + (selectedIndex + 1) * CSEL_MODE_ENTRY_SIZE,
                    0,
                    2,
                    0,
                    0);
                *(int *)(void *)(flow->storage + 0x144) = 1;
                *(int *)(void *)(flow->storage + 0x134) = 3;
            }
        }
        else if (backPressed != 0) {
            *(int *)(void *)(flow->storage + 0x144) = 0;
            *(int *)(void *)(flow->storage + 0x134) = 3;
        }
        for (i = 0; i < 5; i++) {
            CSelModeEntry_ActivateObject(flow->storage + 0x150 + (i + 1) * CSEL_MODE_ENTRY_SIZE, 0);
        }
        return 2;
    }
    if (state == 3) {
        *(int *)(void *)(flow->storage + 0x134) = 4;
        return 2;
    }
    if (state == 4) {
        if (*(int *)(void *)(flow->storage + 0x144) == 1) {
            CSelModeEntry_StartObjectAnimation(0.0, flow->storage + 0x150, 0, 1, 0, 0);
        }
        else {
            CSelModeEntry_ResetObjectAnimation(flow->storage + 0x150, 0);
            CSelModeEntry_StartObjectAnimation(0.0, flow->storage + 0x150, 0, 0, 0, 3);
        }
        *(int *)(void *)(flow->storage + 0x134) = 5;
        return 2;
    }
    if (state == 5 && CSelModeEntry_IsObjectAnimationDone(flow->storage + 0x150, 0) != 0) {
        parentSelectData = (int *)RuntimePointerFromBits(*(int *)(void *)(flow->storage + 0x10));
        if (*(int *)(void *)(flow->storage + 0x144) == 1) {
            if (selectedIndex == 4) {
                return 9;
            }
            if (parentSelectData != 0) {
                parentSelectData[3] = selectedIndex + 1;
                if (parentSelectData[1] == 3) {
                    parentSelectData[4] = parentSelectData[3] > 1 ? 2 : 0;
                    return 5;
                }
            }
            return 0x1b;
        }
        if (parentSelectData != 0 && parentSelectData[0] == 3) {
            return 0x17;
        }
        return 1;
    }

    return 2;
}

static void CSelectPlayerCountFlow_Draw(CSelectPlayerCountFlow *flow) {
    int entryIndex;
    int groupHandles[6];
    int groupCount = 0;

    if (flow == 0) {
        return;
    }
    CzanUiManager_UpdateObjectList(0);
    for (entryIndex = 0; entryIndex < 6; entryIndex++) {
        int groupHandle = CSelModeEntry_GetObjectHandle(
            flow->storage + 0x150 + entryIndex * CSEL_MODE_ENTRY_SIZE,
            0);
        if (groupHandle >= 0) {
            groupHandles[groupCount++] = groupHandle;
        }
    }
    CzanUiManager_DrawObjectGroupsReverse(0, groupHandles, groupCount);
}

static int CSelect_CreateActiveScreen(int *cSelect) {
    static unsigned char cselModeStorage[0x5c0];
    static CSelectBootTitleFlow bootTitleFlowStorage;
    static CSelectTitleFlow titleFlowStorage;
    static CSelectPlayerCountFlow playerCountFlowStorage;
    void *linkData;
    unsigned int linkSize;
    CzanLinkBlock block;
    int nextSelectState;
    static int lastMissingState = -999;

    if (cSelect == 0 || cSelect[0x1bf0 / 4] != 0) {
        return 0;
    }

    nextSelectState = cSelect[0x3c / 4];
    switch (nextSelectState) {
        case 0:
            CSelectBootTitleFlow_Init(&bootTitleFlowStorage);
            cSelect[0x1bf0 / 4] = RuntimePointerBits(&bootTitleFlowStorage);
            cSelect[0x38 / 4] = nextSelectState;
            return 1;
        case 1:
            linkData = CSelect_GetLoadedResourcePayload(cSelect[0x1bfc / 4], &linkSize);
            if (linkData == 0) {
                return 0;
            }

            if (!CzanLinkResource_GetBlock(linkData, linkSize, 0, &block)) {
                return 0;
            }

            HostCzan_RegisterLinkSize(block.data, block.size);
            CSelMode_SetHostLinkResourceSize(block.size);
            CSelMode_Init(cselModeStorage);
            *(int *)(void *)(cselModeStorage + 0x10) = RuntimePointerBits(cSelect + (0xc4 / 4));
            cSelect[0x1bf0 / 4] = RuntimePointerBits(cselModeStorage);
            CSelMode_OnEnter(cselModeStorage, (void *)block.data);
            cSelect[0x38 / 4] = nextSelectState;
            return 1;
        case 0x0c:
            linkData = CSelect_GetLoadedResourceData(cSelect[0x1c04 / 4], &linkSize);
            if (linkData == 0) {
                return 0;
            }

            CSelectTitleFlow_Init(&titleFlowStorage);
            CSelectTitleFlow_LoadSelTitle(&titleFlowStorage, linkData, linkSize);
            cSelect[0x1bf0 / 4] = RuntimePointerBits(&titleFlowStorage);
            cSelect[0x38 / 4] = nextSelectState;
            RuntimeDebugReport("CSelect: created selTitle active screen\n");
            return 1;
        case 2:
            linkData = CSelect_GetLoadedResourcePayload(cSelect[0x1bfc / 4], &linkSize);
            if (linkData == 0) {
                return 0;
            }

            if (!CzanLinkResource_GetBlock(linkData, linkSize, 1, &block)) {
                return 0;
            }

            CSelectPlayerCountFlow_Init(&playerCountFlowStorage);
            *(int *)(void *)(playerCountFlowStorage.storage + 0x10) =
                RuntimePointerBits(cSelect + (0xc4 / 4));
            cSelect[0x1bf0 / 4] = RuntimePointerBits(&playerCountFlowStorage);
            CSelectPlayerCountFlow_OnEnter(&playerCountFlowStorage, (void *)block.data, block.size);
            cSelect[0x38 / 4] = nextSelectState;
            RuntimeDebugReport("CSelect: created player-count active screen\n");
            return 1;
        default:
            if (lastMissingState != nextSelectState) {
                RuntimeDebugReport(
                    "CSelect: missing active-screen constructor for state %d\n",
                    nextSelectState);
                lastMissingState = nextSelectState;
            }
            return 0;
    }
}

int CSelect_Init(void *cSelect) {
    unsigned char *bytes = (unsigned char *)cSelect;
    int *words = (int *)cSelect;
    int offset;

    /* 0x80045AFC / CSelect_Init: module ID 2 is a 0x1C18 object with a
       CSelect vtable, not the host CSelMode debug struct. */
    ClearMemory(bytes, 0, CSELECT_MODULE_SIZE);
    words[1] = RuntimePointerBits((void *)0x802b95f0);
    for (offset = 0x44; offset <= 0x90; offset += 4) {
        *(int *)(bytes + offset) = -1;
    }
    ClearMemory(bytes + 0xc4, 0, 0x910);
    ClearMemory(bytes + 0x9d4, 0, 0x121c);
    words[0x38 / 4] = -1;
    words[0x40 / 4] = -1;
    words[0x3c / 4] = 0;
    words[0x94 / 4] = 0;
    words[0x1bf8 / 4] = -1;
    return (int)(intptr_t)cSelect;
}

int CSelect_OnEnter(int *cSelect, int moduleId) {
    int regionIndex = CSelect_GetRegionIndex();
    int playerDataValue = 0;
    int *bootResourceBundle;

    /* 0x80045B8C / CSelect_OnEnter. This keeps the original CSelect fields and
       resource handles; screen-specific constructors are still called by Tick. */
    ClearMemory((unsigned char *)cSelect + 0xc4, 0, 0x910);
    gCSelectHostBootPhase = CSELECT_HOST_BOOT_PHASE_NONE;
    gCSelectHostBootPhaseTicks = 0;
    cSelect[0x1bf8 / 4] = -1;
    if (gGameMainManagers.playerDataManager != 0) {
        playerDataValue = PlayerDataState_GetCurrentValue(gGameMainManagers.playerDataManager);
    }
    cSelect[0xc4 / 4] = playerDataValue;

    CharacterAssetManager_ResetSelectCommonState(gGameMainManagers.characterAssetManager);
    bootResourceBundle = gGameMainManagers.manager802e70a4;
    if (bootResourceBundle != 0 &&
        (gGameMainManagers.uiRootManager == 0 || gGameMainManagers.uiRootManager[0] == 0)) {
        if (bootResourceBundle[0] == -1) {
            bootResourceBundle[0] = 0;
            bootResourceBundle[1] = 0;
        }
        BootResourceBundle_StartLoading(bootResourceBundle);
        BootResourceBundle_ApplyLoadedResources(bootResourceBundle);
    }

    cSelect[0x10] = -1;
    CSelect_StoreLoadedResource(cSelect, 0x703, CSelectResourcePaths[regionIndex][4]);
    cSelect[0xb] = 1;

    if (cSelect[0] == MODULE_ID_CGAME) {
        if (cSelect[0x32] == 6) {
            cSelect[0xe] = 0x10;
            cSelect[0xf] = 0x10;
            CSelect_StoreLoadedResource(cSelect, 0x700, CSelectResourcePaths[regionIndex][0]);
            cSelect[3] = 1;
        }
        else if (cSelect[0x31] == 8) {
            cSelect[0xe] = 0x0c;
            cSelect[0xf] = 0x0c;
            CSelect_StoreLoadedResource(cSelect, 0x701, CSelectResourcePaths[regionIndex][2]);
            cSelect[7] = 1;
        }
        else {
            cSelect[0xe] = 0x0e;
            cSelect[0xf] = 0x0e;
            RuntimeMemory_SetCriticalFlag(1);
            CSelect_StoreLoadedResource(cSelect, 0x702, CSelectResourcePaths[regionIndex][3]);
            RuntimeMemory_ClearCriticalFlag();
            cSelect[9] = 1;
        }
    }
    else if (cSelect[0] == MODULE_ID_INVALID_ASSERT) {
        CSelect_StoreLoadedResource(cSelect, 0x700, CSelectResourcePaths[regionIndex][0]);
        cSelect[3] = 1;
        cSelect[0xe] = 0x0d;
        cSelect[0xf] = cSelect[0xe];
    }
    else if (playerDataValue == 8) {
        cSelect[0xe] = 0x0c;
        cSelect[0xf] = 0x0c;
        CSelect_StoreLoadedResource(cSelect, 0x701, CSelectResourcePaths[regionIndex][2]);
        cSelect[7] = 1;
    }
    else {
        cSelect[0xe] = 0;
        cSelect[0xf] = 0;
        /* Keep the normal boot path staged like FUN_800470E0: select_bin first,
           then selTitle. The OP movie is started later by FUN_800E1988 state 0x0C. */
        cSelect[3] = 1;
        gCSelectHostBootPhase = CSELECT_HOST_BOOT_PHASE_WAIT_SELECT_BIN;
    }

    cSelect[0x6fc] = 0;
    cSelect[0x6fd] = 0;
    cSelect[0x6fe] = 0;
    cSelect[0x704] = 0;
    cSelect[0x705] = 0;
    cSelect[0x26] = -1;
    cSelect[0x27] = 0;
    (void)moduleId;
    return moduleId;
}

int CSelect_Tick(int *cSelect) {
    int regionIndex;
    int *resourceManager260;

    if (cSelect == 0) {
        return 1;
    }

    regionIndex = CSelect_GetRegionIndex();
    resourceManager260 = GlobalRuntimeContext_GetPointerAt(0x260);

    /* 0x80045C80 / CSelect_Tick resource gate. This follows the recovered load
       flags before handing off to the active-screen switch at +0x1BF0. */
    if (cSelect[7] == 1 && CSelect_IsResourceReady(cSelect[0x701])) {
        cSelect[7] = 0;
        cSelect[6] = 1;
    }
    if (cSelect[9] == 1 && CSelect_IsResourceReady(cSelect[0x702])) {
        cSelect[9] = 0;
        cSelect[8] = 1;
    }

    if (cSelect[0x0c / 4] == 1 && GlobalResourceManager260_UpdateProgress(resourceManager260) == 1 && cSelect[2] == 0) {
        cSelect[0x0c / 4] = 0;
        cSelect[2] = 1;
        if (cSelect[0x10 / 4] == 0) {
            RuntimeMemory_SetCriticalFlag(1);
            CSelect_StoreLoadedResource(cSelect, 0x6ff, CSelectResourcePaths[regionIndex][1]);
            RuntimeMemory_ClearCriticalFlag();
            cSelect[0x14 / 4] = 1;
        }
    }

    if (cSelect[0x14 / 4] == 1 && CSelect_IsResourceReady(cSelect[0x6ff])) {
        CSelect_ActivateSelectBinBridge(cSelect, regionIndex);
        CSelect_CreateActiveScreen(cSelect);
    }

    CSelect_TickHostBootSequence(cSelect, regionIndex);

    if (cSelect[0x10 / 4] == 1 && cSelect[0x1bf0 / 4] == 0) {
        CSelect_CreateActiveScreen(cSelect);
    }

    if (cSelect[0x1bf0 / 4] != 0) {
        CharacterAssetManager_UpdateActiveAssets(gGameMainManagers.characterAssetManager, 0);
        CSelect_UpdateSelectCommonBackground(cSelect);

        if (cSelect[0x38 / 4] == 0) {
            unsigned int backgroundColor = 0xFFFFFFFFu;
            int nextState = CSelectBootTitleFlow_Tick(
                (CSelectBootTitleFlow *)RuntimePointerFromBits(cSelect[0x1bf0 / 4]), 0);
            if (nextState != 0) {
                cSelect[0x3c / 4] = nextState;
                if (nextState == 0x0c &&
                    gCSelectHostBootPhase == CSELECT_HOST_BOOT_PHASE_BOOT_FLOW &&
                    cSelect[0x18 / 4] == 0 &&
                    cSelect[0x1c / 4] == 0) {
                    CSelect_StoreLoadedResource(cSelect, 0x701, CSelectResourcePaths[regionIndex][2]);
                    cSelect[0x1c / 4] = 1;
                    gCSelectHostBootPhase = CSELECT_HOST_BOOT_PHASE_WAIT_SEL_TITLE;
                    gCSelectHostBootPhaseTicks = 0;
                    RuntimeDebugReport("CSelect boot: boot flow requested selTitle\n");
                }
            }
            RenderBeginFrame();
            ApplyRenderConfig(0, &backgroundColor);
            UiRootManager_UpdateRuntimeBeforeDraw(gGameMainManagers.uiRootManager, 0);
            CSelect_DrawSelectCommonBackground();
            if (gGameMainManagers.uiRootManager != 0) {
                UiRootManager_DrawBootCzanGroups(gGameMainManagers.uiRootManager);
            }
            RenderEndFrame();
        }
        else if (cSelect[0x38 / 4] == 1) {
            unsigned int backgroundColor = 0xFFFFFFFFu;
            int nextState = CSelMode_Update();
            if (nextState != 1) {
                cSelect[0x3c / 4] = nextState;
                cSelect[0x1bf0 / 4] = 0;
                CSelect_CreateActiveScreen(cSelect);
            }
            RenderBeginFrame();
            ApplyRenderConfig(0, &backgroundColor);
            CSelect_DrawSelectCommonBackground();
            if (cSelect[0x38 / 4] == 1) {
                CSelMode_DrawHostUi();
            }
            RenderEndFrame();
        }
        else if (cSelect[0x38 / 4] == 2) {
            unsigned int backgroundColor = 0xFFFFFFFFu;
            int nextState = CSelectPlayerCountFlow_Update(
                (CSelectPlayerCountFlow *)RuntimePointerFromBits(cSelect[0x1bf0 / 4]),
                0);
            if (nextState != 2) {
                cSelect[0x3c / 4] = nextState;
                cSelect[0x1bf0 / 4] = 0;
                CSelect_CreateActiveScreen(cSelect);
            }
            RenderBeginFrame();
            ApplyRenderConfig(0, &backgroundColor);
            CSelect_DrawSelectCommonBackground();
            if (cSelect[0x38 / 4] == 2) {
                CSelectPlayerCountFlow_Draw((CSelectPlayerCountFlow *)RuntimePointerFromBits(cSelect[0x1bf0 / 4]));
            }
            else if (cSelect[0x38 / 4] == 1) {
                CSelMode_DrawHostUi();
            }
            RenderEndFrame();
        }
        else if (cSelect[0x38 / 4] == 0x0c) {
            unsigned int backgroundColor = 0xFFFFFFFFu;
            int nextState = CSelectTitleFlow_Tick(
                (CSelectTitleFlow *)RuntimePointerFromBits(cSelect[0x1bf0 / 4]),
                cSelect,
                0);
            if (nextState != 0x0c) {
                cSelect[0x3c / 4] = nextState;
                cSelect[0x1bf0 / 4] = 0;
                CSelect_CreateActiveScreen(cSelect);
            }
            RenderBeginFrame();
            ApplyRenderConfig(0, &backgroundColor);
            CSelect_DrawSelectCommonBackground();
            if (cSelect[0x38 / 4] == 0x0c) {
                CSelectTitleFlow_Draw((CSelectTitleFlow *)RuntimePointerFromBits(cSelect[0x1bf0 / 4]));
            }
            else if (cSelect[0x38 / 4] == 1) {
                CSelMode_DrawHostUi();
            }
            RenderEndFrame();
        }
    }
    else {
        unsigned int backgroundColor = 0xFFFFFFFFu;
        RenderBeginFrame();
        ApplyRenderConfig(0, &backgroundColor);
        RenderEndFrame();
    }
    return 0;
}
