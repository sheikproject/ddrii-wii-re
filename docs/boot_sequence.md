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

Current host integration:

```text
GameMain
  initialize stdout buffering for host diagnostics
  initialize MainLoopManagerKnownFields + pointed ModuleControllerKnownFields
  GameMain_InitRuntimeManagers()
    GlobalRuntimeContext_CreateOnce(1, 0x800, 4, 0x10, 0x32, 3, 0x46, 1, 0xF00403, 1)
      RuntimeLowLevel_InitCoreLibraries()
      RuntimeLowLevel_InitMemoryPools(0x70007)
      RuntimeLowLevel_InitVideoInterface()
      GlobalRuntimeContext_Init()
    DebugText_InitFontBacking()
    GlobalUiFrameState_CreateOnce()
    CzanUiManager_AllocateObjectGroupStorage(*(DAT_802E71B8 +0x270), 300, 0x800)
    GlobalResourceManager260_SetModeTable(*(DAT_802E71B8 +0x260), ...)
    allocate recovered CGame/global manager chain
  MainLoopManager_SetInitialFrameStep(0x3C)
  loop:
    MainLoopManager_Tick(mainLoopManager)
```

The old `DDRII host skeleton` console banner was removed; the host path now enters the
recovered runtime/global-context boot functions before the module controller begins.

The current host `MainLoopManager_Tick` now follows the exported `0x80020CC8` frame
order: debug-text prep, pending-module apply, global pre-frame update, resource/effect
busy gates, active module tick, UI draw/update, movie-slot update, post-frame flush,
UI-frame tick, input update, manager `802E70B4` update, cue-manager update, reset/shutdown
checks, and frame increment. Several deep Wii/UI/audio callees are still named host
shims until their bodies are safe to run on the host, but the boot loop control flow is
no longer the old local wrapper.

The new `main_DDRII.dol` export confirms the real manager allocation sequence:

```text
0x00014 -> gMainLoopManager          -> PlayerDataStateContainer_Init
0x02F54 -> gPlayerDataManager        -> PlayerDataManager_Init
0x28F58 -> gManager_802E70E0         -> Manager802e70e0_Init, then Manager802e70e0_DestroyNoop
0x0049C -> gManager_802E70A4         -> FUN_80022630
0x0000C -> gManager_802E70A8         -> ResourceSlotHandle_Init, then FUN_80024D38
0x00EF0 -> gInputOrMenuStateManager  -> FUN_80029FB0, then FUN_8002A0B0
0x00050 -> gManager_802E70B0         -> FUN_800C0BF8
0x00BF0 -> gManager_802E70B4         -> FUN_8010E504
0x0004C -> gUiRootManager            -> FUN_800FE48C
0x284E0 -> gCharacterAssetManager    -> CharacterAssetManager_Init
0x01B38 -> gManager_802E70B8         -> FUN_80178278, then FUN_80178438(..., 0x100)
0x3010B8 -> gLargeResourceManager    -> FUN_80025A7C
0x00028 -> gBootTempManager          -> FUN_80021D60
```

This also corrects an earlier label: `FUN_80099F70` belongs to
`gManager_802E70E0`; the actual `gCharacterAssetManager` constructor is
`FUN_800CC450`.

`FUN_801438F4` is the lazy creator for the global runtime context stored at
`DAT_802E71B8`. Suggested name:

```text
GlobalRuntimeContext_CreateOnce
```

Confirmed behavior:

```text
if DAT_802E71B8 == 0:
  FUN_801A2C80()
  FUN_80144B50(0x70007)
  FUN_801BACA0()
  context = AllocObjectAligned(0, 0x278, 0x20, 0)
  if context != 0:
    FUN_80142BF0(context, arg0..arg9)

DAT_802E71B8 = context
```

The attached `FUN_801A2C80` is the one-time Revolution SDK / OS core initializer called
first by `GlobalRuntimeContext_CreateOnce`. Suggested name:

```text
RuntimeLowLevel_InitCoreLibraries
```

Confirmed behavior:

```text
if DAT_802E7250 == 0:
  DAT_802E7250 = 1
  DAT_802E7270/DAT_802E7274 = FUN_801AD440()
  enter critical section
  initialize OS globals and callbacks around DAT_802EFF20
  clear/set low-level OS callback slots through FUN_801A2040..FUN_801A2090
  configure MEM1/MEM2 arena boundaries through FUN_801A4320/FUN_801A42F0/FUN_801A4330
  derive boot info from DAT_800000F4 or DAT_800030E8
  initialize runtime subsystems:
    FUN_801ADD50, FUN_801A31E0, FUN_801ABD90, FUN_801A36A0,
    FUN_801A9C00, FUN_801A94D0, FUN_801A56D0, FUN_801A4D50,
    FUN_801D9740, FUN_801DAC60, FUN_801AB400, FUN_801ABE00,
    FUN_801A45B0
  mask OS state through FUN_801A20F0/FUN_801A2100
  initialize scheduler/thread state when DAT_802E7230 == 0
  print/log RVL SDK release build string
  initialize more OS/device pieces:
    FUN_801A29D0, FUN_801DE770, FUN_801ADF10, FUN_801E4D10,
    FUN_801AED80, FUN_801B1BF0, FUN_801AFBB0, FUN_801AE910
  check boot/apploader error bytes DAT_8000315C/DAT_8000315D
  check firmware date/version from c0003140/c0003144 against DAT_80003188
  optionally create an OS alarm/thread hook at DAT_802EFF40/DAT_802EFF60
```

This function is mostly platform startup. The Windows host cannot reproduce the Wii
hardware register reads, firmware checks, or OS interrupt installation, but it must run
before allocator/video/global-context setup in the same order.

`FUN_801A2130` is a small low-level OS register update helper. Suggested name:

```text
RuntimeLowLevel_SetH4AFlag
```

It reads the register/state from `FUN_801A1FC0`, ORs in bit `0x200`, and writes it back
through `FUN_801A1FD0`.

`FUN_801A29D0` prints the Revolution OS/kernel startup report. Suggested name:

```text
RuntimeLowLevel_ReportKernelInfo
```

Confirmed report sequence:

```text
Revolution OS
Kernel built : Aug 23 2010 17:33:06
Console Type : ...
Firmware : major.minor.patch (month/day/year)
Memory N MB
MEM1 Arena : low - high
MEM2 Arena : low - high
```

The console-type branch identifies retail, NDEV, emulator, and TDEV-style hardware
from `FUN_801A2360()` and prints the matching label.

`FUN_80144B50` is the low-level allocator/memory-pool initializer called before the
global context is allocated. Suggested name:

```text
RuntimeLowLevel_InitMemoryPools
```

Confirmed behavior:

