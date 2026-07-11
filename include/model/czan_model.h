#ifndef DDRII_MODEL_CZAN_MODEL_H
#define DDRII_MODEL_CZAN_MODEL_H

typedef struct CzanModelKnownFields {
    void *vtable;
    unsigned char fields[0x2cc];
} CzanModelKnownFields;

typedef struct CzanModelSubmittedPrimitiveBuffer {
    float (*vertices)[3];
    float (*texcoords)[2];
    unsigned int *colors;
    unsigned int vertexCapacity;
    unsigned int vertexCount;
    unsigned int *primitiveStart;
    unsigned int *primitiveVertexCount;
    unsigned int *primitiveTextureIndex;
    unsigned int primitiveCapacity;
    unsigned int primitiveCount;
    unsigned int submittedObjectCount;
    float boundsMin[3];
    float boundsMax[3];
} CzanModelSubmittedPrimitiveBuffer;

int *CzanModel_Init(int *model);
int *CzanModel_Destroy(int *model, short releaseMode);
int CzanModel_SetPrimaryBlock(int *model, int primaryModelBlock, int primaryModelBlockSize);
void CzanModel_AttachTextureSet(int *model, int textureSet);
void CzanModel_SetContinuationCount(int *model, int continuationCount);
int CzanModel_LoadContinuationBlock(int *model, void *continuationBlock, int continuationIndex);
void CzanModel_ParseContinuationAnimationBlock(int *model, int continuationIndex);
void CzanModel_SetFallbackRenderSlot(int *model, int textureSet, int renderMode, unsigned char enabledFlag, unsigned char alpha);
void CzanModel_ReadZmbObjectLocalMatrix(const void *objectEntry, float *outMatrix34);
void CzanModel_BuildZmbObjectWorldMatrices(
    const void *zmbData,
    unsigned int zmbSize,
    unsigned int objectEntryOffset,
    unsigned int objectCount,
    float (*outWorldMatrices34)[12],
    unsigned int maxWorldMatrices);
void CzanModel_TransformPoint(const float *matrix34, const float *point3, float *outPoint3);
void CzanModel_SubmitVisibleZmbPrimitiveStreams(
    const void *zmbData,
    unsigned int zmbSize,
    CzanModelSubmittedPrimitiveBuffer *outBuffer);
void CzanModel_SubmitAnimatedZmbPrimitiveStreams(
    const void *zmbData,
    unsigned int zmbSize,
    const void *zabData,
    unsigned int zabSize,
    float animationTick,
    CzanModelSubmittedPrimitiveBuffer *outBuffer);
int CzanModel_BuildRuntimeData(int *model, int enabled);
int CzanModel_BuildRuntimeDataAndUpdateTransforms(int *model);
void CzanModelOwner_CreateModelFromPrimaryBlock(int *owner, void *primaryBlock, int primaryBlockSize);
void CzanModelOwner_BuildRuntimeDataAt80(int *owner);
void CzanModelOwner_SetContinuationCount(int *owner, int continuationCount);
void CzanModelOwner_LoadContinuationBlock(int *owner, void *continuationBlock, int continuationIndex);
void CzanModelOwner_SetAnimationStartFrame(int *owner, double startFrame);
int *CzanModelOwner_GetHostModel(int *owner);
void *CzanModel_GetHostPrimaryBlock(int *model);
unsigned int CzanModel_GetHostPrimaryBlockSize(int *model);
void *CzanModel_GetHostContinuationBlock(int *model, int continuationIndex);
int CzanModelCollection_LoadFromLinkBlocks(int *collection, void *linkData, int modelCount, unsigned int collectionIndex);
int CzanModelManager_LoadResource(int *manager, unsigned int bankIndex, void *linkData);
int CzanModelManager_UnloadBank(int *manager, unsigned int bankIndex);
void CzanModelManager_Clear(int *manager);
int CzanModelManager_ClearLiveObjects(int *manager);
void CzanModelManager_UpdateVisibleGroup(void *context, void *unused, int removeFinished, char groupId);
void CzanModelObject_UnregisterManagerEntries(int *object);
void CzanModelObject_Update(double delta, unsigned int *object);
void CzanModelObject_UpdateRegistrationTarget(double delta, unsigned int *object, int *registration);
void CzanModelManager_LoadBank1Resource(void *unused, void *linkData);
void CzanModelManager_SetupBank3AndStageObjects(
    void *context,
    void *unused,
    unsigned char flagA,
    unsigned char flagB,
    void *bank3LinkData,
    void *stageObjectLinkData);
