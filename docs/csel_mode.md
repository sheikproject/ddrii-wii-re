# CSelMode

`CSelMode` controls the game mode selection screen inside `CSelect`.

## Functions

```text
CSelMode_Init                    0x8006E2B8
CSelMode_Destroy                 0x8006E314
NoOpVirtualMethod                0x8008D0C4
CSelMode_OnEnter                 0x8006E3B4
CSelMode_OnExit                  0x8006E9B0
CSelMode_Update                  0x8006EAC8
CSelMode_SetInitialSelectedMode  0x8006F2A0
CSelMode_VTable                  0x802B9C90
```

`0x8008D0C4` is a shared empty virtual method stub:

```asm
blr
```

## Init

`CSelMode_Init`:

```c
int CSelMode_Init(int cselMode)
{
    FUN_8008CF54();
    *(undefined ***)(cselMode + 0x12C) = &CSelMode_VTable;
    InitListController_50x14(cselMode + 0x160,
                             listEntryInitCallback,
                             listEntryUpdateCallback,
                             0x50,
                             0xE);
    return cselMode;
}
```

Suggested comment:

```c
/* Initializes CSelMode, the common CSelect sub-screen that controls game mode selection.
   Sets its vtable and initializes a 0xE-entry mode list/controller at +0x160,
   with each entry sized 0x50. */
```

## OnEnter

`CSelMode_OnEnter` links the Czan resource, initializes the 14 mode UI entries, configures
their layout/animation data, sets selectable button state, and marks the screen ready.

Suggested signature:

```c
void CSelMode_OnEnter(int cselMode, undefined4 linkData);
```

Important fields:

```text
+0x010 -> parent/current select data pointer
+0x130 -> modeState / isInitialized during setup
+0x134 -> selectedModeIndex
+0x138 -> renderManager
+0x13C -> uiManager
+0x140 -> characterAssetSystem
+0x144 -> unknownModeStateArray
+0x160 -> modeEntryList[0], 14 entries, 0x50 bytes each
```

`CSelMode_SetInitialSelectedMode` converts the current game mode ID into a button index:

```text
mode ID 1 -> selectedModeIndex 0
mode ID 2 -> selectedModeIndex 1
mode ID 3 -> selectedModeIndex 2
mode ID 6 -> selectedModeIndex 3
other     -> selectedModeIndex 0
```

The defensive error path checks for `-1`, but this appears unreachable with the current
mapping.

Layout tables used during setup:

```text
DAT_802B9A50 -> CSelMode_PositionTable
DAT_802B9B70 -> CSelMode_AnimationTable
DAT_802723A8 -> CSelMode_SoundOrTextIdTable
```

`FUN_80143830(DAT_802E71B8)` chooses a region/language/layout variant and remaps it to
an index for the position/animation tables.

## Update State Machine

`CSelMode_Update` reads input, moves the cursor, updates hover/selection animations,
loads preview character assets, handles confirm/back, writes the selected mode into the
parent select data, and returns the next CSelect state through `FUN_8012A188`.

Suggested comment:

```c
/* Updates the CSelMode game mode selection screen.
   Handles cursor movement, hover selection, confirm/back input, mode highlight animations,
   preview character loading, and writes the selected game mode before leaving the screen. */
```

Known `modeState` values:

```text
1 -> play entry animations / start screen
2 -> wait for entry animation
3 -> normal input/select mode
4 -> start leaving screen
5 -> wait for exit animation/sound
6 -> write selected mode and return next state
```

Input locals in the decompile:

```text
rightPressed   -> FUN_8002AE48(gInputOrMenuStateManager, 4, 8)
leftPressed    -> FUN_8002AE48(gInputOrMenuStateManager, 4, 4)
upPressed      -> FUN_8002AE48(gInputOrMenuStateManager, 4, 1)
downPressed    -> FUN_8002AE48(gInputOrMenuStateManager, 4, 2)
confirmPressed -> FUN_8002ADE0(gInputOrMenuStateManager, 4)
backPressed    -> FUN_8002ADF4(gInputOrMenuStateManager, 4)
```

Update fields:

```text
+0x130 -> modeState
+0x134 -> selectedModeIndex
+0x13C -> uiManager
+0x140 -> characterAssetSystem
+0x144 -> introSoundHandle
+0x148 -> exitSoundHandle
+0x14C -> confirmSoundHandle
+0x150 -> backSoundHandle
+0x154 -> cursorMoveSoundHandle
+0x158 -> previewSoundOrAssetHandle
```

Navigation tables:

```text
DAT_80272348 -> CSelMode_RightNavigationTable
DAT_80272349 -> CSelMode_LeftNavigationTable
DAT_8027234A -> CSelMode_UpNavigationTable
DAT_8027234B -> CSelMode_DownNavigationTable
```

Preview table:

```text
DAT_802723E0 -> CSelMode_PreviewCharacterTable
```

## Choice Table

The selected button maps to the next CSelect state and game mode fields through a table
starting at `0x80272358`.

Ghidra label:

```text
CSelMode_ChoiceTable  0x80272358
```

Suggested plate comment:

```text
CSelMode choice table.
Each selected mode entry is 0x10 bytes / 4 ints:
+0x00 next CSelect state
+0x04 game mode ID
+0x08 game mode sub ID
+0x0C game mode extra ID

Entry index = selectedModeIndex.
```

The game uses the table like this:

```c
base = CSelMode_ChoiceTable + selectedModeIndex * 0x10;

nextSelectState = base[0];
parentSelectData[0] = base[1];  /* gameModeId */
parentSelectData[1] = base[2];  /* gameModeSubId */
parentSelectData[2] = base[3];  /* gameModeExtraId */
```

Visible confirmed values from Ghidra after defining the data as `int[20]`:

```text
selectedModeIndex 0: nextState 0x02, gameModeId 0x01, gameModeSubId 0x01, extra unknown
selectedModeIndex 1: nextState 0x02, gameModeId 0x02, gameModeSubId 0x07, extra unknown
selectedModeIndex 2: nextState 0x1B, gameModeId 0x03, gameModeSubId 0x05, extra unknown
selectedModeIndex 3: nextState 0x15, gameModeId 0x06, gameModeSubId 0x00, extra unknown
selectedModeIndex 4: not fully captured yet
```

The table should remain an `int[20]` or a 5-entry struct array in Ghidra. Creating this
data type does not patch the game; it only makes the decompiler show one coherent table
instead of several misleading byte labels.

## Parent Select Data Writes

On confirm, when `modeState == 6`, `CSelMode_Update` writes the selected mode:

```c
parentSelectData[0] = choice.gameModeId;
parentSelectData[1] = choice.gameModeSubId;
parentSelectData[2] = choice.gameModeExtraId;
```

It also initializes DDR Points related player-data state:

```text
FUN_8011FE9C(gPlayerDataManager + 0x2F1C, 0)
FUN_8011FEA4(gPlayerDataManager + 0x2F1C, 1)
FUN_8011FEAC(gPlayerDataManager + 0x2F1C, parentSelectData[0])
FUN_8011FEB4(gPlayerDataManager + 0x2F1C, parentSelectData[1])
GetDDRPoints(gPlayerDataManager + 0x14A4)
FUN_800F3D9C(gPlayerDataManager + 0x14A4)
```

The returned value from `CSelMode_Update` is the next CSelect state from the choice table,
except for the special selected index `4` path.

