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
/* Wii system language (SCGetLanguage) used for the global context +0x8C.
   0=Japanese 1=English 2=German 3=French 4=Spanish 5=Italian 6=Dutch.
   The USA disc only ships US/FR/SP select data; see CSelectResourcePaths. */
static int gHostSystemLanguage = 1;

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
/* DAT_802E71F8: Mii (RFL) manager; [0] == 1 means Mii data is readable. The PC port
   has no Mii Channel and treats Mii data as available, so boot skips the
   "Mii Channel save data could not be read" prompt (decision 2026-10-02). */
static int gMiiManagerState[1] = { 1 };

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

int HostPointer_ToBits32(const void *pointer, const char *owner) {
    /* The host keeps pointers in 32-bit fields like the Wii. The x64 exe is linked
       /LARGEADDRESSAWARE:NO with a low base, so every host address is below 2 GB
       and survives the round trip. Anything above (e.g. memory owned by a system
       DLL) would be truncated: report it once per owner instead of failing later. */
    static const char *reported[16];
    uintptr_t value = (uintptr_t)pointer;
    int i;

    if (value > 0x7fffffffu) {
        for (i = 0; i < 16 && reported[i] != 0 && reported[i] != owner; i++) {
        }
        if (i < 16 && reported[i] == 0) {
            reported[i] = owner;
            RuntimeDebugReport("HostPointer: %s pointer %p does not fit in 32 bits\n", owner, pointer);
        }
    }
    return (int)value;
}

static int HostPointer_CheckLowAddressSpace(void) {
    /* Startup self-check for the /LARGEADDRESSAWARE:NO link (see build_host_gl.bat). */
    static int staticProbe;
    int stackProbe;
    void *heapProbe = malloc(64);
    int ok = (uintptr_t)&staticProbe <= 0x7fffffffu &&
             (uintptr_t)&stackProbe <= 0x7fffffffu &&
             (uintptr_t)heapProbe <= 0x7fffffffu;

    if (!ok) {
        RuntimeDebugReport(
            "fatal: host addresses are above 2 GB (static=%p stack=%p heap=%p). "
            "Link with /LARGEADDRESSAWARE:NO /DYNAMICBASE:NO /BASE:0x10000000 (tools\\build_host_gl.bat).\n",
            (void *)&staticProbe, (void *)&stackProbe, heapProbe);
    }
    free(heapProbe);
    return ok;
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

    return HostPointer_ToBits32(pointer, "runtime");
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

    /* 0x80142EAC: SCGetLanguage() & 0xFF stored at +0x8C. */
    context[0x23] = gHostSystemLanguage;
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

void GameHost_SetSystemLanguage(int wiiLanguage) {
    if (wiiLanguage >= 0 && wiiLanguage <= 9) {
        gHostSystemLanguage = wiiLanguage;
    }
}

int GameHost_GetSystemLanguage(void) {
    return gHostSystemLanguage;
}

static int HostStrCaseCmp(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int GameHost_ParseLanguageName(const char *name) {
    static const struct { const char *name; int language; } kNames[] = {
        { "ja", 0 }, { "jp", 0 },
        { "en", 1 }, { "us", 1 },
        { "de", 2 }, { "fr", 3 },
        { "es", 4 }, { "sp", 4 },
        { "it", 5 }, { "nl", 6 },
    };
    unsigned int i;

    if (name == 0 || name[0] == '\0') {
        return -1;
    }
    if (name[0] >= '0' && name[0] <= '9') {
        return atoi(name);
    }
    for (i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        if (HostStrCaseCmp(name, kNames[i].name) == 0) {
            return kNames[i].language;
        }
    }
    return -1;
}

void GameHost_ConfigureFromArgs(int argc, char **argv) {
    const char *value = getenv("DDRII_LANG");
    int i;
    int language;

    /* Host replacement for the Wii system settings: --lang en|fr|es (or
       --lang=en, or the DDRII_LANG environment variable). Default is English,
       which is what a USA console reports. */
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--lang=", 7) == 0) {
            value = argv[i] + 7;
        }
        else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            value = argv[++i];
        }
    }
    language = GameHost_ParseLanguageName(value);
    if (value != 0 && language < 0) {
        printf("host: unknown language '%s', using English\n", value);
    }
    if (language >= 0) {
        GameHost_SetSystemLanguage(language);
    }
    printf("host: system language %d\n", gHostSystemLanguage);
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

/* Input/menu manager (gManager_802E70AC, 0xEF0 bytes). Manager +0x00 is the frame
   rate; slot s (0..3 = Wii Remotes, 4 = any controller) has a button-state object at
   manager + 4 + s*0x20:
     +0x00 held now        (record +0x04, tested by 0x8002AE08)
     +0x04 pressed         (record +0x08, 0x8002AE28 / IsConfirmPressed / IsBackPressed)
     +0x08 released        (record +0x0C)
     +0x0C press + repeat  (record +0x10, 0x8002AE48)
     +0x10 previous held   (record +0x14)
     +0x14 repeat delay    (0.5 s), +0x18 repeat interval (0.1 s), +0x1C repeat counter
   Bits are Wii Remote KPAD bits (see MENU_INPUT_* in the platform layer). */
#define INPUT_SLOT_STRIDE 0x20
#define INPUT_BUTTON_MASK 0x1ffffu

static void InputButtonState_Update(unsigned char *state, unsigned int heldNow) {
    /* 0x80146C1C */
    unsigned int *fields = (unsigned int *)(void *)state;
    unsigned int previous = fields[0x10 / 4];
    unsigned int changed = previous ^ heldNow;
    unsigned int repeat = heldNow & previous;

    fields[0x00 / 4] = heldNow;
    fields[0x04 / 4] = changed & heldNow & INPUT_BUTTON_MASK;
    fields[0x08 / 4] = changed & previous & INPUT_BUTTON_MASK;
    if ((repeat & INPUT_BUTTON_MASK) != 0) {
        unsigned int counter = fields[0x1c / 4] + 1;
        unsigned int delay = fields[0x14 / 4];
        unsigned int interval = fields[0x18 / 4];

        fields[0x1c / 4] = counter;
        if (counter < delay || interval == 0 || (counter - delay) % interval != 0) {
            repeat = 0;
        }
    }
    else {
        fields[0x1c / 4] = 0;
    }
    if (fields[0x04 / 4] != 0) {
        fields[0x1c / 4] = 0;
    }
    fields[0x0c / 4] = repeat | fields[0x04 / 4];
    fields[0x10 / 4] = heldNow;
}

static void InputOrMenuStateManager_Update(int *manager, int flags) {
    /* 0x8002A33C: refresh the repeat timing when the frame rate changes, then OR each
       remote's KPAD hold bits (remote, nunchuk, classic) and run 0x80146C1C on every
       slot. The PC host feeds the keyboard/XInput mask to remote 0 and slot 4. */
    unsigned int heldMask = 0;
    unsigned int unusedTriggered = 0;
    float frameRate = 60.0f;
    int slot;

    if (manager == 0 || flags == 1) {
        return;
    }
    if (*(float *)(void *)manager != frameRate) {
        *(float *)(void *)manager = frameRate;
        for (slot = 0; slot < 5; slot++) {
            unsigned char *state = (unsigned char *)manager + 4 + slot * INPUT_SLOT_STRIDE;

            *(unsigned int *)(void *)(state + 0x14) = (unsigned int)(0.5f * frameRate);
            *(unsigned int *)(void *)(state + 0x18) = (unsigned int)(0.1f * frameRate);
            *(unsigned int *)(void *)(state + 0x1c) = 0;
        }
    }

    Platform_PollMenuInput(&heldMask, &unusedTriggered);
    for (slot = 0; slot < 5; slot++) {
        unsigned int held = (slot == 0 || slot == 4) ? heldMask : 0;

        InputButtonState_Update((unsigned char *)manager + 4 + slot * INPUT_SLOT_STRIDE, held);
    }
}

/* ---- Wii Remote pointer manager (gManager_802E70B4, 0xBF0 bytes) ----------------
   +0x000 flags (bit 0 = loaded)        +0x008 hit regions: 0x100 x {group, child}
   +0x804 single-pointer mode           +0x808 Czan UI manager
   +0x80C four 0xF8-byte remote records:
     +0x00 channel  +0x04 flags  +0x08 previous flags  +0x0C x,y,z (screen pixels)
     +0x18 Czan UI manager  +0x1C input manager  +0x24 pointer cooldown (seconds)
     +0x28/+0x6C/+0xB0 cursor entries (0x44-byte CSelModeEntry bases):
       anchor (Pointer.bin 0/0), droplet (0/1, linked to anchor child 0),
       player number (0/2, linked to droplet child 1 '@dummy')
   Record flags: 0x1 connected, 0x2 connection changed, 0x4 active, 0x8 on screen,
   0x10 cursor hidden, 0x20 dimmed, 0x40 pointer usable, 0x100 pointer granted,
   0x200 hidden by game. The KPAD layer (+0x25C, 0x8014B4AC..) is replaced by the
   host mouse: remote 0 is always connected and points where the mouse is. */
#define POINTER_RECORD_BASE 0x80c
#define POINTER_RECORD_SIZE 0xf8
#define POINTER_REGION_COUNT 0x100

static unsigned char *PointerManager_GetRecord(int *manager, int channel) {
    return (unsigned char *)manager + POINTER_RECORD_BASE + channel * POINTER_RECORD_SIZE;
}

#define POINTER_FLAGS(record) (*(int *)(void *)((record) + 0x04))
#define POINTER_PREV_FLAGS(record) (*(int *)(void *)((record) + 0x08))
#define POINTER_POS(record) ((float *)(void *)((record) + 0x0c))
#define POINTER_COOLDOWN(record) (*(float *)(void *)((record) + 0x24))
#define POINTER_ENTRY(record, n) ((void *)((record) + 0x28 + (n) * 0x44))

static int PointerFlags_Set(int flags, int bit, int on) {
    /* 0x8010D928 */
    return on ? (flags | bit) : (flags & ~bit);
}

static int HostKpad_IsConnected(int channel) {
    /* 0x8014B7AC */
    return channel == 0;
}

static int HostKpad_GetPointer(int channel, float *x, float *y) {
    /* 0x8014B4AC / 0x8014B4BC validity and 0x8014B524 position. */
    if (channel != 0) {
        return 0;
    }
    return Platform_GetPointerPosition(x, y);
}

static void PointerEntry_InitBase(void *entry) {
    /* 0x80110BA4: slot type 6, handles and cached animations -1, count 0. */
    int *words = (int *)entry;
    int i;

    words[0] = 6;
    for (i = 1; i <= 4; i++) {
        words[i] = 0;
    }
    for (i = 0; i < 4; i++) {
        words[5 + i] = -1;
        words[9 + i] = -1;
    }
    words[13] = 0;
    words[14] = 0;
    words[15] = 0;
}

static void PointerRecord_Load(unsigned char *record, int channel, const void *pointerData, unsigned int pointerSize) {
    /* 0x8010DC50 */
    static const float offscreen[3] = { -640.0f, -480.0f, 0.0f };
    int handles[3];
    int i;

    POINTER_FLAGS(record) = 0;
    POINTER_PREV_FLAGS(record) = 0;
    memcpy(POINTER_POS(record), offscreen, sizeof(offscreen));
    POINTER_COOLDOWN(record) = 0.0f;
    *(int *)(void *)(record + 0x00) = channel;
    *(int *)(void *)(record + 0x18) = 0;
    *(int *)(void *)(record + 0x1c) = RuntimePointerBits(gGameMainManagers.inputOrMenuStateManager);

    for (i = 0; i < 3; i++) {
        CzanLinkBlock block;

        PointerEntry_InitBase(POINTER_ENTRY(record, i));
        if (CzanLinkResource_GetBlock(pointerData, pointerSize, (unsigned int)i, &block)) {
            HostCzan_RegisterLinkSize(block.data, block.size);
            CSelModeEntry_AddUiObject(POINTER_ENTRY(record, i), (void *)block.data);
        }
    }
    CSelModeEntry_SetPositionOrLayout(POINTER_ENTRY(record, 0), 0, -1, POINTER_POS(record));
    ((int *)POINTER_ENTRY(record, 1))[0] = 3;   /* 0x801105F4 */
    ((int *)POINTER_ENTRY(record, 2))[0] = 3;
    CSelModeEntry_SetObjectFlags(POINTER_ENTRY(record, 1), 0, channel * 2 + 1);
    CSelModeEntry_SetObjectFlags(POINTER_ENTRY(record, 2), 0, channel * 2);
    CSelModeEntry_PlayObject(POINTER_ENTRY(record, 1), 0, 0, channel, 0);
    CSelModeEntry_PlayObject(POINTER_ENTRY(record, 2), 0, 0, channel, 0);
    for (i = 0; i < 3; i++) {
        handles[i] = CSelModeEntry_GetObjectHandle(POINTER_ENTRY(record, i), 0);
    }
    if (handles[1] >= 0 && handles[0] >= 0) {
        CzanUiManager_LinkObjectGroupToReferenceObject(0, handles[1], handles[0], 0, 0x0f);
    }
    if (handles[2] >= 0 && handles[1] >= 0) {
        CzanUiManager_LinkObjectGroupToReferenceObject(0, handles[2], handles[1], 1, 0x0f);
    }
    for (i = 0; i < 3; i++) {
        CSelModeEntry_StartObjectAnimation(0.0, POINTER_ENTRY(record, i), 0, 0, 0, 0);
    }
}

