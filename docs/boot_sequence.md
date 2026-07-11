# Boot Sequence And Module System

## Runtime Entry

```text
RuntimeEntry / __start  0x80006310
GameMain                0x800204E0
```

`RuntimeEntry` is mostly Nintendo/Metrowerks startup code. It performs low-level runtime
setup, relocates boot-info pointers, initializes OS/C runtime pieces, runs constructors,
then calls `GameMain`.

Suggested Ghidra name/comment:

```c
/* DOL runtime entry / __start.
   Performs low-level runtime setup, relocates boot info pointers,
   initializes OS/C runtime, runs constructors, then calls GameMain. */
void RuntimeEntry(void);
```

## GameMain

`GameMain` initializes engine/runtime systems, allocates global managers, runs the main
loop, then destroys managers in reverse order.

Important allocation pattern:

```c
AllocObjectAligned(0, size, 0x20, 0);
```

Known or likely globals:

```text
DAT_802E70A0 -> gMainLoopManager
DAT_802E70D0 -> gPlayerDataManager
DAT_802E70AC -> gInputOrMenuStateManager
DAT_802E70C0 -> gUiRootManager
DAT_802E70C4 -> gCharacterAssetManager
DAT_802E70C8 -> gBootTempManager
```

Other `DAT_802E70xx` globals are manager objects too, but their exact roles should stay
cautious until their constructors and call sites are documented.

## Main Loop

```text
MainLoopManager_Tick  0x80020CC8
```

Suggested signature:

```c
int MainLoopManager_Tick(int *mainLoopManager);
```

Suggested comment:

```c
/* Runs one frame of the main loop.
   Updates engine, UI, input/menu, scene/module managers, and shutdown/reset checks.
   Returns 1 when the game should exit the main loop. */
```

Observed fields:

```text
mainLoopManager[0] -> frameCounter
mainLoopManager[1] -> resetFrameCounter
mainLoopManager[2] -> shutdownFrameCounter
mainLoopManager[3] -> moduleController
```

Return values:

```text
0 -> keep running
1 -> exit main loop / shutdown
```

## Module Controller

```text
ModuleController_ApplyPendingModule   0x80021048
ModuleController_CreatePendingModule  0x80021140
```

Observed fields:

```text
moduleController[0] -> pendingModuleId
moduleController[1] -> activeModuleId
moduleController[2] -> activeModule
```

Inferred virtual method slots:

```text
+0x08 -> Destroy
+0x0C -> setup/enter-like method for some modules
+0x10 -> update/tick-like method for some modules
+0x14 -> exit-like method for some modules
```

The exact meaning of every vtable slot can vary by class. Function behavior should be
trusted more than assumed vtable order.

Module creation map:

```text
ID 0 -> small boot/module object, size 0x0C, ctor 0x80022078
ID 1 -> BootLogoModule, size 0x3C, ctor 0x800212D4
ID 2 -> CSelect, size 0x1C18, ctor 0x800450AC
ID 3 -> CGame, size 0x430, ctor CGameFactorySetup
ID 4 -> invalid/assert path
ID 5 -> CGame, size 0x430, ctor CGameFactorySetup
ID 6 -> CGame, size 0x430, ctor CGameFactorySetup
```

IDs `5` and `6` likely enter CGame variants or debug/game factory modes.

## BootLogoModule

```text
BootLogoModule_Init     0x800212D4
BootLogoModule_Destroy  0x800212F4
BootLogoModule_OnEnter  0x80021360
BootLogoModule_Tick     0x800214FC
BootLogoModule_OnExit   0x80021480
BootLogoModule_VTable   0x802A4368
```

`BootLogoModule_Init` sets the vtable and initializes state:

```text
+0x08 -> logoResourceHandle
+0x0C -> logoTextureHandle
+0x10 -> logoRegionOrLanguageIndex
+0x14 -> screenWidth
+0x18 -> screenHeight
+0x1C -> renderConfig
+0x20 -> state
+0x24 -> logoStepIndex
+0x28 -> frameAnimIndex
+0x2C -> frameAnimTimer
+0x30 -> stateTimer
+0x34 -> skipLogoOrProgressiveFlag
+0x38 -> fadeAlpha
```

Suggested `BootLogoModule_Tick` comment:

```c
/* Runs the boot logo state machine.
   Loads logo/logo_*.tpl, fades logos in/out, handles skip/input timing,
   and returns module ID 2 when state reaches 0xD. */
```

Known tick states:

```text
3  -> initialize skip/progressive mode
4  -> load logo/logo_*.tpl
5  -> create texture/draw resource
6  -> setup fade-in
7  -> fade in
8  -> setup hold/animation
9  -> hold logo, animate frames, wait timeout/input
10 -> setup fade-out
11 -> fade out, maybe next logo step
12 -> wait for system fade/task complete
13 -> finished; returns module ID 2
```

## CSelect Module

```text
CSelect_Init     0x800450AC
CSelect_Destroy  0x80045244
CSelect_OnEnter  0x800452B0
CSelect_Tick     0x800457C8
CSelect_OnExit   0x80045580
CSelect_VTable   0x802B95F0
```

`CSelect_Init` initializes module ID `2`. It clears state, initializes two large internal
buffers, resets handle arrays to `-1`, and releases stale child/resource pointers if
present.

Important fields:

```text
+0x04   vtable
+0x38   currentSelectState?
+0x3C   nextSelectState
+0x40   previousSelectState
+0x44   handle/id array start, initialized to -1
+0x94   state/flag
+0x0C4  buffer A, size 0x910
+0x9D4  buffer B, size 0x121C
+0x1BF0 activeSelectScreen
+0x1BF4 sharedFlowObject
+0x1BFC selectCommonResource
+0x1C00 selectMusicResource
+0x1C04 requestFlowResource
+0x1C08 worldFlowResource
+0x1C10 musicPreviewResource
+0x1C14 extraMusicPreviewResource
```

`CSelect_OnEnter` loads common/select resources and chooses the starting select state.
Known resource handles:

```text
cSelect[0x700] -> selMusicResource
cSelect[0x701] -> selTitleResource
cSelect[0x702] -> selResultResource
cSelect[0x703] -> commonSelectResource
```

Known resource paths:

```text
/select/cmnAccMdl.bin
/select/selMusic_*.bin
/select/selTitle_*.bin
/select/selResult_*.bin
```

Known `CSelect_Tick` sub-screen creation hints:

```text
state 0x01 -> CSelMode / mode select, ctor 0x8006E2B8, size 0x5C0
state 0x04 -> character select, ctor 0x800656CC, size 0x181C
state 0x05 -> music select, ctor 0x8006F6D0, size 0x22EC
state 0x08 -> result-like large screen, ctor 0x800A22E4, size 0xB578
state 0x0F -> result-like large screen, ctor 0x800A22E4, size 0xB578
state 0x10 -> music preview/SSQ flow, ctor 0x800F42F4, size 0x564
state 0x11 -> DDR Points/display related screen, ctor 0x800F8EC0, size 0x160
state 0x1D -> transition into game module/CGame path
```

## CGame Setup

`FUN_8003C8A0` is the top-level CGame state dispatcher. Suggested name:

```text
CGame_UpdateStateMachine
```

Confirmed state map from `cgame[2]`:

