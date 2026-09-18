#ifndef DDRII_GAME_CGAME_H
#define DDRII_GAME_CGAME_H

void CGame_PrepareManagersAndResources(int *cgame);
int CGame_TeardownRuntimeState(int *cgame, int nextModuleOrState);
void CGame_InitDefaultGameplaySetup(int *cgame);
int CGame_UpdateViewerSetupSelection(int *cgame);
void CGame_PrepareSceneFromSelectedSetup(int *cgame);
void CGame_BuildSceneSetupFromPlayerData(int *cgame);
int CGame_UpdateSceneSetupSelection(int *cgame);
void CGame_LoadSceneResourceManagers(int *cgame);
int CGame_UpdateStateMachine(int *cgame, int currentModuleId);
void CGame_PrepareActiveGameplayState(int *cgame);
void CGame_BuildActiveGameplaySetupFromPlayerData(int *cgame);
int CGame_UpdateActiveGameplaySetupSelection(int *cgame);
void CGame_StartActiveGameplayControllerTransition(int *cgame);
void CGame_SetupActiveGameplayTransitionResources(int *cgame);
void CGame_UpdateActiveGameplayTransitionTiming(int *cgame);
void CGame_UpdateActiveGameplayTransitionState(int *cgame);
void CGame_UpdateAndRenderActiveGameplayRuntime(int *cgame, int renderPass);
int *PlayerDataStateContainer_Init(int *container);
int *PlayerDataState_Init(int *state);
void PlayerDataState_SetInitialValue(int *state, int value);
int PlayerDataManager_Init(int *playerDataManager);
void PlayerDataManager_Reset(int *playerDataManager);
void PlayerDataManager_ResetPlayerRecord(int *playerDataManager, int playerIndex);
void PlayerDataManager_ResetMenuRecords(int *playerDataManager);
void PlayerDataManager_ResetModeRecord(int *playerDataManager, int modeIndex);
void PlayerDataManager_ResetSubBlock14A4(int *subBlock);
void PlayerDataManager_ResetSubBlock1BA8(int *subBlock);
void PlayerDataManager_ResetSubBlock1C28(unsigned char *subBlock);
void PlayerDataManager_ResetSubBlock1C54(int *subBlock);
void PlayerDataManager_ResetSubBlock2F1C(int *subBlock);
void PlayerDataManager_ResetSubBlock2F40(int *subBlock);
void PlayerDataManager_SetModeRecordRowValues(
    int *playerDataManager,
    int rowIndex,
    int value0,
    int value1,
    int modeIndex);
void PlayerDataManager_ResetModeRecordRowTail(int *playerDataManager, int rowIndex, int modeIndex);
int PlayerDataState_GetCurrentValue(int *state);
int PlayerDataManager_GetSetupFieldF8(int *playerDataManager);
int PlayerDataManager_GetSetupFieldFC(int *playerDataManager);
int PlayerDataManager_GetInactiveOrCpuPlayerCount(int *playerDataManager);
int PlayerDataManager_GetPlayerCount(int *playerDataManager);
int PlayerStats_GetField170(int *playerStats);
int GameIndexedId_GetCategory(unsigned int indexedId);
int GameIndexedId_ToLinearIndex(int indexedId);
unsigned int InputOrMenuStateManager_TestHeldMask(int *manager, int controllerIndex, unsigned int mask);
unsigned int InputOrMenuStateManager_TestActiveMask(int *manager, int controllerIndex, unsigned int mask);
unsigned int InputOrMenuStateManager_TestTriggeredMask(int *manager, int controllerIndex, unsigned int mask);
unsigned int InputOrMenuStateManager_IsConfirmPressed(int *manager, int controllerIndex);
unsigned int InputOrMenuStateManager_IsBackPressed(int *manager, int controllerIndex);
double UiRootManager_GetSelectionPanelAnimationDuration(int *uiRootManager);
unsigned int UiRootManager_IsSelectionPanelIdle(int *uiRootManager);
unsigned int UiRootManager_IsBootTransitionControllerIdle(int *uiRootManager);
void UiRootManager_StartBootTransitionController(
    int *uiRootManager,
    int mode,
    int arg2,
    int arg3,
    int showSecondChild);
void UiRootManager_ConfigureBootTransitionPrompt(int *uiRootManager, int effectSlot, int baseEffectId);
void UiRootManager_CloseBootTransitionController(int *uiRootManager);
unsigned int UiRootManager_IsBootTransitionPromptReady(int *uiRootManager);
int UiRootManager_GetBootTransitionResult(int *uiRootManager);
void UiRootManager_SetBootTransitionSelectedOption(int *uiRootManager, int selectedOption);
void UiRootManager_SelectDefaultTransition(int *uiRootManager);
void UiRootManager_SelectShortTransition(int *uiRootManager);
void UiRootManager_SelectTertiaryPrompt(int *uiRootManager, unsigned int promptIndex, int textureFrame);
void UiRootManager_SelectImmediateTransition(int *uiRootManager);
void UiRootManager_StartTitleTransitionA(int *uiRootManager, int animationIndex, int priority, int forceAlpha);
void UiRootManager_StartTitleTransitionB(int *uiRootManager, int animationIndex, int priority, int forceAlpha);
unsigned int UiRootManager_IsTitleTransitionAIdle(int *uiRootManager);
void UiRootManager_StartTitleTransitionC(int *uiRootManager, int animationIndex, int priority, int forceAlpha);
unsigned int UiRootManager_IsTitleTransitionCIdle(int *uiRootManager);
void UiRootManager_SetSubManager44Value(int *uiRootManager, int value);
void UiRootSubManager44_SetValue(int *subManager, int value);
double CGameUiSelectionPanel_GetAnimationDuration(int *panelState);
unsigned int CGameUiSelectionPanel_IsState5(int *panelState);
unsigned int CGameUiSelectionPanel_IsState6(int *panelState);
unsigned int CGameUiSelectionPanel_IsState8(int *panelState);
unsigned int CGameUiSelectionPanel_IsIdle(int *panelState);
int CGameUiSelectionPanel_GetFlag18(int *panelState);
int CGameUiSelectionPanel_GetFlag1C(int *panelState);
void CGameUiSelectionPanel_SetManualAdvanceFlag(int *panelState);
void CGameUiRoot_AdvanceSelectionPanelState(int *uiRoot);
void CGameUiSelectionPanel_Update(int *panelState, int *uiRootManager);
void CGameUiSelectionPanel_AdvanceState(int *panelState);
void CGameUiSelectionPanel_SelectDefaultTransition(int *panelState);
void CGameUiSelectionPanel_SelectShortTransition(int *panelState);
void CGameUiSelectionPanel_SelectTertiaryPrompt(int *panelState, unsigned int promptIndex, int textureFrame);
void CGameUiSelectionPanel_SelectImmediateTransition(int *panelState);
void CGameUiReferenceState_Init(
    int *state,
    int enabled,
    int objectGroupHandle,
    int childObjectIndex,
    int trackedChildIndex,
    int linkedHandle);
