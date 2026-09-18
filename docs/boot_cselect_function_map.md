# Boot / CSelect Function Map

Source export: `E:\PS2 Isos\Shrek 2 (Spain)\main_DDRII.dol.c`

This file is the working rename/status map for the boot logo, boot resource bundle,
`CSelect`, and `CSelMode` path. Use it before changing the executable path: if a
function is still marked partial or missing, the host should not pretend that path is
game-accurate.

## Status Legend

```text
implemented  recovered behavior exists in host code
partial      name and some fields/flow are known, but body is incomplete
missing      recovered/exported but not implemented in host code
host bridge  non-game helper used only because the real body is not ported yet
```

## Boot Module

| Address | Name | Host symbol | Status | Notes |
|---|---|---|---|---|
| `0x800212D4` | `BootLogoModule_Init` | `BootLogoModule_Init` | partial | Initializes module fields, but host struct is simplified. |
| `0x800212F4` | `BootLogoModule_Destroy` | none | missing | Needed for exact module replacement/teardown. |
| `0x80021360` | `BootLogoModule_OnEnter` | `BootLogoModule_OnEnter` | partial | Sets screen size, state `3`, language/region, render color. |
| `0x80021480` | `BootLogoModule_OnExit` | none | missing | Original exit method currently skipped. |
| `0x800214FC` | `BootLogoModule_Tick` | `BootLogoModule_Tick` | partial | State flow starts the boot bundle after logo texture creation and applies it before module `2`; timing/draw still simplified. |
| `0x80021700` | `BootLogoModule_Draw` | `BootLogoModule_Draw` | partial | Host draws via OpenGL backend, not the exact GX path. |

Important boot fields:

```text
+0x08 logoResourceHandle
+0x0C logoTextureHandle
+0x10 logoLanguageIndex
+0x14 width
+0x18 height
+0x20 state
+0x24 logoStepIndex
+0x28 logoFrameIndex
+0x2C frameAnimTimer
+0x30 stateTimer
+0x34 skipLogoOrProgressiveFlag
+0x38 fadeAlpha
```

Language note: resource-region rows and logo-language indexes are not the same enum.
For the Spanish data set, resource row `4` maps to `logo/logo_SPA.tpl`, not
`logo/logo_ITA.tpl`.

## Boot Resource Bundle

| Address | Name | Host symbol | Status | Notes |
|---|---|---|---|---|
| `0x800221BC` | `BootResourceBundle_Init` | `BootResourceBundle_Init` | partial | Initializes the 0x498-ish bundle object shape. |
| `0x800220C0` | `BootResourceBundle_Release` | `BootResourceBundle_Release` | partial | Releases/applies flags only; owned memory release is not exact. |
| `0x80022184` | `BootResourceBundle_StartLoading` | `BootResourceBundle_StartLoading` | partial | Queues eight boot resources by region. Verified host path now reaches `2Dcommon/comAF_SP.bin`. |
| `0x80022260` | `BootResourceBundle_ApplyLoadedResources` | `BootResourceBundle_ApplyLoadedResources` | partial | The real game applies all boot resources, including full `UiRootManager_LoadResource(comAF)`. The host currently registers comAF lazily to avoid drawing every hidden CAE until the UI panel state machine is ported. |

Boot resource slots:

```text
slot 0 -> banner/banner_*.bin
slot 1 -> mii/RFLRes01.arc
slot 2 -> text/text_*.bin
slot 3 -> font/font_*.bin
slot 4 -> select/select_cmn.bin
slot 5 -> ssq/SSQ_CMN*.bin
slot 6 -> 2Dcommon/comAF_*.bin
slot 7 -> Pointer/Pointer.bin
```

Missing consumers inside `BootResourceBundle_ApplyLoadedResources`:

| Export call | Suggested name | Status | Why it matters |
|---|---|---|---|
| `FUN_8009A6A8(gManager_802E70E0, banner)` | `BannerManager_LoadResource` | missing | Boot/banner state is not consumed. |
| `FUN_80188EFC(DAT_802E71F8, mii)` | `MiiResourceManager_LoadResource` | missing | Mii/system user resources are skipped. |
| `FUN_800C0D04(gManager_802E70B0, text)` | `TextManager_LoadResource` | partial/implemented | Installs the 17 localized text banks and supports bank select/text lookup through the recovered `0x800C0FBC`/`0x800C101C` behavior. |
| `FUN_800B72F8(font)` | `FontManager_LoadResource` | partial/implemented | Loads font block 0 as a texture through the global texture manager; full text surface renderer is still pending. |
| `FUN_800CC574(gCharacterAssetManager, select_cmn)` | `CSelectCommon_LoadResource` / `CharacterAssetManager_LoadSelectCommon` | implemented/partial | Boot route is wired; block 1 now enters `FUN_800F68FC`'s link-manager setup, while its later live-object/effect consumer is still pending. Block 2 enters model-manager bank 4. |
| `FUN_8010E770(gManager_802E70B4, pointer)` | `PointerManager_LoadResource` | missing | Pointer/cursor resources are not installed. |

Until these are real, boot can reach `CSelect` but the menu cannot match the game.

## Input Manager

| Address | Name | Host symbol | Status | Notes |
|---|---|---|---|---|
| `0x8002A33C` | `InputOrMenuStateManager_Update` | `InputOrMenuStateManager_Update` | partial | Host now writes the real controller-record masks from keyboard/XInput instead of the old select keyboard shim. Analog/repeat timing from `FUN_8002A8D4` is still pending. |
| `0x8002AE08` | `InputOrMenuStateManager_TestActiveMask` | `InputOrMenuStateManager_TestActiveMask` | implemented | Tests record `+0x04`; many CSelect/menu paths use this helper for active/repeat-style checks. |
| `0x8002AE68` | `InputOrMenuStateManager_TestActiveRepeatMask` | doc only | pending | Checks record `+0x04`, expands slot `4` over physical controllers `0..3`, then calls `ControllerManager_TestRepeatTimerForSource` across source selectors. |
| `0x8002B100` | `InputOrMenuStateManager_TestSourceHeldMask` | doc only | pending | Maps player-pair/source mode to a physical controller and tests held masks through `ControllerManager_ReadHeldMaskSource`. |
| `0x8002AE28` | `InputOrMenuStateManager_TestHeldMask` | `InputOrMenuStateManager_TestHeldMask` | implemented | Tests record `+0x08`; confirm/back helpers read this same field. |
| `0x8002AE48` | `InputOrMenuStateManager_TestTriggeredMask` | `InputOrMenuStateManager_TestTriggeredMask` | implemented | Tests record `+0x10`. |
| `0x8002ADE0` | `InputOrMenuStateManager_IsConfirmPressed` | `InputOrMenuStateManager_IsConfirmPressed` | implemented | Returns bit `0x800` from record `+0x08`. |
| `0x8002ADF4` | `InputOrMenuStateManager_IsBackPressed` | `InputOrMenuStateManager_IsBackPressed` | implemented | Returns bit `0x400` from record `+0x08`. |
| `0x8014A6BC` | `ControllerManager_ReadHeldMaskSource` | doc only | pending | Source-selector reader for held/current masks. Sources `0/1/2/3/4/5/6/7/9` map to controller offsets `+0x1118/+0x117C/manager+0x40/+0x11E0/+0x1244/+0x12A8/manager+0x1D0/manager+0x360/ORed`. |
| `0x8014A860` | `ControllerManager_ReadTriggeredMaskSource` | doc only | pending | Same source-selector layout as held masks, but reads current-frame words. |
| `0x8014AA04` | `ControllerManager_TestRepeatTimerForSource` | doc only | pending | Tests per-bit repeat timers against current scaled frame time. |
| `0x8014B7F4` | `ControllerManager_IsAuxSourceActive` | doc only | pending | Returns whether manager byte `+ controller*0x0C +0x1A` is nonzero when manager flag `0x400` is enabled. |
| `0x8014B824` | `ControllerManager_GetControllerType` | doc only | pending | Reads controller type byte at physical-controller object `+0x64`. |

Controller record stride is `0x20`. Boot/select uses logical controller slot `4`.
The host also mirrors the same input into slot `0` because several gameplay setup
checks probe slot `0` for start/confirm masks. Current mapped host bits:

```text
+0x04 active/repeat candidate mask
+0x08 held/current mask
+0x10 triggered/current-frame mask

0x0001 up
0x0002 down
0x0004 left
0x0008 right
0x0400 back/B
0x0800 confirm/A
```

Runtime verification, 2026-07-17:

