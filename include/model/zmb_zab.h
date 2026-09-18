#ifndef DDRII_MODEL_ZMB_ZAB_H
#define DDRII_MODEL_ZMB_ZAB_H

#include <stdint.h>

typedef struct CtsStageObjKnownFields {
    unsigned char base[0x6c];
    void *vtable;
    unsigned int packedFlags;
    char normalizedName[0x40];
    int *objsetReferenceIndices;
    int objsetReferenceCount;
    unsigned char reservedBc[0x1a8];
    void *manager802e70a4;
    void *largeResourceManager;
    int unknown260;
    int unknown264;
    unsigned short state268;
    unsigned short flags26a;
    int temporaryObjsetReferenceCounter;
} CtsStageObjKnownFields;

int ZmbZabModelEntry_Init(void *entry);
int ZmbZabModelEntry_Destroy(void *entry, short releaseMode);
int *CtsStageObj_InitBase(int *entry);
int CtsStageObj_Destroy(void *entry, short releaseMode);
void CtsStageObj_ResetModelBlocks(int *entry);
void CtsStageObj_LoadModelBlocks(
    int *entry,
    intptr_t primaryModelBlock,
    int primaryModelBlockSize,
    intptr_t secondaryTextureBlock,
    int secondaryTextureBlockSize,
    int continuationCount,
    int fallbackTextureSlot);
void CtsStageObj_LoadPrimarySecondaryBlocks(
    void *entry,
    intptr_t primaryBlock,
    int primaryBlockSize,
    intptr_t secondaryBlock,
    int secondaryBlockSize,
    unsigned int continuationCount);
void CtsStageObj_LoadContinuationBlock(void *entry, int continuationIndex, intptr_t continuationBlock);
int *CtsStageObj_GetHostModel(int *entry);
void CtsStageObj_StartAnimation(double startFrame, double speed, void *entry, int arg3, int arg4, int arg5);
void CtsStageObj_SelectAndApplyModelSlot(
    double x,
    double y,
    double width,
    double height,
    double scaleOrDepth,
    double wrapLimit,
    double currentFrame,
    void *stageObj,
    int modelSlotOrSpecialId,
    float baseFrame,
    int flags);
void CtsStageObjDescriptor_SetFrameProgress(double frameProgress, int *descriptor, int useFullDuration);
void CtsStageObjDescriptor_SetCurrentTime(double currentTime, int descriptor);
int CtsStageObjDescriptor_GetEntryHandle(int *descriptor, int entryIndex);
double CtsStageObjDescriptor_GetCurrentDuration(int *descriptor);
int CtsStageObjDescriptor_GetCurrentEntryActiveFlag(int *descriptor);
int CtsStageObjSlot_IsBusy(float *slot);
void CtsStageObjSlot_ApplySelectedDescriptorTransform(int *slot);
void CtsStageObjSlot_InitState5Transform(int *slot);
int CtsStageObjSlot_BeginSpecialDescriptor(int *slot);
void CtsStageObjSlot_EndSpecialDescriptor(int *slot);
void CtsStageObjSlot_SetState(int *slot, int state);
void CtsStageObjSlot_ResetDescriptorFrames(double frameProgress, int *slot);
void CtsStageObjDescriptor_ActivateEntry(double blendDuration, int *descriptor, int entryIndex, int entryHandle);
int CtsStageObj_CopyObjectTransform(void *stageObjOrSlot, void *outMatrix, int objectIndex);
void CtsStageObj_UpdateAnimationFrame(int *stageObj, int holdFrame);
void CtsStageObj_ApplyModelTransform(int *stageObj, int arg1, int arg2);
void CtsStageObj_DrawModelWithFlags(int *stageObj, int arg1, int arg2, unsigned int drawFlags);
void CtsStageObj_DrawModelWithExternalMatrix(
    int *stageObj,
    int arg1,
    const float *matrix34,
    int arg2,
    unsigned int drawFlags);
void ZmbZabModelEntry_UpdatePresentation(int *entry, int arg1, int arg2, int arg3);

#endif
