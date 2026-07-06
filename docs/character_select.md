# Character Select Notes

## Runtime Watch Addresses

From `outputs/dmw_watch_labels.csv`.

### Gameplay Selection State

```text
0x8053F748  Character (Gameplay)
0x8053F74C  Character Costume (Gameplay)
0x8053F758  Character Mii Head (Gameplay)

0x8053F9FC  Character (Boss Songs)
0x8053FA00  Character Costume (Boss Songs)
0x8053FA0C  Character Mii Head (Boss Songs)

0x8053FA28  Backdancer (Gameplay)
0x8053FA2C  Backdancer Costume (Gameplay)
0x8053FA38  Backdancer Mii Head (Gameplay)

0x8053FA54  Backdancer 2 (Gameplay)
0x8053FA58  Backdancer 2 Costume (Gameplay)
0x8053FA64  Backdancer 2 Mii Head (Gameplay)
```

### Character/Model State

```text
0x805753D4  Character ID
0x805753DB  Costume Slot ID
0x8057547F  Character is Blocked?
0x8057548B  Shadow Model
```

### Select-Screen State

```text
0x808A224C  Selected Character
0x808A2253  Selected Costume
0x808A2260  Character Mii Head

0x808A227C  Selected Backdancer
0x808A2280  Backdancer Costume
0x808A2290  Backdancer Mii Head

0x808A22AC  Selected Backdancer 2
0x808A22B0  Backdancer 2 Costume
0x808A22C0  Backdancer 2 Mii Head
```

### Character Select Cursor/Page

```text
0x8148E884  Character Slot
0x8148E88C  Character Page
0x8148EBA4  Character Slot Highlighted
0x8148EBAC  Character Select Page
```

## Static Clues In `main.dol`

Relevant strings/classes found in the DOL:

```text
CselChara
CselMode
CselPlayer
CselMii
CSelect
CselMyBackDancer
CselDance
CselBase
SELECT DANCE
%cCHARA
```

## Ghidra Names Added

```text
FUN_8006C588 -> GetCharacterIdForCurrentSelectSlot
FUN_8006C658 -> SetupCharacterSelectPageSlots
FUN_8006BEE8 -> RefreshCharacterSelectCursorAndPanels
FUN_8006C16C -> GetCharacterGridIndexFromCharacterId
FUN_8006E218 -> SetSelectedCharacterFromMenuState
main character-select state function -> UpdateCharacterSelectState
```

Core flow:

```text
UpdateCharacterSelectState
  -> SetSelectedCharacterFromMenuState
      -> writes selected character/costume into select-screen state

SetupCharacterSelectPageSlots
  -> builds page icons/panels and selectable flags

GetCharacterIdForCurrentSelectSlot
  -> maps slot index to actual character ID
```

## Suggested Breakpoint Workflow

To find character select writers:

```text
Set write watchpoint: 0x808A224C  Selected Character
Change highlighted/selected character in character select.
Record PC, LR, r3-r8.
```

Then repeat for:

```text
0x808A2253  Selected Costume
0x805753D4  Character ID
0x8057547F  Character is Blocked?
0x8148EBA4  Character Slot Highlighted
0x8148EBAC  Character Select Page
```

The best first target is `0x808A224C` because it should change when the visible selected character changes. `0x8053F748` is likely later/persistent gameplay selection state, so it is useful for confirming what gets committed after leaving the select screen.

## Findings

### `0x80066574` Reads Selected Character

Watchpoint on `0x808A224C` broke at:

```text
PC  0x80066574
LR  0x8006653C
r3  0x808A2224
r4  0x8036E290
```

Instruction:

```text
80066574  lwz r0,0x28(r3)
```

With `r3 = 0x808A2224`, this reads:

```text
0x808A2224 + 0x28 = 0x808A224C  Selected Character
```

The surrounding code then stores that value at `r17 + 0x1258`.
If `r17/r30` is `0x8053EAF0`, that destination is:

```text
0x8053EAF0 + 0x1258 = 0x8053F748  Character (Gameplay)
```

This function likely commits selected character/costume data from select-screen state into gameplay state.

### `0x8006E27C` Writes Selected Character