```text
DAT_802E71D0 = 0
DAT_802EE15C = FUN_801A42C0()
DAT_802EE160 = FUN_801A4290()
DAT_802EE190 = FUN_801A42D0()
DAT_802EE194 = FUN_801A42A0()
FUN_801A4320(DAT_802EE160)
FUN_801A4330(DAT_802EE194)
initialize global mutex DAT_802EE1C0
for two memory pool records at DAT_802EE158, stride 0x34:
  pool[0] = FUN_801DC440(pool[1], pool[2] - pool[1], 6)
  initialize pool mutex at +0x1C
  FUN_801DC7C0(pool +0x0C, pool[0], 0x20)
lock global mutex
DAT_802E71C8 = 0
unlock global mutex
```

This is why `AllocObjectAligned`-style calls are valid immediately afterward in
`FUN_801438F4`.

`FUN_801BACA0` is the one-time VI/video interrupt/display-mode initializer called
before `DAT_802E71B8` allocation. Suggested name:

```text
RuntimeLowLevel_InitVideoInterface
```

Confirmed behavior:

```text
if DAT_802E7480 == 0:
  initialize low-level time/interrupt state
  if video register cc002002 bit 0 is clear:
    FUN_801BAAA0(0)
  clear many DAT_802E74xx/DAT_802E75xx state values
  write VI timing registers cc00204c..cc002070 from DAT_802D16A4..DAT_802D16D4
  derive region/display mode from FUN_801E6700, DAT_800000CC, cc002002, cc00206c
  fill DAT_802F6350..DAT_802F63A4 viewport/timing/display-mode records
  install interrupt/callback handlers through FUN_801A94A0, FUN_801AA990, etc.
  choose retrace timing constants:
    mode group 1 -> 15000, 15000, 90000
    other modes -> 18000, 18000, 0x1A5E0
  store progressive/display flags from FUN_801E6860
  call FUN_801BE580(previousState)
```

The host cannot write Wii VI registers, but it should preserve the game-facing derived
state: display width `0x280`, viewport offsets, scan/mode selection, and timing
constants.

The constructor at `FUN_80142BF0` is the next important body here. The global context
is the object later read through offsets such as `+0x258`, `+0x260`, `+0x268`, and
`+0x270`.

`FUN_80142BF0` constructs the `0x278`-byte global runtime context. Suggested name:

```text
GlobalRuntimeContext_Init
```

Confirmed field setup:

```text
+0x000 = 0
+0x004 = -1
+0x008 = 0
+0x08C = low byte from FUN_801E6790()
+0x090..+0x0B4 = two groups of five words, all set to 1
+0x0B8 = -1
+0x0BC = recovered arg0/context value from the register-spill helper
+0x0C0 = 0
+0x244 = 0
+0x248 = 2
+0x24C = FLOAT_802E9C5C
+0x250 = FLOAT_802E9C60
+0x254 = rgba(0xFF,0xFF,0xFF,0xFF)
+0x258 = 0x70-byte submanager from FUN_801410F8
+0x25C = 0x504-byte submanager from FUN_801499F4, then FUN_80149C80(..., arg8)
+0x260 = 0x54-byte resource manager from FUN_80143E7C(..., 0x100, +0x0BC)
+0x264 = 0x874-byte submanager from FUN_801644D0(..., arg2, arg3)
+0x268 = 0x4248-byte effect/scene manager from FUN_80166DF0(..., arg4..arg7, 0)
+0x26C = 0x0C-byte submanager from FUN_80146950(..., arg1)
+0x270 = 0x38-byte UI/object manager from FUN_80173084
+0x274 = 0xB4-byte submanager from FUN_80141AC8(..., arg9)
```

The startup tail registers `LAB_80143A88`, reads environment/region values, seeds other
systems through `FUN_80131C2C`, `FUN_8018D144`, `FUN_8015F4AC`, and stores a region-like
string at `+0x0C` based on the low byte from `FUN_801E6790()`.

`FUN_80143830` chooses the active region/language resource table. Suggested name:

```text
GlobalRuntimeContext_SelectRegionVariant
```

Confirmed behavior:

```text
region = *(globalContext +0x8C)
if *(globalContext +0x90 + region * 4) == 0:
  return *(globalContext +0xB8)
return region
```

This is the selector used by boot resource loading before indexing the
`/banner`, `/text`, `/font`, `/select`, `/ssq`, `/2Dcommon`, and `/Pointer` path table.

`FUN_80144EA0` and `FUN_80144EF4` are the memory/global-state guard pair used around
manager setup. Suggested names:

```text
RuntimeMemory_SetCriticalFlag
RuntimeMemory_ClearCriticalFlag
```

Confirmed behavior:

```text
RuntimeMemory_SetCriticalFlag(value):
  lock DAT_802EE1C0
  DAT_802E71C8 = 1
  uRam802E71CC = value
  unlock DAT_802EE1C0

RuntimeMemory_ClearCriticalFlag():
  lock DAT_802EE1C0
  DAT_802E71C8 = 0
  unlock DAT_802EE1C0
```

`FUN_80143E7C` constructs the `0x54`-byte manager stored at global context `+0x260`.
Suggested name:

```text
GlobalResourceManager260_Init
```

Confirmed behavior:

```text
manager +0x50 = PTR_PTR_802C05C8
DAT_802E71C4 = -1
if DAT_802E71C0 == 0:
  FUN_801B1BF0()
  DAT_802E71C0 = 1
FUN_801B7550(1)

records = AllocObjectAligned(0, capacity * 0x4C + 0x10, 0x20, 0)
records are initialized through FUN_80129C64(..., recordSize=0x4C, count=capacity)

manager +0x00 = 0
manager +0x04 = FLOAT_802E9CA8
manager +0x08 = 0
manager +0x0C = capacity
manager +0x10 = records
manager +0x14 = 0
manager +0x18 = 0
manager +0x1C..+0x2C = five words set to 1
manager +0x30 = 0
manager +0x34 = -1
manager +0x38..+0x48 = 0
manager +0x4C = mode table, PTR_DAT_802C05A0 when param_3 == 2, otherwise PTR_DAT_802C0578
```

Each `0x4C` record has at least:

```text
record +0x00 = -1
record +0x04 = 0
record +0x44 = 0
```

`FUN_80129C64` is the generic contiguous object-array constructor used here and by
other manager pools. Suggested name:

```text
RuntimeObjectArray_Construct
```

Confirmed behavior:

```text
allocation[0] = recordSize
allocation[1] = count
records = allocation + 0x10
for i in 0..count-1:
  constructor(records + i * recordSize, 1)
return records
```

If construction failed before all entries were initialized, it would run the supplied
destructor backward with release mode `-1`. In the recovered decompile, the normal
loop completion makes that rollback branch effectively unreachable.

`FUN_80143E10` is the destructor paired with the `+0x260` manager's `0x4C` records:

```text
ResourceManager260_DestroyRecord
```

