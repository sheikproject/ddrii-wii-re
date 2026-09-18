#ifndef DDRII_RESOURCE_RESOURCE_MANAGER_H
#define DDRII_RESOURCE_RESOURCE_MANAGER_H

typedef struct ResourceHandle {
    const char *path;
    void *data;
    int size;
    int loaded;
} ResourceHandle;

ResourceHandle *LoadResourceByPath(void *resourceManager, const char *path, int flags);
int ResourceManager_HostPointerBits(void *pointer);
void ActiveControllerMovieBindings_SetMovieSlotManagerForHost(int *slotManager);
int ResourceSlotManager_ClaimFreeSlot(int *slotPool);
int *ResourceSlotManager_GetClaimedSlot(int *slotPool, int slotIndex);
void ResourceSlotManager_ReleaseSlot(int *slotPool, int slotIndex);
int ResourceSlotManager_AllocateSlot(int *slotManager, int setupData);
void ResourceSlotHandle_CreateSlotPool(int *slotHandle);
int ResourceSlotHandle_IsActivePending(int *slotHandle);
void ResourceSlotHandle_ReleaseIfManagerPresent(int *slotHandle);
void ResourceSlotHandle_Release(int *slotHandle);
void ResourceSlotHandle_Rebind(int *slotHandle, int resourceOrPayload, int setupData);
void MovieSlotHandle_ResetClaimedSlot(int *slotHandle);
int MovieSlotHandle_IsReadyForDisplay(int *slotHandle, int slotIndex);
void MovieSlotHandle_SetObjectEnabled(int *slotHandle, int slotIndex, int enabled);
void MovieSlotHandle_LoadResource(int *slotHandle, int slotIndex, int resourceOrPath);
void MovieSlotHandle_StartPlayback(int *slotHandle, int slotIndex, int enabled);
void MovieSlotHandle_DrawMovie(int *slotHandle, int slotIndex, int drawFlags);
int *MovieSlotHandle_GetClaimedObject(int *slotHandle, int slotIndex);
int MovieSlotHandle_HasPlaybackStarted(int *slotHandle, int slotIndex);
void MovieSlotHandle_SetPlacementRect(
    int *slotHandle,
    int slotIndex,
    double x,
    double y,
    double width,
    double height);
void MovieSlotHandle_SetPlaybackFlag278(int *slotHandle, int slotIndex, int value);
int ActiveControllerMovieBindings_HasPendingSlots(int *movieBindings, int mode);
void ActiveControllerMovieBindings_Reset(int *movieBindings);
void ActiveControllerMovieBindings_SetMode(int *movieBindings, int mode);
unsigned char ActiveControllerMovieBindings_GetVisibleModeIndex(int *movieBindings);
void ActiveControllerMovieBindings_UpdateCategoryVisibility(int *movieBindings, int forceAllVisible);
void ActiveControllerMovieBindings_SetTransitionFlagAndUpdateVisibility(
    int *movieBindings,
    int forceAllVisible,
    int transitionFlag);
