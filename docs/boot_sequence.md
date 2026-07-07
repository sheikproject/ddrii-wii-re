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