```text
0 -> set cgame[2] = 1
1 -> CGame_PrepareManagersAndResources(cgame)
2 -> CGame_PrepareSceneFromSelectedSetup(cgame)
3 -> CGame_PrepareActiveGameplayState(cgame)
4 -> CGame_UpdateActiveGameplayTransitionState(cgame)
5 -> return cgame[0] as the next module/state
```

It also updates bits `0x40000000/0x80000000` in `cgame +0xB0` depending on
`*(DAT_802E71B8 +0x260 +0x34)`, and resets `cgame[3]` to zero whenever `cgame[2]`
changes.

The next likely per-frame/render targets are therefore:

```text
FUN_800418F8  -> state 3
CGame_UpdateActiveGameplayTransitionState -> state 4
```

`FUN_800418F8` is the state-3 setup/transition state machine. Suggested name:

```text
CGame_PrepareActiveGameplayState
```

Confirmed behavior:

```text
0:
  reset gLargeResourceManager, gManager_802E70A4, gManager_802E70A8
  clear cgame +0x414
  reset cgame +0x428 and optional cgame +0x42C
  reset subsystems cgame +0x400 and cgame +0x3F0
  if cgame +0x0B8 == -1:
    CGame_BuildActiveGameplaySetupFromPlayerData(cgame)
    FUN_80118E54(cgame +0x3F0, cgame +0x0B8)
    substate = 3
  else:
    substate = 2

2:
  result = CGame_UpdateActiveGameplaySetupSelection(cgame)
  if result == -1:
    cgame +0x08 = 2
  else if result == 1:
    FUN_80118E54(cgame +0x3F0, cgame +0x0B8)
    substate = 3

3:
  wait for resource managers
  CGame_CreateActiveGameplayController(cgame)
  substate = 8

8:
  wait for *(DAT_802E71F8 +0x0C) == 0
  substate = 0x0C

0x0C:
  cgame +0x08 = 4
```

This is still setup/transition logic. The next target for the active state path is
`CGame_CreateActiveGameplayController`, because it is called once state 3's resources
are ready.

`FUN_8003FBB0` creates/configures the active gameplay controller. Suggested name:

```text
CGame_CreateActiveGameplayController
```

Confirmed behavior:

```text
resource 5 from cgame +0x3F0:
  FUN_80060BC4(cgame +0x400, 5, resource)

resource 0x32 from cgame +0x3F0:
  FUN_80061EC8(cgame +0x400, 0x40, resource)

for each entry in cgame +0x2D0:
  entry = cgame +0x2D4 + index * 0x38
  FUN_800610B4(cgame +0x400, index, entry)

  build a local list from entry +0x30 flags and resource IDs 0..0x15
  build another local list from FUN_8011950C(cgame +0x3F0, entry[1], slot)

  FUN_800612A4(cgame +0x400, entry[0], entry[1], resourceInfo, ...)
  FUN_80061430(cgame +0x400, entry[0], entry[1], entry[2], ...)
  for entry[6..9] values that are not -1:
    FUN_80061430(cgame +0x400, entry[0], entry[1], value, ...)
  FUN_800611B4(cgame +0x400, index)

handle = FUN_80061C30(cgame +0x400)
FUN_80052D00(cgame +0x404, handle)

cgame +0x42C = thunk_FUN_80115E10(cgame +0xFC)

context contains:
  cgame +0x408  model position set
  cgame +0x3F8
  cgame +0x3FC
  cgame +0x404
  cgame +0x400
  cgame +0x410
  cgame +0x40C
  cgame +0x3F4
  cgame +0x0B8 setup/player data block

controller vtable +0x0C is called with that context
```

This still does not render directly. It creates the object at `cgame +0x42C`; the
controller object's vtable methods are the next likely place to find per-frame update
and draw behavior.

`FUN_80115E10` is the factory for the active gameplay controller stored at
`cgame +0x42C`. Suggested name:

```text
ActiveGameplayController_Create
```

Confirmed behavior:

```text
if 10 <= id < 30:
  allocate 0x2BF0
  FUN_801120D0(controller)
  FUN_801157CC(controller, DAT_80290970 + (id - 10) * 0x18, 0)

else if id < 8:
  allocate 0x2BF0
  FUN_801120D0(controller)
  FUN_801157CC(controller, DAT_802908B0 + id * 0x18, 0)

else if 200 <= id < 206:
  if id == 200 or id == 0xCC or id == 0xCD:
    allocate 0x2C08
    FUN_80116360(controller)
    FUN_801157CC(controller, DAT_80290B50, 0)
  else:
    allocate 0x2BF0
    FUN_801120D0(controller)
    FUN_801157CC(controller, DAT_80290B80, 0)

else:
  allocate 0x2BF0
  FUN_801120D0(controller)
  FUN_801157CC(controller, DAT_80290B80, 0)
```

The next vtable targets are therefore the constructors:

```text
FUN_801120D0 -> base active gameplay controller constructor, size 0x2BF0
FUN_80116360 -> special active gameplay controller constructor, size 0x2C08
FUN_801157CC -> applies the 0x18-byte config table entry to the controller
```

`FUN_801120D0` is the base active gameplay controller constructor. Suggested name:

```text
ActiveGameplayControllerBase_Init
```

Important Ghidra note: this may decompile as `void(void)` because it uses
`FUN_8012A130` / `FUN_8012A17C` to recover and return the object pointer. The logical
signature is:

```c
int ActiveGameplayControllerBase_Init(int *controller);
```

Confirmed base controller fields:

```text
controller +0x2BEC -> vtable PTR_PTR_802BEF00
controller +0x0068 -> subobject initialized by FUN_8012927C
controller +0x1428..+0x2A80 -> repeated 0x478-byte-ish subobjects initialized by FUN_80110D30
controller +0x1300..+0x13FC -> eight repeated 0x20-byte default vectors copied from DAT_8027D118..DAT_8027D134
controller +0x0080..+0x0110, then repeated at +0x4A0 strides -> 0x94-byte default records copied from DAT_8027D07C table
controller +0x2800..+0x2824 -> transient ids/state cleared or set to -1
controller +0x2A80..+0x2A9C -> timing/default floats and ids
controller +0x2AA0..+0x2ACB -> trailing 0x2C-byte state cleared
controller +0x2ACC -> set to 1
```

This constructor is ownership/default-state setup. It does not load the menu
background, THP movies, or ZMB/ZAB data directly. The boot/runtime path still needs
the `PTR_PTR_802BEF00` methods after construction, starting with setup at vtable
`+0x0C`.

`FUN_801125E8` destroys the base active gameplay controller. Suggested name:

```text
ActiveGameplayControllerBase_Destroy
```

It destroys the embedded controller subobject at `controller +0x68` through
`ActiveGameplayControllerSubobject_Destroy(..., -1)`, then frees the controller only
when the release mode is positive.

`FUN_80129280` is the matching embedded-subobject destroy/free helper. Suggested name:

```text
ActiveGameplayControllerSubobject_Destroy
```

The `PTR_PTR_802BEF00` vtable is now the next target. From
`CGame_CreateActiveGameplayController`, we already know:

```text
vtable +0x0C -> called during controller setup with the context struct
vtable +0x10 -> returns embedded controller subobject at controller +0x68
vtable +0x14 -> resets embedded subobject and releases controller +0x5C handle
vtable +0x18 -> refreshes runtime/stage resource state and model slots
vtable +0x1C -> stops/resets stage model slot object and updates controller +0x5C
vtable +0x20 -> starts timed controller transition and kicks fade/UI state
vtable +0x24 -> clears transition state and resets controller movie bindings
```