It only frees the record pointer when the supplied release mode is positive, so records
owned inside a `RuntimeObjectArray_Construct` block are not individually freed during
rollback/destruction with `-1`.

`FUN_80144294` updates the `+0x260` manager's aggregate loading progress. Suggested
name:

```text
GlobalResourceManager260_UpdateProgress
```

Confirmed behavior:

```text
pendingSize = 0
hasNoActiveRecords = true
for each 0x4C record:
  if record +0x04 == 1:
    pendingSize += GlobalResourceManager260_GetRecordPayloadSize(record +0x08)
    hasNoActiveRecords = false

manager +0x04 = (manager +0x14 + pendingSize) / manager +0x18

if manager +0x00 == 0, or no records remain active and progress reaches 1.0:
  clear manager +0x00, +0x14, +0x18
  manager +0x04 = 0.0
  return 1

return 0
```

`FUN_801B1AC0` is the per-record payload size/weight helper used by that progress
scan. Suggested name:

```text
GlobalResourceManager260_GetRecordPayloadSize
```

Most payload kinds return `payload +0x20`; kind `2` returns zero. A few uncommon
sentinel kinds return the payload pointer itself in the original decompile, which is
preserved for now until the resource record constructors are fully named.

`FUN_80173084` constructs the `0x38`-byte UI/object manager stored at global context
`+0x270`. Suggested name:

```text
UiObjectManager270_Init
```

Confirmed defaults:

```text
+0x00 = 0
+0x04 = 0
+0x0C = 0
+0x18 = 1
+0x19 = 0
+0x1A = 0
+0x14 = 0
+0x1C = 0
+0x24 = FLOAT_802E9F6C
+0x28 = FLOAT_802E9F6C
+0x2C = 0
+0x30 = 1
+0x34 = 0x0C
+0x35 = 1
```

The attached `FUN_80166DF0` is the constructor for the large manager stored at global
context `+0x268`. Suggested name:

```text
EffectSceneManager268_Init
```

Confirmed high-level layout:

```text
manager size = 0x4248
+0x4244 = PTR_PTR_802C08A0
+0x080 = 0
+0x084 = -1, later replaced by FUN_80168070 result
+0x088 = -1 / environment mode state
+0x08C = 1
+0x090/+0x0A0 = initialized subobjects
+0x0E8 = initialized 0xC0-ish subobject
+0x0F8 = pointer map over all records
+0x0FC = group0 records, count param_2, stride 0x0D0
+0x100 = group1 records, count param_3, stride 0x220
+0x104 = group2 records, count param_4, stride 0xAA4
+0x108 = group0 start index
+0x10C = group0 end index
+0x110 = group1 start index
+0x114 = group1 end index
+0x118 = group2 start index
+0x11C = group2 end index
+0x120 = total record count
+0x128..+0x9D0 = many cleared runtime/state ranges
+0x8D8 = four 0x50-byte auxiliary slots, each spaced by 0x28 in the owner
+0x9E0 = 0x3800-byte table initialized through FUN_801C41C0(..., 0x100)
+0x41E0 = timestamp/random seed from FUN_801AD410
+0x41F0 = 8 bytes set to 0xFF
```

It builds `+0xF8` as a per-index pointer table: indices in the first range point into
the `0xD0` pool, indices in the second range point into the `0x220` pool, and indices
in the third range point into the `0xAA4` pool. Any non-null record receives its global
index at `record +0xC0`.

The constructor also initializes GX/effect callback hooks, environment mode, timing
subobjects, default float/int tuning fields around `+0x210..+0x87C`, and performs
late activation through helpers like `FUN_801682FC`, `FUN_80168714`, and
`FUN_8016944C`.

`FUN_80023030` resets the large resource manager's pending sound/effect references and
loads the default BRSAR archive through global context `+0x268`. Suggested name:

```text
LargeResourceManager_ResetAudioAndLoadDefaultSound
```

Confirmed behavior:

```text
if owner +0x43C is set:
  release handles listed in DAT_8027A570 through DAT_802E71B8 +0x268
  decrement owner-local refcounts and free handles whose count reaches zero
  clear +0x43C

if owner +0x440 is set:
  resolve a handle list through FUN_800F3024(owner +0x444)
  release/decrement all listed handles
  clear +0x440..+0x447
  set +0x444 = 0xFFFF

if owner +0x448 is set:
  resolve transition record through FUN_800F3068(+0x44C, +0x450)
  call FUN_8016AFB8(soundManager, 1, 0)
  release/decrement record[0] if valid
  clear +0x448 size 0x30
  set +0x44C = -1, +0x454 = -1

FUN_8016A8C0(soundManager)
FUN_8016A638(soundManager, "sound/DDRHP5_SOUND.brsar")
```

`FUN_8003D180` seeds the default CGame gameplay/setup block before menu/player-data
paths overwrite individual fields. Suggested name:

```text
CGame_InitDefaultGameplaySetup
```

Confirmed fields:

```text
cgame +0xD4 = 0x06880000
cgame +0xDC = 0
cgame +0xE0 = 0
cgame +0xE4 = 0
cgame +0xE8 = pointer to default string table beginning with "more than alive"
cgame +0xEC = 0
cgame +0xF0 = 0x12345678
cgame +0xF4 = 0
cgame +0xF8 = 0
cgame +0xFC = -1
cgame +0x100 = 0
cgame +0x104 = 0
cgame +0x108 = 0
cgame +0x10C = 5
cgame +0x114 = 0
cgame +0x118 = 0
cgame +0x3EC = 0
```

The small player-data helpers are direct field accessors:

```text
FUN_80028120 -> playerDataManager +0xF8
FUN_8002813C -> playerDataManager +0xFC
FUN_800282C4 -> playerDataManager +0x110
FUN_800282CC -> playerDataManager +0x114
FUN_800F3B9C -> record +0x170
```

`FUN_8002754C` and `FUN_800275D8` map sparse gameplay/resource ids into compact
indices. Suggested names:

```text
GameIndexedId_GetCategory
GameIndexedId_ToLinearIndex
```

Confirmed categories:

```text
0x00..0x0E -> category 0, count 15, base 0
0x14..0x1F -> category 1, count 12, base 0x14
0x28..0x36 -> category 2, count 15, base 0x28
0x3C..0x4A -> category 3, count 15, base 0x3C
200..208   -> category 4, count 9,  base 200
0x50..0x59 -> category 5, count 10, base 0x50
100        -> category 6, count 1,  base 100
```

`GameIndexedId_ToLinearIndex(id)` returns:

```text
prefixCount(category) + (id - categoryBase)
```

Invalid ids return `-1`.

`FUN_80023634` is the setup/refcount counterpart for the default reference table
`DAT_8027A570`. Suggested name:

```text
LargeResourceManager_ActivateDefaultAudioReferences
```

Confirmed behavior:

```text
if largeResourceManager +0x43C != 0:
  for each handle in DAT_8027A570:
    FUN_8016AABC(*(DAT_802E71B8 +0x268), handle, 0)
  for each handle in DAT_8027A570:
    decrement largeResourceManager[handle +1]
    if refcount reaches 0:
      FUN_8016A9E4(*(DAT_802E71B8 +0x268))
  largeResourceManager +0x43C = 0

largeResourceManager +0x43C = 1
for each handle in DAT_8027A570:
  if handle < 0x10E:
    if refcount is zero:
      FUN_8016A9D0(*(DAT_802E71B8 +0x268))
    increment largeResourceManager[handle +1]
  else:
    RuntimeDebugReport(...)
```

The host currently preserves the state/refcount shape with a placeholder table until
`DAT_8027A570` and `DAT_802E8ED8` are exported.

`FUN_80026AC8` is not a general large-resource transition setter; it forwards the
supplied value into sound player banks 8 and 9. Suggested name:

```text
LargeResourceManager_SetTransitionSoundBanks
```

Confirmed call chain:

```text
FUN_80026AC8(_, value):
  FUN_8002458C(gManager_802E70A4, 8, value)
  FUN_8002458C(gManager_802E70A4, 9, value)

FUN_8002458C(cueManager, bankIndex, value):
  FUN_8016AFB8(*(DAT_802E71B8 +0x268), bankIndex, value)
```

`FUN_8016AFB8` indexes `soundManager +0x9B8` by `bankIndex * 0x9C`, checks
`bankIndex < *(soundManager +0x9B4)`, then walks active linked nodes and applies
`FUN_8018FB20(node, value)`.

`FUN_8018FB20` stores the supplied value at node `+0x2A`, clears `+0x28/+0x30`, sets
`+0x2C` to `0` for hard stop or `1` for passive/fade behavior, and either marks the
state low nibble as `5` or moves a negative owner/link index back into the passive
range. The original also performs vtable-driven unlink/relink callbacks; the host keeps
those as named pending work until sound node vtables are mapped.

`FUN_80024AF4` resets transient global cue manager state during CGame teardown.
Suggested name:

```text
GlobalCueManager_ResetRuntimeState
```

Confirmed behavior:

```text
FUN_80169DDC(*(DAT_802E71B8 +0x268))
for cueManager +0x484 and +0x488:
  if pointer != 0:
    MemoryPool_Free(...)
cueManager +0x480 = 0
clear cueManager +0x484 size 8
cueManager +0x48C = 0
cueManager +0x490 = 0
cueManager +0x494 = 0
```

`FUN_80169DDC` is the sound-manager side of that reset:

```text
CzanSoundManager_ClearAuxState
```

It frees the optional pointer at `soundManager +0x9C0`, then clears
`soundManager +0x9BC` size `0x14`.

`FUN_8016A638` reloads the sound archive on the manager at `DAT_802E71B8 +0x268`.
Suggested name:

```text
CzanSoundManager_LoadArchive
```

It frees the current player list at `+0x9B0/+0x9B4/+0x9B8`, destroys the previous
archive object at `+0x9AC`, allocates a new `0x158`-byte archive object, and binds the
given archive path through `FUN_80181F0C`.

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

`FUN_80027CA8` constructs the player-data manager. Suggested name:

```text
PlayerDataManager_Init
```

Confirmed top-level flow:

```text
playerDataManager +0x2F50 = vtable PTR_PTR_802B92B0
FUN_800F37E8(playerDataManager +0x14A4)
FUN_800F3654(playerDataManager +0x1BA8)
FUN_800DE44C(playerDataManager +0x1C28)
FUN_800DF5F8(playerDataManager +0x1C54)
FUN_8011F920(playerDataManager +0x2F1C)
FUN_80123504(playerDataManager +0x2F40)
FUN_80027DE4(playerDataManager)
```

`FUN_80020B68`, `FUN_80020EE4`, and `FUN_80021030` form a small player-data/state
container:

```text
PlayerDataStateContainer_Init
PlayerDataState_Init
PlayerDataState_SetInitialValue
```

The child state is a `0x10`-byte object with `+0x00 = 0xFFFF`, `+0x04 = 0xFFFF`,
`+0x08 = 0`, and a vtable at `+0x0C`. Its initial value can only be written while
`+0x04` remains `0xFFFF`.

`FUN_80027DE4` is now identified as:

```text
PlayerDataManager_Reset
```

Confirmed reset behavior:

```text
clear playerDataManager +0x0000..+0x14A3
+0x0000 = -1
+0x0004 = 0
+0x0008 = -1
+0x000C = 3
+0x0010 = 0
+0x0014 = 1
+0x0178 halfword = 0
clear +0x0018 size 0xE0
+0x00F8 = 0
+0x00FC = 0x3C
+0x0100 byte = 1
+0x0101 byte = 1
clear +0x0108 size 0x70
+0x0108 = -1
six timer blocks at word indices 0x46/0x4A/0x4E/0x52/0x56/0x5A:
  first word = 0, second word = 0x3C, next two bytes = 1,1
reset seven player records through FUN_80028330
reset +0xCC0 menu/setup records through FUN_80028500
clear +0x0F3C size 0x28 and seed two {0, 0xD, 5, 0, 0} records
reset six mode records through FUN_8002896C
reset constructed subblocks:
  FUN_800F3940(+0x14A4)
  FUN_800F36C4(+0x1BA8)
  FUN_800DE50C(+0x1C28)
  FUN_800DF6BC(+0x1C54)
  FUN_801206D8(+0x2F1C)
  FUN_80123574(+0x2F40)
```

`FUN_80028330` clears one/all seven player records at `+0x17C + index * 0x19C`
and seeds the first word to `-1`.

`FUN_80028500` resets the menu/setup record block:

```text
+0xCC0 = 0
clear +0xCC4 size 0x220
clear +0xEE4 size 0x10
clear +0xEF4 size 0x48
+0xCC0 = 1
for eight records at +0xCC4 + index * 0x44:
  +0x00 = -1
  +0x04 = DAT_8026E8B8[index % 4]
  +0x08 = 0
  +0x0C = 0
  +0x10 = 0
  +0x14 = -1
  +0x18 = 0
  +0x1C = 0
  +0x20 = 0
  +0x24 = 0
  +0x28 = 0
  +0x30 = 0
  +0x34 = 0xD
  +0x38 = 5
  +0x3C = 0
  +0x40 = 0
for indices 0..3:
  +0xEE4 + index * 4 = 0
for indices 4..7:
  clear +0xEF4 + (index - 4) * 0x12 size 0x12
```

`DAT_8026E8B8` is still a pending four-word data export.

`FUN_8002896C` clears one/all six mode/setup records:

```text
+0xF64 + mode * 4 = 0
clear +0xF7C + mode * 0xDC size 0xDC
for five 0x2C-byte rows:
  FUN_80028D4C(playerDataManager, row, 0, 0, mode)
  FUN_80028FA0(playerDataManager, row, mode)
```