```text
resource: LoadResourceByPath path=2Dcommon/comAF_SP.bin hostPath=input\DATA\2Dcommon\comAF_SP.bin loaded=1 size=10951744
resource: LoadResourceByPath path=select/select_bin_sp.bin hostPath=input\DATA\select\select_bin_sp.bin loaded=1 size=15476832
resource: LoadResourceByPath path=select/selTitle_SP.bin hostPath=input\DATA\select\selTitle_SP.bin loaded=1 size=3690112
CSelect boot: select_bin shared resources active, requested selTitle
CSelect boot: selTitle ready, entering title flow
CSelect selTitle: groups=...
CSelect: created selTitle active screen
...
resource: LoadResourceByPath path=movie/select/OP.thp hostPath=input\DATA\movie\select\OP.thp loaded=1 size=16082048
```

The host now starts this bundle from `BootLogoModule_Tick` state `5` after the logo
TPL becomes a texture, and applies it from the second logo fade-out path before
returning module `2`, matching the recovered call placement. `comAF` no longer floods
the screen through eager host group creation; it is registered by block size only.
That is a temporary host bridge. The real game calls full `UiRootManager_LoadResource`
here, then relies on the CGame UI selection-panel state machine to enable only the
right groups and animations.

Important correction: `select_bin_sp.bin` root block `0x0F` is not a boot warning
timeline in this data set. A 2026-07-18 resource dump identifies it as the boss music
folder UI (`boss_bg`, `boss_brd`, `boss_circle01`, ...). The host must not draw this
block during boot. The visible caution/notice/title groups live in `selTitle_SP.bin`
blocks `0..5` and are coordinated by the `FUN_800E140C` title flow.

## CSelect Module

| Address | Name | Host symbol | Status | Notes |
|---|---|---|---|---|
| `0x800450AC` | `CSelect_Init` | `CSelect_Init` | partial | Uses real `0x1C18` object size and key offsets. |
| `0x80045244` | `CSelect_Destroy` | none | missing | Needed for exact module teardown. |
| `0x800452B0` | `CSelect_OnEnter` | `CSelect_OnEnter` | partial | Loads entry resources and initial state flags; boot bundle normally arrives from `BootLogoModule_Tick`. |
| `0x80045580` | `CSelect_OnExit` | none | missing | Releases active screen/resources/UI state. |
| `0x800457C8` | `CSelect_Tick` | `CSelect_Tick` | partial | Resource gates and a partial active-screen switch are present. Unsupported states now report missing constructors instead of drawing the old debug placeholder. |
| `0x800470E0` | `FUN_800470E0` | `CSelect_ActivateSelectBinBridge` | partial/implemented | Uses the root `select_bin` WII resource for shared texture handles, then requests `selTitle`. The nested `select_bin +0xA0` WII is only used later by the state-1 CSelMode constructor. Do not draw root block `0x0F`; it is boss-folder UI in the Spanish data set. |
| `0x80129414` | `FUN_80129414` | `CSelectWarningTimeline_InitFromBlock` | documented/unused host path | Warning/timeline helper recovered from the game, but the host must not feed it `select_bin_sp.bin` block `0x0F`. |
| `0x801294E0` | `FUN_801294E0` | `CSelectWarningTimeline_Start` | documented/unused host path | Starts the recovered warning/timeline group when the correct source object is known. |
| `0x8012953C` | `FUN_8012953C` | `CSelectWarningTimeline_Finalize` | documented/unused host path | Calls the `FUN_80174EB8` equivalent with `(suppressDraw=1, drawState=1)`. |
| `0x80174EB8` | `FUN_80174EB8` | `CzanUiManager_SetObjectGroupDisplayFlags` | implemented | Writes child object `+0x172` and optionally `+0x173`; used by the warning timeline finalizer and the UI selection-panel reset path. |
| `0x80175E00` | `FUN_80175E00` | `CzanUiManager_IsObjectGroupAnimationDone` | implemented | Checks child animation done byte `+0xB1` or `+0xB2` depending on reset mode `+0x174`; it does not advance CAE scripts. Scripts advance from object draw/update paths. |

`selTitle` draw note: the host title flow now follows `FUN_800E25B0` by calling
`UiRootManager_DrawGlobalCzanListOnce(gUiRootManager, 1)`. Any loaded
`comAF`/select-bin groups that still appear here are now object-state bugs
(`+0x172/+0x173`, draw-list flags, presentation masks), not a manual title draw
filter.

Core CSelect fields:

```text
+0x00 previousModuleId
+0x04 vtable -> 0x802B95F0
+0x08 selectMusicLoadedFlag
+0x0C selectMusicLoadPending
+0x10 selectBinParsedFlag
+0x14 selectBinLoadPending
+0x18 selectTitleLoadedFlag
+0x1C selectTitleLoadPending
+0x20 selectResultLoadedFlag
+0x24 selectResultLoadPending
+0x28 commonSelectLoadedFlag
+0x2C commonSelectLoadPending
+0x38 currentSelectState
+0x3C nextSelectState
+0x40 previousSelectState
+0x44 texture handle array start, 0x14 entries initialized to -1
+0x98 previous frame state used by tick
+0x9C cue/fade busy flag
+0x0C4 player/menu setup buffer A, size 0x910
+0x9D4 player/menu setup buffer B, size 0x121C
+0x1BF0 activeSelectScreen
+0x1BF4 sharedFlowObject
+0x1BF8 shared animation/timeline object
+0x1BFC selectBinResource
+0x1C00 selMusicResource
+0x1C04 selTitleResource
+0x1C08 selResultResource
+0x1C0C cmnAccMdlResource
+0x1C10 musicPreviewResource
+0x1C14 extraMusicPreviewResource
```

`CSelect_OnEnter` resource handles:

```text
cSelect +0x1C00 <- select/selMusic_*.bin
cSelect +0x1C04 <- select/selTitle_*.bin
cSelect +0x1C08 <- select/selResult_*.bin
cSelect +0x1C0C <- select/cmnAccMdl.bin
```

Critical missing setup helpers called by `CSelect_OnEnter`:

| Address | Current export | Suggested name | Status | Notes |
|---|---|---|---|---|
| `0x8004749C` | `FUN_8004749c` | `CSelect_ResetFromPlayerData` | partial/missing | Populates `+0xC4` and menu/player setup. Current host only clears part of it. |
| `0x8004BC40` | `FUN_8004bc40` | `CSelect_StartFreshTitleSetup` | missing | Resets menu records and fills player setup defaults. Current host only sets one pending flag. |
| `0x80100470` | `FUN_80100470` | `UiRootManager_ResetMenuPresentationBankFlags` | partial/implemented | Forwards to `FUN_80106B48(*(uiRoot +0x38), bankIndex)`; called on CSelect enter/exit. |
| `0x80100498` | `FUN_80100498` | `UiRootManager_ResetDdrPointDisplay` | doc only | Forwards to `FUN_8010A288(*(uiRoot +0x40))`; called on CSelect enter/exit. |
| `0x800CCB34` | `FUN_800ccb34` | `CharacterAssetManager_ResetSelectCommonState` | implemented | Called before select state preload; resets root manager fields at `+0x10/+0x14/+0x18/+0x1C/+0x20`. |
| `0x80023884` | `FUN_80023884` | `GlobalCueManager_PrepareSelectStatePreloads` | missing | Called with module ID during CSelect enter. |
| `0x8002355C` | `FUN_8002355c` | `GlobalCueManager_AddPreloadCue` | missing | Called for each state preload table entry. |
| `0x80098D54` | `FUN_80098d54` | `CSelectCommon_OnEnterNoop` | exported no-op | The extracted body is empty in this DOL. |

## CSelect Tick Resource Gates

These gates are exact in the export and should stay exact:

```text
if (+0x2C == 1 && ResourceProgress(+0x260) == 1):
    apply cmnAccMdl +0x1C0C, clear +0x2C, set +0x28

if (+0x0C == 1 && ResourceProgress(+0x260) == 1 && +0x08 == 0):
    clear +0x0C, set +0x08
    if +0x10 == 0:
        load select/select_bin_*.bin -> +0x1BFC
        set +0x14 = 1

if (+0x14 == 1 && ResourceProgress(+0x260) == 1):
    CSelect_LoadSelectBinSharedResources(+0x1BFC)

if (+0x1C == 1 && ResourceProgress(+0x260) == 1 && +0x18 == 0):
    clear +0x1C, set +0x18
    if +0x10 == 0:
        load select/select_bin_*.bin -> +0x1BFC
        set +0x14 = 1

if (+0x24 == 1 && ResourceProgress(+0x260) == 1 && +0x20 == 0):
    clear +0x24, set +0x20
    if +0x08 == 0:
        load selMusic_*.bin -> +0x1C00
        set +0x0C = 1
```