So the next function to inspect after setup/accessor/reset/runtime-refresh is the
function pointer at `PTR_PTR_802BEF00 +0x28`.

`FUN_80112670` is `PTR_PTR_802BEF00 +0x0C`, the setup method called by
`CGame_CreateActiveGameplayController` after creating the object at `cgame +0x42C`.
Suggested name:

```text
ActiveGameplayControllerBase_SetupContext
```

Important Ghidra note: this may decompile as `void(void)` because it uses
`FUN_8012A140` to recover both arguments. The logical signature is:

```c
void ActiveGameplayControllerBase_SetupContext(int *controller, int *context);
```

Confirmed context layout from `CGame_CreateActiveGameplayController`:

```text
context[0] -> cgame +0x408 model position set
context[1] -> cgame +0x3F8 subsystem
context[2] -> cgame +0x3FC subsystem
context[3] -> cgame +0x404 subsystem
context[4] -> cgame +0x400 subsystem
context[5] -> cgame +0x410 subsystem
context[6] -> cgame +0x40C subsystem
context[7] -> cgame +0x3F4 subsystem
context[8] -> cgame +0x0B8 setup/player data block
context[9] -> local setup flags
```

Confirmed controller field copies:

```text
controller[0x14] = context[0]
controller[0x00] = context[1]
controller[0x15] = context[2]
controller[0x16] = context[3]
controller[0x17] = context[4]
controller[0x18] = context[5]
controller[0x01] = context[6]
controller[0x02] = context[7]
controller[0x13] = context[8]
controller[0x19] = context[9]
```

Additional confirmed behavior:

```text
FUN_801160A8(controller, context[8])
clear controller +0x0C size 0x40
FUN_800FCD10()
clear controller +0x2AA0 size 0x2C
FUN_801292EC(controller +0x68)
initialize 8 repeated per-player/visual color/state slots
controller[0x1D] = *(controller[0x18] +0x74), or 0 if controller[0x18] is null
controller[0x1E] = *(controller[0x18] +0x80), or 0 if controller[0x18] is null
reset controller[0x500..0x508] setup ids/flags
use setup/player data block at controller[0x13] to set controller[0x1F], [0x507], [0x508], [0xAB3]
FUN_800626B8(controller[0x18], setupBlock +0x70 < 3)
update bit 1 in controller[0x14] from setupBlock +0x70
clear controller +0x2AD4 size 0x8C
clear controller +0x2B60 size 0x8C
```

`FUN_80112D94` is `PTR_PTR_802BEF00 +0x10`. Suggested name:

```text
ActiveGameplayControllerBase_GetEmbeddedSubobject
```

Logical signature:

```c
int ActiveGameplayControllerBase_GetEmbeddedSubobject(int *controller);
```

Confirmed behavior:

```text
return controller +0x68
```

This returns the subobject initialized by `FUN_8012927C` during
`ActiveGameplayControllerBase_Init` and reset by `FUN_801292EC` during
`ActiveGameplayControllerBase_SetupContext`. It is an accessor, not a teardown/reset
method.

This is still setup/state access, not rendering. The next likely controller
lifecycle/render targets are the later `PTR_PTR_802BEF00` methods, especially `+0x18`
onward.

`FUN_80113C24` is `PTR_PTR_802BEF00 +0x14`. Suggested name:

```text
ActiveGameplayControllerBase_ResetEmbeddedSubobject
```

Confirmed behavior:

```text
FUN_8012938C(controller +0x68)
ActiveControllerMovieBindings_SetMode(*(controller +0x5C), 1)
```

This is cleanup/reset for the embedded controller subobject and one resource/object
handle at `controller +0x5C`. It is still not the main update/render method.

`FUN_80113C60` checks movie/background binding readiness for the active controller.
Suggested name:

```text
ActiveGameplayControllerBase_AreMovieBindingsReady
```

Confirmed behavior:

```text
return ActiveControllerMovieBindings_HasPendingSlots(*(controller +0x5C), 1) == 0
```

`FUN_80055314` is the movie binding pending-slot check helper. Suggested name:

```text
ActiveControllerMovieBindings_HasPendingSlots
```

Confirmed behavior:

```text
mode 1:
  if movieBindings +0xB36C == 0:
    check special slot +0xB34C and category slots +0xB340/+0xB344/+0xB348
    through ResourceSlotHandle_IsActivePending
  if none are active/pending:
    required slot +0xB340 must pass MovieSlotHandle_IsReadyForDisplay
    special slot +0xB34C must pass MovieSlotHandle_IsReadyForDisplay

mode 3:
  check category slots 1..2 through MovieSlotHandle_IsReadyForDisplay

return nonzero while any required slot is pending/not ready
```

`FUN_80025148` checks whether a claimed movie slot has progressed far enough to use.
Suggested name:

```text
MovieSlotHandle_IsReadyForDisplay
```

It returns false while the slot is active/pending. Otherwise it checks the claimed
`CzanMovieObj` stream progress and returns true when the video side is at least 60%
and the second stream/progress side is at least 50%.

`FUN_801140E8` is the mode-3 version of the movie/background binding readiness check.
Suggested name:

```text
ActiveGameplayControllerBase_AreMode3MovieBindingsReady
```

Confirmed behavior:

```text
return ActiveControllerMovieBindings_HasPendingSlots(*(controller +0x5C), 3) == 0
```

`FUN_80113CA4` is `PTR_PTR_802BEF00 +0x18`. Suggested name:

```text
ActiveGameplayControllerBase_ResetRuntimeState
```

Confirmed behavior:

```text
clear controller +0x0C size 0x40
FUN_800FCC70(*(controller +0x04))
FUN_800FCD28(*(controller +0x04), *(controller +0x4C))
FUN_800614F4(*(controller +0x60))
FUN_800624C4(*(controller +0x60))

if controller +0x64 exists:
  derive timing scale from gManager_802E70A4
  sample channel/state data through ActiveGameplayControllerEventData_FindNearbyEvent
  apply it to subsystem controller +0x60 through FUN_8006153C and FUN_80061848
  when setup flags allow, calls ActiveGameplayControllerBase_ApplyRuntimeVisualState
  and ActiveGameplayControllerBase_ApplyRuntimeEventChannels

ActiveGameplayControllerBase_UpdateTimelineMarker(controller)
controller +0x3C = setupBlock +0x50 bit 0x200
reset vector at controller +0x40
for each setupBlock +0x218 model slot:
  FUN_8006209C(*(controller +0x60), slot, -1)

if setupBlock +0x64 < 2:
  either disables controller +0x54 through FUN_8005C35C
  or applies a stage model slot through CtsStageObj_SelectAndApplyModelSlot

FUN_800FA954(*(controller +0x08))
ActiveControllerMovieBindings_SetMode(*(controller +0x5C), 2)
```

This is lifecycle/resource-state work. It is closer to runtime boot wiring than the
previous setup helpers, but still not the main draw method.

`FUN_80114A90` updates the active controller timeline/frame marker after runtime
state refresh. Suggested name:

```text
ActiveGameplayControllerBase_UpdateTimelineMarker
```

