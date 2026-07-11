#ifndef DDRII_GAME_CGAME_H
#define DDRII_GAME_CGAME_H

void CGame_PrepareManagersAndResources(int *cgame);
void CGame_PrepareSceneFromSelectedSetup(int *cgame);
void CGame_BuildSceneSetupFromPlayerData(int *cgame);
int CGame_UpdateSceneSetupSelection(int *cgame);
void CGame_LoadSceneResourceManagers(int *cgame);
int CGame_UpdateStateMachine(int *cgame, int currentModuleId);
void CGame_PrepareActiveGameplayState(int *cgame);
void CGame_BuildActiveGameplaySetupFromPlayerData(int *cgame);
int CGame_UpdateActiveGameplaySetupSelection(int *cgame);
void CGame_StartActiveGameplayControllerTransition(int *cgame);
void CGame_UpdateActiveGameplayTransitionState(int *cgame);
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
