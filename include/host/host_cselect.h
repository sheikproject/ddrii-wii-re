#ifndef DDRII_HOST_CSELECT_H
#define DDRII_HOST_CSELECT_H

#define HOST_SELECT_MAX_OBJECT_NAMES 512

typedef struct HostSelectCommonModelBinding {
    const void *zmbData;
    unsigned int zmbSize;
    const void *textureData;
    unsigned int textureSize;
    const void *zabData;
    unsigned int zabSize;
    unsigned int objectNameCount;
    unsigned int zabChannelCount;
    unsigned int matchedChannelCount;
    char objectNames[HOST_SELECT_MAX_OBJECT_NAMES][32];
} HostSelectCommonModelBinding;

typedef struct HostCSelectModule {
    int frame;
    int selectedModeIndex;
    int redrawNeeded;
    void *selectLinkData;
    unsigned int selectLinkSize;
    void *selectCommonLinkData;
    unsigned int selectCommonLinkSize;
    HostSelectCommonModelBinding visibleModel;
    HostSelectCommonModelBinding cameraModel;
} HostCSelectModule;

int CSelect_TickHost(void *cSelect);
void HostCSelect_SetCommonSelectResource(
    HostCSelectModule *module,
    void *selectCommonLinkData,
    unsigned int selectCommonLinkSize);

#endif