Confirmed behavior:

```text
if setup flags permit:
  query category-5 marker data from controller +0x64 through
  ActiveGameplayControllerEventData_SampleCurrentEvent /
  ActiveGameplayControllerEventData_FindNearbyEvent
  store the marker id at controller +0x2AB4
  otherwise fall back to *(controller[0] +0x0C)
else:
  use sentinel time 100000
  if the stage slot at controller +0x54 is in state 0:
    CtsStageObjSlot_SetState(controller +0x54, 6)

write baseTime + markerId * 1000 to:
  *(controller[0] +0x10) when the alternate timeline is active
  *(controller[0] +0x0C) otherwise

refresh *(controller[0] +0x08) from controller +0x64 when present
```

`FUN_8011399C` applies runtime event/channel data from the controller data pointer at
`controller +0x64`. Suggested name:

```text
ActiveGameplayControllerBase_ApplyRuntimeEventChannels
```

Confirmed channel categories:

```text
7  -> calls ActiveGameplayControllerBase_ApplyRuntimeVisibilityMask and
      ActiveGameplayControllerBase_TriggerRuntimeCue, updates controller +0x2ABC
6  -> calls ActiveGameplayControllerBase_ApplyRuntimeVisualState, updates controller +0x2AB8
2  -> updates controller +0x74 and subsystem controller +0x60 +0x74/+0x80
3  -> updates controller +0x78 and subsystem controller +0x60 +0x74/+0x80
8  -> updates controller +0x7C and mirrors it into *(controller +0x54 +0x5C)
      unless preset mode 4 is active
9/10 -> calls ActiveGameplayControllerBase_ApplyModeTransitionEvent
```

When the selected event group changes and `controller +0x5C` exists, the original
also calls `CzanModelManager_StopBank5ModeEffects(0.0f, controller +0x5C)`.

`FUN_8011AB4C` samples the current event record for one runtime category from the
controller event data object at `controller +0x64`. Suggested name:

```text
ActiveGameplayControllerEventData_SampleCurrentEvent
```

`FUN_8011AD00` searches forward/backward from the current event index until a valid
event for the requested category is found. Suggested name:

```text
ActiveGameplayControllerEventData_FindNearbyEvent
```

Both helpers emit the same 9-word event result used by the active controller channel
logic. `FUN_8011AF30` resolves the primary range value from the event data range
table. Suggested name:

```text
ActiveGameplayControllerEventData_ResolveRangeValue
```

`FUN_8011B030` resolves the matching secondary range value from the same range table,
returning the word at `eventData +0x120 + slotIndex*4`. Suggested name:

```text
ActiveGameplayControllerEventData_ResolveSecondaryRangeValue
```

`FUN_8004205C` starts the active gameplay controller transition from CGame after the
controller exists at `cgame +0x42C`. Suggested name:

```text
CGame_StartActiveGameplayControllerTransition
```

It calls active controller vtable `+0x20`, starts optional movie playback through
`MovieSlotHandle_StartPlayback`, kicks a manager transition through
`CGameTransitionManager_Start`, updates subsystem `cgame +0x428` through
`CGameTransitionSlot_Start`, and sets `cgame +0x424 = 1`.

`FUN_800245A4` starts the manager-side transition used by
`CGame_StartActiveGameplayControllerTransition`. Suggested name:

```text
CGameTransitionManager_Start
```

It looks up a transition record from the manager, initializes timer/state fields at
`+0x45C..+0x474`, converts duration/speed through `FUN_8012A03C`, and can spawn a
manager effect through `FUN_8016B270`.

`FUN_80125728` starts/arms the transition subsystem stored at `cgame +0x428`.
Suggested name:

```text
CGameTransitionSlot_Start
```

It calls `FUN_800F8DB0` on `transitionSlot +0x80` when present, then marks
`transitionSlot +0x84 = 1`.

`FUN_800430CC` is the CGame state-4 active gameplay transition/update state. Suggested
name:

```text
CGame_UpdateActiveGameplayTransitionState
```

It is the large CGame-level state machine after `CGame_CreateActiveGameplayController`.
The attached decompile shows controller updates, UI-root checks, movie playback
readiness, input/abort handling, and transition completion/handoff logic.

`FUN_80113098` applies category-6 visual/runtime state to one lane/source record.
Suggested name:

```text
ActiveGameplayControllerBase_ApplyRuntimeVisualState
```

Confirmed behavior:

```text
ignore laneIndex >= 8
require controller +0x1418 lane bit
skip when source record +0xCC is nonzero and controller +0x50 bit 2 is set
blend/copy controller global values at +0x1880/+0x1890
loop eight presentation records at controller +0x1428, stride 0x478:
  update value/color transition fields
  either choose randomized palette data from controller +0x1440
  or use explicit source color/timing fields
if source +0xCC differs from controller +0x2A80:
  call a stage-slot helper at controller +0x54
  store source +0xCC into controller +0x2A80
```

`FUN_80112D9C` applies a category-7 visibility/enable mask to one of the controller's
eight lane tables at `controller +0x1300`. Suggested name:

```text
ActiveGameplayControllerBase_ApplyRuntimeVisibilityMask
```

Confirmed behavior:

```text
ignore laneIndex >= 8
ignore if controller +0x50 low two bits are nonzero
for each signed id in controller +0x1300 + laneIndex * 0x20 until sentinel 0x104:
  normalize negative ids back into the same 0..0x17 id range
  enable when visibilityMask is nonzero and normalized id is 0..0x17
  negative ids route through FUN_80052ED8
  nonnegative ids route through FUN_80052E1C unless setup/model flags keep them enabled

record the processed lane in a four-entry ring at controller +0x1404
```

`FUN_80112F4C` handles category-7 cue ids in the `100..199` range. Suggested name:

```text
ActiveGameplayControllerBase_TriggerRuntimeCue
```

Confirmed behavior:

```text
mappedCueId = cueId +0x7FF9D
for cue ids 100..105:
  compare controller +0x2AD4 and +0x2B60 as five 0x1C-byte records
  copy controller +0x2AD4 to +0x2B60
  suppress the cue unless the records changed and setup flags allow it

if not suppressed:
  FUN_800246E8(gManager_802E70A4, mappedCueId)
```

`FUN_80113838` applies the category-9/10 runtime event payload that can switch the
active bank-5 mode. Suggested name:

```text
ActiveGameplayControllerBase_ApplyModeTransitionEvent
```

Confirmed behavior:

```text
mode = ActiveGameplayControllerSubobject_ResolveModeTransitionSlot(
    controller +0x68, forceImmediate, targetMode, eventArg0, eventArg1)
ignore mode < 0 or not enabled by controller +0x141C
ignore when setup data at controller +0x4C +0x50 has bit 0x1000
controller +0x70 = mode
if preset mode 4 is not active and current movie/model binding mode differs:
  optionally force transitionSeconds to 0 when forceImmediate != 0
  CzanModelManager_RequestBank5ModeTransition(
      transitionSeconds, controller +0x5C, mode, transitionKind)
  controller +0x1424 = min(mode, 3)
  if an active transition is present and transitionSeconds > 0:
    store blend source from old mode's 0x478-byte record into the new mode record
```

`FUN_801140A8` is `PTR_PTR_802BEF00 +0x1C`. Suggested name:

```text
ActiveGameplayControllerBase_StopStageModelSlot
```

Confirmed behavior:

```text
CtsStageObjSlot_ResetDescriptorFrames(0.0f, *(controller +0x54))
ActiveControllerMovieBindings_SetMode(*(controller +0x5C), 3)
```

This is another lifecycle method around the stage model slot object at `controller
+0x54` and the handle at `controller +0x5C`.

`FUN_80114044` checks whether the active gameplay controller should still be treated
as busy because a transition flag or stage model slot is active. Suggested name:

```text
ActiveGameplayControllerBase_IsStageModelSlotBusy
```

Confirmed behavior:

```text
return *(controller +0x0C) != 0 || CtsStageObjSlot_IsBusy(*(controller +0x54)) != 0
```

`FUN_8005CA9C` checks whether the `CtsStageObj` slot/descriptor is still busy.
Suggested name:

```text
CtsStageObjSlot_IsBusy
```

It chooses a descriptor from the slot's sentinel/current-entry state and ultimately
uses `CtsStageObjDescriptor_GetCurrentEntryActiveFlag`.

`FUN_8005C9F4` rewinds/activates all descriptor frames owned by a CtsStageObj slot.
Suggested name:

```text
CtsStageObjSlot_ResetDescriptorFrames
```

Confirmed behavior:

```text
for each descriptor counted by slot +0x15C, base slot +0x164, stride 0x7C:
  CtsStageObjDescriptor_ActivateEntry(1.0f, descriptor, 0, 0)
  CtsStageObjDescriptor_SetFrameProgress(frameProgress,
      descriptor[0] + descriptor[1] * 0xA8, 0)
```

`FUN_8005AC14` returns the current entry active flag from a descriptor. Suggested name:

```text
CtsStageObjDescriptor_GetCurrentEntryActiveFlag
```

Confirmed behavior:

```text
if descriptor[0] == 0:
  return 1
return *(descriptor[0] + descriptor[1] * 0xA8 +0x20)
```

`FUN_8011412C` is `PTR_PTR_802BEF00 +0x20`. Suggested name:

```text
ActiveGameplayControllerBase_StartTimedTransition
```

Logical signature:

```c
void ActiveGameplayControllerBase_StartTimedTransition(int *controller, unsigned int transitionTicks);
```

Confirmed behavior:

```text
duration = FUN_80026500(float(transitionTicks), gLargeResourceManager)
controller +0x0C = 1
controller +0x28 = duration
ActiveControllerMovieBindings_SetMode(*(controller +0x5C), 4)
FUN_800624C4(*(controller +0x60))
FUN_800614F4(*(controller +0x60))
clear controller +0x2AD4 size 0x8C
clear controller +0x2B60 size 0x8C

if controller +0x1420 bit 2:
  controller +0x7C = 1
  if controller +0x1424 != 4:
    *(controller +0x54 +0x5C) = 1

if controller +0x2ACC != 0:
  FUN_800FB698(0.0f, *(controller +0x08))
  if setupBlock +0x50 exists and setup flags allow:
    slot = *(setupBlock +0x50 +0x1C)
    slot < 3  -> FUN_800FB608(...)
    slot == 3 -> FUN_800FB64C(...)
```

This appears to begin a controller transition/timed fade sequence using global large
resource timing and the UI/effect subsystem at `controller +0x08`.

`FUN_80114268` is `PTR_PTR_802BEF00 +0x24`. Suggested name:

```text
ActiveGameplayControllerBase_ResetTransitionMovieBindings
```

Confirmed behavior:

```text
clear controller +0x0C size 0x40
ActiveControllerMovieBindings_Reset(*(controller +0x5C))
```

`FUN_80055268` is the movie/background binding reset helper used by this method.
Suggested name:

```text
ActiveControllerMovieBindings_Reset
```

Confirmed behavior:

```text
movieBindings +0xB360 = 0
if movieBindings +0xB34C != -1:
  MovieSlotHandle_ResetClaimedSlot(gManager_802E70A8)

for categories 0..2:
  if category == 3: special slot +0xB34C path
  else:
    slot index = movieBindings +0xB340 + category * 4
    if slot index != -1:
      MovieSlotHandle_ResetClaimedSlot(gManager_802E70A8)
    movieBindings +0xB334 + category * 4 = 0
```

`FUN_801142C4` is the active gameplay controller runtime tick. Suggested name:

```text
ActiveGameplayControllerBase_TickRuntime
```

This is one of the important runtime methods after controller setup. It advances
controller time through `gLargeResourceManager`, updates event data at `controller
+0x64`, samples category 4 events to select/apply `CtsStageObj` model slots at
`controller +0x54`, samples categories 0 and 1 to update subsystem `controller +0x60`,
applies runtime event channels when setup flags allow, and finishes through
`ActiveGameplayControllerBase_UpdateTimelineMarker`.

`FUN_80114D18` is a later active-controller update pass that pushes model matrices and
advances presentation/preset state. Suggested name:

```text
ActiveGameplayControllerBase_LateUpdateTransforms
```

Confirmed behavior:

```text
copy current matrix from controller +0x54 through CzanModelOwner_CopyCurrentModelMatrix
obtain another matrix from controller +0x54 subobject vtable +0x18
push both into controller +0x5C through CzanModelManager_UpdateCurrentModeMatrices
when not skipped, advance five 0x478-byte presentation records at controller +0x1428
update current mode record controller +0x1428 + controller[0x1424]*0x478
when controller +0x1424 == 4, advance the visual preset table at +0x2A94
mirror selected preset ids into subsystem controller +0x60
call FUN_80114BF0 and possibly restart UI/effect fade state
```

The matrix helpers in that path are:

```text
FUN_8015EAD0 -> CzanModelOwner_CopyCurrentModelMatrix
FUN_80052D98 -> CzanModelManager_UpdateCurrentModeMatrices
FUN_80059294 -> CtsStageObjDescriptor_GetModelTransform
FUN_8011F8A8 -> CzanModelLiveObject_SetModelMatrices
FUN_801459FC / FUN_801B0E00 -> Matrix44_Copy
```

`FUN_80055090` changes the active controller movie/background binding mode. Suggested
name:

```text
ActiveControllerMovieBindings_SetMode
```

Confirmed modes:

```text
1 -> load/rebind categories 0..3 through ActiveControllerMovieBindings_LoadCategoryMovie;
     clear +0xF068/+0xF06C
2 -> enable/switch category movie handles through MovieSlotHandle_SetObjectEnabled and
     ActiveControllerMovieBindings_StartCategoryMovie; set +0xF06C = 1
4 -> enable the special/current mode slot for a timed controller transition
```

`FUN_80055008` resolves which bank-5/movie mode is currently visible. Suggested name:

```text
ActiveControllerMovieBindings_GetVisibleModeIndex
```

It normally returns the current mode byte at `movieBindings +0x2D`, but during
transition state `+0xB0C4 == 3` it can return the previous mode byte at `+0x2E` while
the old background is still effectively visible.

`FUN_80055A94` toggles the claimed movie objects for the category slots. Suggested
name:

```text
ActiveControllerMovieBindings_UpdateCategoryVisibility
```

If the force flag is set, it enables every valid category slot. Otherwise, when
`+0xB36C` is clear and binding mode `+0xB360` is `4`, it disables the currently visible
category slot and enables the others. The actual toggle is
`MovieSlotHandle_SetObjectEnabled(gManager_802E70A8, slot, enabled)`.