void PointerManager_Load(int *manager, void *linkData) {
    /* 0x8010E770 */
    unsigned int linkSize;
    CzanLinkBlock pointerBlock;
    int i;

    if (manager == 0 || linkData == 0) {
        return;
    }
    manager[1] = 0;
    for (i = 0; i < POINTER_REGION_COUNT * 2; i++) {
        manager[2 + i] = -1;
    }
    linkSize = HostCzan_GetRegisteredLinkSize(linkData);
    if (!CzanLinkResource_IsValid(linkData, linkSize) ||
        !CzanLinkResource_GetBlock(linkData, linkSize, 0, &pointerBlock)) {
        return;
    }
    HostCzan_RegisterLinkSize(pointerBlock.data, pointerBlock.size);
    manager[0x804 / 4] = 1;
    manager[0x808 / 4] = 0;
    manager[0] |= 1;
    for (i = 0; i < 4; i++) {
        PointerRecord_Load(PointerManager_GetRecord(manager, i), i, pointerBlock.data, pointerBlock.size);
    }
}

static void PointerRecord_UpdateConnection(unsigned char *record) {
    /* 0x8010E0D8 */
    int flags = POINTER_FLAGS(record);
    int channel = *(int *)(void *)record;

    if ((flags & 2) == 0) {
        int previous = POINTER_PREV_FLAGS(record);

        flags = PointerFlags_Set(flags, 1, HostKpad_IsConnected(channel));
        if ((previous & 1) != (flags & 1)) {
            flags |= 2;
        }
    }
    if ((flags & 2) != 0) {
        flags = PointerFlags_Set(flags, 4, (flags & 1) != 0);
    }
    POINTER_FLAGS(record) = flags;
}

static void PointerRecord_UpdateFlags(unsigned char *record, int pointing) {
    /* 0x8010E1E8 */
    float *pos = POINTER_POS(record);
    int flags = POINTER_FLAGS(record);
    int onScreen = pointing && pos[0] >= 0.0f && pos[0] <= 640.0f && pos[1] >= 0.0f && pos[1] <= 480.0f;

    flags = PointerFlags_Set(flags, 8, onScreen);
    flags = PointerFlags_Set(flags, 0x10,
                             !((flags & 1) && (flags & 4) && (flags & 8) && !(flags & 0x200)));
    flags = PointerFlags_Set(flags, 0x40, (flags & 0x100) && (flags & 8));
    flags = PointerFlags_Set(flags, 0x20, (flags & 0x40) == 0);
    POINTER_FLAGS(record) = flags;
}

static void PointerRecord_ApplyVisuals(unsigned char *record) {
    /* 0x8010E3C8 */
    int flags = POINTER_FLAGS(record);
    unsigned char hidden = (flags & 0x10) != 0;
    unsigned char alpha = (flags & 0x20) ? 0x50 : 0xff;
    int i;

    for (i = 1; i <= 2; i++) {
        int handle = CSelModeEntry_GetObjectHandle(POINTER_ENTRY(record, i), 0);

        CSelModeEntry_SetObjectEnabled(POINTER_ENTRY(record, i), 0, -1, hidden);
        if (handle >= 0) {
            CzanUiManager_SetObjectGroupVertexAlpha(0, handle, alpha);
        }
    }
    POINTER_FLAGS(record) = flags & ~2;
}

static void PointerRecord_Update(unsigned char *record, int skipFrame) {
    /* 0x8010DF54. Remote roll (0x8014B4D0) is level for the mouse, so the cursor
       rotation written through 0x80110720 stays 0. */
    float x;
    float y;
    int pointing;
    int i;

    if (skipFrame != 0) {
        return;
    }
    POINTER_PREV_FLAGS(record) = POINTER_FLAGS(record);
    PointerRecord_UpdateConnection(record);
    pointing = HostKpad_GetPointer(*(int *)(void *)record, &x, &y);
    if ((POINTER_FLAGS(record) & 4) != 0 && pointing) {
        float *pos = POINTER_POS(record);

        pos[0] = x;
        pos[1] = y;
        pos[2] = 0.0f;
        CSelModeEntry_SetPositionOrLayout(POINTER_ENTRY(record, 0), 0, -1, pos);
    }
    PointerRecord_UpdateFlags(record, pointing);
    PointerRecord_ApplyVisuals(record);
    for (i = 0; i < 3; i++) {
        CSelModeEntry_ActivateObject(POINTER_ENTRY(record, i), 0);
    }
}

static void PointerManager_GrantPointers(int *manager) {
    /* 0x8010F0D4: connected, on-screen remotes past their cooldown get 0x100; in
       single-pointer mode (+0x804) only the first one. */
    int granted = 0;
    int channel;

    for (channel = 0; channel < 4; channel++) {
        unsigned char *record = PointerManager_GetRecord(manager, channel);
        int grant = HostKpad_IsConnected(channel) &&
                    (POINTER_FLAGS(record) & 8) != 0 &&
                    POINTER_COOLDOWN(record) <= 0.0f &&
                    (manager[0x804 / 4] == 0 || granted == 0);

        POINTER_FLAGS(record) = PointerFlags_Set(POINTER_FLAGS(record), 0x100, grant);
        granted |= grant;
    }
}

static void Manager802e70b4_Update(int *manager, int skipModuleFrame) {
    /* 0x8010E9DC */
    float frameSeconds = 1.0f / 60.0f;
    int channel;

    if (manager == 0 || skipModuleFrame != 0 || (manager[0] & 1) == 0) {
        return;
    }
    for (channel = 0; channel < 4; channel++) {
        unsigned char *record = PointerManager_GetRecord(manager, channel);

        POINTER_COOLDOWN(record) -= frameSeconds;
        if (POINTER_COOLDOWN(record) < 0.0f) {
            POINTER_COOLDOWN(record) = 0.0f;
        }
    }
    PointerManager_GrantPointers(manager);
    for (channel = 0; channel < 4; channel++) {
        unsigned char *record = PointerManager_GetRecord(manager, channel);

        PointerRecord_Update(record, skipModuleFrame);
        if ((POINTER_FLAGS(record) & 8) != 0 && (POINTER_PREV_FLAGS(record) & 8) == 0) {
            POINTER_FLAGS(record) |= 0x100;
            POINTER_COOLDOWN(record) = 0.0f;
        }
    }
}

int PointerManager_RegisterRegion(int *manager, int groupHandle, int childIndex) {
    /* 0x8010EB78 */
    int i;

    for (i = 0; i < POINTER_REGION_COUNT; i++) {
        if (manager[2 + i * 2] == -1) {
            manager[2 + i * 2] = groupHandle;
            manager[3 + i * 2] = childIndex;
            manager[1]++;
            return i;
        }
    }
    return -1;
}

void PointerManager_UnregisterRegion(int *manager, int region) {
    /* 0x8010EC30 */
    if (manager == 0 || region < 0 || region >= POINTER_REGION_COUNT) {
        return;
    }
    manager[2 + region * 2] = -1;
    manager[3 + region * 2] = -1;
    manager[1]--;
}

int PointerManager_HitTestRegion(int *manager, int *hitPerChannel, int region) {
    /* 0x8010EC54: bit mask of remotes whose usable pointer (0x1|0x4|0x8|0x40, not
       0x10/0x20) lies inside the region's child object. */
    int group;
    int child;
    int hits = 0;
    int channel;

    if (hitPerChannel != 0) {
        memset(hitPerChannel, 0, 4 * sizeof(int));
    }
    if (manager == 0 || region < 0 || region >= POINTER_REGION_COUNT) {
        return 0;
    }
    group = manager[2 + region * 2];
    child = manager[3 + region * 2];
    if (group == -1 || child == -1 || CzanUiManager_IsChildObjectHidden(0, group, child)) {
        return 0;
    }
    for (channel = 0; channel < 4; channel++) {
        unsigned char *record = PointerManager_GetRecord(manager, channel);
        int flags = POINTER_FLAGS(record);
        int hit = 0;

        if ((flags & 1) && (flags & 4) && (flags & 8) && !(flags & 0x10) && !(flags & 0x20) && (flags & 0x40)) {
            hit = CzanUiManager_HitTestChildObject(0, group, child, POINTER_POS(record)[0], POINTER_POS(record)[1]);
        }
        if (hitPerChannel != 0) {
            hitPerChannel[channel] = hit;
        }
        hits |= hit;
    }
    return hits;
}

void PointerManager_SetCooldown(int *manager, int channel) {
    /* 0x8010EEC4 with r4 == 0: 1.5 s before the pointer is granted again. */
    int i;

    for (i = 0; i < 4; i++) {
        if (channel == 4 || channel == i) {
            POINTER_COOLDOWN(PointerManager_GetRecord(manager, i)) = 1.5f;
        }
    }
}

void PointerManager_SetHidden(int *manager, int hidden) {
    /* 0x8010EFB0 */
    int i;

    for (i = 0; i < 4; i++) {
        unsigned char *record = PointerManager_GetRecord(manager, i);
        POINTER_FLAGS(record) = PointerFlags_Set(POINTER_FLAGS(record), 0x200, hidden);
    }
}

void PointerManager_SetSingleMode(int *manager, int singleMode) {
    /* 0x8010EEBC: +0x804 */
    if (manager != 0) {
        manager[0x804 / 4] = singleMode;
    }
}

int *GameMain_GetPointerManager(void) {
    return gGameMainManagers.manager802e70b4;
}

int PointerManager_IsOnScreen(int *manager, int channel) {
    /* 0x8010F014: channel 4 = any remote. */
    int i;

    if (manager == 0) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        if ((channel == 4 || channel == i) &&
            (POINTER_FLAGS(PointerManager_GetRecord(manager, i)) & 8) != 0) {
            return 1;
        }
    }
    return 0;
}

