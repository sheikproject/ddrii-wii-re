#ifndef DDRII_SELECT_CSEL_MODE_H
#define DDRII_SELECT_CSEL_MODE_H

#define CSEL_MODE_ENTRY_COUNT 14
#define CSEL_MODE_ENTRY_SIZE 0x50

#define CSEL_MODE_STATE_ENTER_ANIM 1
#define CSEL_MODE_STATE_WAIT_ENTER_ANIM 2
#define CSEL_MODE_STATE_INPUT 3
#define CSEL_MODE_STATE_LEAVE_ANIM 4
#define CSEL_MODE_STATE_WAIT_LEAVE_ANIM 5
#define CSEL_MODE_STATE_COMMIT 6

typedef struct CSelModeChoice {
    int nextSelectState;
    int gameModeId;
    int gameModeSubId;
    int gameModeExtraId;
} CSelModeChoice;

typedef struct CSelModeKnownFields {
    void *parentSelectData;
    int modeState;
    int selectedModeIndex;
    void *renderManager;
    void *uiManager;
    void *characterAssetSystem;
} CSelModeKnownFields;

typedef struct CSelModeEntryKnownFields {
    unsigned char base[0x14];
    int objectHandles[8];
    int objectHandleCount;
    unsigned char reserved38[4];
    void *uiManager;
    void *vtable;
    int transformOrState0;
    int transformOrState1;
    int transformOrState2;
} CSelModeEntryKnownFields;

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

extern const CSelModeChoice CSelMode_ChoiceTable[5];

int CSelMode_Init(void *cselMode);
void CSelMode_OnEnter(void *cselMode, void *linkData);
void CSelMode_Update(void);
void CSelMode_SetInitialSelectedMode(void *cselMode);
int CSelModeEntry_Init(void *entry);
int CSelModeEntry_Update(void *entry, short activeCountOrFlag);
int CSelModeEntry_AddUiObject(void *entry, int linkBlock);
int CSelModeEntry_AddChildUiObject(void *entry, int objectId);
void CSelModeEntry_SetAnimationOrLayout(void *entry, int objectSlot, int animationId, int animationData);
void CSelModeEntry_PlayObject(void *entry, int objectSlot);
void CSelModeEntry_SetTransformTriplet(void *entry, const int *values);
int CzanUiObjectInstance_Init(void *objectInstance);
int CzanSpriteObject_Init(void *spriteObject);
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
void CzanSpriteObject_SetRenderMode(int spriteObject, int mode);
void CzanUiObjectInstance_RunAnimationScript(int objectInstance, int allowUnknownOpcode);
void CzanUiObjectInstance_ApplyColorBlocks(int objectInstance);
void CzanUiManager_SetObjectTextureFrame(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    int textureFrameOrAuto,
    int updateSpriteDimensions);
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
void CzanUiManager_SetObjectGroupEnabled(int uiManager, int objectGroupHandle, unsigned char enabled);
void CzanUiManager_SetChildObjectEnabled(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    unsigned char enabled);
void CzanUiManager_SetObjectGroupDrawEnabled(int uiManager, int objectGroupHandle, unsigned char drawEnabled);
void CzanUiManager_LinkObjectGroupToReferenceObject(
    int uiManager,
    int targetObjectGroupHandle,
    int referenceObjectGroupHandle,
    int referenceChildIndex,
    unsigned char linkMode);
void CzanSpriteObject_Draw(int spriteObject, int parentTransform, int externalTransform, int drawMode);
void CzanUiObjectInstance_Draw(int objectInstance);
void CzanUiManager_DrawObjectListReverse(int objectList, int drawLayerFilter);
void CzanUiManager_DrawObjectGroupInListOrder(int uiManager, int objectGroupHandle);
void CzanUiManager_DrawChildObject(int uiManager, int objectGroupHandle, int childObjectIndex);
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
void CSelMode_SetHostLinkResourceSize(unsigned int resourceSize);
int CSelMode_ModeIdToSelectedIndex(int modeId);
const CSelModeChoice *CSelMode_GetChoice(int selectedModeIndex);
int CSelMode_MoveSelection(int selectedModeIndex, int direction);

#endif
