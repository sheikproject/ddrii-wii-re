#include "runtime/module_system.h"

void RuntimeEntry(void) {
    /* DOL runtime entry / __start.
       Performs low-level runtime setup, relocates boot info pointers,
       initializes OS/C runtime, runs constructors, then calls GameMain. */
}

int GameMain(void) {
    /* Original initializes global managers, runs MainLoopManager_Tick until shutdown,
       then destroys managers in reverse allocation order. */
    return 0;
}

int MainLoopManager_Tick(int *mainLoopManager) {
    (void)mainLoopManager;

    /* Original updates engine, UI, input/menu, active module, and reset/shutdown checks.
       Returns 1 when the main loop should exit. */
    return 0;
}

void ModuleController_ApplyPendingModule(int *moduleController) {
    (void)moduleController;

    /* Original destroys the active module when pendingModuleId != activeModuleId,
       creates the pending module, calls its enter/setup method, then stores activeModuleId. */
}

void ModuleController_CreatePendingModule(int *moduleController) {
    (void)moduleController;

    /* Original allocates a module object based on pendingModuleId:
       0 small boot object, 1 BootLogoModule, 2 CSelect, 3/5/6 CGame. */
}

void BootLogoModule_Init(void *module) {
    (void)module;

    /* Original sets BootLogoModule_VTable and initializes logo/timer/fade fields. */
}

int BootLogoModule_Tick(void *bootLogoModule, int nextModuleId) {
    (void)bootLogoModule;
    (void)nextModuleId;

    /* Original runs logo/logo_*.tpl loading, fade-in, hold, fade-out,
       and returns module ID 2 when the boot logo sequence is finished. */
    return MODULE_ID_BOOT_LOGO;
}

int CSelect_Init(void *cSelect) {
    (void)cSelect;

    /* Original initializes module ID 2, clears CSelect state, sets CSelect_VTable,
       initializes internal buffers, and clears resource/sub-screen handles. */
    return 0;
}