## CSelect Select-Bin Shared Resource Loader

| Address | Current export | Suggested name | Status | Notes |
|---|---|---|---|---|
| `0x800470E0` | `FUN_800470e0` | `CSelect_ActivateSelectBinBridge` | partial/implemented | Parses the root `select_bin` WII pointer from the loaded resource, creates shared texture handles, sets `+0x10 = 1`, and requests `selTitle` when needed. |

`FUN_800470E0` is not the actual mode-select screen creation. It only prepares shared
resources and sets `+0x10 = 1`. The actual active screen is created later by the
`nextSelectState` switch below.

The recovered order is important:

```text
CSelect_Tick resource gate:
  load select/select_bin_*.bin -> +0x1BFC

FUN_800470E0 / CSelect_LoadSelectBinSharedResources:
  parse the root select_bin WII pointer from the loaded resource handle
  create shared texture handles at +0x44...
  leave +0x1BF8 empty during boot; root block 0x0F is boss-folder UI in select_bin_sp.bin
  set +0x10 = 1
  if +0x18 == 0:
    LoadResourceByPath(select/selTitle_*.bin) -> +0x1C04
    set +0x1C = 1

selTitle resource gate:
  clear +0x1C
  set +0x18 = 1
  active-screen switch creates state 0x0C from selTitle +0x10

FUN_800E140C:
  create selTitle groups 0,1,2
  because titleFlow +0x0C defaults to 0, set state +0x144 = 3
  start animation 0 on group at +0x20

FUN_800E1988:
  state 3 waits for group +0x20 animation done
  state 4 waits FLOAT_802E8DE8
  state 5 starts group +0x20 animation 1 and hides it when done
  state 0x0C waits UI root transition groups +0x0C and +0x10
  state 10 starts movie/select/OP.thp through FUN_8010A808
```

The previous host bridge loaded `movie/select/OP.thp` before `selTitle`. That was not
from the executable. The host now follows the executable order above: OP is owned by
the `FUN_800E1988` title-flow state machine, not by the shared `select_bin` loader.

## CSelect Active Screen Creation Switch

This switch runs only when:

```text
cSelect +0x1BF0 == 0
state is not resource-blocked
UiRoot/character/common-select gating allows a new screen
```

| `nextSelectState` | Size | Constructor | Link source/block | Suggested screen name | Status |
|---:|---:|---|---|---|---|
| `0x00` | `0x138` | `CSelectBootTitleFlow_Init` / `CSelectBootTitleFlow_Tick` | none | title/save/boot flow screen | partial |
| `0x01` | `0x5C0` | `CSelMode_Init` | nested `select_bin +0xA0`, block `0` | mode select | partial |
| `0x02` | `0x350` | `CSelectPlayerCountFlow_Init` | `select_bin`, block `1` | player-count/options screen after mode select | partial |
| `0x03` | `0x3E8` | `FUN_80096A00` | `select_bin`, block `7` | unknown/common select screen | missing |
| `0x04` | `0x181C` | `FUN_800656CC` | `select_bin`, block `2` | character select | missing |
| `0x05` | `0x22EC` | `FUN_8006F6D0` | `selMusic`, block `0` | music select | missing |
| `0x06` | `0x854` | `FUN_80062FCC` | `select_bin`, block `3` | unknown select screen | missing |
| `0x07` | `0x9FC` | `FUN_8008D690` | `select_bin`, block `4` | unknown select screen | missing |
| `0x08` | `0xB578` | `FUN_800A22E4` | `select_bin`, block `10` | large result/title flow | missing |
| `0x09` | `0x198` | `FUN_800BA52C` | `select_bin`, block `6` | unknown select screen | missing |
| `0x0A` | `0x2020` plus `0xCCC` shared | `FUN_800C6984` + `FUN_800BCD34` | `select_bin`, blocks `5` and `9` | shared flow + screen | missing |
| `0x0B` | n/a | debug report only | none | request flow marker | missing |
| `0x0C` | `0x264` | `FUN_800E12D0` | `selTitle +0x10` | title request flow | partial |
| `0x0D` | n/a | debug report only | none | world flow marker | missing |
| `0x0E` | `0x1F50` | `FUN_800E262C` | `selResult +0x10` | result/world flow | missing |
| `0x0F` | `0xB578` | `FUN_800A22E4` | `select_bin`, block `10` | same as state 8 | missing |
| `0x10` | `0x564` | `FUN_800F42F4` | `select_bin`, block `8` | music preview/SSQ flow | missing |
| `0x11` | `0x160` | `FUN_800F8EC0` | `select_bin`, block `0x0C` | DDR points/display flow | missing |
| `0x12` | `0x3B4` | `FUN_800FDDBC` | `select_bin`, block `9` | unknown flow | missing |
| `0x13` | `0x180` | `FUN_80107050` | `select_bin`, block `4` | unknown flow | missing |
| `0x14` | `0x150` | `FUN_8010AA78` | `select_bin`, block `0x0C` | unknown flow | missing |
| `0x15` | `0x41C` | `FUN_80102FB4` | `select_bin`, block `0x0B` | unknown flow | missing |
| `0x16` | `0x3E8` | `FUN_80096A00` | `select_bin`, block `7` | same family as state 3 | missing |
| `0x17` | `0x3E8` | `FUN_80096A00` | `select_bin`, block `7` | same family as state 3 | missing |
| `0x18` | `0x3E8` | `FUN_80096A00` | `select_bin`, block `7` | same family as state 3 | missing |
| `0x19` | `0x148` | `FUN_8010B2D0` | `select_bin`, block `0x0C` | unknown flow | missing |
| `0x1A` | `0x138` | `CSelectSavePromptFlow_Init` | none | save prompt/title branch flow | missing |

This is the main reason the current host does not load like the game. Calling
`CSelMode_OnEnter` directly from `CSelect_LoadSelectBinSharedResources` was a host
bridge. The host runtime now routes state `1` through a named active-screen creation
helper first, then invokes the partial `CSelMode` enter path as the temporary vtable
stand-in. State `0` now has a host `CSelectBootTitleFlow` object matching the
recovered constructor fields at `+0x130/+0x134` and a partial `FUN_800DE0DC` tick
path. It is still not game-accurate because several UI-root save prompt helpers and
character/save-manager gates are only mapped to existing host equivalents.

State `0` now routes through the recovered title/save bootstrap state machine
instead of immediately requesting `selTitle`. The host now constructs the
`uiRoot +0x30` boot-transition controller from `comAF_*.bin` block `4`, drives it
through the `0x80100280/90/A0/A8/C0/C8/D4` wrapper functions, and only requests
`selTitle_*.bin` after `CSelectBootTitleFlow_Tick` returns state `0x0C`.
The block-4 loader now also allocates the real eight-helper family of `0xCC`
prompt/effect helper records (`0x800FD46C`, bound through `0x800FD630`,
detached through `0x800FD80C`) at `subManager +0x7C/+0x80/+0x84..+0x98`.
The host now models `0x80175CC4` as `CzanUiManager_SetChildObjectExtensionPointer`,
so bind/detach updates the child-object extension slot used by the retail callback
path instead of leaving those children as unrelated raw sprites.
`UiRootBootTransition_AttachPromptHelpersForStart` now ports the table-independent
reattach side of `0x801023D8`: it invokes the recovered `0x800FD7AC` behavior for
the active primary helper and the six option helpers when the transition starts.
The `DAT_8027BA50` mode table that picks the exact primary helper/child is still
unrecovered in the host, so this keeps the construction-time child bindings.
Prompt configuration now writes the active helper's effect slot/text index via
the `0x800FDC24` / `0x800FDBA0` behavior. The helper draw/update path
(`0x800FD908`) is still not ported, so this fixes construction/order state first,
not the final rendered prompt surface.

State `0x0C` now has a host `CSelectTitleFlow` object matching `FUN_800E12D0`
constructor fields, `FUN_800E140C` group creation for `selTitle` blocks 0..2,
and the `FUN_800E25B0` style Czan UI object-list draw path. `selTitle_*.bin`
is passed from the start of the loaded WII container, while `select_bin_*.bin`
still uses the nested `+0xA0` WII payload.

The host now includes the first executable-backed `FUN_800E1988` title states:

```text
3    wait CzanUiManager_IsObjectGroupAnimationDone(group +0x20)
4    wait before closing the notice group
5    start animation 1 on group +0x20, then hide it when done
0x0C wait UI-root transition idle, then start OP.thp
10   OP movie state: rebinds `gManager_802E70A8` through `FUN_8010A808`-style movie-slot setup
0x0B OP movie playback-start/skip poll through the movie slot
6    wait title transition A idle; start title-call entry animations at +0x16C/+0x20C
0x0D title-call active state begins; entry layers draw instead of the old group +0x20
```

Still incomplete:

```text
CSelModeEntry blocks 3..5 from selTitle are not constructed yet.
FUN_80100550 / FUN_80100618 / FUN_801006F0 / FUN_80100798 UI-root title
transition helpers are still partial.
FUN_8010A808 movie object setup is represented by `ResourceSlotHandle_Rebind`
on `gManager_802E70A8`, with the host `CzanMovieObj_LoadResource` resolving the
mapped THP path and marking the synchronously loaded slot ready. THP video now
decodes enough to display frames, but audio pacing, orientation, and full Wii
movie-object scheduling still need work.
FLOAT_802E8DE8 is still an unresolved data constant; the host currently uses the
parsed Czan animation duration as the best executable-backed delay source, clamped
to a 120-frame fallback when the current partial CAE parser reports zero.
```

## CSelMode

| Address | Name | Host symbol | Status | Notes |
|---|---|---|---|---|
| `0x8006E2B8` | `CSelMode_Init` | `CSelMode_Init` | partial | Clears the host 0x5C0 object; the real 14-entry list controller is still incomplete. |
| `0x8006E314` | `CSelMode_Destroy` | none | missing | Needed for screen teardown. |
| `0x8006E3B4` | `CSelMode_OnEnter` | `CSelMode_OnEnter` | partial | Runtime calls it from the active-screen switch for state `1`; host now follows the real 14-entry handle topology and cloned mode-button groups, but exact region layout/update/state is still incomplete. |
| `0x8006E9B0` | `CSelMode_OnExit` | none | missing | Needed for exact transition out. |
| `0x8006EAC8` | `CSelMode_Update` | `CSelMode_Update` | partial | Host now reads the real input manager, advances enter/input/leave/commit states, writes the selected mode into CSelect through the host pointer table, and returns the recovered choice-table next state. Exact cues and every animation side effect are still incomplete. |
| `0x8006F2A0` | `CSelMode_SetInitialSelectedMode` | `CSelMode_SetInitialSelectedMode` | partial | Stubbed in host. |
| `0x8006309C` | `CSelModeEntry_Init` | `CSelModeEntry_Init` | partial | Initializes the host entry object and handle array; still missing the exact shared base state. |
| `0x800630D8` | `CSelModeEntry_Update` | `CSelModeEntry_Update` | partial | Host object animation handling is incomplete. |

## CSelect State 2 Player-Count Flow

| Address | Name | Host symbol | Status | Notes |
|---|---|---|---|---|
| `0x8008C11C` | `FUN_8008C11C` | `CSelectPlayerCountFlow_Init` | partial | Constructs the state `2` object, initializes the six `CSelModeEntry` records at `+0x150`, and binds CSelect/common manager pointers. |
| `0x8008C254` | `FUN_8008C254` | `CSelectPlayerCountFlow_OnEnter` | partial | Consumes `select_bin` block `1`, creates the header entry, clones the five option entries, links them to header children `5..9`, and initializes max/selected player count. |
| `0x8008C554` | `FUN_8008C554` | `CSelectPlayerCountFlow_OnExit` | missing | Teardown/effect cleanup not ported yet. |
| `0x8008C5E4` | `FUN_8008C5E4` | `CSelectPlayerCountFlow_Update` | partial | Advances enter/input/leave/commit states and writes player count to CSelect. Prompt branches and exact cues/effects are still incomplete. |
| `0x8008CF54` | `FUN_8008CF54` | `CSelectScreenBase_Init` | partial | Shared CSelect screen base initializer used before the state-2 constructor. |
| `0x8008D034` | `FUN_8008D034` | `CSelectScreenBase_Destroy` | missing | Shared CSelect screen base destructor. |

## UI Root / Transition Helpers Seen In CSelect

| Address | Export | Suggested name | Status |
|---|---|---|---|
| `0x800FE548` | `FUN_800FE548` | `UiRootManager_LoadResource` | partial, active |
| host only | none | `UiRootManager_RegisterResource` | host bridge |
| `0x800FEB58` | `FUN_800FEB58` | `UiRootManager_DrawFrame` | partial |
| host only | none | `UiRootManager_DrawBootCzanGroups` | host bridge |
| `0x80100280` | `FUN_80100280` | `UiRootManager_StartBootTransitionController` | partial/implemented |
| `0x80100290` | `FUN_80100290` | `UiRootManager_ConfigureBootTransitionPrompt` | partial/implemented |
| `0x801002A0` | `FUN_801002A0` | `UiRootManager_CloseBootTransitionController` | partial/implemented |
| `0x801002A8` | `FUN_801002A8` | `UiRootManager_IsBootTransitionControllerIdle` | implemented |
| `0x801002C0` | `FUN_801002C0` | `UiRootManager_IsBootTransitionPromptReady` | partial/implemented |
| `0x801002C8` | `FUN_801002C8` | `UiRootManager_GetBootTransitionResult` | partial/implemented |
| `0x801002D4` | `FUN_801002D4` | `UiRootManager_SetBootTransitionSelectedOption` | partial/implemented |
| `0x80100450` | `FUN_80100450` | `UiRootManager_SetMenuPresentationMode` | partial/implemented |
| `0x80100458` | `FUN_80100458` | `UiRootManager_SetAlternateMenuPresentationMode` | implemented |
| `0x80100460` | `FUN_80100460` | `UiRootManager_ResetMenuPresentationGrid` | partial/implemented |
| `0x80100468` | `FUN_80100468` | `UiRootManager_SetMenuPresentationBankValue` | implemented |
| `0x80100470` | `FUN_80100470` | `UiRootManager_ResetMenuPresentationBankFlags` | implemented |
| `0x80100478` | `FUN_80100478` | `UiRootManager_StartModalPrompt` | doc only |
| `0x80100480` | `FUN_80100480` | `UiRootManager_IsModalPromptIdle` | doc only |
| `0x80100488` | `FUN_80100488` | `UiRootManager_StartDdrPointDisplay` | doc only |
| `0x80100490` | `FUN_80100490` | `UiRootManager_UpdateDdrPointDisplay` | doc only |
| `0x80100498` | `FUN_80100498` | `UiRootManager_ResetDdrPointDisplay` | doc only |
| `0x801004A0` | `FUN_801004A0` | `UiRootManager_SetDdrPointDisplayValue` | doc only |
| `0x801004A8` | `FUN_801004A8` | `UiRootManager_ClearDdrPointDisplay` | doc only |
| `0x801004B0` | `FUN_801004b0` | `UiRootManager_UpdateBeforeDraw` | partial |
| `0x80100500` | `FUN_80100500` | `UiRootManager_IsSelectionPanelBusyA` | missing |
| `0x80100518` | `FUN_80100518` | `UiRootManager_IsSelectionPanelIdle` | implemented as `UiRootManager_IsSelectionPanelIdle` |
| `0x80100520` | `FUN_80100520` | `UiRootManager_IsSelectionPanelIdleFlag` | partial/doc only |
| `0x80100528` | `FUN_80100528` | `UiRootManager_IsModalBusyA` | missing |
| `0x80100530` | `FUN_80100530` | `UiRootManager_IsModalBusyB` | missing |
| `0x80100538` | `FUN_80100538` | `UiRootManager_GetSelectionPanelAnimationDuration` | implemented |
| `0x80100550` | `FUN_80100550` | `UiRootManager_StartTitleTransitionA` | partial/implemented |
| `0x80100618` | `FUN_80100618` | `UiRootManager_StartTitleTransitionB` | partial/implemented |
| `0x801007B8` | `FUN_801007B8` | `UiRootManager_SetSubManager44Value` | implemented |
| `0x801007C0` | `FUN_801007C0` | `UiRootManager_GetMovieOrTransitionFlag` | missing |

Current UI-root implementation status:

