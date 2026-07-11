#ifndef DDRII_RESOURCE_RESOURCE_MANAGER_H
#define DDRII_RESOURCE_RESOURCE_MANAGER_H

typedef struct ResourceHandle {
    const char *path;
    void *data;
    int size;
    int loaded;
} ResourceHandle;

ResourceHandle *LoadResourceByPath(void *resourceManager, const char *path, int flags);
int ResourceSlotManager_ClaimFreeSlot(int *slotPool);
int *ResourceSlotManager_GetClaimedSlot(int *slotPool, int slotIndex);
int ResourceSlotManager_AllocateSlot(int *slotManager, int setupData);
int ResourceSlotHandle_IsActivePending(int *slotHandle);
void ResourceSlotHandle_Rebind(int *slotHandle, int resourceOrPayload, int setupData);
void MovieSlotHandle_ResetClaimedSlot(int *slotHandle);
int MovieSlotHandle_IsReadyForDisplay(int *slotHandle, int slotIndex);
void MovieSlotHandle_SetObjectEnabled(int *slotHandle, int slotIndex, int enabled);
void MovieSlotHandle_LoadResource(int *slotHandle, int slotIndex, int resourceOrPath);
void MovieSlotHandle_StartPlayback(int *slotHandle, int slotIndex, int enabled);
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
void CzanMovieObj_InitDefaults(int *movieObj);
void CzanMovieObj_Reset(int *movieObj);
void CzanMovieObj_ClearPlaybackState(int *movieObj);
void CzanMovieObj_StartPlayback(int *movieObj, int enabled, int startParam);
void CzanMovieObj_LoadResource(int *movieObj, int resourceOrPayload);
void CzanMovieObjChild_SetEnabled(int *movieChild, int enabled);
void LargeResourceManager_ReloadFromLink(int *largeResourceManager, int linkData);
void CharacterAssetManager_UnloadActiveAssets(int *characterAssetManager);

#endif
