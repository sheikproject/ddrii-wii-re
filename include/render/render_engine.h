#ifndef DDRII_RENDER_RENDER_ENGINE_H
#define DDRII_RENDER_RENDER_ENGINE_H

typedef struct RenderQuad {
    float x;
    float y;
    float z;
    float width;
    float height;
} RenderQuad;

typedef struct TextureSlotKnownFields {
    void *resourceData;
    int resourceSizeOrTplBase;
    void *tplHeader;
    void *gxTextureObjects;
} TextureSlotKnownFields;

typedef struct TextureManagerKnownFields {
    TextureSlotKnownFields *slots;
    unsigned int nextTextureSlot;
    unsigned int slotCount;
} TextureManagerKnownFields;

void RenderBeginFrame(void);
void RenderEndFrame(void);
void ApplyRenderConfig(int screenManager, const unsigned int *renderConfigColor);
void RenderFlushPendingState(void);
void RenderFlushTexGenState(void);
void RenderFlushNoOpState(void);
void RenderCopyTexGenState(int sourceSlot, int destinationSlot);
void RenderFlushVertexDescriptorState(void);
void RenderFlushVertexAttributeFormatState(void);
void RenderRecomputeVertexStride(void);
void RenderFlushProjectionState(void);
void RenderFlushViewportState(void);
void RenderFlushMatrixIndexState(int selector);
void RenderClearVertexDescriptors(void);
void RenderBeginPrimitiveBatch(unsigned char primitiveType, unsigned char vertexFormat, unsigned short vertexCount);
void RenderSetVertexArray(int attribute, unsigned int arrayBase, unsigned int stride);
void RenderSetVertexAttrDescriptor(unsigned int vertexFormat, int attribute, unsigned int attrType, unsigned int componentType, unsigned int componentCount);
void RenderSetVertexAttrFormat(int attribute, unsigned int format);
void RenderSetBlendMode(unsigned int blendEnabled, unsigned int srcFactor, unsigned int dstFactor, unsigned int logicOp);
void RenderSetAlphaUpdate(unsigned int enabled);
void RenderSetAlphaCompare(unsigned int compare0, unsigned int reference0, unsigned int op, unsigned int compare1, unsigned int reference1);
int GetTextureDimensions(void *textureHandle, int textureIndex, int *width, int *height);
void DrawTexturedQuad(
    RenderQuad *position,
    int width,
    int height,
    unsigned char *color,
    void *textureHandle,
    int textureIndex
);
unsigned int CreateTextureFromTplResource(
    TextureManagerKnownFields *textureManager,
    void *resourceData,
    int resourceSizeOrTplBase,
    unsigned int textureSlot
);
int TextureSlot_InitFromTpl(TextureSlotKnownFields *textureSlot);
int BindTextureFromTextureSet(void *textureHandle, void *outTextureObject, int textureIndex);
void DrawFilledRect(int x, int y, int z, int width, int height, const unsigned int *color, int flags);
void UiRootManager_LoadResource(int *uiRootManager, void *linkData);
int UiRootManager_CreateReferenceObjectGroup(
    int *uiRootManager,
    int referenceObjectGroupHandle,
    int referenceChildIndex,
    unsigned char linkMode
);
void UiRootSubManager_LoadCzanGroups(int *subManager, void *linkData);
void UiRootSubManager_LoadCzanGroupsWithTexture(int *subManager, void *linkData);
void UiRootSubManager_InitTextureFrameGroups(int *subManager);
void UiRootSubManager_LoadLinkedObjectGroup(int *subManager, void *linkData);
void UiRootSubManager_LoadIndexedHiddenGroups(int *subManager, void *linkData, int setupValue);
void UiRootSubManager_ConfigureIndexedHiddenGroup(int *subManager, int groupIndex, int setupValue);
void UiRootManager_DrawFrame(int *uiRootManager);

#endif