```text
implemented enough to run:
  full UiRootManager_LoadResource can create top-level comAF groups when explicitly used
  boot path currently calls UiRootManager_RegisterResource instead of eager full comAF load
  block 4 calls UiRootSubManager_LoadCzanGroups
  block 5 calls UiRootSubManager_LoadCzanGroupsWithTexture
  block 6 calls UiRootSubManager_LoadLinkedObjectGroup
  blocks 9..13 create top-level TPL texture slots
  UiRootManager_CreateReferenceObjectGroup clones uiRootManager[1] through CzanUiManager_CloneObjectGroup
  UiRootManager_CreateReferenceObjectGroup now suppresses the referenced source child, matching FUN_801002FC
  CzanUiManager_LinkObjectGroupToReferenceObject stores a live reference identity; host draw now resolves it per frame
  CzanUiManager_GetObjectAnimationDuration reads the parsed CAE animation duration

still missing / likely next cause of white or incomplete screen:
  UiRootManager_UpdateBeforeDraw / FUN_801004B0
  CGameUiSelectionPanel_Update / FUN_8010476C
  UiRootSubManager_InitTextureFrameGroups / FUN_80106054
  uiRootManager[0x10] loader / FUN_8010A058
  uiRootManager[0x12] loader / FUN_801209A8
  remaining reference-group color/placement edge cases inside FUN_800FEC3C beyond live linked-transform resolution
```

## CGame UI Selection Panel

This is the missing runtime logic that decides which comAF/select-bin CAE groups are
visible, which animation frame is active, and when the boot warning/prompt panels
advance. Without it, the host either shows a white/blank screen or every parsed CAE
asset at once.

| Address | Export | Suggested name | Status | Notes |
|---|---|---|---|---|
| `0x8010476C` | `FUN_8010476C` | `CGameUiSelectionPanel_Update` | partial/implemented | Main state machine for states `1..9`; advances prompts, waits on group animation completion, resets groups through the `FUN_80174EB8` equivalent, and preserves the `FUN_80106850` presentation-grid hook. Host input still uses the current null input-manager shim. |
| `0x8010502C` | `FUN_8010502C` | `CGameUiSelectionPanel_SelectDefaultTransition` | implemented | Starts primary group animation `panel[0x20]`, cue `0x25A`. |
| `0x801050BC` | `FUN_801050BC` | `CGameUiSelectionPanel_SelectShortTransition` | implemented | Starts primary group animation `panel[0x20] + 2`, cue `0x261`. |
| `0x80105150` | `FUN_80105150` | `CGameUiSelectionPanel_SelectImmediateTransition` | implemented | Starts alternate group animation `0`, sets `panel[8] = 1`, cue `0x274`. |
| `0x801051D4` | `FUN_801051D4` | `CGameUiSelectionPanel_SelectTertiaryPrompt` | implemented | Enables group `panel[3]`, assigns texture frames for children `0..3`, cue `0x25E`. |
| `0x801052F0` | `FUN_801052F0` | `CGameUiSelectionPanel_IsState5` | implemented | Returns true when state is `5`. |
| `0x80105304` | `FUN_80105304` | `CGameUiSelectionPanel_IsState8` | implemented | Returns true when state is `8`. |
| `0x80105318` | `FUN_80105318` | `CGameUiSelectionPanel_AdvanceState` | implemented | Advances `5->6`, `2->3`, `8->9`. |
| `0x801054A4` | `FUN_801054A4` | `CGameUiSelectionPanel_IsState6` | implemented | Returns true when state is `6`. |
| `0x801054B8` | `FUN_801054B8` | `CGameUiSelectionPanel_IsIdle` | implemented | Returns true when state is `0`. |
| `0x801054C8` | `FUN_801054C8` | `CGameUiSelectionPanel_GetFlag18` | implemented | Reads flag at `+0x18`. |
| `0x801054D0` | `FUN_801054D0` | `CGameUiSelectionPanel_GetFlag1C` | implemented | Reads flag at `+0x1C`. |
| `0x801054D8` | `FUN_801054D8` | `CGameUiSelectionPanel_GetAnimationDuration` | implemented | Chooses group `panel[1]` when `panel[8] == 1`, otherwise `panel[0]`; divides CAE duration by `60.0`. |
| `0x8010553C` | `FUN_8010553C` | `CGameUiSelectionPanel_SetManualAdvanceFlag` | implemented | Sets `panel[9] = 1`. |
| `0x800FEA60` | `FUN_800FEA60` | `UiRootManager_UpdateRuntimeBeforeDraw` | partial/implemented | Runtime update pass before draw; host now calls the recovered `uiRoot[0x0E]` presentation-bank update instead of resetting the grid every frame. Full submanager update order remains pending. |
| `0x80105DE4` | `FUN_80105DE4` | `UiRootSubManager_InitTextureFrameGroupState` | doc only | Constructor/reset for the 0x7C8 presentation object: clears masks, seeds per-entry offset records, and enables default-visible Czan groups. |
| `0x80106054` | `FUN_80106054` | `UiRootSubManager_InitTextureFrameGroups` | implemented | Creates two 0x28-entry reference-group banks, hides bank 1, and assigns texture frames 0..0x27. Host bridge starts both banks suppressed and forces the first mask pass so flat cached clones do not draw raw before the recovered UI state enables them. |
| `0x801061B4` | `FUN_801061B4` | `UiRootSubManager_UpdateTextureFrameGroupMotion` | partial/implemented | Consumes bank masks, detects changes, and applies Czan object-group offsets. The DOL interpolation constants are still symbolic in the export. |
| `0x801064F8` | `FUN_801064F8` | `UiRootSubManager_SeedTextureFrameGroupMotion` | doc only | Seeds per-entry target offset records when the two-word bank mask changes. |
| `0x80106724` | `FUN_80106724` | `UiRootSubManager_ApplyTextureFrameGroupMotion` | partial/implemented | Applies active per-entry offsets through `CzanUiManager_ApplyObjectGroupPositionLayout`. |
| `0x80106850` | `FUN_80106850` | `UiRootSubManager_SetMenuPresentationMode` | partial/implemented | Writes bank mask words at `+0xA8/+0xAC`; the runtime update pass now consumes them later like the executable. |
| `0x801068E4` | `FUN_801068E4` | `UiRootSubManager_SetAlternateMenuPresentationMode` | implemented | On first use, switches the first 14 bank-0 groups to texture frames `0x28..0x35`, then writes the first bank's target mask words. |
| `0x80106964` | `FUN_80106964` | `UiRootSubManager_ResetMenuPresentationGrid` | partial/implemented | Resets texture frames and applies the current two-bank visibility masks. Full position-seeding remains tied to `0x801061B4/0x801064F8/0x80106724`. |
| `0x80106ADC` | `FUN_80106ADC` | `UiRootSubManager_SetMenuPresentationBankValue` | implemented | Writes a per-child value through `FUN_80175744` for all 0x28 groups in the selected bank. |
| `0x80106B48` | `FUN_80106B48` | `UiRootSubManager_ResetMenuPresentationBankFlags` | implemented | Clears a per-child flag/value through `FUN_801757E4` for all 0x28 groups in the selected bank. |
| `0x80106CE4` | `FUN_80106CE4` | `UiRootSubManager_UpdateLinkedObjectPrompt` | doc only | Drives the linked prompt object group at uiRoot `+0x3C`, including confirm handling and cue cleanup. |

## Select Common / Character Asset Helpers

| Address | Export | Suggested name | Status |
|---|---|---|---|
| `0x80058EA8` | `FUN_80058EA8` | `CtsStageObj_LoadModelBlocks` | implemented/partial |
| `0x80059058` | `FUN_80059058` | `CtsStageObj_LoadPrimarySecondaryBlocks` | implemented |
| `0x80059158` | `FUN_80059158` | `CtsStageObj_LoadContinuationBlock` | implemented/partial |
| `0x800591BC` | `FUN_800591BC` | `CtsStageObj_StartAnimation` | implemented/partial |
| `0x8004E140` | `FUN_8004E140` | `CtsStageObj_UpdateAnimationFrame` | implemented/partial |
| `0x8005923C` | `FUN_8005923C` | `CtsStageObj_UpdateAnimationChannel` | implemented/partial |

