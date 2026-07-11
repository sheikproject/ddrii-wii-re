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
       controller +0x1428..+0x2A80 -> repeated 0x478-byte entry/controllers initialized
                                      through FUN_80110D30
       controller +0x1300..+0x13FC -> eight repeated 0x20-byte default vectors copied
                                      from DAT_8027D118..DAT_8027D134
       controller +0x0080..+0x0110, then repeated at +0x4A0 strides -> 0x94-byte
                                      default records copied from DAT_8027D07C table
       controller +0x2800..+0x2824 -> transient ids/state cleared or set to -1
       controller +0x2A80..+0x2A9C -> timing/default floats and ids
       controller +0x2AA0..+0x2ACB -> cleared trailing state
       controller +0x2ACC -> initialized to 1

       This constructor is mostly ownership/default-state setup. It does not load the
       menu background, model files, THP movies, or ZMB/ZAB data directly. Those enter
       later through SetupContext and the vtable update/render methods.
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
       setup-block flags from context[8].

       Confirmed side effects from the original:
       - calls FUN_801160A8(controller, context[8]) before copying fields
       - clears controller +0x0C size 0x40
       - calls FUN_800FCD10()
       - clears controller +0x2AA0 size 0x2C
       - calls FUN_801292EC(controller +0x68)
       - initializes eight repeated visual/color/state slots from DAT_802E9410 and
         randomized entries at controller +0x1440
       - mirrors selected default records into the repeated controller slots
       - derives controller[0x1D] and [0x1E] from context[5] +0x74/+0x80
       - uses setup/player data at context[8] to decide [0x1F], [0x507], [0x508],
         and [0xAB3]
       - calls FUN_800626B8(controller[0x18], setupBlock +0x70 < 3)
       - clears controller +0x2AD4 and +0x2B60, each size 0x8C

       It is still not the draw function. */
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

int ActiveGameplayControllerBase_GetEmbeddedSubobject(int *controller) {
    /* 0x80112D94 is PTR_PTR_802BEF00 vtable +0x10.

       It returns the embedded controller subobject initialized at controller +0x68 by
       ActiveGameplayControllerBase_Init and reset by ActiveGameplayControllerBase_SetupContext.
       This is an accessor, not a teardown/reset method. */
    if (controller == 0) {
        return 0;
    }
    return (int)(uintptr_t)(controller + 0x1a);
}

void ActiveGameplayControllerBase_ResetEmbeddedSubobject(int *controller) {
    /* 0x80113C24 is PTR_PTR_802BEF00 vtable +0x14.

       It resets/destroys the embedded controller +0x68 subobject, then switches the
       movie/background binding object at controller +0x5C to mode 1. */
    (void)controller;
}

int ActiveGameplayControllerBase_AreMovieBindingsReady(int *controller) {
    /* 0x80113C60 checks the active controller movie/background binding readiness.

       Original behavior:
       return ActiveControllerMovieBindings_HasPendingSlots(*(controller +0x5C), 1) == 0

       The helper returns nonzero while a slot is still active/pending or not far
       enough through its movie/audio streams, so this wrapper returns true when the
       bindings are ready. */
    (void)controller;
    return 1;
}

int ActiveGameplayControllerBase_AreMode3MovieBindingsReady(int *controller) {
    /* 0x801140E8 checks mode-3 movie/background binding readiness.

       Original behavior:
       return ActiveControllerMovieBindings_HasPendingSlots(*(controller +0x5C), 3) == 0 */
    (void)controller;
    return 1;
}

void ActiveGameplayControllerBase_ResetRuntimeState(int *controller) {
    /* 0x80113CA4 is PTR_PTR_802BEF00 vtable +0x18.

       This refreshes active gameplay/controller runtime state after setup:
       - clears controller +0x0C size 0x40
       - resets/rewires the subsystem at controller +0x04 using controller +0x4C
       - resets stage/resource subsystem controller +0x60
       - when controller +0x64 exists, samples timing/input/resource data and applies
         it through controller +0x60 stage model slots
       - calls ActiveGameplayControllerBase_UpdateTimelineMarker(controller)
       - sets controller +0x3C from setup block flags at controller +0x4C +0x50 bit 0x200
       - resets the vector at controller +0x40
       - clears all model slots counted by *(controller +0x4C +0x218)
       - may select/apply a model slot through controller +0x54
       - refreshes subsystem controller +0x08
       - switches controller +0x5C movie/background bindings to mode 2

       This is lifecycle/resource-state work. It is closer to runtime boot wiring than
       rendering, but still not the main draw method. */
    (void)controller;
}

