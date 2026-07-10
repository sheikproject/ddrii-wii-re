#include "runtime/module_system.h"
#include "runtime/boot_logo.h"
#include "host/host_cselect.h"
#include "platform/render_backend.h"
#include "render/render_engine.h"
#include "resource/resource_manager.h"
#include "select/csel_mode.h"

#include <stdio.h>
#include <windows.h>

void RuntimeEntry(void) {
    /* DOL runtime entry / __start.
       Performs low-level runtime setup, relocates boot info pointers,
       initializes OS/C runtime, runs constructors, then calls GameMain. */
    (void)GameMain();
}

int GameMain(void) {
    ModuleControllerKnownFields moduleController = {
        MODULE_ID_BOOT_LOGO,
        -1,
        0,
    };

    setvbuf(stdout, 0, _IONBF, 0);
    puts("DDRII host skeleton: GameMain");

    while (!Platform_ShouldQuit() && ModuleController_Update(&moduleController) == 0) {
        Sleep(16);
    }

    puts("DDRII host skeleton: shutdown");
    return 0;
}

int MainLoopManager_Tick(int *mainLoopManager) {
    (void)mainLoopManager;

    /* Original updates engine, UI, input/menu, active module, and reset/shutdown checks.
       Returns 1 when the main loop should exit. */
    return 0;
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
       [7] -> gLargeResourceManager via LargeResourceManager_ReloadFromLink
       [8] -> gUiRootManager via UiRootManager_LoadResource
       [9] -> gManager_802E70B4

       The original wraps the manager setup calls with FUN_80144EA0(1) / FUN_80144EF4(). */
    (void)resourceBundle;
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
    (void)resourceBundle;
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

    /* Original destroys the active module when pendingModuleId != activeModuleId,
       creates the pending module, calls its enter/setup method, then stores activeModuleId. */
    if (controller->pendingModuleId == controller->activeModuleId) {
        return;
    }

    printf("ModuleController: switch %d -> %d\n",
           controller->activeModuleId,
           controller->pendingModuleId);
    ModuleController_CreatePendingModule(moduleController);
    controller->activeModuleId = controller->pendingModuleId;
}

void ModuleController_CreatePendingModule(int *moduleController) {
    static BootLogoModuleKnownFields bootLogoModule;
    static HostCSelectModule cSelectModule;
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
            return CSelect_TickHost(moduleController->activeModule);
        default:
            return 1;
    }
}

int CSelect_Init(void *cSelect) {
    HostCSelectModule *module = (HostCSelectModule *)cSelect;
    ResourceHandle *selectBin;
    ResourceHandle *selectCommon;
    unsigned char *modeSelectLinkData;

    /* Original initializes module ID 2, clears CSelect state, sets CSelect_VTable,
       initializes internal buffers, and clears resource/sub-screen handles. */
    module->frame = 0;
    module->selectedModeIndex = 0;
    module->redrawNeeded = 1;
    module->selectLinkData = 0;
    module->selectLinkSize = 0;
    module->selectCommonLinkData = 0;
    module->selectCommonLinkSize = 0;
    puts("CSelect: init");
    CSelMode_Init(0);

    selectCommon = LoadResourceByPath(0, "select/select_cmn.bin", 0);
    if (selectCommon != 0 && selectCommon->loaded) {
        HostCSelect_SetCommonSelectResource(module, selectCommon->data, (unsigned int)selectCommon->size);
    }
    else {
        puts("CSelect: failed to load select/select_cmn.bin");
    }

    selectBin = LoadResourceByPath(0, "select/select_bin_sp.bin", 0);
    if (selectBin != 0 && selectBin->loaded && selectBin->size > 0xA0) {
        modeSelectLinkData = (unsigned char *)selectBin->data + 0xA0;
        module->selectLinkData = modeSelectLinkData;
        module->selectLinkSize = (unsigned int)selectBin->size - 0xA0u;
        CSelMode_SetHostLinkResourceSize(module->selectLinkSize);
        CSelMode_OnEnter(0, modeSelectLinkData);
    }
    else {
        puts("CSelect: failed to load select/select_bin_sp.bin");
        CSelMode_SetHostLinkResourceSize(0);
        CSelMode_OnEnter(0, 0);
    }
    return 0;
}
