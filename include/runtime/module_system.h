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
int RuntimeHostPointerBits(void *pointer);
int HostPointer_ToBits32(const void *pointer, const char *owner);
void *RuntimeHostPointerFromBits(int bits);
int *GlobalRuntimeContext_Get(void);
int *GlobalRuntimeContext_GetPointerAt(int byteOffset);
int GlobalRuntimeContext_SelectRegionVariant(int *globalContext);
void GameHost_SetSystemLanguage(int wiiLanguage);
int GameHost_GetSystemLanguage(void);
void GameHost_ConfigureFromArgs(int argc, char **argv);
void GlobalRuntimeContext_SetRegionVariantEntry(int *globalContext, int variantIndex, int value);
void GlobalRuntimeContext_SetFallbackRegionVariant(int *globalContext, int value);
int RuntimeVideo_GetFramebufferWidth(void);
int RuntimeVideo_GetFramebufferHeight(void);
int RuntimeVideo_GetViewportX(void);
int RuntimeVideo_GetViewportY(void);
int RuntimeVideo_GetViewportWidth(void);
int RuntimeVideo_GetViewportHeight(void);
int Runtime_GetMainLoopFrameCounter(void);
void GlobalResourceManager260_SetModeTable(int *manager, int modeTable);
void RuntimeMemory_SetCriticalFlag(int value);
void RuntimeMemory_ClearCriticalFlag(void);
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
    int arg9);
int MainLoopManager_Tick(int *mainLoopManager);
void ModuleController_ApplyPendingModule(int *moduleController);
void ModuleController_CreatePendingModule(int *moduleController);
int ModuleController_Update(ModuleControllerKnownFields *moduleController);
int *BootResourceBundle_Init(int *resourceBundle);
int *GameMain_GetBootResourceBundle(void);
int *GameMain_GetTextManager(void);
int *GameMain_GetUiRootManager(void);
int *GameMain_GetCharacterAssetManager(void);
int *GameMain_GetModelEffectManager(void);
int *GameMain_GetInputOrMenuStateManager(void);
int *GameMain_GetPointerManager(void);
void PointerManager_Load(int *manager, void *linkData);
int PointerManager_RegisterRegion(int *manager, int groupHandle, int childIndex);
void PointerManager_UnregisterRegion(int *manager, int region);
int PointerManager_HitTestRegion(int *manager, int *hitPerChannel, int region);
void PointerManager_SetCooldown(int *manager, int channel);
void PointerManager_SetSingleMode(int *manager, int singleMode);
void PointerManager_SetHidden(int *manager, int hidden);
int PointerManager_IsOnScreen(int *manager, int channel);
void BootResourceBundle_StartLoading(int *resourceBundle);
void BootResourceBundle_ApplyLoadedResources(int *resourceBundle);
int *BootResourceBundle_Release(int *resourceBundle, short releaseMode);
int GlobalResourceManager260_UpdateProgress(int *manager);
int GlobalResourceManager260_GetRecordPayloadSize(int *payload);
void Runtime_SetSoundArchiveReloadGuard(int enabled);
void GlobalSubManager274_CopyRgba48(int *manager, unsigned char *outColor);

int CSelect_Init(void *cSelect);
int CSelect_OnEnter(int *cSelect, int moduleId);
int CSelect_Tick(int *cSelect);

#endif
