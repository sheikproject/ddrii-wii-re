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
