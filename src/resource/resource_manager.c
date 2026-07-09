#include "resource/resource_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ResourceHandle gLastLoadedResource;
static unsigned char *gLastLoadedResourceData;

static int ReadFile(const char *path, void **outData, int *outSize) {
    FILE *file = fopen(path, "rb");
    long size;
    void *data;

    if (file == 0) {
        return 0;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return 0;
    }

    data = malloc((size_t)size);
    if (data == 0) {
        fclose(file);
        return 0;
    }

    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return 0;
    }

    fclose(file);
    *outData = data;
    *outSize = (int)size;
    return 1;
}

ResourceHandle *LoadResourceByPath(void *resourceManager, const char *path, int flags) {
    char hostPath[512];
    (void)resourceManager;

    if (gLastLoadedResourceData != 0) {
        free(gLastLoadedResourceData);
        gLastLoadedResourceData = 0;
    }

    snprintf(hostPath, sizeof(hostPath), "input\\DATA\\%s", path);
    {
        char *p;
        for (p = hostPath; *p != '\0'; p++) {
            if (*p == '/') {
                *p = '\\';
            }
        }
    }

    gLastLoadedResource.path = path;
    gLastLoadedResource.data = 0;
    gLastLoadedResource.size = 0;
    gLastLoadedResource.loaded = ReadFile(hostPath, &gLastLoadedResource.data, &gLastLoadedResource.size);
    gLastLoadedResourceData = (unsigned char *)gLastLoadedResource.data;

    printf("resource: LoadResourceByPath path=%s hostPath=%s flags=%d loaded=%d size=%d\n",
           path,
           hostPath,
           flags,
           gLastLoadedResource.loaded,
           gLastLoadedResource.size);
    return &gLastLoadedResource;
}

void LargeResourceManager_ReloadFromLink(int *largeResourceManager, int linkData) {
    /* 0x80025CA0 reloads the huge global manager allocated at DAT_802E70BC
       with size 0x3010B8. It initializes a CzanLinkManager-like stack object
       through CzanLinkManager_InitAndSetLink, tears down existing state if largeResourceManager[0]
       is nonzero, loads block 0 into the sub-manager at +0x2FBA7C, initializes
       a common manager at +0x10, initializes seven large banks starting at
       +0x91610 with stride 0x58534, marks the manager active, refreshes state,
       then releases the stack link manager with releaseMode -1.

       Bank setup passes 0x5460 for banks 0..4 and 0 for banks 5..6.
       The listing confirms incoming r4 is passed through as linkData. */
    (void)largeResourceManager;
    (void)linkData;
}