`FUN_80028D4C` writes row `+0x00/+0x04` for one row across one or all six mode
records. `FUN_80028FA0` resets row `+0x0C = 0` and `+0x10 = -1` for one row across
one or all six mode records.

The subblock reset helpers are now identified:

```text
FUN_800F3940 -> PlayerDataManager_ResetSubBlock14A4
  clear +0x000 size 0x128
  clear +0x128 size 4
  clear +0x154 size 0x14
  +0x15C = -1
  clear +0x168 size 0x388
  +0x17C = 5
  +0x4F4 = 0
  +0x4F8 = 0
  +0x12C/+0x130/+0x134/+0x138/+0x13C/+0x140/+0x144/+0x148/+0x14C/+0x150 = 0
  +0x4F0 = 1

FUN_800F36C4 -> PlayerDataManager_ResetSubBlock1BA8
  four 0x20-byte records:
    {6, 0, 0x52, 0, 0, 0, 0, 0}
    {6, 1, 0x52, 0, 0, 0, 0, 0}
    {6, 2, 0x52, 0, 0, 0, 0, 0}
    {6, 3, 0x52, 0, 0, 0, 0, 0}

FUN_800DE50C -> PlayerDataManager_ResetSubBlock1C28
  clear first 0x14 bytes
  bytes +0x00..+0x05 = 0xFF
  bytes +0x08..+0x0F except +0x0A? are explicitly set to 0xFF by the pasted body
  +0x14/+0x18/+0x1C/+0x20 = 1
  +0x24 = -1
  +0x28 = 0

FUN_800DF6BC -> PlayerDataManager_ResetSubBlock1C54
  nine records at +0x000 + i * 0x204:
    clear size 0x204
    byte +0x50 = 1
    byte +0x51 = 1
  four records at +0x1224 + i * 0x20:
    clear size 0x20
  +0x12B0/+0x12B4/+0x12B8/+0x12BC/+0x12C0/+0x12C4 = 0

FUN_801206D8 -> PlayerDataManager_ResetSubBlock2F1C
  +0x04 = 0
  +0x08 = 1
  +0x0C = -1
  +0x10 = 0
  +0x14 = -1
  +0x18 = 0
  +0x19 = byte derived from RuntimeRandom_Next15()
  +0x1A..+0x1D = 0
  +0x20 = -1

FUN_80123574 -> PlayerDataManager_ResetSubBlock2F40
  four words cleared to 0
```

The remaining pending item in this exact reset path is the four-word data table
`DAT_8026E8B8`.

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
9  -> hold logo, animate frames, wait timeout/input, and on the second logo step wait
      for GlobalResourceManager260_UpdateProgress(DAT_802E71B8 +0x260) to finish
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

`FUN_8003C4F8` tears down/reset CGame runtime state and returns the caller-supplied
next module/state value. Suggested name:

```text
CGame_TeardownRuntimeState
```

Confirmed behavior:

```text
mirror cgame +0xB0 low five bits into DAT_802E71B8 +0x260 records
reset gLargeResourceManager and gManager_802E70A4
release movie slot cgame +0x414 and reset transition subsystem cgame +0x428
destroy active controller cgame +0x42C through vtable +0x10 and controller destroy
reset/clear CGame subsystems +0x3F0..+0x410 and +0x428
release owned subsystem objects and null +0x40C/+0x410
restore UI/input flags from cgame +0xA8/+0xB4 and +0xB0
run local cleanup 0x8003BF90, clear UI root, reset render/GX state
```

`FUN_800439AC` is the heavy active CGame runtime update/render pass. Suggested name:

```text
CGame_UpdateAndRenderActiveGameplayRuntime
```

The attached decompile shows the live frame path after the active controller exists at
`cgame +0x42C`. It runs controller vtable `+0x34`, toggles render/movie manager state,
calls `ActiveControllerMovieBindings_SetTransitionFlagAndUpdateVisibility`, updates
large-resource transition state, calls controller vtable `+0x38`, pushes matrices into
model subsystems, calls controller vtable `+0x3C`, walks the `cgame +0x2D0` entry list
to submit model draws, then runs the UI/effect/model-manager render phases and updates
subsystem `cgame +0x428`.

`FUN_80027FFC` returns the first word of a small player-data/state block. Suggested
name:

```text
PlayerDataState_GetCurrentValue
```

Confirmed behavior:

```text
return state[0]
```

The attached `FUN_80041A9C` sets up active gameplay transition resources immediately
before the transition timing/update handoff. Suggested name:

```text
CGame_SetupActiveGameplayTransitionResources
```

Confirmed behavior:

```text
cgame +0x41C = 0
cgame +0x418 = 0
if cgame +0xC0 == 5 or cgame +0xBC == 6:
  cgame +0x418 = 1

read player-data values through FUN_80028040/FUN_80028048
reset transition subsystem cgame +0x428
configure gLargeResourceManager from cgame +0xC4/+0xD0/+0x11C/+0x120
configure large-resource banks 5..6 from entries at cgame +0x258
configure five large-resource banks from entries at cgame +0x12C
store derived bank ids in a five-entry local map
apply tempo/timing values from DAT_8026E828
wire transition subsystem cgame +0x428 with cgame +0xB8 and cgame +0x3F8
call active controller vtable +0x18
set per-entry state on subsystem cgame +0x400
FUN_801007B8(gUiRootManager, 1)
FUN_800249A8(gManager_802E70A4, FLOAT_802E8308)
cgame +0x420 = 1
```

`FUN_800421A0` derives and commits active gameplay transition timing. Suggested name:

```text
CGame_UpdateActiveGameplayTransitionTiming
```

Confirmed behavior:

```text
duration = UiRootManager_GetSelectionPanelAnimationDuration(gUiRootManager)
transitionTicks = FLOAT_802E8304 * (duration / FLOAT_802E831C)

if cgame +0xB8 == -1:
  if cgame +0xBC == 8 or cgame +0x94 == 0:
    playerCount = FUN_800282CC(gPlayerDataManager)
    inactiveCount = FUN_800282C4(gPlayerDataManager)
    if playerCount - inactiveCount <= 1:
      if cgame +0xBC == 8 or cgame +0x94 != 0:
        FUN_801004F8(gUiRootManager)
      else:
        FUN_801004E8(gUiRootManager)
        transitionTicks = 2000
    else:
      FUN_801004E0(gUiRootManager)
  else:
    FUN_801004E0(gUiRootManager)

halfTicks = transitionTicks >> 1 & 0x7FFF
FUN_8012576C(cgame +0x428)
FUN_80026AC8(gLargeResourceManager, halfTicks)
FUN_80024578(gManager_802E70A4, halfTicks)
FUN_8002483C(gManager_802E70A4, transitionTicks & 0xFFFF)
```