static void PointerManager_DrawHost(int *manager) {
    /* Host bridge: the retail global Czan list draws the cursor entries with the
       front-most priorities; the host's scoped draws add them last. */
    int handles[12];
    int count = 0;
    int channel;
    int i;

    if (manager == 0 || (manager[0] & 1) == 0) {
        return;
    }
    for (channel = 0; channel < 4; channel++) {
        for (i = 1; i <= 2; i++) {
            int handle = CSelModeEntry_GetObjectHandle(POINTER_ENTRY(PointerManager_GetRecord(manager, channel), i), 0);
            if (handle >= 0) {
                handles[count++] = handle;
            }
        }
    }
    CzanUiManager_DrawObjectGroupsReverse(0, handles, count);
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
    if (!HostPointer_CheckLowAddressSpace()) {
        return 1;
    }

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
        /* 0x800FE548: the full loader, as in the DOL. Groups created with flags 0 stay
           inert and hidden until a state machine starts them (Czan +0x171/+0xB0), so
           comAF no longer appears all at once. This creates uiRoot +0x0C (fade quad)
           and +0x10 (dimmer) used by the title transitions. */
        UiRootManager_LoadResource(gGameMainManagers.uiRootManager, uiRootLinkData);
    }
    if (pointerLinkData != 0) {
        PointerManager_Load(gGameMainManagers.manager802e70b4, pointerLinkData);
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

static float Runtime_GetFrameStep(void) {
    /* Shared by several per-frame updates (0x800CC690, 0x800E1988, ...):
       step = 60.0f / (settings +0x50 / (settings +0x54 + 1)), settings =
       DAT_802E71B8 +0x258. 1.0 at 60 Hz with no frame skip; the host does not fill
       these fields yet, so fall back to 1.0. */
    int *settings = GlobalRuntimeContext_GetPointerAt(0x258);
    float rate;
    int divisor;

    if (settings == 0) {
        return 1.0f;
    }
    rate = *(float *)(void *)(settings + 0x50 / 4);
    divisor = settings[0x54 / 4] + 1;
    if (!(rate > 0.0f) || divisor <= 0) {
        return 1.0f;
    }
    return 60.0f / (rate / (float)divisor);
}

/* ---- Save data manager (gManager_802E70E0, 0x28F58 bytes) ----------------------
   +0x00000      pointer to the NAND transfer buffer
   +0x00004      save image, 0x28F38 bytes (what is written to the save file)
   +0x28DA4      boot flags (bit 0x2 = boot import done; inside the save image)
   +0x28EE4      runtime flags (bit 0x1 cleared by 0x8009B9AC)
   +0x28F44      "loaded data is valid" (0x8009B99C sets, 0x8009B9AC clears)
   +0x28F48..54  NAND operation handles; +0x28F54 = DDR (first game) import object
   On the Wii the file I/O runs asynchronously through the NAND manager
   (DAT_802E71B8 +0x264, 0x80165xxx/0x80166xxx). The PC port keeps the save image
   unchanged in save/ddr2.dat next to the exe and performs each operation
   synchronously, so the NAND manager is never busy. */
#define SAVE_IMAGE_SIZE 0x28f38
#define SAVE_MANAGER_LOADED_VALID_OFFSET 0x28f44
#define SAVE_MANAGER_RUNTIME_FLAGS_OFFSET 0x28ee4

enum {
    SAVE_LOAD_OK = 0,
    SAVE_LOAD_NO_FILE = 1,
    SAVE_LOAD_CORRUPT = 2
};

static unsigned char gSaveHostBuffer[SAVE_IMAGE_SIZE];
static int gSaveHostLoadResult = SAVE_LOAD_NO_FILE;

static void SaveHost_GetPath(char *path, size_t size, int temporary) {
    char exePath[MAX_PATH];
    char *slash;

    if (GetModuleFileNameA(0, exePath, sizeof(exePath)) == 0) {
        strcpy(exePath, ".\\x.exe");
    }
    slash = strrchr(exePath, '\\');
    if (slash != 0) {
        slash[1] = '\0';
    }
    snprintf(path, size, "%ssave\\ddr2.dat%s", exePath, temporary ? ".tmp" : "");
}

static int SaveDataManager_IsNandBusy(void) {
    /* 0x80166CB8(NAND manager): host operations complete immediately. */
    return 0;
}

static int SaveDataManager_CheckFreeSpace(void) {
    /* 0x801660E8(NAND manager): 0 ok, 4 not enough blocks, 5 not enough inodes. */
    return 0;
}

static void SaveDataManager_StartLoad(int *saveManager) {
    /* 0x8009A850: open and read the save file into the NAND buffer. */
    char path[MAX_PATH + 32];
    FILE *file;
    size_t got;
    int extra;

    (void)saveManager;
    SaveHost_GetPath(path, sizeof(path), 0);
    file = fopen(path, "rb");
    if (file == 0) {
        gSaveHostLoadResult = SAVE_LOAD_NO_FILE;
        RuntimeDebugReport("SaveData: no save file (%s)\n", path);
        return;
    }
    got = fread(gSaveHostBuffer, 1, SAVE_IMAGE_SIZE, file);
    extra = fgetc(file);
    fclose(file);
    gSaveHostLoadResult = (got == SAVE_IMAGE_SIZE && extra == EOF) ? SAVE_LOAD_OK : SAVE_LOAD_CORRUPT;
    RuntimeDebugReport("SaveData: read %s -> %s\n", path, gSaveHostLoadResult == SAVE_LOAD_OK ? "ok" : "corrupt");
}

static int SaveDataManager_GetLoadResult(int *saveManager) {
    /* 0x8009A8B0: 0 loaded, 1 no file, other = error (corrupt / access error). */
    (void)saveManager;
    return gSaveHostLoadResult;
}

static void SaveDataManager_EndOperation(int *saveManager) {
    /* 0x8009AACC: release the finished NAND operation handle. */
    (void)saveManager;
}

static void SaveDataManager_StartSpaceCheck(int *saveManager) {
    /* 0x8009AAB4 -> 0x80165FC8: query free blocks/inodes for the new file. */
    (void)saveManager;
}

static void SaveDataManager_MarkLoadedValid(int *saveManager) {
    /* 0x8009B99C */
    if (saveManager != 0) {
        *(int *)(void *)((unsigned char *)saveManager + SAVE_MANAGER_LOADED_VALID_OFFSET) = 1;
    }
}

static void SaveDataManager_ClearLoadedValid(int *saveManager) {
    /* 0x8009B9AC */
    if (saveManager != 0) {
        unsigned char *bytes = (unsigned char *)saveManager;

        *(int *)(void *)(bytes + SAVE_MANAGER_LOADED_VALID_OFFSET) = 0;
        *(unsigned int *)(void *)(bytes + SAVE_MANAGER_RUNTIME_FLAGS_OFFSET) &= ~1u;
    }
}

static void SaveDataManager_ApplyToGame(int *saveManager) {
    /* 0x8009A99C distributes the save image into the game managers (records,
       unlocks, options, Mii backup dancers via gManager_802E70D0, ...).
       TODO: not ported yet; the image is kept in the manager only. */
    (void)saveManager;
    RuntimeDebugReport("SaveData: apply-to-game (0x8009A99C) not ported yet\n");
}

static void SaveDataManager_ApplyLoaded(int *saveManager) {
    /* 0x8009A948: when the loaded data is valid, copy the buffer into the image. */
    if (saveManager == 0 ||
        *(int *)(void *)((unsigned char *)saveManager + SAVE_MANAGER_LOADED_VALID_OFFSET) != 1) {
        return;
    }
    memcpy((unsigned char *)saveManager + 4, gSaveHostBuffer, SAVE_IMAGE_SIZE);
    SaveDataManager_ApplyToGame(saveManager);
}

static void SaveDataManager_InitNewData(int *saveManager, int forBootCreate) {
    /* 0x8009AC64: clear the image and fill the defaults for a new file.
       TODO: only the clear is ported; the default tables (0x80273700 +0xA0 ...)
       are filled once this function is ported. */
    (void)forBootCreate;
    if (saveManager != 0) {
        memset((unsigned char *)saveManager + 4, 0, SAVE_IMAGE_SIZE);
    }
    RuntimeDebugReport("SaveData: new-file defaults (0x8009AC64) only cleared, not ported yet\n");
}

static void SaveDataManager_WriteFile(int *saveManager) {
    /* 0x8009AB1C: copy the image into the NAND buffer and write the file. */
    char path[MAX_PATH + 32];
    char tempPath[MAX_PATH + 32];
    char dir[MAX_PATH + 32];
    char *slash;
    FILE *file;
    int ok = 0;

    if (saveManager == 0) {
        return;
    }
    memcpy(gSaveHostBuffer, (unsigned char *)saveManager + 4, SAVE_IMAGE_SIZE);
    SaveHost_GetPath(path, sizeof(path), 0);
    SaveHost_GetPath(tempPath, sizeof(tempPath), 1);
    strcpy(dir, path);
    slash = strrchr(dir, '\\');
    if (slash != 0) {
        *slash = '\0';
        CreateDirectoryA(dir, 0);
    }
    file = fopen(tempPath, "wb");
    if (file != 0) {
        ok = fwrite(gSaveHostBuffer, 1, SAVE_IMAGE_SIZE, file) == SAVE_IMAGE_SIZE;
        ok = (fclose(file) == 0) && ok;
    }
    if (ok) {
        ok = MoveFileExA(tempPath, path, MOVEFILE_REPLACE_EXISTING) != 0;
    }
    RuntimeDebugReport("SaveData: write %s -> %s\n", path, ok ? "ok" : "FAILED");
}

static void SaveDataManager_DeleteFile(int *saveManager) {
    /* 0x8009AB90: delete the save file (corrupted data -> "Delete file"). */
    char path[MAX_PATH + 32];

    (void)saveManager;
    SaveHost_GetPath(path, sizeof(path), 0);
    DeleteFileA(path);
    gSaveHostLoadResult = SAVE_LOAD_NO_FILE;
    RuntimeDebugReport("SaveData: deleted %s\n", path);
}

/* DDR (first game) save import, object at save manager +0x28F54. It only exists
   when the console has a DanceDanceRevolution save; the PC has none, so the game
   reports "No save data for DanceDanceRevolution found." exactly like such a Wii. */
static int SaveDataManager_IsImportBusy(int *saveManager) { (void)saveManager; return 0; }      /* 0x8009C264 */
static void SaveDataManager_PollImport(int *saveManager) { (void)saveManager; }                /* 0x8009C1B4 */
static int SaveDataManager_ImportFound(int *saveManager) { (void)saveManager; return 0; }      /* 0x8009C294 */
static int SaveDataManager_ImportAlreadyUnlocked(int *saveManager) { (void)saveManager; return 0; } /* 0x8009C2B4 */
static void SaveDataManager_ApplyImport(int *saveManager) { (void)saveManager; }               /* 0x8009C1F8 */
static void SaveDataManager_FinishImportRead(int *saveManager) { (void)saveManager; }          /* 0x8009C170 */
static void SaveDataManager_ReleaseImport(int *saveManager) { (void)saveManager; }             /* 0x8009C080 */

static void SaveFlow_ShowMessage(int message, int selectedOption) {
    /* Configure prompt text (block 0 = save messages) and preselect a choice. */
    UiRootManager_ConfigureBootTransitionPrompt(gGameMainManagers.uiRootManager, 0, message);
    if (selectedOption >= 0) {
        UiRootManager_SetBootTransitionSelectedOption(gGameMainManagers.uiRootManager, selectedOption);
    }
}

static void SaveFlow_ShowNotice(int message) {
    /* Configure + 0x80100288(1): message that waits for A (state 0x16). */
    UiRootManager_ConfigureBootTransitionPrompt(gGameMainManagers.uiRootManager, 0, message);
    UiRootManager_SetBootTransitionAdvanceLock(gGameMainManagers.uiRootManager, 1);
}

static void SaveFlow_Close(void) {
    UiRootManager_CloseBootTransitionController(gGameMainManagers.uiRootManager);
}

static void SaveFlow_Update(int *flowOwner, float step) {
    /* 0x800CCD2C: save-data flow on gManager_802E70C4 (jump table 0x802BD220).
       Text messages are text block 0 (tools/dump_text.py --block 0). */
    int *saveManager = gGameMainManagers.manager802e70e0;
    int *uiRoot = gGameMainManagers.uiRootManager;
    float *timer;
    int state;
    int result;

    if (flowOwner == 0) {
        return;
    }
    timer = (float *)(void *)(flowOwner + 0x28 / 4);
    state = flowOwner[0x24 / 4];

#define SAVE_SET_STATE(s) (flowOwner[0x24 / 4] = (s))
    switch (state) {
        case 1:   /* boot load: "Loading." */
        case 2: { /* reload */
            *timer += step;
            if (SaveDataManager_IsNandBusy()) {
                break;
            }
            result = SaveDataManager_GetLoadResult(saveManager);
            if (result == SAVE_LOAD_OK) {
                if (*timer >= 60.0f) {
                    SaveDataManager_EndOperation(saveManager);
                    if (state == 1) {
                        SaveDataManager_MarkLoadedValid(saveManager);
                        SaveDataManager_ApplyLoaded(saveManager);
                    }
                    SaveFlow_Close();
                    SAVE_SET_STATE(0x0e);
                }
            }
            else if (result == SAVE_LOAD_NO_FILE) {
                SaveDataManager_EndOperation(saveManager);
                SaveDataManager_StartSpaceCheck(saveManager);
                SAVE_SET_STATE(3);
            }
            else if (*timer >= 60.0f) {
                /* "The file cannot be used because the data is corrupted." */
                SaveDataManager_EndOperation(saveManager);
                SaveFlow_ShowMessage(13, 1);
                SAVE_SET_STATE(4);
            }
            break;
        }
        case 3:
            /* No file: check free space, then offer to create one. */
            *timer += step;
            if (SaveDataManager_IsNandBusy() || *timer < 60.0f) {
                break;
            }
            switch (SaveDataManager_CheckFreeSpace()) {
                case 0:
                    /* "There is no file loaded..." -> "Create new file. Proceed? Yes/No" */
                    SaveFlow_ShowMessage(0, 0);
                    SAVE_SET_STATE(6);
                    break;
                case 4:
                    SaveFlow_ShowMessage(2, 1);
                    SAVE_SET_STATE(8);
                    break;
                case 5:
                    SaveFlow_ShowMessage(5, 1);
                    SAVE_SET_STATE(9);
                    break;
                default:
                    break;
            }
            break;
        case 4:
            /* Corrupted: "Continue without saving / loading" or "Delete file". */
            result = UiRootManager_GetBootTransitionResult(uiRoot);
            if (result == 0) {
                SaveFlow_ShowMessage(9, 1);
                SAVE_SET_STATE(0x0a);
            }
            else if (result == 1) {
                SaveFlow_ShowMessage(12, -1); /* "Deleting data." */
                *timer = 0.0f;
                SaveDataManager_DeleteFile(saveManager);
                SAVE_SET_STATE(5);
            }
            break;
        case 5:
            *timer += step;
            if (!SaveDataManager_IsNandBusy()) {
                SaveDataManager_EndOperation(saveManager);
                SaveDataManager_StartSpaceCheck(saveManager);
                SAVE_SET_STATE(3);
            }
            break;
        case 6:
            /* "Create new file. Proceed?" */
            result = UiRootManager_GetBootTransitionResult(uiRoot);
            if (result == 0) {
                SaveFlow_ShowMessage(8, -1); /* "Creating data." */
                *timer = 0.0f;
                if (flowOwner[0x2c / 4] == 1) {
                    SaveDataManager_InitNewData(saveManager, 1);
                }
                SaveDataManager_MarkLoadedValid(saveManager);
                SaveDataManager_WriteFile(saveManager);
                SaveDataManager_ApplyLoaded(saveManager);
                SAVE_SET_STATE(7);
            }
            else if (result == 1) {
                /* "You will not save. Continue playing?" */
                SaveFlow_ShowMessage(9, 1);
                SAVE_SET_STATE(0x0d);
            }
            break;
        case 7:
            *timer += step;
            if (!SaveDataManager_IsNandBusy() && *timer >= 60.0f) {
                SaveDataManager_EndOperation(saveManager);
                SaveFlow_Close();
                SAVE_SET_STATE(0x0e);
            }
            break;
        case 8:
        case 9:
            /* Not enough space: continue without saving / return to the Wii Menu. */
            result = UiRootManager_GetBootTransitionResult(uiRoot);
            if (result == 0) {
                SaveFlow_ShowMessage(9, 1);
                SAVE_SET_STATE(state == 8 ? 0x0b : 0x0c);
            }
            else if (result == 1) {
                SaveFlow_Close();
                SAVE_SET_STATE(0x0f);
            }
            break;
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
            /* "You will not save. Continue playing?" */
            result = UiRootManager_GetBootTransitionResult(uiRoot);
            if (result == 0) {
                SaveFlow_Close();
                SaveDataManager_ClearLoadedValid(saveManager);
                if (flowOwner[0x2c / 4] == 1) {
                    SaveDataManager_InitNewData(saveManager, 0);
                }
                else {
                    SaveDataManager_ApplyToGame(saveManager);
                }
                SAVE_SET_STATE(0x0e);
            }
            else if (result == 1) {
                if (state == 0x0a) {
                    SaveFlow_ShowMessage(13, 1);
                    SAVE_SET_STATE(4);
                }
                else if (state == 0x0d) {
                    SaveFlow_ShowMessage(0, 0);
                    SAVE_SET_STATE(6);
                }
                else if (state == 0x0b) {
                    SaveFlow_ShowMessage(2, 1);
                    SAVE_SET_STATE(8);
                }
                else {
                    SaveFlow_ShowMessage(5, 1);
                    SAVE_SET_STATE(9);
                }
            }
            break;
        case 0x0e:
            if (UiRootManager_IsBootTransitionControllerIdle(uiRoot) != 0) {
                SAVE_SET_STATE(0x11);
            }
            break;
        case 0x0f:
        case 0x10:
            /* Exit requests: global +0x274 -> +0xA0 = 2 (Data Management) / 1 (Wii Menu). */
            if (UiRootManager_IsBootTransitionControllerIdle(uiRoot) != 0) {
                int *submanager274 = GlobalRuntimeContext_GetPointerAt(0x274);

                SAVE_SET_STATE(0x11);
                if (submanager274 != 0) {
                    submanager274[0xa0 / 4] = state == 0x0f ? 2 : 1;
                }
            }
            break;
        case 0x12:
            /* DDR (first game) save import: "Loading DanceDanceRevolution Save Data." */
            *timer += step;
            if (SaveDataManager_IsImportBusy(saveManager) == 0) {
                SaveDataManager_PollImport(saveManager);
                if (*timer >= 60.0f) {
                    unsigned int flags = (unsigned int)flowOwner[0x30 / 4];

                    if (SaveDataManager_ImportFound(saveManager) == 1) {
                        if ((flags & 1u) != 0) {
                            if (SaveDataManager_ImportAlreadyUnlocked(saveManager) == 1) {
                                if ((flags & 2u) != 0) {
                                    SaveFlow_ShowNotice(0x16);
                                    SAVE_SET_STATE(0x16);
                                }
                                else {
                                    SAVE_SET_STATE(0x17);
                                }
                            }
                            else {
                                SaveFlow_ShowMessage(0x17, 1); /* "Do you want to unlock...?" */
                                SAVE_SET_STATE(0x14);
                            }
                        }
                        else if ((flags & 2u) != 0) {
                            SaveFlow_ShowNotice(0x14);
                            SAVE_SET_STATE(0x16);
                        }
                        else {
                            SAVE_SET_STATE(0x17);
                        }
                    }
                    else {
                        /* "No save data for DanceDanceRevolution found." */
                        SaveFlow_ShowNotice(0x15);
                        SAVE_SET_STATE(0x16);
                        flowOwner[0x30 / 4] = (int)(flags | 4u);
                    }
                }
            }
            if (flowOwner[0x24 / 4] == 0x17) {
                SaveFlow_Close();
            }
            break;
        case 0x14:
            result = UiRootManager_GetBootTransitionResult(uiRoot);
            if (result == 0) {
                SaveDataManager_ApplyImport(saveManager);
                SaveDataManager_FinishImportRead(saveManager);
                UiRootManager_ConfigureBootTransitionPrompt(uiRoot, 0, 0x1a);
                SAVE_SET_STATE(0x15);
                *timer = 0.0f;
            }
            else if (result == 1) {
                SaveFlow_Close();
                SAVE_SET_STATE(0x17);
            }
            break;
        case 0x15:
            *timer += step;
            if (SaveDataManager_IsImportBusy(saveManager) == 0 && *timer >= 60.0f) {
                if (((unsigned int)flowOwner[0x30 / 4] & 2u) != 0) {
                    SaveFlow_ShowNotice(0x18);
                    SAVE_SET_STATE(0x16);
                }
                else {
                    SaveFlow_Close();
                    SAVE_SET_STATE(0x17);
                }
            }
            break;
        case 0x16:
            /* Notice: wait for A. */
            if (InputOrMenuStateManager_IsConfirmPressed(gGameMainManagers.inputOrMenuStateManager, 4) != 0) {
                SaveFlow_Close();
                SAVE_SET_STATE(0x17);
                CharacterAssetManager_PlayCue(flowOwner, 0x265);
            }
            break;
        case 0x17:
            if (UiRootManager_IsBootTransitionControllerIdle(uiRoot) != 0) {
                if (((unsigned int)flowOwner[0x30 / 4] & 4u) != 0) {
                    SaveDataManager_ReleaseImport(saveManager);
                }
                SAVE_SET_STATE(0x11);
            }
            break;
        default:
            break;
    }
#undef SAVE_SET_STATE

    if (flowOwner[0x24 / 4] != state) {
        const char *trace = getenv("DDRII_TRACE_TITLE");

        RuntimeDebugReport("SaveFlow: state 0x%x -> 0x%x\n", state, flowOwner[0x24 / 4]);
        if (trace != 0 && trace[0] == '1' && uiRoot != 0) {
            /* Prompt controller groups (uiRoot +0x30): [0] window, [1]/[2] text
               panels, [3..8] buttons, [9]/[10] page/next indicators. */
            int *controller = (int *)UiRootHostPointerFromBits(uiRoot[0x0c]);
            int i;

            RuntimeDebugReport("  uiRoot groups [1..4] = %d %d %d %d\n", uiRoot[1], uiRoot[2], uiRoot[3], uiRoot[4]);
            for (i = 0; controller != 0 && i <= 10; i++) {
                RuntimeDebugReport("  prompt group c[%d] = %d\n", i, controller[i]);
            }
            for (i = 0; controller != 0 && i <= 10; i++) {
                char tag[16];

                snprintf(tag, sizeof(tag), "prompt%d", i);
                CzanUiManager_DebugDumpGroup(controller[i], tag);
            }
        }
    }
}

/* gManager_802E70E0 (save/player-data manager) status flags live at +0x28DA4
   (addis +3, -0x725C). Bit 0x2 = the boot save load/import has run. */
#define SAVE_MANAGER_FLAGS_OFFSET 0x28da4

static unsigned int *SaveDataManager_GetFlags(int *saveManager) {
    if (saveManager == 0) {
        return 0;
    }
    return (unsigned int *)(void *)((unsigned char *)saveManager + SAVE_MANAGER_FLAGS_OFFSET);
}

static int SaveDataManager_NeedsBootImport(int *saveManager) {
    /* 0x8009BDFC: returns 1 while flag bit 0x2 is clear. */
    unsigned int *flags = SaveDataManager_GetFlags(saveManager);

    return flags == 0 || (*flags & 2U) == 0;
}

static void SaveDataManager_MarkBootImportDone(int *saveManager) {
    /* 0x8009BE14 */
    unsigned int *flags = SaveDataManager_GetFlags(saveManager);

    if (flags != 0) {
        *flags |= 2U;
    }
}

static int MiiStore_ValidateBackupDancers(void) {
    /* 0x800F3A34(gManager_802E70D0 +0x14A4): with Mii data available, checks each
       of the 6 saved backup-dancer Mii references (0x8009BEE8 / 0x80189DCC) and
       returns 0 when a referenced Mii has been deleted. The host has no Mii
       database, so no reference can be broken. */
    return 1;
}

/* Save-data flow on gManager_802E70C4 (host characterAssetManager): +0x24 state
   (0x11 = idle), +0x28 timer, +0x2C/+0x30 flow flags. */
#define SAVE_FLOW_STATE_IDLE 0x11

static void SaveFlow_StartBootLoad(int *flowOwner, int showSecondChild) {
    /* 0x800CCBE4: start the boot save-data load ("Loading. Please do not touch..."). */
    if (flowOwner == 0 || flowOwner[0x24 / 4] != SAVE_FLOW_STATE_IDLE) {
        return;
    }
    SaveDataManager_StartLoad(gGameMainManagers.manager802e70e0);
    UiRootManager_StartBootTransitionController(gGameMainManagers.uiRootManager, 6, 0, 0, showSecondChild);
    UiRootManager_ConfigureBootTransitionPrompt(gGameMainManagers.uiRootManager, 0, 10);
    flowOwner[0x24 / 4] = 1;
    *(float *)(void *)(flowOwner + 0x28 / 4) = 0.0f;
    flowOwner[0x2c / 4] = 1;
    RuntimeDebugReport("SaveFlow: boot load started (state 1)\n");
}

static void SaveFlow_StartBootImport(int *flowOwner, int showSecondChild, int flagA, int flagB, int flagC) {
    /* 0x800CCC68: start the DDR (first game) save import ("Loading
       DanceDanceRevolution Save Data", text block 0 message 25). */
    if (flowOwner == 0 || flowOwner[0x24 / 4] != SAVE_FLOW_STATE_IDLE) {
        return;
    }
    /* 0x8009BF88 / 0x8009C12C(gManager_802E70E0): prepare the import buffers. */
    UiRootManager_StartBootTransitionController(gGameMainManagers.uiRootManager, 6, 0, 0, showSecondChild);
    UiRootManager_ConfigureBootTransitionPrompt(gGameMainManagers.uiRootManager, 0, 0x19);
    flowOwner[0x24 / 4] = 0x12;
    flowOwner[0x30 / 4] = (flagA != 0 ? 1 : 0) | (flagB != 0 ? 2 : 0) | (flagC != 0 ? 4 : 0);
    *(float *)(void *)(flowOwner + 0x28 / 4) = 0.0f;
    flowOwner[0x2c / 4] = 0;
    RuntimeDebugReport("SaveFlow: boot import started (state 0x12)\n");
}

static void CSelectBootTitleFlow_OnEnter(CSelectBootTitleFlow *flow) {
    /* 0x800DE068 (vtable enter): cue mode 0, owner +0x10 -> [0] = 0 (host has no
       owner record yet), fade quad out of white (A anim 1) and background dimmer on
       (C anim 0, priority 100): the darker stage behind the save prompt and NOTICE. */
    (void)flow;
    CharacterAssetManager_SetCueMode(gGameMainManagers.characterAssetManager, 0);
    UiRootManager_StartTitleTransitionA(gGameMainManagers.uiRootManager, 1, 0, 0);
    UiRootManager_StartTitleTransitionC(gGameMainManagers.uiRootManager, 0, 100, 0);
}

static int CSelectBootTitleFlow_Tick(CSelectBootTitleFlow *flow, int forceIdle) {
    /* 0x800DE0DC: CSelect state 0. Mii-data check, then the boot save load/import,
       then hands off to the title flow (returns 0x0C). Jump table 0x802BD2D0. */
    int state;
    int next = 0;
    int *uiRootManager;
    int *flowOwner;
    int *saveManager;

    if (flow == 0 || forceIdle == 1) {
        return 0;
    }

    state = *(int *)(void *)(flow->storage + 0x130);
    uiRootManager = gGameMainManagers.uiRootManager;
    flowOwner = gGameMainManagers.characterAssetManager;
    saveManager = gGameMainManagers.manager802e70e0;

#define BOOT_SET_STATE(s) (*(int *)(void *)(flow->storage + 0x130) = (s))
    switch (state) {
        case 0:
            if (UiRootManager_IsTitleTransitionAIdle(uiRootManager) != 0 &&
                UiRootManager_IsTitleTransitionCIdle(uiRootManager) != 0) {
                BOOT_SET_STATE(1);
            }
            break;
        case 1:
            if (gMiiManagerState[0] == 1) {
                BOOT_SET_STATE(4);
                break;
            }
            /* "Mii Channel save data could not be read" ->
               Continue without Mii characters / Return to the Wii Menu. */
            UiRootManager_StartBootTransitionController(uiRootManager, 6, 1, 0, 1);
            UiRootManager_ConfigureBootTransitionPrompt(uiRootManager, 9, 0);
            BOOT_SET_STATE(2);
            UiRootManager_SetBootTransitionSelectedOption(uiRootManager, 1);
            *(float *)(void *)(flow->storage + 0x134) = 0.0f;
            break;
        case 2:
            if (UiRootManager_GetBootTransitionResult(uiRootManager) == 0) {
                /* "You will not be able to use Mii characters. Proceed? Yes/No" */
                UiRootManager_ConfigureBootTransitionPrompt(uiRootManager, 9, 2);
                BOOT_SET_STATE(3);
                UiRootManager_SetBootTransitionSelectedOption(uiRootManager, 1);
            }
            else if (UiRootManager_GetBootTransitionResult(uiRootManager) == 1) {
                UiRootManager_CloseBootTransitionController(uiRootManager);
                BOOT_SET_STATE(6);
            }
            break;
        case 3:
            if (UiRootManager_GetBootTransitionResult(uiRootManager) == 0) {
                UiRootManager_CloseBootTransitionController(uiRootManager);
                BOOT_SET_STATE(4);
            }
            else if (UiRootManager_GetBootTransitionResult(uiRootManager) == 1) {
                UiRootManager_ConfigureBootTransitionPrompt(uiRootManager, 9, 0);
                BOOT_SET_STATE(2);
                UiRootManager_SetBootTransitionSelectedOption(uiRootManager, 1);
            }
            break;
        case 4:
            if (UiRootManager_IsBootTransitionControllerIdle(uiRootManager) != 0) {
                SaveFlow_StartBootLoad(flowOwner, 0);
                BOOT_SET_STATE(5);
            }
            break;
        case 5:
            if (CharacterAssetManager_IsSelectCommonIdle(flowOwner) == 0) {
                break;
            }
            if (MiiStore_ValidateBackupDancers() != 0) {
                if (SaveDataManager_NeedsBootImport(saveManager) == 0) {
                    BOOT_SET_STATE(10);
                    next = 0x0c;
                }
                else {
                    SaveFlow_StartBootImport(flowOwner, 0, 1, 1, 1);
                    BOOT_SET_STATE(9);
                }
            }
            else {
                /* "The Mii character set as MY BACKUP DANCER has been deleted." */
                BOOT_SET_STATE(7);
                UiRootManager_StartBootTransitionController(uiRootManager, 6, 1, 0, 1);
                UiRootManager_ConfigureBootTransitionPrompt(uiRootManager, 9, 4);
            }
            break;
        case 6:
            /* "Return to the Wii Menu": global +0x274 -> +0xA0 requests the exit. */
            if (UiRootManager_IsBootTransitionControllerIdle(uiRootManager) != 0) {
                int *submanager274 = GlobalRuntimeContext_GetPointerAt(0x274);

                BOOT_SET_STATE(10);
                if (submanager274 != 0) {
                    submanager274[0xa0 / 4] = 1;
                }
            }
            break;
        case 7:
            if (UiRootManager_IsBootTransitionPromptReady(uiRootManager) != 0 &&
                InputOrMenuStateManager_IsConfirmPressed(gGameMainManagers.inputOrMenuStateManager, 4) != 0) {
                BOOT_SET_STATE(8);
                UiRootManager_CloseBootTransitionController(uiRootManager);
            }
            break;
        case 8:
            if (UiRootManager_IsBootTransitionControllerIdle(uiRootManager) != 0) {
                if (SaveDataManager_NeedsBootImport(saveManager) == 0) {
                    BOOT_SET_STATE(10);
                    next = 0x0c;
                }
                else {
                    SaveFlow_StartBootImport(flowOwner, 0, 1, 1, 1);
                    BOOT_SET_STATE(9);
                }
            }
            break;
        case 9:
            if (CharacterAssetManager_IsSelectCommonIdle(flowOwner) != 0) {
                SaveDataManager_MarkBootImportDone(saveManager);
                BOOT_SET_STATE(10);
                next = 0x0c;
            }
            break;
        default:
            break;
    }
#undef BOOT_SET_STATE

    if (*(int *)(void *)(flow->storage + 0x130) != state) {
        RuntimeDebugReport("CSelect boot flow: state %d -> %d\n", state, *(int *)(void *)(flow->storage + 0x130));
    }
    return next;
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
    /* 0x80102E5C: FUN_8015F080(owner, fov +0x38, aspect, 1.0, 10000.0) with
       aspect 16:9 when widescreen (global +0x258 -> +0x4C), else 4:3. */
    {
        int *videoSettings = GlobalRuntimeContext_GetPointerAt(0x258);
        float aspect = (videoSettings != 0 && videoSettings[0x4c / 4] != 0) ? 1.7777778f : 1.3333334f;
        CzanModelOwner_SetProjectionParams(
            owner, *(float *)(void *)(bytes + 0x38), aspect, 1.0, 10000.0);
    }
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
    {
        /* 0x800E14EC..0x800E1520: register entry1 child 1 ('Lets_A_US') as a pointer
           hit region and keep its index in entry1 +0x4C (triplet {0, 0, region}). */
        int triplet[3];

        triplet[0] = 0;
        triplet[1] = 0;
        triplet[2] = PointerManager_RegisterRegion(gGameMainManagers.manager802e70b4,
                                                   CSelModeEntry_GetObjectHandle(entry1, 0), 1);
        CSelModeEntry_SetTransformTriplet(entry1, triplet);
    }
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
        /* 0x800E15F0..0x800E1668: title model camera. FUN_8008D5EC(flow,
           eye, up, target, fov) stores eye at focus +0x14, up at +0x2C,
           target at +0x20 and the FOV at +0x38. */
        static const float titleCameraEye[3] = { 40.0f, -15.0f, 170.0f };
        static const float titleCameraUp[3] = { 0.0f, 1.0f, 0.0f };
        static const float titleCameraTarget[3] = { 40.0f, -15.0f, 0.0f };

        CSelectTitleCoordinator_Create(flow, entry0->objectHandles[0], 0, 0, 0);
        CSelectTitleCoordinator_ApplyVectors(
            flow,
            45.0,
            titleCameraEye,
            titleCameraUp,
            titleCameraTarget);
    }

    /* 0x800E1670..0x800E17B8: title entry mode +0x0C picks the first state.
       0 = cold boot (state 3: group +0x20 intro), 1 = return to title (state 0x0D:
       logo + title call), anything else = straight into the OP movie (state 0x0A). */
    CharacterAssetManager_SetCueMode(gGameMainManagers.characterAssetManager, 0);
    group0 = *(int *)(void *)(flow->storage + 0x1c);
    group2 = *(int *)(void *)(flow->storage + 0x24);
    if (group0 >= 0) {
        CzanUiManager_SetObjectGroupDisplayFlags(0, group0, 1, 1);
    }
    if (group2 >= 0) {
        CzanUiManager_SetObjectGroupDisplayFlags(0, group2, 1, 1);
    }
    group1 = *(int *)(void *)(flow->storage + 0x20);
    switch (*(int *)(void *)(flow->storage + 0x0c)) {
        case 0:
            *(int *)(void *)(flow->storage + 0x144) = 3;
            if (group1 >= 0) {
                CSelect_StartObjectGroupAnimation(group1, 0, 0, 0);
            }
            break;
        case 1:
            *(int *)(void *)(flow->storage + 0x144) = 0x0d;
            CSelModeEntry_StartObjectAnimation(0.0, entry0, 0, 0, 0, 0);
            CSelModeEntry_StartObjectAnimation(0.0, entry2, 0, 0, 0, 0);
            break;
        default:
            /* The original also calls 0x8010A808 here to start OP/OP43.thp. The host
               never enters with this mode yet; TODO start the movie when it does. */
            *(int *)(void *)(flow->storage + 0x144) = 0x0a;
            break;
    }
    *(float *)(void *)(flow->storage + 0x148) = 0.0f;
    *(int *)(void *)(flow->storage + 0x164) = 0;
    *(int *)(void *)(flow->storage + 0x168) = -1;
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
        /* The DOL draws the whole global list (0x800FEBE8); the UI-root fade quad
           (+0x0C) and dimmer (+0x10) belong to it during the title. */
        for (i = 3; i <= 4; i++) {
            if (gGameMainManagers.uiRootManager[i] >= 0) {
                groupHandles[groupCount++] = gGameMainManagers.uiRootManager[i];
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

    /* 0x800E1780 / 0x800E1C28 / 0x800E1D14: path table at r13-0x7C38 picks
       OP.thp for widescreen and OP43.thp for 4:3. */
    {
        int *videoSettings = GlobalRuntimeContext_GetPointerAt(0x258);
        moviePath = ResourceManager_HostPointerBits(
            (videoSettings != 0 && videoSettings[0x4c / 4] != 0) ?
                "movie/select/OP.thp" : "movie/select/OP43.thp");
    }
    CharacterAssetManager_SetCueMode(gGameMainManagers.characterAssetManager, 0);
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

/* Title cue ids played through 0x800CC894. Names come from the DOL's OSReport strings
   at 0x80279EA4. */
enum {
    TITLE_CUE_OPEN = 0x25b,       /* dtwDefSndTitleOpen */
    TITLE_CUE_VOICE_CALL = 0x286, /* dtwDefSndVoiceTitleCall */
    TITLE_CUE_OK = 0x264          /* dtwDefSndTitleOK1 / dtwDefSndTitleOK4 */
};

/* Wii button masks tested on controller slot 4 (any controller). */
#define TITLE_INPUT_CONFIRM 0x800u
#define TITLE_INPUT_CONFIRM_OR_BACK 0xc00u
#define TITLE_INPUT_ANY 0x1ffffu

static int CSelectTitle_TestPressed(unsigned int mask) {
    /* 0x8002AE28 on gManager_802E70AC: newly pressed this frame. */
    return InputOrMenuStateManager_TestPressedMask(gGameMainManagers.inputOrMenuStateManager, 4, mask) != 0;
}

static int CSelectTitle_TestHeld(unsigned int mask) {
    /* 0x8002AE08 on gManager_802E70AC: currently held. */
    return InputOrMenuStateManager_TestActiveMask(gGameMainManagers.inputOrMenuStateManager, 4, mask) != 0;
}

static float CSelectTitle_GetFrameStep(void) {
    return Runtime_GetFrameStep();
}

static void CSelectTitleFlow_UpdateOpeningMovieFade(CSelectTitleFlow *flow, float step) {
    /* 0x8010A770: +0x130 fade mode (-1 off, 0 fading, 1 fade then switch), +0x134
       fade timer, +0x138 fade length, +0x13C next fade length. */
    unsigned char *f = flow->storage;
    float *fadeTimer = (float *)(void *)(f + 0x134);
    float fadeLength = *(float *)(void *)(f + 0x138);

    if (*(int *)(void *)(f + 0x130) < 0) {
        return;
    }
    *fadeTimer += step;
    if (*fadeTimer >= fadeLength) {
        if (*(int *)(void *)(f + 0x130) == 1) {
            *fadeTimer = 0.0f;
            *(float *)(void *)(f + 0x138) = *(float *)(void *)(f + 0x13c);
            *(int *)(void *)(f + 0x130) = 2;
        }
        else {
            *fadeTimer = fadeLength;
        }
    }
}

static int CSelectTitleFlow_IsOpeningMovieReady(CSelectTitleFlow *flow) {
    /* 0x8010A9BC: fade finished and the movie slot is no longer loading. */
    return *(float *)(void *)(flow->storage + 0x134) >= *(float *)(void *)(flow->storage + 0x138) &&
           ResourceSlotHandle_IsActivePending(gGameMainManagers.manager802e70a8) == 0;
}

static void CSelectTitleFlow_ResetOpeningMovieFade(CSelectTitleFlow *flow) {
    /* 0x8010AA08 */
    *(int *)(void *)(flow->storage + 0x130) = -1;
    *(float *)(void *)(flow->storage + 0x134) = 0.0f;
    *(float *)(void *)(flow->storage + 0x138) = 30.0f;
    *(float *)(void *)(flow->storage + 0x13c) = 30.0f;
}

static void CSelectTitleFlow_StartOpeningMoviePlayback(CSelectTitleFlow *flow) {
    /* 0x8010A950 */
    (void)flow;
    if (gGameMainManagers.manager802e70a8 != 0) {
        MovieSlotHandle_StartPlayback(gGameMainManagers.manager802e70a8, gGameMainManagers.manager802e70a8[1], 0);
    }
}

static int CSelectTitleFlow_IsOpeningMovieNotPlaying(CSelectTitleFlow *flow) {
    /* 0x8010A990 */
    (void)flow;
    return gGameMainManagers.manager802e70a8 != 0 &&
           MovieSlotHandle_HasPlaybackStarted(gGameMainManagers.manager802e70a8, gGameMainManagers.manager802e70a8[1]) == 0;
}

static void CSelectTitleFlow_ReleaseOpeningMovie(CSelectTitleFlow *flow) {
    /* 0x8010A960 */
    CharacterAssetManager_SetCueMode(gGameMainManagers.characterAssetManager, 1);
    ResourceSlotHandle_Release(gGameMainManagers.manager802e70a8);
    *(int *)(void *)(flow->storage + 0x154) = 0;
}

static int CSelectTitle_IsPointerOnScreen(CSelectTitleFlow *flow) {
    /* 0x8010F014(+0x158, 4): any remote's record flag 0x8 (pointer on screen). */
    (void)flow;
    return PointerManager_IsOnScreen(gGameMainManagers.manager802e70b4, 4);
}

static void CSelectTitle_SetPressStartColor(CSelectTitleFlow *flow, unsigned int rgba) {
    /* 0x8011078C(entry1, 0, 1, &rgba, 1) -> CzanUiManager_SetObjectGroupColorBlocks.
       Pointer hover only: grey 0x808080FF normal, white 0xFFFFFFFF highlighted.
       TODO: port 0x80175B00 / 0x80175C28. */
    (void)flow;
    (void)rgba;
}

static void CSelectTitle_StartEntryAnimation(CSelectTitleFlow *flow, int entryOffset, int anim, int loop, int mode, double startFrame) {
    /* 0x801105FC(f1 startFrame, entry, slot 0, anim, loop, mode) */
    CSelModeEntry_StartObjectAnimation(startFrame, flow->storage + entryOffset, 0, anim, loop, mode);
}

static void CSelectTitle_HideEntry(CSelectTitleFlow *flow, int entryOffset) {
    int handle = CSelModeEntry_GetObjectHandle(flow->storage + entryOffset, 0);

    if (handle >= 0) {
        CzanUiManager_SetObjectGroupDisplayFlags(0, handle, 1, 1);
    }
}

static void CSelectTitleFlow_TraceGroups(CSelectTitleFlow *flow) {
    /* Debug aid: DDRII_TRACE_TITLE=1 dumps every title object on each state change. */
    static int enabled = -1;
    static const char *const names[6] = { "caution", "notice", "bemani", "logo", "pressA", "bg" };
    static const int offsets[6] = { 0x1c, 0x20, 0x24, 0x16c, 0x1bc, 0x20c };
    int i;

    if (enabled < 0) {
        const char *value = getenv("DDRII_TRACE_TITLE");
        enabled = value != 0 && value[0] == '1';
    }
    if (!enabled || flow == 0) {
        return;
    }
    for (i = 0; i < 6; i++) {
        int handle = i < 3 ? *(int *)(void *)(flow->storage + offsets[i])
                           : CSelModeEntry_GetObjectHandle(flow->storage + offsets[i], 0);

        if (handle >= 0) {
            CzanUiManager_DebugDumpGroup(handle, names[i]);
        }
    }
    if (gGameMainManagers.uiRootManager != 0) {
        RuntimeDebugReport("  uiRoot fade +0x0C=%d dim +0x10=%d\n",
                           gGameMainManagers.uiRootManager[3], gGameMainManagers.uiRootManager[4]);
        CzanUiManager_DebugDumpGroup(gGameMainManagers.uiRootManager[3], "fade");
        CzanUiManager_DebugDumpGroup(gGameMainManagers.uiRootManager[4], "dim");
    }
}

static int CSelectTitleFlow_Tick(CSelectTitleFlow *flow, int *cSelect, int forceIdle) {
    /* 0x800E1988. Returns the next CSelect state: 0x0C (stay), 1 (mode select), or
       0x1D (idle timeout to attract/demo). Timer +0x148 advances by the frame step,
       so thresholds below are in 60 Hz frames. */
    enum { E0 = 0x16c, E1 = 0x1bc, E2 = 0x20c };
    unsigned char *f;
    int state;
    int next = 0x0c;
    int group;
    float step;
    float *timer;

    if (flow == 0 || forceIdle == 1) {
        return 0x0c;
    }

    f = flow->storage;
    timer = (float *)(void *)(f + 0x148);
    step = CSelectTitle_GetFrameStep();
    CSelectTitleFlow_UpdateOpeningMovieFade(flow, step);
    state = *(int *)(void *)(f + 0x144);

#define TITLE_SET_STATE(s) (*(int *)(void *)(f + 0x144) = (s))
#define TITLE_I32(off) (*(int *)(void *)(f + (off)))
    switch (state) {
        case 0x00:
            /* Group +0x1C (first notice) intro animation. */
            group = TITLE_I32(0x1c);
            if (group >= 0 && CzanUiManager_IsObjectGroupAnimationDone(0, group) != 0) {
                TITLE_SET_STATE(1);
                *timer = 0.0f;
            }
            break;
        case 0x01:
            /* Hold the notice: 480 frames, or 120 once A/B was pressed. */
            *timer += step;
            if (CSelectTitle_TestPressed(TITLE_INPUT_CONFIRM_OR_BACK)) {
                TITLE_I32(0x14c) = 1;
            }
            if (*timer >= 480.0f || (TITLE_I32(0x14c) == 1 && *timer >= 120.0f)) {
                TITLE_I32(0x14c) = 0;
                TITLE_SET_STATE(2);
                CSelect_StartObjectGroupAnimation(TITLE_I32(0x1c), 1, 0, 0);
            }
            break;
        case 0x02:
            group = TITLE_I32(0x1c);
            if (group >= 0 && CzanUiManager_IsObjectGroupAnimationDone(0, group) != 0) {
                CzanUiManager_SetObjectGroupDisplayFlags(0, group, 1, 1);
                TITLE_SET_STATE(3);
                CSelect_StartObjectGroupAnimation(TITLE_I32(0x20), 0, 0, 0);
            }
            break;
        case 0x03:
            /* Cold boot enters here: group +0x20 intro animation. */
            group = TITLE_I32(0x20);
            if (group >= 0 && CzanUiManager_IsObjectGroupAnimationDone(0, group) != 0) {
                TITLE_SET_STATE(4);
                *timer = 0.0f;
            }
            break;
        case 0x04:
            /* Fixed 120-frame hold (FLOAT_802E8DE8 = 120.0f); not skippable. */
            *timer += step;
            if (*timer >= 120.0f) {
                TITLE_SET_STATE(5);
                TITLE_I32(0x14c) = 0;
                CSelect_StartObjectGroupAnimation(TITLE_I32(0x20), 1, 0, 0);
            }
            break;
        case 0x05:
            group = TITLE_I32(0x20);
            if (group >= 0 && CzanUiManager_IsObjectGroupAnimationDone(0, group) != 0) {
                CzanUiManager_SetObjectGroupDisplayFlags(0, group, 1, 1);
                TITLE_SET_STATE(0x0c);
                UiRootManager_StartTitleTransitionC(gGameMainManagers.uiRootManager, 1, 100, 0);
                UiRootManager_StartTitleTransitionA(gGameMainManagers.uiRootManager, 0, 0x32, 0);
            }
            break;
        case 0x0c:
            if (UiRootManager_IsTitleTransitionAIdle(gGameMainManagers.uiRootManager) != 0 &&
                UiRootManager_IsTitleTransitionCIdle(gGameMainManagers.uiRootManager) != 0) {
                TITLE_SET_STATE(0x0a);
                CSelectTitleFlow_StartOpeningMovie(flow, cSelect);
            }
            break;
        case 0x07:
            group = TITLE_I32(0x24);
            if (group >= 0 && CzanUiManager_IsObjectGroupAnimationDone(0, group) != 0) {
                TITLE_SET_STATE(8);
                *timer = 0.0f;
                TITLE_I32(0x14c) = 0;
            }
            break;
        case 0x08:
            /* 120-frame hold on group +0x24; A/B skips only when entry mode +0x0C != 0. */
            *timer += step;
            if (CSelectTitle_TestPressed(TITLE_INPUT_CONFIRM_OR_BACK) && TITLE_I32(0x0c) != 0) {
                TITLE_I32(0x14c) = 1;
            }
            if (*timer >= 120.0f || TITLE_I32(0x14c) == 1) {
                TITLE_I32(0x14c) = 0;
                TITLE_SET_STATE(9);
                CSelect_StartObjectGroupAnimation(TITLE_I32(0x24), 0, 0, 3);
                CSelectTitleFlow_StartOpeningMovie(flow, cSelect);
            }
            break;
        case 0x09:
            group = TITLE_I32(0x24);
            if (group >= 0 && CzanUiManager_IsObjectGroupAnimationDone(0, group) != 0) {
                CzanUiManager_SetObjectGroupDisplayFlags(0, group, 1, 1);
                TITLE_SET_STATE(0x0a);
            }
            break;
        case 0x0a:
            /* OP movie loading. A/B skips straight to the title. */
            if (CSelectTitleFlow_IsOpeningMovieReady(flow)) {
                TITLE_SET_STATE(0x0b);
                CSelectTitleFlow_ResetOpeningMovieFade(flow);
                CSelectTitleFlow_StartOpeningMoviePlayback(flow);
            }
            else if (CSelectTitle_TestHeld(TITLE_INPUT_CONFIRM_OR_BACK)) {
                CSelectTitleFlow_ReleaseOpeningMovie(flow);
                TITLE_SET_STATE(6);
                UiRootManager_StartTitleTransitionC(gGameMainManagers.uiRootManager, 1, 100, 0);
                UiRootManager_StartTitleTransitionB(gGameMainManagers.uiRootManager, 1, 0x32, 0);
            }
            break;
        case 0x0b:
            /* OP movie playing until it ends or A/B is pressed. */
            if (CSelectTitleFlow_IsOpeningMovieNotPlaying(flow) ||
                CSelectTitle_TestHeld(TITLE_INPUT_CONFIRM_OR_BACK)) {
                CSelectTitleFlow_ReleaseOpeningMovie(flow);
                TITLE_SET_STATE(6);
                UiRootManager_StartTitleTransitionC(gGameMainManagers.uiRootManager, 1, 100, 0);
                UiRootManager_StartTitleTransitionB(gGameMainManagers.uiRootManager, 1, 0x32, 0);
            }
            break;
        case 0x06:
            if (UiRootManager_IsTitleTransitionAIdle(gGameMainManagers.uiRootManager) != 0) {
                int handle;

                TITLE_SET_STATE(0x0d);
                CSelectTitle_StartEntryAnimation(flow, E0, 0, 0, 0, 0.0);
                CSelectTitle_StartEntryAnimation(flow, E2, 0, 0, 0, 0.0);
                *timer = 0.0f;
                handle = CSelModeEntry_GetObjectHandle(f + E2, 0);
                if (handle >= 0) {
                    CzanUiManager_SetObjectGroupAnimationResetMode(0, handle, 1);
                }
                TITLE_I32(0x260) = 0;
            }
            break;
        case 0x0d:
            /* Title logo intro (entry0) with "Press A" (entry1) fading in at frame 390. */
            *timer += step;
            if (TITLE_I32(0x150) == 1 && TITLE_I32(0x164) == 0 && *timer >= 150.0f) {
                TITLE_I32(0x168) = CharacterAssetManager_PlayCue(gGameMainManagers.characterAssetManager, TITLE_CUE_OPEN);
                RuntimeDebugReport("dtwDefSndTitleOpen\n");
                TITLE_I32(0x150) = 0;
            }
            if (CSelectTitle_TestPressed(TITLE_INPUT_CONFIRM) && TITLE_I32(0x260) == 0) {
                /* A during the intro: jump the logo to its end and show "Press A" now. */
                TITLE_I32(0x260) = 1;
                *timer = 200.0f;
                CSelectTitle_StartEntryAnimation(flow, E0, 0, 0, 0, 1000.0);
                CSelModeEntry_SetObjectEnabled(f + E1, 0, -1, 0);
                CSelectTitle_StartEntryAnimation(flow, E1, 0, 0, 0, 0.0);
                TITLE_I32(0x164) = 1;
                if (TITLE_I32(0x168) >= 0) {
                    /* 0x8002456C(gManager_802E70A4, handle, 250): fade out the open cue. */
                    RuntimeDebugReport("CSelect title: fade cue %d (250)\n", TITLE_I32(0x168));
                }
            }
            if (*timer >= 390.0f && TITLE_I32(0x260) == 0) {
                CSelModeEntry_SetObjectEnabled(f + E1, 0, -1, 0);
                CSelectTitle_StartEntryAnimation(flow, E1, 0, 0, 0, 0.0);
                TITLE_I32(0x260) = 1;
            }
            if (CSelModeEntry_IsObjectAnimationDone(f + E0, 0) != 0) {
                CharacterAssetManager_SetCueMode(gGameMainManagers.characterAssetManager, 1);
                TITLE_SET_STATE(0x0e);
                *timer = 0.0f;
                CharacterAssetManager_PlayCue(gGameMainManagers.characterAssetManager, TITLE_CUE_VOICE_CALL);
                RuntimeDebugReport("dtwDefSndVoiceTitleCall\n");
            }
            else if (*timer > 390.0f && CSelectTitle_TestPressed(TITLE_INPUT_CONFIRM)) {
                CSelectTitle_StartEntryAnimation(flow, E1, 5, 0, 0, 0.0);
                CSelectTitle_StartEntryAnimation(flow, E0, 1, 0, 0, 0.0);
                /* 0x8009B59C(gManager_802E70E0): commit pending player/controller setup. */
                TITLE_SET_STATE(0x0f);
                CharacterAssetManager_PlayCue(gGameMainManagers.characterAssetManager, TITLE_CUE_OK);
                RuntimeDebugReport("dtwDefSndTitleOK1\n");
            }
            /* "Press A" intro (anim 0) done -> loop the blink (anim 1). */
            if (CSelModeEntry_IsObjectAnimationDone(f + E1, 0) != 0 &&
                CSelModeEntry_GetCachedAnimationId(f + E1, 0) == 0) {
                CSelectTitle_StartEntryAnimation(flow, E1, 1, 1, 0, 0.0);
            }
            break;
        case 0x0e: {
            /* Title idle: waiting for A. 1200 frames with no input -> attract timeout. */
            int previousPointer = TITLE_I32(0x15c);
            int pointer;

            if (CSelModeEntry_IsObjectAnimationDone(f + E1, 0) != 0 &&
                CSelModeEntry_GetCachedAnimationId(f + E1, 0) == 0) {
                CSelectTitle_StartEntryAnimation(flow, E1, 1, 1, 0, 0.0);
            }
            pointer = CSelectTitle_IsPointerOnScreen(flow) != 0;
            TITLE_I32(0x15c) = pointer;
            if (!CSelectTitle_TestHeld(TITLE_INPUT_ANY) && pointer == 0) {
                *timer += step;
            }
            else {
                *timer = 0.0f;
            }
            if (previousPointer != pointer) {
                if (pointer == 1) {
                    CSelectTitle_StartEntryAnimation(flow, E1, 2, 0, 0, 0.0);
                    CSelectTitle_SetPressStartColor(flow, 0x808080ffu);
                }
                else {
                    CSelectTitle_StartEntryAnimation(flow, E1, 2, 0, 3, 0.0);
                }
            }
            /* 0x800E22B4..0x800E23E8: pointer hit test on 'Lets_A_US' (region at
               entry1 +0x4C). Hover plays anim 4 once the flip-in (anim 2) is shown;
               leaving plays anim 2 from frame 100; a finished flip-out with the
               pointer gone returns to the 'Point at the screen' blink (anim 1). */
            {
                CSelModeEntryKnownFields *entry1 = (CSelModeEntryKnownFields *)(void *)(f + E1);
                int hits = PointerManager_HitTestRegion(gGameMainManagers.manager802e70b4, 0,
                                                        entry1->transformOrState2);

                if (hits == 1) {
                    if (CSelModeEntry_GetCachedAnimationId(f + E1, 0) == 2) {
                        CSelectTitle_StartEntryAnimation(flow, E1, 4, 0, 0, 0.0);
                        CSelectTitle_SetPressStartColor(flow, 0xffffffffu);
                        /* 0x8010F0A4: rumble the hovering remotes (no host rumble). */
                    }
                }
                else if (CSelModeEntry_GetCachedAnimationId(f + E1, 0) == 4) {
                    CSelectTitle_StartEntryAnimation(flow, E1, 2, 0, 0, 100.0);
                    CSelectTitle_SetPressStartColor(flow, 0x808080ffu);
                }
                else if (CSelModeEntry_GetCachedAnimationId(f + E1, 0) == 2 &&
                         CSelModeEntry_IsObjectAnimationDone(f + E1, 0) != 0 &&
                         TITLE_I32(0x15c) == 0) {
                    CSelectTitle_StartEntryAnimation(flow, E1, 1, 1, 0, 0.0);
                }
            }

            if (*timer >= 1200.0f) {
                CSelectTitle_StartEntryAnimation(flow, E0, 1, 0, 0, 0.0);
                CSelectTitle_StartEntryAnimation(flow, E1, 0, 0, 3, 0.0);
                TITLE_SET_STATE(0x10);
                CSelectTitleCoordinator_ClearModelGroup(flow);
            }
            else if (CSelectTitle_TestPressed(TITLE_INPUT_CONFIRM)) {
                CSelectTitle_StartEntryAnimation(flow, E1, 5, 0, 0, 0.0);
                CharacterAssetManager_PlayCue(gGameMainManagers.characterAssetManager, TITLE_CUE_OK);
                RuntimeDebugReport("dtwDefSndTitleOK4\n");
                CSelectTitle_StartEntryAnimation(flow, E0, 1, 0, 0, 0.0);
                CSelectTitleCoordinator_ClearModelGroup(flow);
                /* 0x8009B59C(gManager_802E70E0) */
                TITLE_SET_STATE(0x0f);
            }
            break;
        }
        case 0x0f:
            /* A accepted: wait for the logo outro, then go to mode select. */
            if (CSelModeEntry_IsObjectAnimationDone(f + E0, 0) != 0) {
                CSelectTitle_HideEntry(flow, E0);
                CSelectTitleCoordinator_ClearModelGroup(flow);
                next = 1;
            }
            break;
        case 0x10:
            /* Idle timeout: wait for the logo outro, flag the CSelect owner and
               return 0x1D (attract/demo request). */
            if (CSelModeEntry_IsObjectAnimationDone(f + E0, 0) != 0) {
                int *owner = (int *)RuntimePointerFromBits(TITLE_I32(0x10));

                if (owner != 0) {
                    owner[0] = 8;
                }
                CSelectTitle_HideEntry(flow, E0);
                CSelectTitleCoordinator_ClearModelGroup(flow);
                next = 0x1d;
            }
            break;
        default:
            break;
    }
#undef TITLE_I32
#undef TITLE_SET_STATE

    if (*(int *)(void *)(f + 0x144) != state) {
        RuntimeDebugReport("CSelect title: state 0x%x -> 0x%x\n", state, *(int *)(void *)(f + 0x144));
        CSelectTitleFlow_TraceGroups(flow);
    }
    if (next != 0x0c) {
        RuntimeDebugReport("CSelect title: state 0x%x returns CSelect state 0x%x\n", state, next);
    }

    UiRootManager_UpdateGlobalCzanListOnce(gGameMainManagers.uiRootManager, 0);
    if (gGameMainManagers.uiRootManager != 0) {
        gGameMainManagers.uiRootManager[0x28 / 4] = 0;
    }
    CSelectTitleCoordinator_TickDraw(flow);
    return next;
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

/* 0x80129414 / 0x801294E0 / 0x8012953C: small 0x0C-byte timeline object built
   from select_bin root block 0x0F (boss-folder UI). Layout:
   +0x00 uiManager, +0x04 object group handle, +0x08 start frame. */
typedef struct CSelectSelectBinTimeline {
    int uiManager;
    int groupHandle;
    float startFrame;
} CSelectSelectBinTimeline;

static void CSelectSelectBinTimeline_InitFromBlock(
    CSelectSelectBinTimeline *timeline,
    int uiManager,
    const unsigned char *blockData,
    unsigned int blockSize,
    float startFrame) {
    timeline->groupHandle = -1;
    timeline->startFrame = 0.0f;
    timeline->uiManager = uiManager;
    HostCzan_RegisterLinkSize(blockData, blockSize);
    timeline->groupHandle = CzanUiManager_CreateObjectGroup(uiManager, (void *)blockData, 2, 0);
    CzanUiManager_SetObjectGroupPriority(uiManager, timeline->groupHandle, 0x400);
    /* +0x173 = 1: the group exists but is not drawn until Start. */
    CzanUiManager_SetObjectGroupEnabled(uiManager, timeline->groupHandle, 1);
    timeline->startFrame = startFrame;
}

void CSelectSelectBinTimeline_Start(void *timelinePointer) {
    CSelectSelectBinTimeline *timeline = (CSelectSelectBinTimeline *)timelinePointer;

    if (timeline == 0 || timeline->groupHandle < 0) {
        return;
    }
    CzanUiManager_SetObjectGroupAnimationResetMode(timeline->uiManager, timeline->groupHandle, 1);
    CzanUiManager_SetObjectGroupEnabled(timeline->uiManager, timeline->groupHandle, 0);
    CzanUiManager_StartObjectGroupAnimation(
        timeline->startFrame, timeline->uiManager, timeline->groupHandle, 0);
}

void CSelectSelectBinTimeline_Finalize(void *timelinePointer) {
    CSelectSelectBinTimeline *timeline = (CSelectSelectBinTimeline *)timelinePointer;

    if (timeline == 0 || timeline->groupHandle < 0) {
        return;
    }
    CzanUiManager_SetObjectGroupDisplayFlags(timeline->uiManager, timeline->groupHandle, 1, 1);
}

static void CSelect_LoadSelectBinSharedResources(int *cSelect) {
    void *linkData;
    unsigned int linkSize;
    int blockCount;
    int blockIndex;
    int regionIndex;
    CzanLinkBlock block;
    CSelectSelectBinTimeline *timeline;

    /* 0x800470E0, called from the CSelect_Tick gate once select_bin_XX.bin is
       loaded (+0x14 == 1). Order and conditions follow the DOL exactly. */
    regionIndex = CSelect_GetRegionIndex();
    if (cSelect == 0 || cSelect[0x10 / 4] != 0) {
        return;
    }
    cSelect[0x14 / 4] = 0;

    linkData = CSelect_GetLoadedResourceData(cSelect[0x1bfc / 4], &linkSize);
    if (linkData == 0 || !CzanLinkResource_IsValid(linkData, linkSize)) {
        ResourceHandle *debugHandle = (ResourceHandle *)RuntimePointerFromBits(cSelect[0x1bfc / 4]);
        RuntimeDebugReport("CSelect: %s is missing or invalid (handle=%p loaded=%d size=%d)\n",
                           CSelectResourcePaths[regionIndex][1], (void *)debugHandle,
                           debugHandle != 0 ? debugHandle->loaded : -1,
                           debugHandle != 0 ? (int)debugHandle->size : -1);
        return;
    }

    /* Shared TPL textures: blocks 0 .. blockCount - 0x12 into +0x44. With the
       16-block US/FR/SP files this loop runs zero times, as on hardware. */
    blockCount = (int)CzanLinkResource_GetBlockCount(linkData, linkSize);
    for (blockIndex = 0; blockIndex < blockCount - 0x11; blockIndex++) {
        if (CzanLinkResource_GetBlock(linkData, linkSize, (unsigned int)blockIndex, &block)) {
            cSelect[(0x44 / 4) + blockIndex] = (int)CreateTextureFromTplResource(
                (TextureManagerKnownFields *)GlobalRuntimeContext_GetPointerAt(0x26c),
                (void *)block.data,
                (int)block.size,
                0xFFFFFFFFu);
        }
    }

    /* +0x1BF8: timeline object from root block 0x0F, created hidden. */
    timeline = (CSelectSelectBinTimeline *)MemoryPool_AllocateAligned(0, 0x0c, 0x20);
    cSelect[0x1bf8 / 4] = RuntimePointerBits(timeline);
    if (timeline != 0 && CzanLinkResource_GetBlock(linkData, linkSize, 0x0f, &block)) {
        CSelectSelectBinTimeline_InitFromBlock(
            timeline,
            RuntimePointerBits(GlobalRuntimeContext_GetPointerAt(0x270)),
            block.data,
            block.size,
            0.0f);
    }

    cSelect[0x10 / 4] = 1;
    if (cSelect[0x18 / 4] == 0) {
        CSelect_StoreLoadedResource(cSelect, 0x1c04 / 4, CSelectResourcePaths[regionIndex][2]);
        cSelect[0x1c / 4] = 1;
    }
    else if (cSelect[0x08 / 4] == 0) {
        CSelect_StoreLoadedResource(cSelect, 0x1c00 / 4, CSelectResourcePaths[regionIndex][0]);
        cSelect[0x0c / 4] = 1;
    }
    RuntimeDebugReport("CSelect: %s parsed\n", CSelectResourcePaths[regionIndex][1]);
}

/* Resource gating in front of the active-screen switch (0x80045D2C-0x80045EB8).
   Returns 1 when the next state must wait. The cue-manager checks
   (FUN_80023DA8) are not ported yet. */
static int CSelect_IsNextStateBlocked(const int *cSelect) {
    switch (cSelect[0x3c / 4]) {
        case 0:
            return 0;
        case 5:
            return cSelect[0x08 / 4] == 0 || cSelect[0x28 / 4] == 0;
        case 0x0c:
            return cSelect[0x18 / 4] == 0;
        case 0x0e:
            return cSelect[0x20 / 4] == 0 || cSelect[0x28 / 4] == 0;
        case 0x10:
            return cSelect[0x34 / 4] != 0 || cSelect[0x28 / 4] == 0;
        default:
            return cSelect[0x10 / 4] == 0 || cSelect[0x28 / 4] == 0;
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
        if (InputOrMenuStateManager_TestRepeatMask(inputManager, 4, 2) != 0) {
            selectedIndex = (selectedIndex + 1) % 5;
        }
        else if (InputOrMenuStateManager_TestRepeatMask(inputManager, 4, 1) != 0) {
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
        int *selectCommon = (int *)RuntimePointerFromBits(*(int *)(void *)(flow->storage + 0x130));
        if (*(int *)(void *)(flow->storage + 0x144) == 1) {
            CSelModeEntry_StartObjectAnimation(0.0, flow->storage + 0x150, 0, 1, 0, 0);
            /* 0x8008CE08: confirm moves the background forward. */
            CSelectCommon_AdvanceBackgroundForward(selectCommon, 1);
        }
        else {
            CSelModeEntry_ResetObjectAnimation(flow->storage + 0x150, 0);
            CSelModeEntry_StartObjectAnimation(0.0, flow->storage + 0x150, 0, 0, 0, 3);
            /* 0x8008CE5C: back moves the background backward. */
            CSelectCommon_AdvanceBackgroundBackward(selectCommon, 1);
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
            CSelectBootTitleFlow_OnEnter(&bootTitleFlowStorage);
            cSelect[0x1bf0 / 4] = RuntimePointerBits(&bootTitleFlowStorage);
            cSelect[0x38 / 4] = nextSelectState;
            return 1;
        case 1:
            linkData = CSelect_GetLoadedResourceData(cSelect[0x1bfc / 4], &linkSize);
            if (linkData == 0) {
                if (lastMissingState != 1001) {
                    lastMissingState = 1001;
                    RuntimeDebugReport("CSelect: mode select waits for select_bin payload\n");
                }
                return 0;
            }

            if (!CzanLinkResource_GetBlock(linkData, linkSize, 0, &block)) {
                RuntimeDebugReport("CSelect: select_bin payload has no block 0\n");
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
            linkData = CSelect_GetLoadedResourceData(cSelect[0x1bfc / 4], &linkSize);
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
        /* 0x80045454: normal boot. State 0 (boot/save flow) starts at once and
           selTitle_XX.bin is requested here. select_bin_XX.bin follows from the
           selTitle gate in CSelect_Tick, then selMusic from 0x800470E0. */
        cSelect[0xe] = 0;
        cSelect[0xf] = 0;
        CSelect_StoreLoadedResource(cSelect, 0x701, CSelectResourcePaths[regionIndex][2]);
        cSelect[7] = 1;
        /* TODO: FUN_8004BC40 (fresh title setup of the +0xC4 player data). */
    }

    /* 0x800454CC */
    cSelect[0x1bf0 / 4] = 0;
    cSelect[0x1bf4 / 4] = 0;
    cSelect[0x1bf8 / 4] = 0;

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

    /* CSelect_Tick resource gates, 0x80045B20-0x80045D28, in DOL order. Every
       gate waits for the whole resource queue (FUN_80144294 == 1). */
    if (cSelect[0x2c / 4] == 1 && GlobalResourceManager260_UpdateProgress(resourceManager260) == 1) {
        /* TODO: FUN_80104340(cmnAccMdl data) and FUN_80098D58 (movie slot). */
        cSelect[0x2c / 4] = 0;
        cSelect[0x28 / 4] = 1;
    }

    /* selMusic loaded */
    if (cSelect[0x0c / 4] == 1 && GlobalResourceManager260_UpdateProgress(resourceManager260) == 1 &&
        cSelect[0x08 / 4] == 0) {
        cSelect[0x0c / 4] = 0;
        cSelect[0x08 / 4] = 1;
        if (cSelect[0x10 / 4] == 0) {
            CSelect_StoreLoadedResource(cSelect, 0x1bfc / 4, CSelectResourcePaths[regionIndex][1]);
            cSelect[0x14 / 4] = 1;
        }
    }

    /* select_bin loaded -> 0x800470E0 */
    if (cSelect[0x14 / 4] == 1 && GlobalResourceManager260_UpdateProgress(resourceManager260) == 1) {
        CSelect_LoadSelectBinSharedResources(cSelect);
    }

    /* selTitle loaded */
    if (cSelect[0x1c / 4] == 1 && GlobalResourceManager260_UpdateProgress(resourceManager260) == 1 &&
        cSelect[0x18 / 4] == 0) {
        cSelect[0x1c / 4] = 0;
        cSelect[0x18 / 4] = 1;
        if (cSelect[0x10 / 4] == 0) {
            CSelect_StoreLoadedResource(cSelect, 0x1bfc / 4, CSelectResourcePaths[regionIndex][1]);
            cSelect[0x14 / 4] = 1;
        }
    }

    /* selResult loaded */
    if (cSelect[0x24 / 4] == 1 && GlobalResourceManager260_UpdateProgress(resourceManager260) == 1 &&
        cSelect[0x20 / 4] == 0) {
        cSelect[0x24 / 4] = 0;
        cSelect[0x20 / 4] = 1;
        if (cSelect[0x08 / 4] == 0) {
            CSelect_StoreLoadedResource(cSelect, 0x1c00 / 4, CSelectResourcePaths[regionIndex][0]);
            cSelect[0x0c / 4] = 1;
        }
    }

    if (cSelect[0x1bf0 / 4] == 0) {
        if (!CSelect_IsNextStateBlocked(cSelect)) {
            CSelect_CreateActiveScreen(cSelect);
        }
        else {
            static int lastBlockedState = -1;
            if (lastBlockedState != cSelect[0x3c / 4]) {
                lastBlockedState = cSelect[0x3c / 4];
                RuntimeDebugReport("CSelect: state %d waits (+0x10=%d +0x28=%d +0x2c=%d)\n",
                                   cSelect[0x3c / 4], cSelect[0x10 / 4], cSelect[0x28 / 4], cSelect[0x2c / 4]);
            }
        }
    }

    if (cSelect[0x1bf0 / 4] != 0) {
        CharacterAssetManager_UpdateActiveAssets(gGameMainManagers.characterAssetManager, 0);
        /* 0x800CC690 also runs the save-data flow 0x800CCD2C each frame. */
        SaveFlow_Update(gGameMainManagers.characterAssetManager, Runtime_GetFrameStep());
        CSelect_UpdateSelectCommonBackground(cSelect);

        if (cSelect[0x38 / 4] == 0) {
            unsigned int backgroundColor = 0xFFFFFFFFu;
            int nextState = CSelectBootTitleFlow_Tick(
                (CSelectBootTitleFlow *)RuntimePointerFromBits(cSelect[0x1bf0 / 4]), 0);
            if (nextState != 0) {
                /* Boot/save flow finished: drop it and let the gate create the
                   next screen (0x0C waits for selTitle, +0x18). */
                cSelect[0x3c / 4] = nextState;
                cSelect[0x1bf0 / 4] = 0;
            }
            RenderBeginFrame();
            ApplyRenderConfig(0, &backgroundColor);
            /* The UiRoot update pass (0x800FEA60) runs once per frame from the main
               loop; ticking it here too consumed each button press twice. */
            CSelect_DrawSelectCommonBackground();
            if (gGameMainManagers.uiRootManager != 0) {
                UiRootManager_DrawBootCzanGroups(gGameMainManagers.uiRootManager);
            }
            PointerManager_DrawHost(gGameMainManagers.manager802e70b4);
            RenderEndFrame();
        }
        else if (cSelect[0x38 / 4] == 1) {
            unsigned int backgroundColor = 0xFFFFFFFFu;
            int nextState = CSelMode_Update();
            if (nextState != 1) {
                CSelMode_Release();
                cSelect[0x3c / 4] = nextState;
                cSelect[0x1bf0 / 4] = 0;
                CSelect_CreateActiveScreen(cSelect);
            }
            RenderBeginFrame();
            ApplyRenderConfig(0, &backgroundColor);
            CSelect_DrawSelectCommonBackground();
            if (cSelect[0x38 / 4] == 1) {
                int presentationGroups[0x50];
                int presentationCount;

                CSelMode_DrawHostUi();
                /* comAF header/footer (UI-root texture-frame banks) set up by
                   CGameUiRoot_SetMenuPresentationMode; the DOL draws them through the
                   global Czan list. */
                presentationCount = UiRootManager_GetPresentationGroups(
                    gGameMainManagers.uiRootManager, presentationGroups, 0x50);
                CzanUiManager_DrawObjectGroupsReverse(0, presentationGroups, presentationCount);
            }
            PointerManager_DrawHost(gGameMainManagers.manager802e70b4);
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
            PointerManager_DrawHost(gGameMainManagers.manager802e70b4);
            RenderEndFrame();
        }
        else if (cSelect[0x38 / 4] == 0x0c) {
            unsigned int backgroundColor = 0xFFFFFFFFu;
            int nextState = CSelectTitleFlow_Tick(
                (CSelectTitleFlow *)RuntimePointerFromBits(cSelect[0x1bf0 / 4]),
                cSelect,
                0);
            if (nextState != 0x0c) {
                /* 0x800E1904..0x800E1914 (title destructor): drop the pointer region. */
                CSelectTitleFlow *title = (CSelectTitleFlow *)RuntimePointerFromBits(cSelect[0x1bf0 / 4]);
                if (title != 0) {
                    CSelModeEntryKnownFields *entry1 = (CSelModeEntryKnownFields *)(void *)(title->storage + 0x1bc);
                    PointerManager_UnregisterRegion(gGameMainManagers.manager802e70b4, entry1->transformOrState2);
                }
            }
            if (nextState == 0x1d) {
                /* Host bridge: 0x1D is the title idle-timeout attract/demo request.
                   The demo path is not ported, so re-enter the title the way the
                   attract loop does after the demo: entry mode 2 (OP movie, state
                   0x0A) rather than the cold-boot NOTICE (mode 0). */
                CSelectTitleFlow *title;

                RuntimeDebugReport("CSelect: attract/demo request 0x1D not ported; restarting title\n");
                nextState = 0x0c;
                cSelect[0x3c / 4] = nextState;
                cSelect[0x1bf0 / 4] = 0;
                CSelect_CreateActiveScreen(cSelect);
                title = (CSelectTitleFlow *)RuntimePointerFromBits(cSelect[0x1bf0 / 4]);
                if (title != 0 && cSelect[0x38 / 4] == 0x0c) {
                    int noticeGroup = *(int *)(void *)(title->storage + 0x20);

                    if (noticeGroup >= 0) {
                        CzanUiManager_SetObjectGroupDisplayFlags(0, noticeGroup, 1, 1);
                    }
                    *(int *)(void *)(title->storage + 0x0c) = 2;
                    *(int *)(void *)(title->storage + 0x144) = 0x0a;
                    CSelectTitleFlow_StartOpeningMovie(title, cSelect);
                }
            }
            else if (nextState != 0x0c) {
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
            PointerManager_DrawHost(gGameMainManagers.manager802e70b4);
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