Write watchpoint on `0x808A224C` broke at:

```text
PC  0x8006E27C
LR  0x80069088
r3  0x8148D660
r4  0x808A2224
r5  0x00000049
r6  0x000000CB
r7  0x0000004D
r8  0x00000015
```

The selected character was Yuni, documented as ID `203` / `0xCB`.

Instruction:

```text
8006E27C  stw r6,0x28(r4)
```

With `r4 = 0x808A2224`, this writes:

```text
0x808A2224 + 0x28 = 0x808A224C  Selected Character
```

Surrounding logic:

```text
8006E228  lwz   r6,0x1258(r3)
8006E22C  cmpwi r6,0x47
8006E234  stw   r5,0x28(r4)   ; special case 0x47 -> 0x49
8006E240  cmpwi r6,0x48
8006E24C  stw   r0,0x28(r4)   ; special case 0x48 -> 0x4A
8006E254  addi  r0,r6,-0x50
8006E258  cmplwi r0,9
8006E264  stw   r6,0x28(r4)   ; 0x50-0x59 range
8006E27C  stw   r6,0x28(r4)   ; default selected character write
```

Likely function role: apply/restore selected character from a cursor or menu object into the select-screen state object.

### `0x80068F04` Writes Object Selected Character ID

Write watchpoint on `0x8148E8B8` broke at:

```text
PC  0x80068F04
LR  0x80068EE4
r3  0x000000C8
r4  0x00000000
r5  0x00000002
r6  0x00000000
r7  0x00000000
r8  0x8148D660
```

Selected character was Emi, documented as ID `200` / `0xC8`.

Instruction:

```text
80068F04  stw r3,0x1258(r8)
```

With `r8 = 0x8148D660`, this writes:

```text
0x8148D660 + 0x1258 = 0x8148E8B8
```

The return value from `0x8006C588` is stored as the selected character ID:

```text
80068EBC  mr    r3,r30
80068EC0  lwz   r5,0x1228(r30)
80068ED0  lwz   r0,0x122C(r30)
80068ED4  lwz   r4,0x1224(r30)
80068EE0  bl    0x8006C588
80068F04  stw   r3,0x1258(r8)
```

Next target: analyze `0x8006C588`, which likely resolves current page/slot/selection into a character ID.

### `0x8006C588` Resolves Select Slot To Character ID

Ghidra decompile:

```c
undefined4 GetCharacterIdForCurrentSelectSlot(undefined4 param_1, undefined4 param_2)
{
  switch (param_2) {
  case 0:  return 10000;
  case 1:  return 0x2712;
  case 2:  return 0x2711;
  case 3:  return 0x50;
  case 4:  return 200;
  case 5:  return 0xC9;
  case 6:  return 0xCB;
  case 7:  return 0xCC;
  case 8:  return 0xCA;
  case 9:
  case 10:
  case 11: return 0xFFFFFFFF;
  case 12: return 0xCD;
  case 13: return 0xCE;
  case 14: return 0xCF;
  case 15: return 0xD0;
  case 16:
  case 17:
  case 18:
  case 19:
  case 20: return 0xFFFFFFFF;
  default: return 0xFFFFFFFF;
  }
}
```

Known IDs from `Characters IDs (In-Game).md`:

```text
0x52 /  82  Mii
0xC8 / 200  EMI
0xC9 / 201  DISCO
0xCA / 202  RUBY
0xCB / 203  YUNI
0xCC / 204  RAGE
0xCD / 205  Rena
0xCE / 206  NAOKI
0xCF / 207  jun
0xD0 / 208  U1
```

Current slot map:

```text
slot 0x00 -> 0x2710 / 10000
slot 0x01 -> 0x2712
slot 0x02 -> 0x2711
slot 0x03 -> 0x0052 / 82 / Mii
slot 0x04 -> 0x00C8 / 200 / EMI
slot 0x05 -> 0x00C9 / 201 / DISCO
slot 0x06 -> 0x00CB / 203 / YUNI
slot 0x07 -> 0x00CC / 204 / RAGE
slot 0x08 -> 0x00CA / 202 / RUBY
slot 0x09 -> invalid
slot 0x0A -> invalid
slot 0x0B -> invalid
slot 0x0C -> 0x00CD / 205 / Rena
slot 0x0D -> 0x00CE / 206 / NAOKI
slot 0x0E -> 0x00CF / 207 / jun
slot 0x0F -> 0x00D0 / 208 / U1
slot 0x10 -> invalid
slot 0x11 -> invalid
slot 0x12 -> invalid
slot 0x13 -> invalid
slot 0x14 -> invalid
```

