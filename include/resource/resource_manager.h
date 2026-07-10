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
void CzanMovieObj_AllocBuffer(int *movieObj, int bufferSize);
void CzanMovieObj_InitDefaults(int *movieObj);
void CzanMovieObj_Reset(int *movieObj);
void CzanMovieObj_LoadResource(int *movieObj, int resourceOrPayload);
void LargeResourceManager_ReloadFromLink(int *largeResourceManager, int linkData);
void CharacterAssetManager_UnloadActiveAssets(int *characterAssetManager);

#endif