`FUN_80055084` stores the global transition/visibility flag at `+0xB36C`, then calls
the visibility update helper. Suggested name:

```text
ActiveControllerMovieBindings_SetTransitionFlagAndUpdateVisibility
```

`FUN_80055590` chooses and binds the THP movie path for one active-controller
background category. Suggested name:

```text
ActiveControllerMovieBindings_LoadCategoryMovie
```

Confirmed path rules:

```text
category 3 -> movie/stage/single01_w.thp
type 0     -> movie/stage/upt01.thp
type 1     -> movie/bgv/%s.thp
type 2     -> randomized numbered stage path based on owner +0xB368:
              1 -> movie/stage/upt_%s%02d.thp
              2 -> movie/stage/pop_%s%02d.thp
              4 -> movie/stage/mvo_%s%02d.thp
              5/default -> movie/stage/fvo_%s%02d.thp
type 3     -> movie/stage/%s.thp
type 4     -> movie/zz_pv/ddr%03d.thp
```

`FUN_80055914` starts/enables one already-bound active-controller movie category.
Suggested name:

```text
ActiveControllerMovieBindings_StartCategoryMovie
```

It selects the category slot, marks per-category started state, calls
`MovieSlotHandle_StartPlayback(1.0f, gManager_802E70A8, slot, enableFlag)`, and for
category byte `1` also applies position/scale/timing through
`MovieSlotHandle_SetPlacementRect`.

`FUN_80025248` toggles the object enabled flag on a claimed movie slot. Suggested name:

```text
MovieSlotHandle_SetObjectEnabled
```

Confirmed behavior:

```text
slot = ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotIndex)
if slot exists:
  CzanMovieObjChild_SetEnabled(slot +0x114, enabled)
```

`FUN_80024F3C` binds a THP/movie resource path to a claimed movie slot. Suggested name:

```text
MovieSlotHandle_LoadResource
```

Confirmed behavior:

```text
slot = ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotIndex)
if slot exists:
  CzanMovieObj_Reset(slot)
  CzanMovieObj_LoadResource(slot, resourceOrPath, 0)
```

`FUN_8002500C` starts/prepares playback on a claimed movie slot. Suggested name:

```text
MovieSlotHandle_StartPlayback
```

Confirmed behavior:

```text
slot = ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotIndex)
if slot exists:
  CzanMovieObj_ClearPlaybackState(slot)
  slot +0x150 = clamp(inputScalar)
  CzanMovieObj_StartPlayback(slot, enabled, 0)
```

`FUN_800250B0` polls bit `0x400` on a claimed movie slot. Suggested name:

```text
MovieSlotHandle_HasPlaybackStarted
```

That bit is set by `CzanMovieObj_StartPlayback`, so this is the select/common
movie-background playback-started poll rather than a full decode/readiness check.

`FUN_80025368` is the thin getter for a claimed movie object. Suggested name:

```text
MovieSlotHandle_GetClaimedObject
```

`FUN_80025508` writes four float placement/timing values to a claimed movie object.
Suggested name:

```text
MovieSlotHandle_SetPlacementRect
```

`FUN_8002561C` writes a control value to claimed movie object `+0x278`. Suggested
temporary name:

```text
MovieSlotHandle_SetPlaybackFlag278
```

`FUN_80185150` clears transient playback/subsystem state on an active movie object.
Suggested name:

```text
CzanMovieObj_ClearPlaybackState
```

`FUN_80184FE8` starts playback on an already-loaded movie object and sets flag bit
`0x400`. Suggested name:

```text
CzanMovieObj_StartPlayback
```

`FUN_80190440` toggles bit `0x200` on the child/object record at movie slot `+0x114`.
Suggested name:

```text
CzanMovieObjChild_SetEnabled
```

`FUN_8005CB94` applies the selected descriptor transform into a CtsStageObj slot.
Suggested name:

```text
CtsStageObjSlot_ApplySelectedDescriptorTransform
```

`FUN_8005CE7C` initializes the transform used by CtsStageObj slot state 5. Suggested
name:

```text
CtsStageObjSlot_InitState5Transform
```

`FUN_8005C4AC` and `FUN_8005C50C` begin/end the special descriptor path at
`slot +0x1FC`. Suggested names:

```text
CtsStageObjSlot_BeginSpecialDescriptor
CtsStageObjSlot_EndSpecialDescriptor
```

`FUN_8005D0E8` changes the state at `CtsStageObjSlot +0x80`. Suggested name:

```text
CtsStageObjSlot_SetState
```

State `6` is the one used by `ActiveGameplayControllerBase_UpdateTimelineMarker` for
the forced/sentinel path.

`FUN_80025104` is the claimed movie-slot reset wrapper. Suggested name:

```text
MovieSlotHandle_ResetClaimedSlot
```

Confirmed behavior:

```text
if slotHandle[0] exists:
  slot = ResourceSlotManager_GetClaimedSlot(slotHandle[0], slotHandle[1])
  if slot exists:
    CzanMovieObj_Reset(slot)
```

`FUN_80115B80` applies a controller visual/stage preset selected by two indices and a
group. Suggested name:

```text
ActiveGameplayControllerBase_ApplyVisualPreset
```

Logical signature:

```c
void ActiveGameplayControllerBase_ApplyVisualPreset(int *controller, int presetA, int presetB, int presetGroup);
```

Confirmed behavior:

```text
normalized -1 preset indices become 0 for table lookup
controller +0x2A94 = PTR_DAT_802BEE60[presetGroup][presetB][presetA]
controller +0x2A98 = 0
controller +0x2A9C = 0.0f
default vectors/colors come from DAT_802BEA80[presetB][presetA]

if selected table entry has first id != -1:
  *(controller +0x60 +0x74) = id
  *(controller +0x60 +0x7C) = 1
  *(controller +0x60 +0x80) = id
  *(controller +0x60 +0x88) = 1

*(controller +0x54 +0x5C) = 0
write selected defaults into controller +0x2A70/+0x2A60/+0x267C ranges
controller +0x1424 = 4
if controller +0x2ACC != 0:
  UiEffectController_ResetOrStartFade(0.0f, *(controller +0x08))
```

`FUN_800FB698` resets or starts the UI/effect fade controller used above and by
`ActiveGameplayControllerBase_StartTimedTransition`. Suggested name:

```text
UiEffectController_ResetOrStartFade
```

When duration is positive, it starts a fade if one is not already active. When duration
is zero or negative, it clears active ids at `+0x44/+0x58/+0x5C/+0x60` and clears the
fade timer fields at `+0x64/+0x68`.

The two concrete fade/effect start variants are:

```text
FUN_800FB608 -> UiEffectController_StartMultiTargetFade
FUN_800FB64C -> UiEffectController_StartSingleTargetFade
FUN_800FD3B8 -> UiEffectController_GetState
```

Both return immediately unless the effect controller is enabled at `+0x40` and no
primary target is active at `+0x44`. The multi-target variant fills
`+0x44/+0x5C/+0x60`; the single-target variant fills `+0x44/+0x58` and sets `+0x50 = 1`.
Both reset `+0x64` and store the fade duration at `+0x68`.

`UiEffectController_GetState` simply returns `effectController +0x48`.