### `0x800678B4` Writes Current Character Select Slot

Breakpoint hit while moving in the character menu:

```text
PC   0x800678B4
r0   0x00000004
r3   0x00000000
r4   0x00000004
r5   0x00000000
r7   0x00000000
r26  0x00000001
r30  0x8148D660
```

Instruction:

```text
800678B4  stw r0,0x1224(r30)
```

With `r30 = 0x8148D660`, this writes:

```text
0x8148D660 + 0x1224 = 0x8148E884  Character Slot
```

At this hit, `r0 = 4`, so the cursor/current slot was set to slot `4` / EMI.

### Slot Availability Flags

The cursor movement code checks an availability flag before accepting a slot:

```c
*(int *)(iVar4 + 0x12EC + *(int *)(iVar4 + 0x122C) * 0x30 + *(int *)(iVar4 + 0x1224) * 4)
```

If that value is `0`, the cursor keeps moving/skips the slot. This is why changing the icon/slot display table alone can show a character portrait in a blank box, but the cursor still cannot stop there.

Observed object base:

```text
iVar4 = 0x8148D660
```

For page `1`, local slot `4`, the availability flag address is:

```text
0x8148D660 + 0x12EC + 1 * 0x30 + 4 * 4 = 0x8148E98C
```

Live tests:

```text
Changing 0x8148E94C from 1 to 0 makes the ALL RANDOM slot unselectable.
Changing a blank slot flag from 0 to 1 lets the cursor land on that blank slot.
```

Conclusion:

```text
availability flag 1 = selectable
availability flag 0 = skipped/blocked
```

The enabled blank slot still did not load a character until its real slot ID was mapped in
`GetCharacterIdForCurrentSelectSlot`. A breakpoint on `0x8006C588` while confirming that
blank reported:

```text
r4 = 0x10
```

So that blank resolves through `case 0x10`.

Candidate character-ID patch for that blank:

```text
0406C628 386000CD  ; slot 0x10 returns 0xCD / Rena instead of -1
```

Result:

```text
Worked after confirming the Gecko code was applied. The instruction at `0x8006C628`
changed from `3860FFFF` (`li r3,-1`) to `386000CD` (`li r3,0xCD`).

With the slot availability flag manually changed from `0` to `1`, the blank slot became
selectable and loaded Rena.
```

### Character Page Layout Table

`SetupCharacterSelectPageSlots` copies a 7 page x 12 slot display table from:

```text
RAM  0x802721D0
file 0x0026E2D0
```

The value `0x15` means a blank display/panel slot. This table controls what the page/panel
shows, but it does not decide the final loaded character by itself.

Current decoded page layout:

```text
page 0: 10 11 12 0B 01 00 04 03 02 15 15 15
page 1: 05 08 07 06 15 15 15 15 15 15 15 15
page 2: 17 18 19 1A 1B 1C 1D 1E 15 1F 20 15
page 3: 00 01 02 03 04 15 15 15 15 15 15 15
page 4: 00 01 02 03 04 05 06 07 08 09 0A 0B
page 5: 0C 0D 0E 0F 10 11 12 13 14 15 16 17
page 6: 00 01 02 03 04 05 06 07 08 09 15 15
```

Exported as:

```text
data/select/character_page_layouts.json
include/select/character_select_pages.h
src/select/character_select_pages.c
```

## Questions To Answer

```text
Which function writes selected character during cursor movement?
Which function commits selected character into gameplay state?
Which table maps page/slot to character ID?
Which check marks characters as blocked/locked?
Can blocked characters be made selectable by patching the check or table?
```

## PC Port Documentation Notes

The character select flow currently appears to be split into distinct systems:

