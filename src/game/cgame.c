#include "game/cgame.h"

#include <stdint.h>

void CGame_PrepareManagersAndResources(int *cgame) {
    /* 0x8003CDC4 is the CGame setup/loading state machine.

       Key fields:
       cgame +0x008 -> next/active module state, set to 5 on error and 2 when ready
       cgame +0x00C -> setup substate
       cgame +0x0B8 -> pending/current game setup mode; -1 means use player data directly
       cgame +0x3F0..+0x428 -> owned CGame subsystem pointers created by CGameFactorySetup

       Substates:
       0 -> reset managers/subsystems and choose direct player-data setup or boot-temp loading
       1 -> wait for gBootTempManager to finish loading
       2 -> apply boot resource bundle and run CGame readiness check
       3 -> wait for external/resource managers, then wire CGame subsystems
       8 -> wait for DAT_802E71F8 +0x0C to clear
       0x0C -> ready; sets cgame +0x08 to module state 2

       The important resource path is:
       if substate 1 and FUN_8002202C(gBootTempManager) != 0:
         BootResourceBundle_ApplyLoadedResources(gBootTempManager)
         substate = 2
    */
    (void)cgame;
}

void CGame_PrepareSceneFromSelectedSetup(int *cgame) {
    /* 0x8003EE48 is a CGame setup/loading state machine sibling to 0x8003CDC4.

       It resets the same global managers and CGame subsystems at substate 0, then:
       - if cgame +0x0B8 == -1, calls FUN_8003D1F0 and configures subsystem +0x3F0
         directly, entering substate 3
       - otherwise enters substate 2 and runs FUN_8003DC74 until it returns -1 or 1
       - when ready, waits for the resource managers, then calls
         CGame_LoadSceneResourceManagers(cgame)
       - substate 0x0C sets cgame +0x08 = 3

       Compared with CGame_PrepareManagersAndResources, failure sets cgame +0x08 = 1,
       and ready completion sets cgame +0x08 = 3 instead of 2. */
    (void)cgame;
}

void CGame_LoadSceneResourceManagers(int *cgame) {
    /* 0x8003D740 wires the loaded CGame resource bundle into the scene/gameplay
       managers. Ghidra may show this as void(void) because it starts with the
       saved-register helper FUN_8012A164, but the recovered object is the CGame
       instance.

       Confirmed calls/resources:
       - fetches resources from cgame +0x3F0 through FUN_80119300
       - configures gLargeResourceManager through FUN_80025EA8 and related slots
       - loads five position/model resources into cgame +0x408 with
         CzanModelPositionSet_LoadFromLinkList
       - feeds resource lists into subsystem cgame +0x3F8
       - configures subsystem cgame +0x404 with mode/flag-dependent resources
       - when cgame +0x108 is nonzero, loads model-manager bank 1 and optionally
         sets up bank 3 plus five CtsStageObj wrappers
       - fills 0x18 large-resource table entries and finishes by wiring UI/effects

       This is an owner-side scene setup function, not a renderer. */
    (void)cgame;
}

int CGame_UpdateStateMachine(int *cgame, int currentModuleId) {
    int oldState;

    /* 0x8003C8A0 is the CGame top-level state dispatcher. It updates a flag at
       cgame +0xB0 based on DAT_802E71B8 +0x260 +0x34, then dispatches cgame[2]:
       0 -> enter state 1
       1 -> CGame_PrepareManagersAndResources
       2 -> CGame_PrepareSceneFromSelectedSetup
       3 -> CGame_PrepareActiveGameplayState
       4 -> FUN_800430CC, likely exit/result/transition state
       5 -> return cgame[0] as next module/state

       If the state changes, it resets cgame[3] to zero. */
    if (cgame == 0) {
        return currentModuleId;
    }

    oldState = cgame[2];
    if (cgame[2] == 0) {
        cgame[2] = 1;
    }
    else if (cgame[2] == 1) {
        CGame_PrepareManagersAndResources(cgame);
    }
    else if (cgame[2] == 2) {
        CGame_PrepareSceneFromSelectedSetup(cgame);
    }
    else if (cgame[2] == 5) {
        currentModuleId = cgame[0];
    }

    if (oldState != cgame[2]) {
        cgame[3] = 0;
    }

    return currentModuleId;
}

void CGame_PrepareActiveGameplayState(int *cgame) {
    /* 0x800418F8 is the state-3 CGame setup/transition state machine.

       It resets a smaller subset of global managers/subsystems, then:
       - if cgame +0x0B8 == -1, calls FUN_8003F24C and configures subsystem +0x3F0
         through FUN_80118E54, entering substate 3
       - otherwise enters substate 2 and waits for FUN_8003FEB0 to return 1
       - once resource managers are ready, calls FUN_8003FBB0(cgame)
       - substate 0x0C sets cgame +0x08 = 4

       This is still setup/transition logic. FUN_8003FBB0 is the next likely owner-side
       function after setup completes. */
    (void)cgame;
}