void ActiveControllerMovieBindings_LoadCategoryMovie(int *movieBindings, unsigned int category);
void ActiveControllerMovieBindings_StartCategoryMovie(int *movieBindings, unsigned int category);
void CzanMovieObj_AllocBuffer(int *movieObj, int bufferSize);
void CzanMovieObj_FreeBuffer(int *movieObj);
void CzanMovieObj_InitDefaults(int *movieObj);
void CzanMovieObj_Reset(int *movieObj);
void CzanMovieObj_ClearPlaybackState(int *movieObj);
void CzanMovieObj_StartPlayback(int *movieObj, int enabled, int startParam);
void CzanMovieObj_LoadResource(int *movieObj, int resourceOrPayload);
float CzanMovieObj_GetPlaybackFps(int *movieObj);
void CzanMovieObjChild_SetEnabled(int *movieChild, int enabled);
void LargeResourceManager_ReloadFromDefaultLink(int *largeResourceManager, int linkData);
int *LargeResourceManager_Init(int *largeResourceManager);
void LargeResourceManager_ResetLoadedState(int *largeResourceManager);
void LargeResourceManager_ActivateDefaultAudioReferences(int *largeResourceManager, int *globalContext);
void LargeResourceManager_ResetAudioAndLoadDefaultSound(int *largeResourceManager, int *globalContext);
int *Manager802e70e0_Init(int *manager);
void Manager802e70e0_DestroyNoop(void);
int *CharacterAssetManager_Init(int *characterAssetManager);
void CharacterAssetManager_ResetSelectCommonState(int *characterAssetManager);
void CharacterAssetManager_LoadSelectCommon(int *characterAssetManager, void *linkData);
void CharacterAssetManager_UpdateActiveAssets(int *characterAssetManager, int skipSelectCommonUpdate);
int CharacterAssetManager_IsSelectCommonIdle(int *characterAssetManager);
int CharacterAssetManager_AreSelectSupportRecordsIdle(int *characterAssetManager);
void CharacterAssetManager_DestroyLiveSelectSupportObjects(int *characterAssetManager);
void CharacterAssetManager_SetSelectSupportEntryId(
    int *characterAssetManager,
    int entryIndex,
    int indexedId,
    int subIndex);
void CharacterAssetManager_MarkSelectSupportEntryDirty(int *characterAssetManager, int entryIndex);
void CharacterAssetManager_SetSelectSupportPartValue(
    int *characterAssetManager,
    int entryIndex,
    int partIndex,
    int value);
void CharacterAssetManager_SetSelectSupportPartColor(
    int *characterAssetManager,
    int entryIndex,
    int partIndex,
    const unsigned char rgba[4]);
void CharacterAssetManager_SetSelectSupportEntryAnimation(
    int *characterAssetManager,
    int entryIndex,
    int animationId,
    int animationSubId);
void CharacterAssetManager_ClearSelectSupportEntryAnimation(int *characterAssetManager, int entryIndex);
void CharacterAssetManager_UnloadActiveAssets(int *characterAssetManager);
int *Manager802e70b0_Init(int *manager);
void TextManager_LoadResource(int *manager, void *textLinkData);
void TextManager_SelectBank(int *manager, int bankIndex);
const char *TextManager_GetText(int *manager, int textIndex);
void FontManager_LoadResource(void *fontLinkData);
void UiRootManager_Init(int *uiRootManager);
int *ModelEffectManager_Init(int *manager);
void BootTempManager_Init(int *bootTempManager);
void CzanSoundManager_LoadArchive(int *soundManager, const char *path);
void CzanSoundManager_StopAll(int *soundManager);
void CzanSoundManager_ReleaseHandle(int *soundManager, int handle);
void CzanSoundManager_SetGlobalPause(int *soundManager, int enabled, int immediate);
void CzanSoundManager_ClearAuxState(int *soundManager);
int *ResourceSlotHandle_Init(int *slotHandle);
void GlobalCueManager_ResetRuntimeState(int *cueManager);
int *CzanSoundPlayerBank_FindFirstActiveNode(int *playerBank);
void CzanSoundPlayerNode_SetStopOrPassive(int *playerNode, int stopParam);
void CzanSoundManager_SetPlayerBankStopParam(int *soundManager, int bankIndex, int stopParam);
void GlobalCueManager_SetSoundPlayerBankStopParam(int *cueManager, int bankIndex, int stopParam);
void LargeResourceManager_SetTransitionSoundBanks(int *largeResourceManager, int stopParam);
void RuntimeSlotTable_ClearEntry(int *slotTable, int slotIndex);
void RuntimeSlotTable_SetSortKey(int *slotTable, int slotIndex, int sortKey);
void RuntimeSlotTable_SetPayload(int *slotTable, int slotIndex, int payload);
void LargeResourceStageBank_ClearStageSlot(int *stageBank, int slotIndex, int payload, int sortKey);
void LargeResourceStageBank_ClearMatrixSlot(
    int *stageBank,
    int bankIndex,
    int rowIndex,
    int payload,
    int sortKey);
void CGameTransitionSlot_Reset(int *transitionSlot);

#endif