`FUN_80115CE8` commits the controller's selected visual/stage preset. Suggested name:

```text
ActiveGameplayControllerBase_CommitVisualPreset
```

Confirmed behavior:

```text
copy controller +0x74 into *(controller +0x60 +0x74), clear +0x7C
copy controller +0x78 into *(controller +0x60 +0x80), clear +0x88
controller +0x1424 = min(controller +0x70, 3)
*(controller +0x54 +0x5C) = controller +0x7C
CzanModelManager_RequestBank5ModeTransition(0.0f, *(controller +0x5C), controller +0x70, 0)

if controller +0x2ACC != 0 and controller +0x04 != 0:
  if FUN_800FD3B8() == 4:
    controller +0x2AD0 = 1
  else:
    UiEffectController_ResetOrStartFade(0.0f, *(controller +0x08))
    optionally starts/sets the UI effect path through FUN_800FB608/FUN_800FB64C
```

`FUN_8005386C` requests the bank-5/model-owner mode transition used above. Suggested
name:

```text
CzanModelManager_RequestBank5ModeTransition
```

It validates the requested mode against owner `+0x2C`, computes transition state
`+0xB0C4`, records requested mode/kind at `+0xB374/+0xB378`, clears old live/effect
objects through `CzanEffectManager_SetStopTime`, resets transition timers
`+0xB0BC/+0xB0C0`, then calls `CzanModelManager_SwitchBank5ForMode`.

`FUN_800534F0` stops and clears bank-5 live effects for the owner's current mode.
Suggested name:

```text
CzanModelManager_StopBank5ModeEffects
```

`FUN_8011E770` is a small duration helper for those mode transitions. It uses a Czan UI
object animation duration when the owner has a UI table at `+0x3324`.

`FUN_80175F58` returns a Czan UI object animation duration. Suggested name:

```text
CzanUiManager_GetObjectAnimationDuration
```

`FUN_80179178` sets an effect object's stop time. Suggested name:

```text
CzanEffectManager_SetStopTime
```

`FUN_8003CDC4` is the CGame setup/loading state machine run after `CGameFactorySetup`.
Suggested name:

```text
CGame_PrepareManagersAndResources
```

Confirmed key fields:

```text
cgame +0x008 -> next/active module state
cgame +0x00C -> setup substate
cgame +0x0B8 -> setup mode; -1 means use player-data path directly
cgame +0x3F0 -> subsystem pointer
cgame +0x3F4 -> subsystem pointer
cgame +0x3F8 -> subsystem pointer
cgame +0x3FC -> subsystem pointer
cgame +0x400 -> subsystem pointer
cgame +0x404 -> subsystem pointer
cgame +0x408 -> subsystem pointer
cgame +0x40C -> subsystem pointer
cgame +0x410 -> subsystem pointer
cgame +0x428 -> subsystem pointer
cgame +0x42C -> optional owned object/resource pointer
```

Confirmed substates:

```text
0:
  reset global managers and owned subsystems
  if cgame +0x0B8 == -1:
    copy player-data settings into cgame +0x0B8..+0x0D8
    configure subsystem at +0x3F0
    substate = 3
  else:
    BootResourceBundle_StartLoading(gBootTempManager)
    substate = 1

1:
  wait until FUN_8002202C(gBootTempManager) != 0
  BootResourceBundle_ApplyLoadedResources(gBootTempManager)
  substate = 2

2:
  result = FUN_8003C9A0(cgame)
  if result == -1:
    cgame +0x08 = 5
  else if result == 1:
    configure subsystem at +0x3F0
    substate = 3

3:
  wait for resource managers to become ready
  wire CGame subsystem outputs together
  substate = 8

8:
  wait for *(DAT_802E71F8 +0x0C) == 0
  substate = 0x0C

0x0C:
  cgame +0x08 = 2
```

This is the runtime path that calls `BootResourceBundle_ApplyLoadedResources`, which then
feeds `resourceBundle[7] +0x10` into `LargeResourceManager_ReloadFromLink`.

`FUN_8003EE48` is a sibling CGame setup/loading state machine that eventually calls
`CGame_LoadSceneResourceManagers`, but has different result states. Suggested name:

```text
CGame_PrepareSceneFromSelectedSetup
```

Confirmed behavior:

```text
0:
  reset global managers and the same owned CGame subsystems as FUN_8003CDC4
  if cgame +0x0B8 == -1:
    CGame_BuildSceneSetupFromPlayerData(cgame)
    FUN_80118724(cgame +0x3F0, cgame +0x0B8, 0)
    substate = 3
  else:
    substate = 2

2:
  result = CGame_UpdateSceneSetupSelection(cgame)
  if result == -1:
    cgame +0x08 = 1
  else if result == 1:
    readyFlag = (countLeadingZeros(*(cgame +0x0B8)) >> 5)
    FUN_80118724(cgame +0x3F0, cgame +0x0B8, readyFlag)
    substate = 3

3:
  wait until resource managers are ready
  CGame_LoadSceneResourceManagers(cgame)
  substate = 8

8:
  wait for *(DAT_802E71F8 +0x0C) == 0
  substate = 0x0C

0x0C:
  cgame +0x08 = 3
```

Compared with `CGame_PrepareManagersAndResources`, this path does not start the
boot-temp resource bundle in substate 0/1. It either prepares from direct setup data or
waits on `CGame_UpdateSceneSetupSelection`, then enters the same detailed scene wiring
function.

`FUN_8003D1F0` builds the scene/setup block directly from player/setup data when
`cgame +0x0B8` is `-1`. Suggested name:

```text
CGame_BuildSceneSetupFromPlayerData
```

`FUN_8003DC74` is the interactive/waiting scene setup path used when CGame must resolve
selection/setup state across frames. Suggested name:

```text
CGame_UpdateSceneSetupSelection
```

`FUN_8003F24C` is the direct active-gameplay setup builder used by
`CGame_PrepareActiveGameplayState` when `cgame +0x0B8` is `-1`. Suggested name:

```text
CGame_BuildActiveGameplaySetupFromPlayerData
```

`FUN_8003FEB0` is the interactive/options waiting path for active gameplay setup.
Suggested name:

```text
CGame_UpdateActiveGameplaySetupSelection
```

`FUN_8003D740` is the detailed CGame scene/resource-manager wiring function reached
after the loaded resource managers are ready. Suggested name:

```text
CGame_LoadSceneResourceManagers
```

Ghidra may decompile it as `void(void)` because it begins with the saved-register helper
`FUN_8012A164`; the recovered pointer is the active `CGame` object.

Confirmed behavior:

```text
cgame = recovered from FUN_8012A164()

resource 7 from cgame +0x3F0:
  feeds FUN_80025EA8(gLargeResourceManager, ...)

resources 0x12..0x16 from cgame +0x3F0:
  decoded through temporary link managers
  passed into cgame +0x3F4 setup

nonzero decoded model-position resources:
  packed into a list
  CzanModelPositionSet_LoadFromLinkList(cgame +0x408, list, count)
  cgame +0x408 +0x08 receives a handle from cgame +0x3F4

resources from the secondary list:
  passed into subsystem cgame +0x3F8

mode/flag-dependent resources:
  configure subsystem cgame +0x404
  load extra blocks through CzanModelOwner_SetCategoryAndLoadStageResourceGroup /
  FUN_80054CF4 / FUN_8004EBB8

if cgame +0x108 != 0:
  resource 9 -> CzanModelManager_LoadBank1Resource(cgame +0x404, resource)
  resources 0x0C/0x0D plus selector from cgame +0x410:
    CzanModelManager_SetupBank3AndStageObjects(cgame +0x404, selector, 0x41, 0x42, ...)

for i in 0..0x17:
  resource table entries from cgame +0x3F0 are copied into gLargeResourceManager +0x2FBA7C

resource 0x0B optionally configures cgame +0x40C
resource 0x10 configures cgame +0x428 slot 3
```