| Address | Export | Suggested name | Status |
|---|---|---|---|
| `0x800982A8` | `FUN_800982A8` | `CSelectCommon_LoadResource` | implemented/partial | Host now calls `CSelectCommon_Init`, loads the two `CtsStageObj` layers, loads the owner model/ZAB blocks, sets owner animation speed to `0.0`, allocates the `+0x234` texture surface as `screenWidth/2 x screenHeight/4` format `5`, initializes `+0x230`, and binds the three `CSelModeEntry` records. |
| `0x8009812C` | `FUN_8009812C` | `CSelectCommon_Init` | implemented/partial | Initializes the two `CtsStageObj` layers, `CzanModelOwner`, the `CzanTextureSurface` at `+0x234`, three `CSelModeEntry` records, identity matrix at `+0x18`, and state fields `+0x344/+0x348/+0x36C/+0x370`. |
| `0x800981DC` | `FUN_800981DC` | `CSelectCommon_Destroy` | named |
| `0x800986F8` | `FUN_800986F8` | `CSelectCommon_ResetLoadedEffects` | named |
| `0x80098810` | renamed | `CSelectCommon_UpdateMovieBackground` | stub/partial |
| `0x80098BF0` | `FUN_80098bf0` | `CSelectCommon_DrawEffects` | implemented/partial | Draw order now follows the executable: update/copy owner matrix, draw `+0x48`, capture/draw the reflection surface, update/copy owner matrix again, draw `+0xB8`. |
| `0x80098D54` | `FUN_80098d54` | `CSelectCommon_OnEnterNoop` | exported no-op |
| `0x80098D58` | `FUN_80098d58` | `CSelectCommon_AllocateMovieSlot` | missing |
| `0x80098DA4` | `FUN_80098da4` | `CSelectCommon_ReleaseMovieSlot` | missing |
| `0x80098DFC` | `FUN_80098dfc` | `CSelectCommon_AdvanceBackgroundForward` | implemented/partial | Sets owner animation speed to `1.0`, starts channel animation `currentIndex * 2`, advances `+0x230` modulo 5, and optionally reveals alternate movie entries. |
| `0x80098EAC` | `FUN_80098eac` | `CSelectCommon_AdvanceBackgroundBackward` | implemented/partial | Moves `+0x230` backward modulo 5, sets owner animation speed to `1.0`, starts channel animation `index * 2 + 1`, and optionally reveals primary movie entries. |
| `0x80098F5C` | `FUN_80098f5c` | `CSelectCommon_ResetMovieSlotOnExit` | missing |
| `0x80098F74` | `FUN_80098f74` | `CSelectCommon_RequestMovieBackground` | missing |
| `0x80098F88` | `FUN_80098f88` | `CSelectCommon_RequestMovieBackgroundRelease` | missing |
| `0x80098FA0` | renamed | `CSelectCommon_RevealMovieEntriesPrimary` | doc only |
| `0x800991C4` | renamed | `CSelectCommon_RevealMovieEntriesAlternate` | doc only |
| `0x80099398` | `FUN_80099398` | `CSelectCommon_SetEffectsEnabled` | named/partial |
| `0x800993B4` | `FUN_800993b4` | `SelectCommonListEntry_Destroy` | named |
| `0x800CC574` | `FUN_800CC574` | `CharacterAssetManager_LoadSelectCommon` | implemented/partial |
| `0x800CCB34` | `FUN_800CCB34` | `CharacterAssetManager_ResetSelectCommonState` | implemented |

| Address | Export | Suggested name | Status |
|---|---|---|---|
| `0x800F9898` | `FUN_800F9898` | `CzanTextureSurface_Init` | implemented/host partial |
| `0x800F98CC` | `FUN_800F98CC` | `CzanTextureSurface_Destroy` | named |
| `0x800F9984` | `FUN_800F9984` | `CzanTextureSurface_Allocate` | implemented/host partial |
| `0x800F9A38` | `FUN_800F9A38` | `CzanTextureSurface_ReleasePixels` | named |
| `0x800F9C14` | `FUN_800F9C14` | `CzanTextureSurface_CopyRegionFromFrame` | host partial |
| `0x800F9E40` | `FUN_800F9E40` | `CzanTextureSurface_DrawProjectedQuad` | host partial |
| `0x800F9F64` | `FUN_800F9F64` | `CzanTextureSurface_DrawQuadRaw` | named |
| `0x80162330` | `FUN_80162330` | `RenderQuadVertices_BuildXYWH` | named |
| `0x80162560` | `FUN_80162560` | `RenderQuadTexcoords_BuildLTRB` | named |
| `0x80162584` | `FUN_80162584` | `RenderState_SetTranslucentBlendMode` | named |
| `0x801625E4` | `FUN_801625E4` | `RenderState_SetAlternateTranslucentBlendMode` | named |

New select-common state notes:

- `CSelectCommon_DrawEffects` only draws the two `CtsStageObj` layers at
  `selectCommon +0x48` and `selectCommon +0xB8` when `selectCommon +0x36C == 1`.
  The owner model at `+0x128` supplies/copies the current matrix; it is not drawn
  directly by this function.
- `CtsStageObj_ApplyModelTransform` (`0x800594B8`) and
  `CtsStageObj_DrawModelWithFlags` (`0x800594DC`) are draw-only wrappers in the
  executable. They do not advance the host animation frame; that belongs to the
  update/timeline path before `CSelectCommon_DrawEffects`.
- Between those two layers, `CSelectCommon_DrawEffects` uses the texture surface at
  `selectCommon +0x234`: `CzanTextureSurface_CopyRegionFromFrame` captures the
  upper half-width frame region, then `CzanTextureSurface_DrawProjectedQuad` draws
  it into the lower half. This is the select-background reflection/overlay path
  and now has a host OpenGL framebuffer-copy path. The host still does not emulate
  the raw GX TEV/material setup from `CzanTextureSurface_DrawQuadRaw`, but the
  secondary stage layer now follows the retail draw order instead of an env gate.
- `CzanTextureSurface_DrawProjectedQuad` builds its draw data through
  `RenderQuadVertices_BuildXYWH` and `RenderQuadTexcoords_BuildLTRB`, then calls
  `CzanTextureSurface_DrawQuadRaw` with four vertices. The default color comes
  from `DAT_802E90D0` unless the caller passes an override pointer.
- `CSelectCommon_UpdateMovieBackground` refreshes the owner state first:
  `FUN_8015EEB4(selectCommon +0x128, 0)`, then
  `CzanModelOwner_CopyCurrentModelMatrix(selectCommon +0x128, selectCommon +0x18)`.
  The update is gated by the same skip/force condition as `FUN_80098810`.
  `FUN_8015EEB4` begins with `FUN_8015E7FC`, now named
  `CzanModelOwner_UpdateModelDrivenMatrix`: it advances owner model channel 0,
  extracts object 1/2 transforms into the owner position/up/target vectors, and
  rebuilds owner `+0x4C` through the same look-at matrix helper.
  `CSelectCommon_DrawEffects` now also performs the executable's two draw-time
  refreshes: update/copy before the `+0x48` stage draw, then update/copy again
  before the `+0xB8` stage draw.
- `CzanModel_UpdateAnimationChannel` now advances channel frame `+0x230` from
  speed `+0x250` and model frame scale `+0x19C`, wraps/clamps against duration
  `+0x240`, and marks bytes `+0x244/+0x245`, matching the recovered
  `0x8015627C` control flow used by the select-common owner.
- Host animation guards now read the active `GameMain` loop object instead of
  the manager placeholder allocated during startup. Without that, the frame token
  stayed at zero and ZAB channels advanced only once.
- `CzanModel_UpdateHostRuntimeObjectTransformsFromBlocks` now mirrors the normal
  ZMB runtime layout more closely: animated local matrices are written to
  `model[7]`, then composed through the parent table into world matrices at
  `model[8]`, which is the array consumed by the host draw cache.
- `CtsStageObj_UpdateAnimationFrame` now mirrors `FUN_8004E140` / vtable
  `+0x34`: it advances the CtsStageObj-owned model animation channel without
  drawing. `CSelectCommon_UpdateMovieBackground` calls it for both select-common
  stage layers, and the host draw path now invokes that update once per frame
  before presenting the background.
- `CSelectCommon_AdvanceBackgroundForward` starts owner animation
  `selectCommon[+0x230] * 2`, sets reveal path `+0x368 = 1`, then advances
  `+0x230 = (+0x230 + 1) % 5`.
- `CSelectCommon_AdvanceBackgroundBackward` backs `+0x230` by one modulo five,
  starts owner animation `+0x230 * 2 + 1`, and sets reveal path `+0x368 = 0`.
- `CSelectCommon_SetEffectsEnabled` is a direct store to `selectCommon +0x36C`.

## Current Visual Failures

The current host is in the right resource order, but it still does not draw like the
game.