`FUN_80100538` forwards to the selection panel at `gUiRootManager +0x34`. Suggested
name:

```text
UiRootManager_GetSelectionPanelAnimationDuration
```

`FUN_801054D8` computes the underlying panel duration. Suggested name:

```text
CGameUiSelectionPanel_GetAnimationDuration
```

Confirmed behavior:

```text
if panel +0x20 == 1:
  duration = CzanUiManager_GetObjectAnimationDuration(global UI manager, panel[1], 0, 1)
else:
  duration = CzanUiManager_GetObjectAnimationDuration(global UI manager, panel[0], 0, 1)

return duration / FLOAT_802E9214
```

`FUN_80100520` forwards to the same selection panel at `gUiRootManager +0x34`.
Suggested name:

```text
UiRootManager_IsSelectionPanelIdle
```

`FUN_801054B8` is the underlying panel-state test. Suggested name:

```text
CGameUiSelectionPanel_IsIdle
```

Confirmed behavior:

```text
return countLeadingZeros(*(panel +0x14)) >> 5
```

That is `1` only when the panel state word at `+0x14` is zero.

The host has these names and call shapes in code, but the full tick computation still
needs exported `gUiRootManager`, `gLargeResourceManager`, `gManager_802E70A4`, and
`gPlayerDataManager` pointers to drive the exact manager side effects.

`FUN_801004E0`, `FUN_801004E8`, and `FUN_801004F8` are UI-root wrappers for the three
transition branches used by `CGame_UpdateActiveGameplayTransitionTiming`.

```text
FUN_801004E0 -> UiRootManager_SelectDefaultTransition
  calls CGameUiSelectionPanel_SelectDefaultTransition(*(uiRootManager +0x34))

FUN_801004E8 -> UiRootManager_SelectShortTransition
  calls CGameUiSelectionPanel_SelectShortTransition(*(uiRootManager +0x34))

FUN_801004F8 -> UiRootManager_SelectImmediateTransition
  calls CGameUiSelectionPanel_SelectImmediateTransition(*(uiRootManager +0x34))
```

`FUN_8010502C` is the default selection-panel transition branch. Suggested name:

```text
CGameUiSelectionPanel_SelectDefaultTransition
```

Confirmed behavior:

```text
if panel[5] == 0:
  panel[5] = 4
  CzanUiManager_SetObjectGroupEnabled(global UI manager, panel[0], 0)
  CzanUiManager_ResetObjectGroupAnimationTime(0.0, global UI manager, panel[0])
  CGameUi_StartObjectGroupAnimation(panel[0], panel[0x20], 0, 0)
  GlobalCueManager_PlayCue(0.0, gManager_802E70A4, 0x25A, 0, 0)
```

`FUN_801050BC` is the short/alternate selection-panel transition branch. Suggested
name:

```text
CGameUiSelectionPanel_SelectShortTransition
```

Confirmed behavior:

```text
if panel[5] == 0:
  panel[5] = 4
  CzanUiManager_SetObjectGroupEnabled(global UI manager, panel[0], 0)
  CzanUiManager_ResetObjectGroupAnimationTime(0.0, global UI manager, panel[0])
  CGameUi_StartObjectGroupAnimation(panel[0], panel[0x20] + 2, 0, 0)
  GlobalCueManager_PlayCue(0.0, gManager_802E70A4, 0x261, 0, 0)
```

`FUN_80105150` is the immediate selection-panel transition branch. Suggested name:

```text
CGameUiSelectionPanel_SelectImmediateTransition
```

Confirmed behavior:

```text
if panel +0x14 == 0:
  panel +0x14 = 4
  CzanUiManager_SetObjectGroupEnabled(global UI manager, panel[1], 0)
  CGameUi_StartObjectGroupAnimation(panel[1], 0, 0, 0)
  panel +0x20 = 1
  GlobalCueManager_PlayCue(0.0, gManager_802E70A4, 0x274, 0, 0)
```

`FUN_801007B8` forwards to a sub-manager at `uiRootManager +0x44`. Suggested name:

```text
UiRootManager_SetSubManager44Value
```

`FUN_8010D8C0` is the sub-manager setter. Suggested name:

```text
UiRootSubManager44_SetValue
```

Confirmed behavior:

```text
if subManager +0x08 == 1:
  subManager +0x14 = -1
  subManager +0x0C = value
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
FUN_8015EAD0 -> CzanModelOwner_CopyCurrentModelMatrix; copies owner +0x4C
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
  result = CGame_UpdateViewerSetupSelection(cgame)
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
feeds `resourceBundle[7] +0x10` into `LargeResourceManager_ReloadFromDefaultLink`.

`FUN_8003C9A0` is the interactive debug/viewer setup selector used by
`CGame_PrepareManagersAndResources` after the boot resource bundle is applied.
Suggested name:

```text
CGame_UpdateViewerSetupSelection
```

Confirmed behavior:

```text
left/right on controller 4:
  switch selected row cgame +0x14 between STYLE and MODE
  clear cgame +0x48 pulse timer

when selected row is STYLE:
  up/down cycles cgame +0xC8 through allowed style ids 0, 3, 4, 6

when selected row is MODE:
  up/down cycles cgame +0x10 between 0 and 1

cgame +0xC0 = local type table[cgame +0x10]
accept input -> return 1
cancel input -> return -1
otherwise -> return 0

draws one of:
  SSQ VIEWER
  MOTION VIEWER
  VIEWER

then draws:
  STYLE : <label>
  MODE  : <label>

cgame +0x48 increments every frame
```

Important fields:

```text
cgame +0x10 -> mode/type index, 0..1
cgame +0x14 -> selected debug row, 0 STYLE, 1 MODE
cgame +0x48 -> pulse/highlight timer
cgame +0xB8 -> viewer kind; 0 SSQ, 1 MOTION, otherwise generic VIEWER
cgame +0xC0 -> selected local type value/pointer
cgame +0xC8 -> style id
```

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
slot 5 -> resourceBundle[7] -> /ssq/SSQ_CMN*.bin -> LargeResourceManager_ReloadFromDefaultLink
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

`FUN_800221BC` constructs that boot/resource bundle before loading begins. Suggested
name:

```text
BootResourceBundle_Init
```

Confirmed behavior:

```text
resourceBundle[0x126] = vtable/type pointer
resourceBundle[0] = -1
clear resourceBundle[1..] size 0x438
resourceBundle[0x10F] = 0
clear resourceBundle[0x110..0x111] size 8
resourceBundle[0x111] = 0xFFFF
clear resourceBundle[0x112..] size 0x30
resourceBundle[0x113] = -1
resourceBundle[0x115] = -1
resourceBundle[0x11E..0x120] = 0
clear resourceBundle[0x121..] size 8
resourceBundle[0x123..0x125] = 0
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
resourceBundle[7] +0x10 -> gLargeResourceManager via LargeResourceManager_ReloadFromDefaultLink
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
  LargeResourceManager_ResetLoadedState(gLargeResourceManager)
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
LargeResourceManager_ReloadFromDefaultLink
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

