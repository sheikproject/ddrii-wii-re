#ifndef DDRII_RUNTIME_MODULE_SYSTEM_H
#define DDRII_RUNTIME_MODULE_SYSTEM_H

#define MODULE_ID_BOOT_SMALL 0
#define MODULE_ID_BOOT_LOGO 1
#define MODULE_ID_CSELECT 2
#define MODULE_ID_CGAME 3
#define MODULE_ID_INVALID_ASSERT 4
#define MODULE_ID_CGAME_VARIANT_5 5
#define MODULE_ID_CGAME_VARIANT_6 6

typedef struct ModuleControllerKnownFields {
    int pendingModuleId;
    int activeModuleId;
    void *activeModule;
} ModuleControllerKnownFields;

void RuntimeEntry(void);
int GameMain(void);
int MainLoopManager_Tick(int *mainLoopManager);
void ModuleController_ApplyPendingModule(int *moduleController);
void ModuleController_CreatePendingModule(int *moduleController);
int ModuleController_Update(ModuleControllerKnownFields *moduleController);

int CSelect_Init(void *cSelect);

#endif