void ActiveGameplayControllerBase_UpdateTimelineMarker(int *controller) {
    /* 0x80114A90 updates the active controller timeline/frame marker after runtime
       state has been refreshed.

       Confirmed behavior:
       - when setup flags permit, asks the controller data at +0x64 for category-5
         marker data through FUN_8011AB4C/FUN_8011AD00
       - stores the selected marker id at controller +0x2AB4
       - falls back to the current base timeline value at *(controller[0] +0x0C)
       - in forced/disabled cases uses a large sentinel value 100000 and may force
         the stage slot object at controller +0x54 into state 6 through
         CtsStageObjSlot_SetState
       - writes baseTime + markerId * 1000 either to *(controller[0] +0x10) when
         an alternate timeline is active, or to *(controller[0] +0x0C) otherwise
       - refreshes *(controller[0] +0x08) from the controller data helper when
         controller +0x64 is present

       This is timing/state selection, not rendering. */
    (void)controller;
}

void ActiveGameplayControllerBase_ApplyRuntimeEventChannels(int *controller, int frameContext) {
    /* 0x8011399C consumes event/channel records from the controller data pointer at
       controller +0x64 and mirrors them into active runtime state.

       Confirmed channel categories:
       - 7: applies ActiveGameplayControllerBase_ApplyRuntimeVisibilityMask and
            ActiveGameplayControllerBase_TriggerRuntimeCue, then refreshes +0x2ABC
       - 6: applies ActiveGameplayControllerBase_ApplyRuntimeVisualState and
            refreshes controller +0x2AB8
       - 2: updates controller +0x74 and the subsystem at controller +0x60
       - 3: updates controller +0x78 and the subsystem at controller +0x60
       - 8: updates stage slot flag controller +0x7C and, unless preset mode 4 is
            active, mirrors it into *(controller +0x54 +0x5C)
       - 9/10: applies ActiveGameplayControllerBase_ApplyModeTransitionEvent

       If the currently selected event group changes and controller +0x5C exists,
       the original also calls CzanModelManager_StopBank5ModeEffects(0.0f, ...). */
    (void)controller;
    (void)frameContext;
}

void ActiveGameplayControllerBase_ApplyRuntimeVisualState(int *controller, int laneIndex, unsigned int enabledMask) {
    /* 0x80113098 applies category-6 visual/runtime state to one of the controller's
       eight 0x94-byte source records and the eight 0x478-byte presentation records.

       Confirmed behavior:
       - ignores laneIndex >= 8
       - requires controller +0x1418 to have the lane bit set
       - skips when source record +0xCC is nonzero and controller +0x50 bit 2 is set
       - converts enabledMask to a boolean and uses source +0x80 as a blend duration
       - blends/copies two global controller values at +0x1880/+0x1890 and their
         transition timers at +0x1884..+0x189C
       - loops eight 0x478-byte presentation records at controller +0x1428, updating
         value/color transition records from the selected source record
       - when a source mask bit is clear, it chooses a randomized palette/color from
         controller +0x1440 and source-local color tables
       - when a source mask bit is set, it uses explicit source colors/timing fields
       - if source +0xCC differs from controller +0x2A80, calls one of the stage-slot
         helpers at controller +0x54, then stores the new +0x2A80 state

       This is the missing category-6 runtime visual/presentation updater, not a
       geometry loader. */
    (void)controller;
    (void)laneIndex;
    (void)enabledMask;
}

void ActiveGameplayControllerBase_ApplyRuntimeVisibilityMask(int *controller, int laneIndex, unsigned int visibilityMask) {
    /* 0x80112D9C applies a category-7 runtime visibility/enable mask to one of the
       controller's eight 0x20-byte lane tables at controller +0x1300.

       Confirmed behavior:
       - ignores laneIndex >= 8 and controller states where controller +0x50 low bits
         are nonzero
       - walks signed ids in the selected lane until sentinel 0x104
       - converts negative ids back into the same 0..0x17 range
       - enables the id when visibilityMask is nonzero and the normalized id is in
         0..0x17, otherwise disables it
       - negative ids route through FUN_80052ED8; nonnegative ids route through
         FUN_80052E1C unless the model/setup flag path says the id should be kept
       - records the processed lane in a four-entry ring at controller +0x1404,
         indexed by controller +0x1400

       The helper affects controller-managed presentation state, not geometry decode. */
    (void)controller;
    (void)laneIndex;
    (void)visibilityMask;
}