The `FUN_8002ADxx/FUN_8002AExx` helpers are input/menu-state queries over one
`0x20`-byte controller record. Suggested names:

```text
FUN_8002AE28 -> InputOrMenuStateManager_TestHeldMask
FUN_8002AE48 -> InputOrMenuStateManager_TestTriggeredMask
FUN_8002ADE0 -> InputOrMenuStateManager_IsConfirmPressed
FUN_8002ADF4 -> InputOrMenuStateManager_IsBackPressed
```

Confirmed behavior:

```text
InputOrMenuStateManager_TestHeldMask(manager, controller, mask):
  return (*(manager + controller * 0x20 +0x08) & mask) != 0

InputOrMenuStateManager_TestTriggeredMask(manager, controller, mask):
  return (*(manager + controller * 0x20 +0x10) & mask) != 0

InputOrMenuStateManager_IsConfirmPressed(manager, controller):
  return (*(manager + controller * 0x20 +0x08) >> 11) & 1

InputOrMenuStateManager_IsBackPressed(manager, controller):
  return (*(manager + controller * 0x20 +0x08) >> 10) & 1
```

The mask helpers use the branchless `(-mask | mask) >> 31` boolean idiom.

`FUN_80131B38` formats a string into a caller-provided buffer through
`RuntimeFormatWrite`. Suggested name:

```text
RuntimeString_FormatBuffer
```

`FUN_80145060`, `FUN_80145104`, and `FUN_80145390` are the debug text draw helpers
used by `CGame_UpdateViewerSetupSelection`.

```text
FUN_80145060 -> DebugText_SetGlyphSize
FUN_80145104 -> DebugText_Draw
FUN_80145390 -> DebugText_ConfigureRenderState
FUN_80144F48 -> DebugText_InitFontBacking
FUN_801A9050 -> DebugText_LoadFontPlanes
FUN_801A8460 -> DebugText_LoadFontPlane
FUN_801A8DE0 -> DebugText_DecodePackedGlyphPlane
```

`DebugText_Draw` resolves glyphs from the font texture globals, configures render state
through `DebugText_ConfigureRenderState(7)`, and emits one quad per glyph.

The font backing init sets glyph size `0x18`, plane count `6`, allocates the backing
buffer, loads/decodes font plane data, then derives font UV scales from header fields
`+0x10/+0x12` divided by `+0x1E/+0x20`. The actual plane load still depends on fixed
Wii source reads through `FUN_801AB8F0` and a `Yay` decompression path; the host now has
the named entry points and the packed glyph-plane decoder, but not the fixed-address
asset read.

`FUN_80188284` lazily creates the `DAT_802E71F8` global UI/frame state object:

```text
GlobalUiFrameState_CreateOnce
```

It allocates `0xF4` bytes, clears `+0x00..+0xE3`, clears `+0xE4/+0xE8/+0xEC`, and
stores the vtable pointer at `+0xF0`.

`FUN_801731E4` allocates the Czan UI manager object-group storage:

```text
CzanUiManager_AllocateObjectGroupStorage
```

It first updates projection globals through `FUN_80170FA4`, then allocates
`groupCapacity` records of size `0x28`, initializes each group as empty
(`+0x00 = -1`, cleared fields), allocates a small four-byte table at manager `+0x08`,
stores pointer capacity at `+0x0C`, and allocates the pointer table at `+0x1C`.

`FUN_80170FA4` derives the global UI projection/view matrices from display config
under `DAT_802E71B8 +0x258`. It calls `FUN_801B0C30`, now named
`BuildPerspectiveProjectionMatrix`, to fill the projection matrix.

`FUN_80168060` only writes `DAT_802E71E0`. Suggested host name:

```text
Runtime_SetSoundArchiveReloadGuard
```

The global is read around `CzanSoundManager_LoadArchive`, where the game temporarily
queries/restores low-level state while replacing the active BRSAR archive. The exact
subsystem meaning should stay cautious until `FUN_80144F34`, `FUN_80144F3C`,
`FUN_80144EF4`, and `FUN_80144EA0` are fully recovered.

`FUN_80141AA4` copies a four-byte color/config block from the global-context
submanager family. Suggested name:

```text
GlobalSubManager274_CopyRgba48
```

Confirmed behavior:

```text
out[0] = *(manager +0x48)
out[1] = *(manager +0x49)
out[2] = *(manager +0x4A)
out[3] = *(manager +0x4B)
```

`FUN_80100510` is a tiny UI-root wrapper. Suggested name:

```text
CGameUiRoot_AdvanceSelectionPanelState
```

Confirmed behavior:

```text
CGameUiSelectionPanel_AdvanceState(*(uiRoot +0x34))
```

`FUN_80105318` advances one selection-panel state machine and starts the matching UI
object group animation. Suggested name:

```text
CGameUiSelectionPanel_AdvanceState
```

Confirmed state transitions:

```text
state 5 -> 6:
  if panel +0x20 == 0:
    disable panel[0] through CzanUiManager_SetObjectGroupEnabled
    reset panel[0] animation time to 0.0
    start panel[0] animation panel[0x20] + 1 through CGameUi_StartObjectGroupAnimation
    play global cue 0x259 through GlobalCueManager_PlayCue
  else:
    disable panel[1]
    start panel[1] animation 1 through CGameUi_StartObjectGroupAnimation
  reset menu presentation grid through gUiRootManager

state 2 -> 3:
  disable panel[2]
  start panel[2] animation 1 through CGameUi_StartObjectGroupAnimation
  play global cue 0x25F through GlobalCueManager_PlayCue
  reset menu presentation grid

state 8 -> 9:
  disable panel[3]
  start panel[3] animation 1 through CGameUi_StartObjectGroupAnimation
  play global cue 0x25D through GlobalCueManager_PlayCue
  reset menu presentation grid
```

The function uses the global Czan UI manager at `DAT_802E71B8 +0x270`.

`FUN_80062D58` is the CGame/UI convenience wrapper used by the state machine above.
Suggested name:

```text
CGameUi_StartObjectGroupAnimation
```

Confirmed behavior:

```text
CzanUiManager_SetObjectGroupAnimationMode(global UI manager, group, arg3)
CzanUiManager_StartObjectGroupAnimation(0.0f, global UI manager, group, animationIndex)
CzanUiManager_SetObjectGroupAnimationResetMode(global UI manager, group, arg2)
```

The three Czan UI manager helpers are:

```text
FUN_80174FE0 -> CzanUiManager_SetObjectGroupAnimationMode
  writes object +0x175 for every child in the group

FUN_80174E2C -> CzanUiManager_StartObjectGroupAnimation
  calls CzanUiObjectInstance_StartAnimation(startFrame, child, animationIndex)
  for every child in the group, then marks uiManager +0x18 dirty when +0x1A is zero

FUN_80174F60 -> CzanUiManager_SetObjectGroupAnimationResetMode
  writes object +0x174 and clears object +0xB1 for every child in the group
```

`FUN_8002425C` resolves and starts a global cue/effect. Suggested name:

```text
GlobalCueManager_PlayCue
```

Confirmed behavior:

```text
initialize stack transform/state through FUN_80166D54
local_7C = startTime
local_78 = FLOAT_802E7BC8
local_74 = FLOAT_802E7BC8
local_88 = -1
local_84 = -1
GlobalCueManager_ResolveCueId(cueId, &local_88, &local_84)
if local_88 != -1:
  return CzanEffectManager_StartEffect(*(DAT_802E71B8 +0x268),
                                       local_88,
                                       local_84,
                                       arg4 != 0,
                                       stackTransform,
                                       arg3,
                                       -1)
return -1
```

`FUN_800F3158` resolves packed/randomized cue ids. Suggested name:

```text
GlobalCueManager_ResolveCueId
```

Confirmed behavior:

```text
outEffectId = -1
outEffectParam = -1

if cueId != -1:
  if cueId bit 0x80000 is set:
    cueId = -1
  if cueId has any high bits in 0xFFFF8000:
    variantBits = cueId & 0x78000
    cueId = cueId & 0x7FFF
    if variantBits != 0:
      variantCount = variantBits >> 15
      cueId += RuntimeRandom_Next15() % variantCount

outEffectId = cueId
```

`FUN_80131C0C` is the RNG used by the cue resolver and other random menu paths.
Suggested name:

```text
RuntimeRandom_Next15
```

It updates `DAT_802E68D8` with:

```text
seed = seed * 0x41C64E6D + 0x3039
return seed >> 16 & 0x7FFF
```

`FUN_8016B270` is now named `CzanEffectManager_StartEffect`, but its internal effect
pool still needs a dedicated decompile before the host can do more than preserve
synthetic effect handles.

`FUN_80100460` forwards from `gUiRootManager` to the menu presentation grid object.
Suggested name:

```text
CGameUiRoot_ResetMenuPresentationGrid
```

Confirmed behavior:

```text
ActiveGameplayControllerBase_ResetMenuPresentationGrid(*(uiRoot +0x38))
```

`FUN_800DA01C` initializes one menu/UI reference-state record. Suggested name:

```text
CGameUiReferenceState_Init
```

Confirmed layout:

```text
state +0x14 = enabled

if enabled:
  +0x08 = trackedChildIndex
  +0x00 = objectGroupHandle
  +0x04 = childObjectIndex
  +0x0C = linkedHandle
  +0x10 = 0
  +0x18 = 0
  +0x1C = 0
  +0x20 = FLOAT_802E8BC8
  CzanUiManager_GetChildObjectDimensions(global UI manager,
                                         objectGroupHandle,
                                         childObjectIndex,
                                         state +0x24,
                                         state +0x28)
  if width/height are negative, multiply by FLOAT_802E8CB0
  for 0x16 color words at +0x30:
    set rgba to 0xFF,0xFF,0xFF,0xFF
```

`FUN_801760EC` is the child dimension query used above. Suggested name:

```text
CzanUiManager_GetChildObjectDimensions
```

`FUN_80176E34` writes one child object's `+0x188` field. Suggested name:

```text
CzanUiManager_SetChildObjectLinkedHandle
```

Confirmed behavior:

```text
object = *( *( *(uiManager +4) + group * 0x28 +0x20 ) + childIndex * 4 )
object +0x188 = linkedHandle
```

The attached `FUN_800CF044` initializes a larger selection/menu UI sub-manager around
object group `+0x96C`. Suggested name:

```text
CGameUiSubManager_InitSelectionReferenceGroups
```

Confirmed flow:

```text
build small position offset vector
run local reset/setup helper FUN_800D67B8(subManager)

CGameUiReferenceState_Init(subManager,
                           enabled = (subManager +0xA90 == 0),
                           group = subManager +0x96C,
                           child = 0x0C,
                           trackedChild = 0x40,
                           linkedHandle = -1)

if enabled:
  +0x9B0 = +0x96C
  +0x9B4 = 10
  +0x9B8 = UiRootManager_CreateReferenceObjectGroup(gUiRootManager, +0x96C, 10, 0x1F)

for six secondary records:
  CGameUiReferenceState_Init(record, 0, -1, -1, -1, -1)
  clear companion state

if +0x4E8 == 2:
  +0xA80 = 1
  +0xA74 = +0x96C
  +0xA78 = 2
  +0xA7C = UiRootManager_CreateReferenceObjectGroup(gUiRootManager, +0x96C, 2, 0x1F)
else:
  +0xA80 = 0

if +0x550 bit 0x2000:
  link +0x970 to child 0x3A of +0x96C
  link +0x98C to child 0x3E of +0x96C
  apply child position offset to children 0x3A and 0x3E
else:
  link +0x970 to child 0x38 of +0x96C
  link +0x98C to child 0x3C of +0x96C

enable groups +0x970 and +0x98C

set +0x188 to 0 on children:
  4, 6, 0x42, 0x0C..0x21, 0x40, 8, 10, 0x44, 0x4A, 0x4B,
  0x38, 0x3A, 0x3C, 0x3E

set +0x188 to 1 on children:
  5, 7, 0x43, 0x22..0x37, 0x41, 9, 0x0B, 0x45, 0x4C, 0x4D,
  0x39, 0x3B, 0x3D, 0x3F
```

The host has the small helpers implemented. The full sub-manager body still needs the
real `gUiRootManager` export before its `UiRootManager_CreateReferenceObjectGroup`
calls can be made accurately.

`FUN_80106964` resets the two-bank menu presentation grid. Suggested name:

```text
ActiveGameplayControllerBase_ResetMenuPresentationGrid
```

Confirmed structure:

```text
if grid +0x04 == 1:
  grid +0x04 = 0
  for childIndex in 0..0x27:
    CzanUiManager_SetObjectTextureFrame(global UI manager,
                                        groupHandle = grid[2 + childIndex],
                                        childObjectIndex = 0,
                                        textureFrame = childIndex,
                                        updateSpriteDimensions = 0)

clear aggregate/timer floats at +0xA8/+0xAC/+0x488/+0x48C

for two banks:
  for 0x28 entries:
    mask = FUN_8012A5DC(0, 1, childIndex)
    if entry is not masked:
      seed per-entry animation bounds from FLOAT_802E923C/FLOAT_802E9240
      enable the entry object group when handle != -1
    else:
      copy cached position pair
```

The packed float/int layout for the two 0xF8-word banks is not fully mutated in the
host yet; the current executable preserves the confirmed texture-frame reset and
group-enable side effects.
