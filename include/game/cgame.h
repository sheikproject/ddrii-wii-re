#ifndef DDRII_GAME_CGAME_H
#define DDRII_GAME_CGAME_H

void CGame_PrepareManagersAndResources(int *cgame);
void CGame_PrepareSceneFromSelectedSetup(int *cgame);
void CGame_LoadSceneResourceManagers(int *cgame);
int CGame_UpdateStateMachine(int *cgame, int currentModuleId);
void CGame_PrepareActiveGameplayState(int *cgame);
void CGame_CreateActiveGameplayController(int *cgame);
int ActiveGameplayController_Create(unsigned int characterOrSetupId);
int ActiveGameplayControllerBase_Init(int *controller);
int ActiveGameplayControllerSpecial_Init(int *controller);
void ActiveGameplayControllerBase_SetupContext(int *controller, int *context);
int ActiveGameplayControllerBase_GetEmbeddedSubobject(int *controller);
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
int ActiveGameplayControllerBase_AreMovieBindingsReady(int *controller);
int ActiveGameplayControllerBase_AreMode3MovieBindingsReady(int *controller);
void ActiveGameplayControllerBase_ApplyVisualPreset(int *controller, int presetA, int presetB, int presetGroup);
void ActiveGameplayControllerBase_CommitVisualPreset(int *controller);

#endif