```text
White screen where select_cmn should appear:
  The boot bundle now routes select/select_cmn.bin through
  CharacterAssetManager_LoadSelectCommon -> CSelectCommon_LoadResource.
  That path creates two CtsStageObj layers, one CzanModelOwner, attaches model
  continuation/ZAB blocks, and the host CSelect frame now routes those two stage
  models through CzanModel_DrawVisibleObjectsWithMode instead of the older
  select-only cached renderer. Remaining gaps:
  CharacterAssetManager block 1 still needs FUN_800F68FC, model-manager bank 4
  still needs its live-object/effect consumers, and the CSelectCommon
  update/effect path is still partial. The host path now obeys the real
  `CSelectCommon_DrawEffects` `+0x36C` gate and draws only the two stage layers by
  default; the owner/debug layer is no longer part of the normal path. The active
  model draw path now uses a CtsStageObj-owned host wrapper so the game-supplied
  `selectCommon +0x18` matrix is preserved while the stage animation frame still
  advances from `CtsStageObj_StartAnimation`.
  The framebuffer-copy/reflection half of CSelectCommon_DrawEffects now has a host
  OpenGL equivalent for CzanTextureSurface_CopyRegionFromFrame and
  CzanTextureSurface_DrawProjectedQuad. The secondary stage layer is now drawn
  unconditionally inside the retail `+0x36C` gate. The remaining select_cmn
  mismatch is the secondary layer's GX material/TEV path and the full CzanModel
  part/material tree.

Wrong select_cmn texture scale / mirrored draw:
  The host primitive stream was forwarding raw model UVs directly to OpenGL. The
  current host path normalizes UVs only when they are clearly pixel-scale
  coordinates and keeps the projection orientation that matched the user's
  reference capture. The remaining accurate fix is still the full CzanModel
  material/part tree path, especially
  CzanModel_DrawMaterialPartTree and the four primitive submit leaves.
  2026-07-18 projection correction: `FUN_8015EEB4` does not draw from raw
  owner `+0xF8/+0xFC` after reading object-1 projection aux. It calls
  `FUN_8015F3F4`, which converts the model's horizontal FOV with
  `atan(tan(fov/2) * videoScale) * 2` and stores the matrix FOV/aspect in the
  projection object at owner `+0xE4/+0xE8`. The host projection now reads those
  fields and the OpenGL backend letterboxes non-4:3 windows instead of
  stretching the 640x480 logical render target.
  2026-07-18 vertical orientation correction: the CPU-projected host primitive
  path now maps CzanModel projected Y into the top-left OpenGL surface without
  applying the older select debug-path inversion. This matches the DOL matrix
  helpers (`FUN_801B0850`, `FUN_801B0AD0`) while keeping the 2D/tint draw path
  untouched.

All 2D textures appearing at once:
  FUN_800FEBE8 / UiRootManager_DrawGlobalCzanListOnce is only a thin wrapper around
  CzanUiManager_DrawObjectListReverse(globalUiManager, -1). It assumes the game has
  already driven every object/group flag. The host currently creates too many CAE
  groups without the exact CGameUiSelectionPanel_Update and UI-root submanager
  state machines, so the global draw pass can still see objects as drawable too
  early. The select-bin warning/timeline bridge has been removed from the host
  boot path because `select_bin_sp.bin` root block `0x0F` is boss-folder UI, not
  the warning timeline. The selTitle/title phase now uses the recovered
  `FUN_800E25B0` global Czan draw helper.

Wrong title/strap timeframe:
  `FUN_800E1988` calls `FUN_800FEB08(gUiRootManager, 0)` after the title state
  switch and coordinator tick. The host now exposes this as
  `UiRootManager_UpdateGlobalCzanListOnce`, which calls the recovered
  `FUN_801745B8` role before draw. CAE scripts no longer advance inside the draw
  helpers themselves; they advance from the UI-root update path like the retail
  executable.

Wrong 2D coordinates:
  CAE group position is not only raw texture coordinates. The executable applies
  reference groups, edge alignment, offsets, animation offsets, texture-frame size
  updates, color blocks, and widescreen/projection setup. The important confirmed
  helpers are CzanUiManager_SetObjectTextureFrame, FUN_8017559C
  (edge alignment), FUN_80175B00 (color blocks), the position/offset helpers at
  0x801751B8/0x80175240, and UiScreenProjection_UpdateGlobals.

Texture decode caveat fixed in host:
  Wii TPL formats `0`/`1`/`4` are I4/I8/RGB565. The host used to reject them and
  only load IA4/IA8/RGB5A3/RGBA32/CMPR, which made some select/title/common
  resources appear missing or as fallback colored sheets. The OpenGL backend now
  decodes I4, I8, and RGB565 too.

ZMB material caveat fixed in host:
  `CzanModel_BuildRuntimeData` chooses material stride `0x38` or `0x50` from the
  material-table version. The flat host submitter now uses that same versioned
  stride when resolving material texture slots, instead of always using `0x50`.
```

Confirmed Czan/UI helper names that matter for the texture/coordinate issue:

