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
4 -> FUN_800430CC(cgame)
5 -> return cgame[0] as the next module/state
```

It also updates bits `0x40000000/0x80000000` in `cgame +0xB0` depending on
`*(DAT_802E71B8 +0x260 +0x34)`, and resets `cgame[3]` to zero whenever `cgame[2]`
changes.

The next likely per-frame/render targets are therefore:

```text
FUN_800418F8  -> state 3
FUN_800430CC  -> state 4
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
    FUN_8003F24C(cgame)
    FUN_80118E54(cgame +0x3F0, cgame +0x0B8)
    substate = 3
  else:
    substate = 2

2:
  result = FUN_8003FEB0(cgame)
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
controller +0x1428 -> first of several repeated subobjects initialized by FUN_80110D30
controller +0x2A80 -> cleared/default state and timing fields
controller +0x2AA0 -> trailing 0x2C-byte state cleared
controller +0x2ACC -> set to 1
```

The `PTR_PTR_802BEF00` vtable is now the next target. From
`CGame_CreateActiveGameplayController`, we already know:

```text
vtable +0x0C -> called during controller setup with the context struct
vtable +0x10 -> called during teardown/reset paths
vtable +0x14 -> called before controller destruction/reset
```

So the next functions to inspect are the function pointers at `PTR_PTR_802BEF00 +0x0C`,
`+0x10`, and `+0x14`.

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

This is setup and state initialization, not rendering. The next likely controller
lifecycle/render targets are the later `PTR_PTR_802BEF00` methods, especially
`+0x10`, `+0x14`, and then `+0x18` onward.

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
    FUN_8003D1F0(cgame)
    FUN_80118724(cgame +0x3F0, cgame +0x0B8, 0)
    substate = 3
  else:
    substate = 2

2:
  result = FUN_8003DC74(cgame)
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
waits on `FUN_8003DC74`, then enters the same detailed scene wiring function.

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
  load extra blocks through FUN_8004EB54 / FUN_80054CF4 / FUN_8004EBB8

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