void CGame_CreateActiveGameplayController(int *cgame) {
    /* 0x8003FBB0 wires active gameplay resources after state 3 setup is ready.

       Confirmed behavior:
       - configures subsystem cgame +0x400 from resources in cgame +0x3F0
       - iterates cgame +0x2D0 entries, each 0x38 bytes at cgame +0x2D4
       - builds per-entry controller/resource lists from resource flags and ids
       - commits those entries into subsystem cgame +0x400 through FUN_800612A4,
         FUN_80061430, and FUN_800611B4
       - passes a handle from cgame +0x400 into subsystem cgame +0x404
       - creates/stores the active gameplay controller at cgame +0x42C through
         thunk_FUN_80115E10(cgame +0xFC)
       - builds a context struct containing cgame +0x408, +0x3F8, +0x3FC, +0x404,
         +0x400, +0x410, +0x40C, +0x3F4, and cgame +0xB8
       - calls controller vtable +0x0C with that context

       This function does not draw directly. The next likely render/update target is
       the active gameplay controller object stored at cgame +0x42C. */
    (void)cgame;
}

int ActiveGameplayControllerBase_Init(int *controller) {
    /* 0x801120D0 initializes the common 0x2BF0-byte active gameplay controller.
       Ghidra may show void(void) because the function uses FUN_8012A130/FUN_8012A17C
       to recover/return the object pointer.

       Confirmed fields:
       controller +0x2BEC -> vtable PTR_PTR_802BEF00
       controller +0x0068 -> subobject initialized by FUN_8012927C
       controller +0x1428..+0x2A80 -> six 0x478-byte-ish entry/controllers initialized
                                  through FUN_80110D30
       controller +0x2800..+0x281C -> eight 4-word/vector defaults repeated
       controller +0x2A80..+0x2A9C -> timing/default floats and ids
       controller +0x2AA0..+0x2AAC -> cleared 0x2C-byte trailing state, then +0x2ACC=1
    */
    if (controller == 0) {
        return 0;
    }
    return (int)(uintptr_t)controller;
}

int ActiveGameplayControllerSpecial_Init(int *controller) {
    /* 0x80116360 initializes the larger 0x2C08-byte special active gameplay
       controller used by selected character/setup IDs. */
    if (controller == 0) {
        return 0;
    }
    return (int)(uintptr_t)controller;
}

void ActiveGameplayControllerBase_SetupContext(int *controller, int *context) {
    uintptr_t subsystem410;

    /* 0x80112670 is PTR_PTR_802BEF00 vtable +0x0C.
       Ghidra may show void(void) because FUN_8012A140 recovers both arguments.

       This is the setup method called by CGame_CreateActiveGameplayController after
       the active gameplay controller is created at cgame +0x42C. It copies the
       CGame-owned resource/subsystem context into the controller, clears transient
       setup state, initializes the repeated per-player/visual slots, and applies
       setup-block flags from context[8]. It is still not the draw function. */
    if (controller == 0 || context == 0) {
        return;
    }

    controller[0x14] = context[0];
    controller[0x00] = context[1];
    controller[0x15] = context[2];
    controller[0x16] = context[3];
    controller[0x17] = context[4];
    controller[0x18] = context[5];
    controller[0x01] = context[6];
    controller[0x02] = context[7];
    controller[0x13] = context[8];
    controller[0x19] = context[9];

    subsystem410 = (uintptr_t)(unsigned int)controller[0x18];
    controller[0x1D] = subsystem410 != 0 ? *(int *)(subsystem410 + 0x74) : 0;
    controller[0x1E] = subsystem410 != 0 ? *(int *)(subsystem410 + 0x80) : 0;
    controller[0x500] = 0;
    controller[0x501] = -1;
    controller[0x502] = -1;
    controller[0x503] = -1;
    controller[0x504] = -1;
    controller[0x507] = -1;
    controller[0x508] = -1;
}

int ActiveGameplayController_Create(unsigned int characterOrSetupId) {
    /* 0x80115E10 creates the active gameplay controller stored at cgame +0x42C.
       It chooses between:
       - 0x2BF0-byte base controller initialized by FUN_801120D0
       - 0x2C08-byte special controller initialized by FUN_80116360

       After construction it calls FUN_801157CC(controller, configTable, 0).
       Known special branch: IDs 200, 0xCC, 0xCD use the larger controller and
       config table DAT_80290B50. IDs 0xC9 and 0xCB use the base controller with
       DAT_80290B80. */
    (void)characterOrSetupId;
    return 0;
}