```text
0x800E25B0 -> CSelectTitleFlow_Draw
0x800FEB08 -> UiRootManager_UpdateGlobalCzanListOnce
0x800FEBE8 -> UiRootManager_DrawGlobalCzanListOnce
0x8016F99C -> CzanSpriteObject_GetEffectiveCornerAlphas
0x801745B8 -> CzanUiManager_UpdateObjectList
0x80174BA8 -> CzanUiManager_DrawObjectListReverse
0x80174E2C -> CzanUiManager_StartObjectGroupAnimation
0x80174F60 -> CzanUiManager_SetObjectGroupAnimationResetMode

Implementation note: CAE animation opcodes `0x06` and `0x07` are sprite transform X/Y in
`CzanUiObjectInstance_RunAnimationScript`, not texture dimensions. Dimensions are driven by
opcodes `0x41` and `0x42`. Opcodes `0x36/0x37` write normalized UV/pivot offsets at
sprite `+0xD8/+0xDC` that the mode-0 quad emitter adds to sprite `+0xC8..+0xD4`.
Opcodes `0x43..0x46` only consume one script value; mapping those to host UV base/span
makes select-bin/comAF sprites appear as huge tiled sheets rather than positioned UI
objects.

`CzanUiManager_CreateObjectGroup` also seeds sprite bases differently by descriptor
type: type `0` texture descriptors use half width/height at sprite `+0x78/+0x7C`,
dummy type `2` uses `4,4` for an `8x8` placeholder, but cloned texture descriptors
copy the source texture and use full width/height at `+0x78/+0x7C`. The host now
keeps that distinction instead of centering clones.

`CzanUiManager_DrawObjectGroupInListOrder` still filters down to one object group, but
the DOL walks the global object list from last to first before comparing each object
against the group's child table. The host group draw now uses that reverse object-list
order so overlays/tint layers land over, not under, their matching sprites.

`CzanUiManager_DrawObjectListReverse` has the same last-to-first requirement for the
global list. The host previously split objects into a window/non-window pass and walked
forward; that was a host-only shortcut and could pull resident tint/overlay objects in
front of select/title layers that the DOL draws later.

Host caveat: the real `gUiRootManager` global object list is curated by the active
screen helpers before `FUN_800E25B0` draws it. The host was still using the broad
global draw in the title bridge, which let resident select_bin/comAF objects leak into
the selTitle phase. The title bridge now scopes draw to the three selTitle groups and
the three title `CSelModeEntry` groups that `CSelectTitleFlow_LoadSelTitle` actually
owns. Remaining out-of-order sprites should be fixed in the update/list-curation path
(`FUN_800FEB08 -> FUN_801745B8`) or the recovered title state machine, not by drawing
every decoded resident group.

2026-07-18 update: the host primitive cache also carries the DOL material byte
`material +0x12` per submitted ZMB primitive. `CzanModel_ApplyMaterialBlendMode` uses
this byte as `mode = byte & 0x7F` and `alphaRefLow = byte >> 7`; the host now uses it
to keep opaque/default primitives and material-blended primitives in separate draw
passes for both generic Czan models and the select_cmn stage cache.

2026-07-18 update: the host now carries a per-frame `listedForDraw` bit that is set
from the `FUN_801745B8`-style admission test instead of drawing every decoded
`HostCzanObject`. This uses the confirmed DOL conditions that the selected CAE
animation is runnable, object `+0x173` is clear, object `+0x181` is set, the sprite
is valid, and the effective corner alpha from `FUN_8016F99C` is not fully
transparent. The select-mode branch now also runs this update before drawing, so
resident `select_bin`/`comAF` resources should not appear merely because their file
has been loaded. Host-decoded CAE entries with a zero parsed duration but a valid
command stream are still admitted, because otherwise title/select phases collapse
to one or two surviving sprites.

2026-07-18 draw-order fix: the live Czan draw bridges now sort admitted objects by
the recovered object order fields before drawing the list in reverse order, closer
to `FUN_801745B8` + `FUN_80174BA8` than the old flat host cache order.

2026-07-18 select_cmn layering fix: the host now draws the `+0xB8` select_cmn stage
pass before the `+0x48` pass/reflection copy. The previous host order put the
secondary pass on top, which made the blue tint layer behave like a foreground
overlay instead of a back layer.

2026-07-18 boot-title draw fix: while `CSelect` owns the frame, the host skips the
normal `UiRootManager_DrawFrame` path from the main loop. The `cSelect +0x38 == 0`
boot/title branch no longer draws the entire resident Czan UI list, because the
host flat cache keeps later `select_bin`/`comAF` groups resident before the real UI
list curation is fully ported. It now draws only the root transition groups
`uiRoot[1..4]` and the active boot-transition controller groups `uiRoot[0x0C][0..10]`
through the host-only `UiRootManager_DrawBootCzanGroups` bridge.

2026-07-18 UV fix: `FUN_80175998` writes left/top/right/bottom edge values into
sprite `+0xC8/+0xCC/+0xD0/+0xD4`; `CzanSpriteObject_Draw` then passes
`left + offsetX`, `top + offsetY`, `right + offsetX`, `bottom + offsetY` into
`CzanDrawTexturedOrColoredQuad`. The host had treated right/bottom as spans, so the
draw path produced `u1 = left + right` and `v1 = top + bottom`, which tiled or
stretched select/comAF sheets. The host now stores `right-left` and `bottom-top` in
its span fields.

Title-flow coordinator helpers found while chasing the selTitle coordinate issue:

```text
0x8008D4DC -> CSelectTitleCoordinator_Create
0x8008D57C -> CSelectTitleCoordinator_Destroy
0x8008D5EC -> CSelectTitleCoordinator_ApplyVectors
0x8008D630 -> CSelectTitleCoordinator_ClearModelGroup
0x8008D674 -> CSelectTitleCoordinator_TickDraw
0x8008D68C -> CSelectTitleCoordinator_DrawNoop
0x80102BE4 -> CSelectTitleModelFocus_Init
0x80102CFC -> CSelectTitleModelFocus_BindObject
0x80102D50 -> CSelectTitleModelFocus_SetVectors
0x80102DA0 -> CSelectTitleModelFocus_BuildInitialMatrix
0x80102EB8 -> CSelectTitleModelFocus_TickDraw
```

Host status, 2026-07-18: the `FUN_8008D4DC -> FUN_80102BE4/2CFC`
creation/bind path, the `FUN_8008D5EC -> FUN_80102D50/2DA0` vector/matrix path,
and the per-frame `FUN_8008D674 -> FUN_80102EB8` tick path are now wired into
`CSelectTitleFlow_LoadSelTitle` and `CSelectTitleFlow_Tick`. It is still partial:
the exact `FLOAT_802E8DC0/8DC4/8DC8/8DCC/8DD0/8DD4` constants have not been
resolved from the data section, so the host uses neutral vectors until those values
are dumped, and `CzanModelManager_UpdateVisibleGroup` is still a stub.

`CSelectWarningTimeline_Start` should start animation `0` at the frame stored by
`CSelectWarningTimeline_InitFromBlock` (`FUN_80129414`). The exported C calls it with
`FLOAT_802e83c8`, but only the C/H export is present in the extracted folder, so the
exact numeric value still needs the raw DOL/data section before it can be proven.

`CSelectTitleFlow_Draw` (`0x800E25B0`) has now been inspected against the export:
it calls `UiRootManager_DrawGlobalCzanListOnce(gUiRootManager, 1)`, then calls
`FUN_8008D68C`, which is a no-op in this DOL, and only draws the OP movie helpers
while title state is `10` or `11`. The host title draw now follows that shape instead
of manually selecting selTitle groups.

Additional confirmed CAE transform opcodes in
`CzanUiObjectInstance_RunAnimationScript`:

```text
0x05 -> object +0x44, sprite +0x80        depth/extra quad parameter
0x0C -> object +0x38, sprite +0x44        transform Z
0x0D -> object +0x50, sprite +0x98        scale Z
0x10 -> object +0x5C, sprite +0xA4        rotation Z
0x11 -> object +0x54, sprite +0x9C        rotation X
0x12 -> object +0x58, sprite +0xA0        rotation Y
```

The host now stores all of these and applies Z rotation in the 2D quad emitter.
X/Y rotation still needs the full `CzanSpriteObject_Draw` matrix/projection path
(`FUN_801708F0`) before it can match the game exactly.
0x80174FA4 -> CzanUiManager_ResetObjectGroupAnimationTime
0x80174FE0 -> CzanUiManager_SetObjectGroupAnimationMode
0x8017501C -> CzanUiManager_StartObjectGroupAnimationForChildren
0x80175448 -> CzanUiManager_SetObjectTextureFrame
0x8017559C -> CzanUiManager_AlignObjectGroupByReferenceEdge
0x80175744 -> CzanUiManager_SetChildObjectReferenceEdge
0x8017576C -> CzanUiManager_SetObjectGroupPriority
0x801757A8 -> CzanUiManager_SetObjectGroupReferenceEdgeActive
0x801757E4 -> CzanUiManager_SetChildObjectReferenceEdgeActive
0x80175804 -> CzanUiManager_GetChildObjectReferenceEdge
0x80175B00 -> CzanUiManager_SetObjectGroupColorBlocks
```

Next functions to find/rename before another visual pass:

```text
0x8008D68C -> CSelectTitleFlow_DrawSharedOverlaysOrEntryLayerNoOp
0x8010A590 -> CSelectTitleFlow_DrawOpeningMovie4x3
0x8010A60C -> CSelectTitleFlow_DrawOpeningMovie16x9
0x8010A698 -> CSelectTitleFlow_DrawOpeningMovieOverlay
0x8010476C -> CGameUiSelectionPanel_Update
0x80105DE4 -> UiRootSubManager_InitTextureFrameGroupState
0x80106054 -> UiRootSubManager_InitTextureFrameGroups
0x801061B4 -> UiRootSubManager_UpdateTextureFrameGroupMotion
0x801064F8 -> UiRootSubManager_SeedTextureFrameGroupMotion
0x80106724 -> UiRootSubManager_ApplyTextureFrameGroupMotion
0x80106850 -> UiRootSubManager_SetMenuPresentationMode
0x801068E4 -> UiRootSubManager_SetAlternateMenuPresentationMode
0x80106964 -> UiRootSubManager_ResetMenuPresentationGrid
0x80106ADC -> UiRootSubManager_SetMenuPresentationBankValue
0x80106B48 -> UiRootSubManager_ResetMenuPresentationBankFlags
0x80106CE4 -> UiRootSubManager_UpdateLinkedObjectPrompt
0x800FEC3C -> UiRootManager_CreateReferenceObjectGroup
0x800FED80 -> UiRootManager_AlignReferenceObjectGroup
0x800FEDF8 -> UiRootManager_UpdateReferenceObjectGroupLayout
```

The `0x8008D68C` and `0x8010A590/0x8010A60C/0x8010A698` names are still working
names until their bodies are fully inspected. The dependency is confirmed by
`FUN_800E25B0`: draw global Czan UI once, draw shared title/entry overlay, then draw
OP/movie overlay when state is `10` or `11`.

## Immediate Implementation Order

To make boot/select load like the game, port in this order:

1. Remaining `BootResourceBundle_ApplyLoadedResources` consumers, especially
   `CharacterAssetManager_LoadSelectCommon` / `CSelectCommon_LoadResource`.
2. `CSelect_ResetFromPlayerData` (`FUN_8004749C`).
3. `CSelect_StartFreshTitleSetup` (`FUN_8004BC40`).
4. Complete the DOL interpolation constants for `UiRootSubManager_UpdateTextureFrameGroupMotion`.
5. `CSelect_LoadSelectBinSharedResources` (`FUN_800470E0`) as a real shared loader.
6. Finish state `0` (`FUN_800DDF98` / `FUN_800DE0DC`) by porting the exact
   UI-root save-prompt helpers and save/player-data gates.
7. Complete `UiPromptEffectHelper_Draw` (`0x800FD908`) and the companion
   text/surface helpers. The host now invokes the child extension callback and
   resolves `TextManager` ids, but `FontTextSurface_RenderLayout`
   (`0x800B7778`) / `FontManager_DrawTextOnSurface` (`0x800B937C`) still need
   their glyph/surface rendering bodies.
8. Full `CSelMode_Init`, `CSelMode_OnEnter`, and `CSelMode_Update`.

Until state `0` is complete, boot can reach `CSelect` but cannot fully reproduce the
boot/title save checks before mode select. Until step 1 and the remaining
`UiRootManager_LoadResource` sub-managers exist, the select common background/boot UI
will not match the game even if `select_bin_*` loads.
