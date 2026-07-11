#ifndef DDRII_HOST_CSELECT_H
#define DDRII_HOST_CSELECT_H

#define HOST_SELECT_MAX_OBJECT_NAMES 512
#define HOST_SELECT_DEBUG_VERTEX_CAP 32768
#define HOST_SELECT_DEBUG_PRIMITIVE_CAP 8192

typedef struct HostSelectCommonModelBinding {
    const void *zmbData;
    unsigned int zmbSize;
    unsigned char *ownedZmbData;
    unsigned int ownedZmbSize;
    const void *textureData;
    unsigned int textureSize;
    unsigned int textureSlot;
    unsigned int hasTextureSlot;
    const void *zabData;
    unsigned int zabSize;
    unsigned char *ownedZabData;
    unsigned int ownedZabSize;
    unsigned int objectNameCount;
    unsigned int zabChannelCount;
    unsigned int zabDurationTicks;
    unsigned int matchedChannelCount;
    unsigned int panelObjectEntryOffset;
    unsigned int hasPanelObjectEntry;
    unsigned int panelVertexCount;
    float panelVertices[64][3];
    unsigned int panelPrimitiveCount;
    unsigned int panelPrimitiveStart[8];
    unsigned int panelPrimitiveVertexCount[8];
    unsigned int debugVertexCount;
    float debugVertices[HOST_SELECT_DEBUG_VERTEX_CAP][3];
    float debugTexcoords[HOST_SELECT_DEBUG_VERTEX_CAP][2];
    unsigned int debugColors[HOST_SELECT_DEBUG_VERTEX_CAP];
    unsigned int debugPrimitiveCount;
    unsigned int debugPrimitiveStart[HOST_SELECT_DEBUG_PRIMITIVE_CAP];
    unsigned int debugPrimitiveVertexCount[HOST_SELECT_DEBUG_PRIMITIVE_CAP];
    unsigned int debugPrimitiveTextureIndex[HOST_SELECT_DEBUG_PRIMITIVE_CAP];
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
    int selectCommonModelOwner[0x40];
} HostCSelectModule;

int CSelect_TickHost(void *cSelect);
void HostCSelect_SetCommonSelectResource(
    HostCSelectModule *module,
    void *selectCommonLinkData,
    unsigned int selectCommonLinkSize);

#endif
