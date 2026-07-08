#include "runtime/module_system.h"
#include "runtime/boot_logo.h"
#include "host/host_cselect.h"
#include "platform/render_backend.h"
#include "render/render_engine.h"
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

    /* Original initializes module ID 2, clears CSelect state, sets CSelect_VTable,
       initializes internal buffers, and clears resource/sub-screen handles. */
    module->frame = 0;
    module->selectedModeIndex = 0;
    module->redrawNeeded = 1;
    puts("CSelect: init");
    CSelMode_Init(0);
    CSelMode_OnEnter(0, 0);
    return 0;
}
