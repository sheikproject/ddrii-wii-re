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

#endif
