#ifndef DDRII_SELECT_CSEL_MODE_H
#define DDRII_SELECT_CSEL_MODE_H

#include "ui/czan_ui.h"

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

extern const CSelModeChoice CSelMode_ChoiceTable[5];

int CSelMode_Init(void *cselMode);
void CSelMode_OnEnter(void *cselMode, void *linkData);
void CSelMode_Update(void);
void CSelMode_SetInitialSelectedMode(void *cselMode);
void CSelectCommon_LoadResource(int *selectCommon, void *linkData);
void CSelectCommon_UpdateMovieBackground(int *selectCommon, int skipInitialUpdate, int allowMovieStart, int forceInitialBind);
void CSelectCommon_RevealMovieEntriesPrimary(int *selectCommon, int useImmediateTiming);
void CSelectCommon_RevealMovieEntriesAlternate(int *selectCommon, int useImmediateTiming);
int CSelModeEntry_Init(void *entry);
int CSelModeEntry_Update(void *entry, short activeCountOrFlag);
int CSelModeEntry_AddUiObject(void *entry, int linkBlock);
int CSelModeEntry_AddChildUiObject(void *entry, int objectId);
void CSelModeEntry_SetAnimationOrLayout(void *entry, int objectSlot, int animationId, int animationData);
void CSelModeEntry_ResetObjectAnimation(void *entry, int objectSlot);
void CSelModeEntry_StartObjectAnimation(
    double startFrame,
    void *entry,
    int objectSlot,
    int animationId,
    unsigned char mode,
    int playbackMode);
void CSelModeEntry_PlayObject(void *entry, int objectSlot);
void CSelModeEntry_SetTransformTriplet(void *entry, const int *values);
void CSelMode_SetHostLinkResourceSize(unsigned int resourceSize);
int CSelMode_ModeIdToSelectedIndex(int modeId);
const CSelModeChoice *CSelMode_GetChoice(int selectedModeIndex);
int CSelMode_MoveSelection(int selectedModeIndex, int direction);

#endif