void CzanModelManager_SwitchBank5ForMode(int *owner);
void CzanModelManager_RequestBank5ModeTransition(double duration, int *owner, unsigned int modeIndex, int transitionAnimIndex);
void CzanModelManager_StopBank5ModeEffects(double stopTime, int *owner);
void CzanModelManager_InitBank5LiveObjectsForMode(int *owner);
void CzanModelManager_UpdateCurrentModeMatrices(int *owner, const void *modelMatrix, const void *objectMatrix);
void CzanModelOwner_LoadStageResourceGroup(int *owner, void *linkData);
void CzanModelOwner_SetCategoryAndLoadStageResourceGroup(int *owner, unsigned char groupCategory, void *linkData);
void CzanModelOwner_SetupResourceGroupEntries(int *owner, unsigned char groupCategory, void *bank5LinkData, void *entryListLinkData);
int CzanModelOwner_EnsureResourceGroupHandle(int *owner, unsigned int groupIndex);
void CzanModelOwner_CopyCurrentModelMatrix(int *owner, void *outMatrix);
int CtsStageObjDescriptor_GetModelTransform(int descriptor);
void CzanModelLiveObject_SetModelMatrices(int *object, int transformSource, const void *modelMatrix, const void *objectMatrix);
void CzanModelLiveObject_Init(int *object);
void CzanModelManager_SetLiveObjectMatrix(int *manager, int liveObjectHandle, const void *matrix);
void CzanModelLiveObject_ApplyGlobalScaleToMatrix(int *liveObjectTransform);
void CzanEffectManager_SetStopTime(int *manager, int effectHandle, int stopTime);
unsigned char CzanModelOwner_SelectModeSlot(int *owner);
void CzanModelPositionSet_Clear(int *positionSet);
void CzanModelPositionSet_LoadFromLinkList(int *positionSet, void **linkDataList, int linkDataCount);
int CzanModel_SetEnabled(int *model, unsigned int enabled);
int CzanModel_FindObjectIndexByName(int *model, int objectName);
int CzanModel_GetObjectTransform(int *model, int objectIndex);
int CzanModel_EvaluateTranslationKeys(double frame, double duration, float *outVec3, const void *keys, int keyCount, int loop, int cachedKeyIndex);
int CzanModel_EvaluateRotationKeys(double frame, double duration, float *outQuat, const void *keys, int keyCount, int loop, int cachedKeyIndex);
int CzanModel_EvaluateScaleKeys(double frame, double duration, float *outVec3, const void *keys, int keyCount, int loop, int cachedKeyIndex);
void CzanModel_UpdateAnimationChannel(int *model, int holdFrame, int channelIndex);
void CzanModel_ApplyAnimationChannelFrame(double frame, int *model, int channelIndex);
void CzanModel_BlendAnimationChannelFrame(double deltaOrScale, int *model, float *channel, int channelIndex);
void CzanModel_SolveObjectAnimationTransform(void *outMatrix, void *objectWorkspace, int *objectAnimState, int *objectAnimConfig);
void CzanModel_UpdateObjectAnimation(double deltaOrScale, int *model, int objectIndex);
void CzanModel_UpdateType2WeightedVectors(int *model, int *objectEntry, int *drawContext);
void CzanModel_UpdateObjectTransforms(double deltaOrScale, int *model);
void CzanModel_BuildSpecialObjectMatrix(int *model, float *outMatrix, const float *baseMatrix, const float *objectMatrix);
void CzanModel_DrawVisibleObjects(int *model, int arg1, const void *baseMatrix, int arg2);
void CzanModel_DrawType2PartTree(
    int *model,
    int *partIndexSource,
    int submeshIndex,
    int partTableBase,
    const void *objectMatrix,
    int childIndex,
    int *drawContext,
    int objectIndex);
void CzanModel_DrawStandardPartTree(
    int *model,
    int *partIndexSource,
    int partTableBase,
    int childPass,
    const void *objectMatrix,
    int *drawContext,
    int submeshIndex,
    int objectIndex);
void CzanModel_UpdateStandardPartTexcoords(int *model, int *partIndexSource, const void *objectMatrix, unsigned short *submesh);
void CzanModel_UpdateStandardSpecialPartTexcoords(
    int *model,
    int *partIndexSource,
    int partEntry,
    const void *objectMatrix,
    unsigned short *submesh);
void CzanModel_UpdateType2PartTexcoords(
    int *model,
    int *partIndexSource,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh);
void CzanModel_UpdateType2SpecialPartTexcoords(
    int *model,
    int *partIndexSource,
    int partEntry,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh);
void CzanModel_ApplyMaterialCullMode(int *partMaterial, int forceCullBack);
void CzanModel_ApplyMaterialBlendMode(int *partMaterial, int forceAlphaCompare, int forceBlendEnabled);
void CzanModel_SetupMaterialVertexAttributes(
    int *submesh,
    int *partMaterial,
    int forceNormalAttr,
    int useGeneratedColorAttr,
    int useTexcoordAttr);
int CzanModel_SetupPartRenderState(int *model, void *outState, int *drawArgs);
void CzanModel_SubmitPartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    const void *objectMatrix,
    unsigned short *submesh);
void CzanModel_SubmitSpecialPartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    const void *objectMatrix,
    unsigned short *submesh);
void CzanModel_SubmitType2PartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh);
void CzanModel_SubmitType2SpecialPartPrimitive(
    int *model,
    int *partIndexSource,
    int partEntry,
    int childIndex,
    int *drawContext,
    int submeshIndex,
    unsigned short *submesh);
void CzanModel_DrawBaseMaterialPartTree(
    int *model,
    int *partIndexSource,
    int partTableBase,
    int childIndex,
    const void *objectMatrix,
    int *drawContext,
    int submeshIndex,
    int objectIndex);
void CzanModel_DrawMaterialPartTree(
    int *model,
    int *partIndexSource,
    int partTableBase,
    int childIndex,
    const void *objectMatrix,
    int *drawContext,
    int submeshIndex,
    int objectIndex);
void CzanModel_FinalizeTransformUpdate(double deltaOrScale, int *model);
void CzanModel_InitVisiblePartUvRuntime(int *model);
void CzanModel_UpdatePartUvAnimation(double deltaOrScale, int *model);

#endif