void ActiveGameplayControllerBase_TriggerRuntimeCue(int *controller, int cueId) {
    /* 0x80112F4C handles category-7 cue ids in the 100..199 range.

       Confirmed behavior:
       - maps cueId to a manager cue id with cueId +0x7FF9D
       - for cue ids 100..105, compares controller buffers +0x2AD4 and +0x2B60
         as five 0x1C-byte records and copies +0x2AD4 into +0x2B60
       - suppresses the cue unless the compared records changed and setup flags at
         controller +0x4C allow it
       - dispatches valid cues through FUN_800246E8(gManager_802E70A4, mappedCueId)

       This looks like a runtime cue/sound/manager trigger driven by controller event
       data rather than a model or movie loader. */
    (void)controller;
    (void)cueId;
}

void ActiveGameplayControllerBase_ApplyModeTransitionEvent(
    double transitionSeconds,
    int *controller,
    int targetMode,
    int transitionKind,
    int forceImmediate,
    int eventArg0,
    int eventArg1) {
    /* 0x80113838 applies the category-9/10 runtime event payload that can switch the
       active bank-5 mode.

       Confirmed behavior:
       - asks the embedded controller subobject at controller +0x68 to resolve a
         target mode through FUN_80129308(forceImmediate, targetMode, eventArg0, eventArg1)
       - ignores modes not enabled by controller +0x141C
       - ignores the transition when setup data at controller +0x4C +0x50 has bit
         0x1000 set
       - writes controller +0x70 to the resolved mode
       - unless preset mode 4 is active, compares against the current movie/model
         binding mode from controller +0x5C and calls
         CzanModelManager_RequestBank5ModeTransition
       - clamps controller +0x1424 to 0..3
       - when a transition is already active and transitionSeconds > 0, stores a
         blend reference from the previous 0x478-byte mode record into the new one

       This is one of the important menu/runtime transition functions. */
    (void)transitionSeconds;
    (void)controller;
    (void)targetMode;
    (void)transitionKind;
    (void)forceImmediate;
    (void)eventArg0;
    (void)eventArg1;
}

void ActiveGameplayControllerBase_StopStageModelSlot(int *controller) {
    /* 0x801140A8 is PTR_PTR_802BEF00 vtable +0x1C.

       It resets/fades the stage model slot object at controller +0x54 to zero time,
       then switches controller +0x5C movie/background bindings to mode 3. */
    (void)controller;
}

int ActiveGameplayControllerBase_IsStageModelSlotBusy(int *controller) {
    /* 0x80114044 returns whether the controller is currently in a transition or its
       stage model slot object at controller +0x54 is still busy.

       Original behavior:
       return controller +0x0C != 0 || CtsStageObjSlot_IsBusy(*(controller +0x54)) != 0 */
    (void)controller;
    return 0;
}

void ActiveGameplayControllerBase_StartTimedTransition(int *controller, unsigned int transitionTicks) {
    /* 0x8011412C is PTR_PTR_802BEF00 vtable +0x20.

       It converts transitionTicks through gLargeResourceManager timing, marks
       controller +0x0C active, stores the resulting duration at +0x28, updates the
       controller +0x5C handle with mode 4, resets subsystem +0x60, clears the two
       0x8C-byte runtime buffers, and optionally kicks UI/fade state through
       controller +0x08. */
    (void)controller;
    (void)transitionTicks;
}

void ActiveGameplayControllerBase_ResetTransitionMovieBindings(int *controller) {
    /* 0x80114268 is PTR_PTR_802BEF00 vtable +0x24.

       It clears controller +0x0C size 0x40, then resets the active controller movie
       bindings object stored at controller +0x5C through FUN_80055268. */
    (void)controller;
}

void ActiveGameplayControllerBase_ApplyVisualPreset(int *controller, int presetA, int presetB, int presetGroup) {
    /* 0x80115B80 applies one controller visual/stage preset selected by two preset
       indices and a group.

       It stores a selected pointer-table entry at controller +0x2A94, resets the
       preset transition state at +0x2A98/+0x2A9C, writes default vector/color records
       from DAT_802BEA80 into controller +0x2A70/+0x2A60/+0x267C ranges, optionally
       writes a selected id into subsystem controller +0x60 at +0x74/+0x80, clears the
       stage model slot flag at *(controller +0x54 +0x5C), sets controller +0x1424 = 4,
       and optionally resets/starts the UI/effect controller at controller +0x08. */
    (void)controller;
    (void)presetA;
    (void)presetB;
    (void)presetGroup;
}

void ActiveGameplayControllerBase_CommitVisualPreset(int *controller) {
    /* 0x80115CE8 commits the controller visual/stage preset currently stored in
       controller +0x70/+0x74/+0x78/+0x7C.

       It copies the chosen ids into subsystem controller +0x60, clamps controller
       +0x1424 from +0x70, copies +0x7C into the stage model slot at controller +0x54,
       requests a bank-5 mode transition through controller +0x5C, then optionally
       resets/starts the UI/effect controller at controller +0x08. */
    (void)controller;
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
