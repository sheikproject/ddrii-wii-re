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

extern const CSelModeChoice CSelMode_ChoiceTable[5];

int CSelMode_Init(void *cselMode);
void CSelMode_OnEnter(void *cselMode, void *linkData);
void CSelMode_Update(void);
void CSelMode_SetInitialSelectedMode(void *cselMode);
int CSelMode_ModeIdToSelectedIndex(int modeId);
const CSelModeChoice *CSelMode_GetChoice(int selectedModeIndex);

#endif