void CGameUiSubManager_InitSelectionReferenceGroups(int *subManager);
void CGameUiRoot_SetMenuPresentationMode(int *uiRoot, int mode, int highMask, int lowMask, int bankIndex);
void CGameUiRoot_SetAlternateMenuPresentationMode(int *uiRoot, int mode, int highMask, int lowMask);
void CGameUiRoot_ResetMenuPresentationGrid(int *uiRoot);
void CGameUiRoot_SetMenuPresentationBankValue(int *uiRoot, int referenceEdge, int bankIndex);
void CGameUiRoot_ResetMenuPresentationBankFlags(int *uiRoot, int bankIndex);
void ActiveGameplayControllerBase_ResetMenuPresentationGrid(int *controller);
void UiRootSubManager_UpdateTextureFrameGroupMotion(int *controller);
unsigned int RuntimeRandom_Next15(void);
void GlobalCueManager_ResolveCueId(unsigned int cueId, unsigned int *outEffectId, int *outEffectParam);
void CGame_CreateActiveGameplayController(int *cgame);
void CGameTransitionManager_Start(double duration, double speed, int *manager, int cueOrDelay, int spawnEffect);
void CGameTransitionSlot_Start(int *transitionSlot);
int ActiveGameplayController_Create(unsigned int characterOrSetupId);
int ActiveGameplayControllerBase_Init(int *controller);
int ActiveGameplayControllerBase_Destroy(int *controller, short releaseMode);
int ActiveGameplayControllerSpecial_Init(int *controller);
void ActiveGameplayControllerBase_SetupContext(int *controller, int *context);
int ActiveGameplayControllerBase_GetEmbeddedSubobject(int *controller);
int ActiveGameplayControllerEventData_SampleCurrentEvent(int *eventData, int *outEvent, unsigned int category);
int ActiveGameplayControllerEventData_FindNearbyEvent(
    int *eventData,
    int *outEvent,
    unsigned int category,
    int direction,
    int skipCurrent);
int ActiveGameplayControllerEventData_ResolveRangeValue(int *eventData, int frameOrIndex);
int ActiveGameplayControllerEventData_ResolveSecondaryRangeValue(int *eventData, int frameOrIndex);
int ActiveGameplayControllerSubobject_ResolveModeTransitionSlot(
    int *subobject,
    int forceImmediate,
    int targetMode,
    int transitionKind,
    int tableIndex);
void ActiveGameplayControllerBase_ResetEmbeddedSubobject(int *controller);
void ActiveGameplayControllerBase_ResetRuntimeState(int *controller);
void ActiveGameplayControllerBase_UpdateTimelineMarker(int *controller);
void ActiveGameplayControllerBase_ApplyRuntimeEventChannels(int *controller, int frameContext);
void ActiveGameplayControllerBase_ApplyRuntimeVisualState(int *controller, int laneIndex, unsigned int enabledMask);
void ActiveGameplayControllerBase_ApplyRuntimeVisibilityMask(int *controller, int laneIndex, unsigned int visibilityMask);
void ActiveGameplayControllerBase_TriggerRuntimeCue(int *controller, int cueId);
void ActiveGameplayControllerBase_ApplyModeTransitionEvent(
    double transitionSeconds,
    int *controller,
    int targetMode,
    int transitionKind,
    int forceImmediate,
    int eventArg0,
    int eventArg1);
void ActiveGameplayControllerBase_StopStageModelSlot(int *controller);
int ActiveGameplayControllerBase_IsStageModelSlotBusy(int *controller);
void ActiveGameplayControllerBase_StartTimedTransition(int *controller, unsigned int transitionTicks);
void ActiveGameplayControllerBase_ResetTransitionMovieBindings(int *controller);
void ActiveGameplayControllerBase_TickRuntime(int *controller, int transitionPass);
void ActiveGameplayControllerBase_LateUpdateTransforms(int *controller, int skipRuntimeAdvance);
int ActiveGameplayControllerBase_AreMovieBindingsReady(int *controller);
int ActiveGameplayControllerBase_AreMode3MovieBindingsReady(int *controller);
void ActiveGameplayControllerBase_ApplyVisualPreset(int *controller, int presetA, int presetB, int presetGroup);
void ActiveGameplayControllerBase_CommitVisualPreset(int *controller);
int ActiveGameplayControllerSubobject_Destroy(int *subobject, short releaseMode);

#endif
