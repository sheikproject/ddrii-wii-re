#ifndef DDRII_UI_CZAN_UI_H
#define DDRII_UI_CZAN_UI_H

typedef struct CzanUiObjectInstanceKnownFields {
    unsigned char matrixOrBase[0x20];
    void *manager;
    void *spriteObject;
    int reserved28;
    int reserved2c;
    unsigned char transformAndColorState[0x120];
    int animationCounterOrTimer;
    int currentAnimationId;
    int activeAnimationEntry;
    int initialAnimIndex;
    unsigned char flags170[0x10];
    float playbackRate;
    unsigned char flags17c[0x0c];
    int unknownHandle188;
    int unknown18c;
    int unknown190;
    int unknown194;
    void *descriptor;
    int currentAnimValue;
    unsigned char tail[0x14];
} CzanUiObjectInstanceKnownFields;

typedef struct CzanSpriteObjectKnownFields {
    unsigned char base[0x20];
    int enabled;
    void *textureSlot;
    void *textureHeader;
    int textureResourceHandle;
    int textureIndex;
    int uvOrFrameIndex;
    unsigned char xAnchorMode;
    unsigned char yAnchorMode;
    unsigned char ownsTexture;
    unsigned char textureReady;
    unsigned char transformState[0x78];
    unsigned char color0[4];
    unsigned char color1[4];
    unsigned char color2[4];
    unsigned char color3[4];
    unsigned char parameterBlock[0xa8];
    unsigned char visibleFlag;
    unsigned char renderMode174;
    unsigned char tail[0x63];
} CzanSpriteObjectKnownFields;

int CzanUiObjectInstance_Init(void *objectInstance);
int CzanSpriteObject_Init(void *spriteObject);
int *GlobalUiFrameState_CreateOnce(void);
int CzanUiManager_AllocateObjectGroupStorage(int *uiManager, int groupCapacity, int pointerCapacity);
int CzanUiManager_CreateObjectGroup(
    int uiManager,
    void *linkData,
    unsigned int flags,
    int initialAnimIndex);
int CzanUiManager_ValidateAndRelocateObjectGroupMetadata(int uiManager, char *metadataBlock);
int CzanUiManager_CloneObjectGroup(
    int uiManager,
    int sourceObjectGroupHandle,
    unsigned int cloneFlags,
    int initialAnimIndex);
void CzanUiObjectInstance_StartAnimation(double startFrame, int objectInstance, int animationIndex);
void CzanUiObjectInstance_PreplayInitialAnimation(int objectInstance);
void CzanUiManager_SetObjectGroupAnimationMode(int uiManager, int objectGroupHandle, unsigned char mode);
void CzanUiManager_StartObjectGroupAnimation(double startFrame, int uiManager, int objectGroupHandle, int animationIndex);
void CzanUiManager_SetObjectGroupAnimationResetMode(int uiManager, int objectGroupHandle, unsigned char resetMode);
void CzanUiManager_ResetObjectGroupAnimationTime(double frame, int uiManager, int objectGroupHandle);
int CzanUiManager_IsObjectGroupAnimationDone(int uiManager, int objectGroupHandle);
void CzanSpriteObject_SetRenderMode(int spriteObject, int mode);
void CzanUiObjectInstance_RunAnimationScript(int objectInstance, int allowUnknownOpcode);
void CzanUiObjectInstance_ApplyColorBlocks(int objectInstance);
void CzanUiManager_SetObjectTextureFrame(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    int textureFrameOrAuto,
    int updateSpriteDimensions);
void CzanUiManager_GetChildObjectDimensions(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    float *outWidth,
    float *outHeight);
void CzanUiManager_ApplyObjectGroupPositionLayout(int uiManager, int objectGroupHandle, float *xyOffset);
void CzanUiManager_ApplyChildObjectPositionLayout(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    float *xyOffset);
void CzanUiManager_ApplyObjectGroupAnimationOffset(int uiManager, int objectGroupHandle, float *xyOffset);
void CzanUiManager_ApplyChildObjectAnimationOffset(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    float *xyOffset);
void CzanUiManager_SetObjectGroupDisplayFlags(
    int uiManager,
    int objectGroupHandle,
    signed char suppressDraw,
    unsigned char drawState);
void CzanUiManager_SetObjectGroupEnabled(int uiManager, int objectGroupHandle, unsigned char enabled);
void CzanUiManager_SetChildObjectEnabled(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    unsigned char enabled);
void CzanUiManager_SetChildObjectLinkedHandle(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    int linkedHandle);
void CzanUiManager_SetChildObjectExtensionPointer(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    int extensionPointer,
    int releaseExisting,
    int extensionSlot);
void CzanUiManager_SetObjectGroupPriority(int uiManager, int objectGroupHandle, int priority);
void CzanUiManager_AlignObjectGroupByReferenceEdge(int uiManager, int objectGroupHandle, int referenceEdge, int alignToMax);
void CzanUiManager_SetChildObjectReferenceEdge(int uiManager, int objectGroupHandle, int childObjectIndex, int referenceEdge);
void CzanUiManager_SetObjectGroupReferenceEdgeActive(int uiManager, int objectGroupHandle, unsigned char active);
void CzanUiManager_SetChildObjectReferenceEdgeActive(int uiManager, int objectGroupHandle, int childObjectIndex, unsigned char active);
int CzanUiManager_GetChildObjectReferenceEdge(int uiManager, int objectGroupHandle, int childObjectIndex);
void CzanUiManager_SetObjectGroupDrawEnabled(int uiManager, int objectGroupHandle, unsigned char drawEnabled);
double CzanUiManager_GetObjectAnimationDuration(double fallbackDuration, int uiManager, int objectGroupHandle, int childObjectIndex, int animationIndex);
int CzanUiManager_GetChildObjectInstance(int uiManager, int objectGroupHandle, int childObjectIndex);
void CzanUiManager_LinkObjectGroupToReferenceObject(
    int uiManager,
    int targetObjectGroupHandle,
    int referenceObjectGroupHandle,
    int referenceChildIndex,
    unsigned char linkMode);
void CzanSpriteObject_Draw(int spriteObject, int parentTransform, int externalTransform, int drawMode);
void CzanUiObjectInstance_Draw(int objectInstance);
void CzanUiObjectInstance_SetColorBlocks(int objectInstance, int colorSlot, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void CzanUiManager_DrawObjectListReverse(int objectList, int drawLayerFilter);
void CzanUiManager_UpdateObjectList(int uiManager);
void CzanUiManager_DrawObjectGroupInListOrder(int uiManager, int objectGroupHandle);
void CzanUiManager_DrawChildObject(int uiManager, int objectGroupHandle, int childObjectIndex);
void HostCzan_RegisterLinkSize(const void *data, unsigned int size);
unsigned int HostCzan_GetRegisteredLinkSize(const void *data);
void CzanDrawTexturedOrColoredQuad(
    double u0,
    double v0,
    double u1,
    double v1,
    int spriteObject,
    void *quadData,
    int width,
    unsigned int height,
    unsigned char *vertexColors,
    int textureObject,
    int unknownArg11,
    int unknownArg12);

#endif
