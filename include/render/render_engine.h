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
void BuildPerspectiveProjectionMatrix(double fovYRadians, double aspect, double nearZ, double farZ, float *outMatrix44);
void UiScreenProjection_UpdateGlobals(void);
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
void DebugText_SetGlyphSize(int glyphSize);
void DebugText_InitFontBacking(void);
int DebugText_LoadFontPlanes(void *fontMemory);
int DebugText_LoadFontPlane(void *scratchOrCompressedData, int planeIndex, void *fontMemory);
void DebugText_DecodePackedGlyphPlane(void *fontHeader, void *source, void *destination);
void DebugText_ConfigureRenderState(int textureMap);
void DebugText_Draw(int x, int y, const char *text);
int GetTextureDimensions(void *textureHandle, int textureIndex, int *width, int *height);
void DrawTexturedQuad(
    RenderQuad *position,
    int width,
    int height,
    unsigned char *color,
    void *textureHandle,
    int textureIndex
);
void *CaptureFrameTextureRegion(int x, int y, int width, int height, int halfScale);
void DrawCapturedTextureQuad(
    void *textureHandle,
    int x,
    int y,
    int width,
    int height,
    const unsigned int *color,
    int flipY);
unsigned int CreateTextureFromTplResource(
    TextureManagerKnownFields *textureManager,
    void *resourceData,
    int resourceSizeOrTplBase,
    unsigned int textureSlot
);
int TextureSlot_InitFromTpl(TextureSlotKnownFields *textureSlot);
int TextureSlot_Release(TextureSlotKnownFields *textureSlot);
int TextureManager_DeleteTexture(TextureManagerKnownFields *textureManager, int textureSlot);
int BindTextureFromTextureSet(void *textureHandle, void *outTextureObject, int textureIndex);
void DrawFilledRect(int x, int y, int z, int width, int height, const unsigned int *color, int flags);
void DrawLine2D(int x0, int y0, int x1, int y1, const unsigned int *color);
void DrawTriangle2D(
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    const unsigned int *color);
void DrawTexturedTriangle2D(
    int x0,
    int y0,
    float u0,
    float v0,
    int x1,
    int y1,
    float u1,
    float v1,
    int x2,
    int y2,
    float u2,
    float v2,
    void *textureHandle,
    int textureIndex,
    const unsigned int *color);
void DrawTexturedTriangleStrip2D(
    const int (*points)[2],
    const float (*texcoords)[2],
    const unsigned int *colors,
    unsigned int vertexCount,
    void *textureHandle,
    int textureIndex);
void DrawTexturedTriangleList2D(
    const int (*points)[2],
    const float (*texcoords)[2],
    const unsigned int *colors,
    unsigned int vertexCount,
    void *textureHandle,
    int textureIndex);
void DrawMovieYuvFrame(
    const unsigned char *planeY,
    const unsigned char *planeU,
    const unsigned char *planeV,
    int width,
    int height,
    int frameToken,
    int x,
    int y,
    int drawWidth,
    int drawHeight);
void PlayMoviePcm16(const short *samples, int sampleCount, int channelCount, int sampleRate);
void UiRootManager_LoadResource(int *uiRootManager, void *linkData);
void UiRootManager_RegisterResource(int *uiRootManager, void *linkData);
int UiRootHostPointerBits(void *pointer);
void *UiRootHostPointerFromBits(int bits);
void UiEffectController_ResetOrStartFade(double duration, int *effectController);
void UiEffectController_StartMultiTargetFade(double duration, int *effectController, int primaryTarget, int secondaryTargetA, int secondaryTargetB);
void UiEffectController_StartSingleTargetFade(double duration, int *effectController, int primaryTarget, int secondaryTarget);
int UiEffectController_GetState(int *effectController);
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
void UiRootBootTransition_Update(int *subManager);
void UiRootBootTransition_AttachPromptHelpersForStart(int *subManager);
void UiRootBootTransition_ConfigurePromptHelper(int *subManager, int effectSlot, int baseEffectId);
void UiRootBootTransition_ClearActivePromptHelperText(int *subManager);
void UiRootBootTransition_SetSelectedOptionHelperText(int *subManager, int selectedOption);
void UiPromptEffectHelper_DrawByBits(int helperBits);
void UiRootManager_DrawFrame(int *uiRootManager);
void UiRootManager_DrawGlobalCzanListOnce(int *uiRootManager, int shouldDraw);
void UiRootManager_DrawBootCzanGroups(int *uiRootManager);
void UiRootManager_UpdateGlobalCzanListOnce(int *uiRootManager, int shouldUpdate);

#endif