This is still not the draw function. It is the owner-side scene setup function that
loads/links Czan model resources, position markers, stage-object wrappers, UI/effect
resources, and large-resource table entries.

## Large Resource Manager

`FUN_80021E98` starts loading the boot/CGame resource bundle into `gBootTempManager`.
Suggested name:

```text
BootResourceBundle_StartLoading
```

Confirmed behavior:

```text
if resourceBundle[0] == 0:
  regionIndex = FUN_80143830(DAT_802E71B8)
  pathTable = PTR_s_/banner/banner_US.bin_802A4390 + regionIndex * 8

  for slot in 0..7:
    if resourceBundle[slot + 2] == 0:
      resourceBundle[slot + 2] =
        LoadResourceByPath(*(DAT_802E71B8 +0x260), pathTable[slot], 0)

  FUN_80023634(gManager_802E70A4)
  resourceBundle[0] = 1
```

This means `BootResourceBundle_ApplyLoadedResources` consumes handles loaded here:

```text
slot 0 -> resourceBundle[2] -> /banner/banner_*.bin, loaded but not consumed here
slot 1 -> resourceBundle[3] -> /mii/RFLRes01.arc
slot 2 -> resourceBundle[4] -> /text/text_*.bin
slot 3 -> resourceBundle[5] -> /font/font_*.bin
slot 4 -> resourceBundle[6] -> /select/select_cmn.bin
slot 5 -> resourceBundle[7] -> /ssq/SSQ_CMN*.bin -> LargeResourceManager_ReloadFromLink
slot 6 -> resourceBundle[8] -> /2Dcommon/comAF_*.bin -> UiRootManager_LoadResource
slot 7 -> resourceBundle[9] -> /Pointer/Pointer.bin
```

Known path variants at `0x802A4390`:

```text
region 0: banner_US, RFLRes01, text_eng, font_us, select_cmn, SSQ_CMN,    comAF_US, Pointer
region 1: banner_US, RFLRes01, text_eng, font_us, select_cmn, SSQ_CMN,    comAF_US, Pointer
region 2: banner_US, RFLRes01, text_eng, font_us, select_cmn, SSQ_CMN,    comAF_US, Pointer
region 3: banner_US, RFLRes01, text_fra, font_fr, select_cmn, SSQ_CMN_FR, comAF_FR, Pointer
region 4: banner_US, RFLRes01, text_spa, font_sp, select_cmn, SSQ_CMN_SP, comAF_SP, Pointer
region 5: banner_US, RFLRes01, text_eng, font_us, select_cmn, SSQ_CMN,    comAF_US, Pointer
```

`FUN_80021F58` applies a loaded boot/resource bundle into the global managers once.
Suggested name:

```text
BootResourceBundle_ApplyLoadedResources
```

Confirmed behavior:

```text
if resourceBundle[0] == 1:
  if resourceBundle[1] == 0:
    begin global manager setup lock/scope
    apply resource handle slots into global managers
    end global manager setup lock/scope
  resourceBundle[1] = 1
```

Confirmed resource handle slots:

```text
resourceBundle[3] +0x10 -> DAT_802E71F8 manager setup
resourceBundle[4] +0x10 -> gManager_802E70B0 setup
resourceBundle[5] +0x10 -> large sub-manager setup through FUN_800B72F8
resourceBundle[6] +0x10 -> gCharacterAssetManager setup
resourceBundle[7] +0x10 -> gLargeResourceManager via LargeResourceManager_ReloadFromLink
resourceBundle[8] +0x10 -> gUiRootManager via UiRootManager_LoadResource
resourceBundle[9] +0x10 -> gManager_802E70B4 setup
```

`FUN_80021D90` releases the same boot/CGame resource bundle. Suggested name:

```text
BootResourceBundle_Release
```

Confirmed behavior:

```text
if resourceBundle[1] == 1:
  FUN_8010E95C(gManager_802E70B4)
  FUN_80025DEC(gLargeResourceManager)
  FUN_800FE8C8(gUiRootManager)
  CharacterAssetManager_UnloadActiveAssets(gCharacterAssetManager)
  FUN_800B73F4()
  FUN_800C0FB8(gManager_802E70B0)
  FUN_801890E8(DAT_802E71F8)
  FUN_8009A7E0(gManager_802E70E0)
  resourceBundle[1] = 0

if resourceBundle[0] == 1:
  for slots 0..7:
    if resourceBundle[slot + 2] != 0:
      release resource handle through DAT_802E71B8 +0x260 manager
  FUN_80023794(gManager_802E70A4)
  resourceBundle[0] = 0

if releaseMode > 0:
  free resourceBundle
```

`FUN_800CC630` tears down active character assets. Suggested name:

```text
CharacterAssetManager_UnloadActiveAssets
```

Confirmed behavior:

```text
if characterAssetManager +0x28164 == 1:
  characterAssetManager +0x28164 = 0
  release/clear characterAssetManager +0x28168
  CzanModelManager_UnloadBank(gManager_802E70B8, 4)
  clear special/character part state at characterAssetManager +0x34
```

`GameMain` allocates `DAT_802E70BC` with size `0x3010B8`, constructor `0x80025A7C`.
Suggested provisional name:

```text
gLargeResourceManager
```

`FUN_80025CA0` reloads this manager from a link resource passed in `r4`. Suggested name:

```text
LargeResourceManager_ReloadFromLink
```

Confirmed behavior:

```text
CzanLinkManager_InitAndSetLink(stackLinkManager, linkData)

if manager[0] != 0:
  tear down existing manager state

block0 = CzanLinkManager_GetBlock(stackLinkManager, 0)
FUN_800B6484(manager + 0x2FBA7C, block0)
FUN_8002F1C8(manager + 0x10)

for bankIndex in 0..6:
  bank = manager + 0x91610 + bankIndex * 0x58534
  setupValue = bankIndex < 5 ? 0x5460 : 0
  FUN_800338BC(bank, setupValue)

manager[0] = 1
refresh manager state
CzanLinkManager_Release(stackLinkManager, -1)
```

The stack link manager is released with `-1`, so it does not free allocator memory.

The listing confirms `FUN_80025CA0` does not write `r4` before calling
`CzanLinkManager_InitAndSetLink`, so the function has a hidden second parameter:

```text
r3 = largeResourceManager
r4 = linkData
```

`FUN_8016032C` sets the CzanLinkManager vtable at `+0x10` to `PTR_PTR_802C0790`, then
calls `CzanLinkManager_SetLink(linkManager, linkData)`. Suggested name:

```text
CzanLinkManager_InitAndSetLink
```

The listing confirms `r3` is preserved as `linkManager` and incoming `r4` is passed
through as `linkData`.