```text
1. Page/icon setup
2. Slot availability
3. Slot index -> character ID resolution
4. Commit selected character/costume into select/game state
```

This matters for a future PC port because replacing or expanding the character grid
requires updating all of these layers. Changing only one layer can produce partial results:

```text
Icon table only:
  The panel portrait changes, but the cursor may still skip the slot and no character loads.

Availability flag only:
  The cursor can land on the slot, but no character loads if the slot resolves to -1.

Character ID switch only:
  The slot can return a real character ID, but the cursor cannot reach it if the availability flag is 0.
```

Confirmed working combination:

```text
availability flag = 1
GetCharacterIdForCurrentSelectSlot(slot 0x10) returns 0xCD / Rena
```

Result:

```text
The previously blank slot became selectable and loaded Rena.
```

### Current Conceptual Model

```text
SetupCharacterSelectPageSlots
  Builds/selects visible panel data and initializes slot availability flags.

RefreshCharacterSelectCursorAndPanels
  Updates panel/cursor state and reads selected slot/character data.

GetCharacterIdForCurrentSelectSlot
  Converts a slot index into the character ID used by gameplay/select state.

UpdateCharacterSelectState
  State-machine handler for character select. On confirm, calls
  SetSelectedCharacterFromMenuState.

SetSelectedCharacterFromMenuState
  Writes final selected character/costume into select-screen state.
```

### Important Fields

Observed `CselChara` object base during testing:

```text
0x8148D660
```

Useful offsets:

```text
+0x0010  pointer to select-screen/player state
+0x072C  first panel/cursor object
+0x121C  state machine state
+0x1220  player/index
+0x1224  local selected slot
+0x1228  costume/variant index?
+0x122C  page/slot offset
+0x1258  resolved selected character ID
+0x1264  selected costume/variant
+0x127C  confirm/menu substate
+0x12EC  selectable flags table
```

Selectable flag formula:

```text
flag_address = CselChara + 0x12EC + pageOffset * 0x30 + localSlot * 4
```

Values:

```text
1 = selectable
0 = skipped/blocked
```

## Confirmed Facts

These are confirmed by Ghidra decompile and/or Dolphin runtime tests:

```text
GetCharacterIdForCurrentSelectSlot is at 0x8006C588.
It maps character select slot index to in-game character ID.
Invalid/blank slots return -1.
```

Confirmed slot -> character mappings:

```text
slot 0x03 -> 0x52 /  82 / Mii
slot 0x04 -> 0xC8 / 200 / EMI
slot 0x05 -> 0xC9 / 201 / DISCO
slot 0x06 -> 0xCB / 203 / YUNI
slot 0x07 -> 0xCC / 204 / RAGE
slot 0x08 -> 0xCA / 202 / RUBY
slot 0x0C -> 0xCD / 205 / Rena
slot 0x0D -> 0xCE / 206 / NAOKI
slot 0x0E -> 0xCF / 207 / jun
slot 0x0F -> 0xD0 / 208 / U1
```

Confirmed invalid slots:

```text
slot 0x09 -> -1
slot 0x0A -> -1
slot 0x0B -> -1
slot 0x10 -> -1
slot 0x11 -> -1
slot 0x12 -> -1
slot 0x13 -> -1
slot 0x14 -> -1
```

Confirmed character select systems:

```text
Icon/panel display data is separate from actual character selection.
Selectable flags are separate from actual character selection.
The actual loaded character comes from GetCharacterIdForCurrentSelectSlot.
```

Confirmed selectable flag behavior:

```text
Changing 0x8148E94C from 1 to 0 made ALL RANDOM unselectable.
Changing a blank slot flag from 0 to 1 allowed the cursor to land on it.
```

Confirmed working blank-slot experiment:

```text
Patch slot 0x10 return value at 0x8006C628 from -1 to 0xCD.
With the corresponding availability flag set to 1, the blank slot loaded Rena.
```

## Exported Reconstruction Files

The slot resolver has been exported in a decomp/recomp-style layout:

```text
include/game/character_ids.h
include/select/csel_chara.h
include/select/character_select_pages.h
src/select/character_select.c
src/select/character_select_pages.c
data/select/character_slot_table.json
data/select/character_page_layouts.json
```
