#ifndef DDRII_RESOURCE_RESOURCE_MANAGER_H
#define DDRII_RESOURCE_RESOURCE_MANAGER_H

typedef struct ResourceHandle {
    const char *path;
    void *data;
    int size;
    int loaded;
} ResourceHandle;

ResourceHandle *LoadResourceByPath(void *resourceManager, const char *path, int flags);
void LargeResourceManager_ReloadFromLink(int *largeResourceManager, int linkData);

#endif
