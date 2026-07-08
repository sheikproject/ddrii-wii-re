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

Confirmed runtime call for the Spanish build:

```text
Breakpoint: 0x8006E3B4
r3 = 8148E780  -> CSelMode object
r4 = 904ADFA0  -> linkData
LR = 80046BC8
```

The memory at `904ADFA0` starts with `57 49 49 00` (`"WII\0"`) and matches
`input/DATA/select/select_bin_sp.bin` at file offset `0xA0`. For the host port,
`CSelMode_OnEnter` should therefore receive the Czan link resource starting at
`select_bin_sp.bin + 0xA0`, not necessarily the start of the file.

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

### OnEnter Resource/Object Map

The renamed `CSelMode_OnEnter` shows how the mode-select screen is assembled from the
Czan link resource passed by `CSelect`.

Suggested helper names from this function:

```text
FUN_801606AC -> CzanLinkManager_GetBlock(linkManager, blockIndex)
FUN_801106E8 -> CSelModeEntry_SetPositionOrLayout(entry, objectSlot, animationId, positionData)
FUN_80110B94 -> CSelModeEntry_GetObjectHandle(entry, objectSlot)
FUN_80110B14 -> CSelModeEntry_SetObjectEnabled(entry, objectSlot, childSlot, enabled)
FUN_80110B04 -> CSelModeEntry_SetObjectFlags(entry, objectSlot, flags)
FUN_80176D68 -> CzanRenderManager_LinkTextOrSound(renderManager, objectHandle, baseHandle, id, mode)
FUN_8010EB78 -> UiManager_GetLabelOrTexture(uiManager, objectHandle, index)
FUN_8010EEBC -> UiManager_SetVisibleOrEnabled(uiManager, enabled)
```

Some calls pass extra zero arguments after the useful parameters. The callee decompiles
for `CSelModeEntry_AddUiObject` and `CSelModeEntry_AddChildUiObject` show only the first
two parameters are used; the extra registers appear to be caller convention/noise.

Entry creation map:

```text
entry 0  at cselMode +0x160:
  AddUiObject(block 0)
  Serves as the base/root handle used by other entries.

entry 1  at cselMode +0x1B0:
  AddUiObject(block 2)
  Later PlayObject is called for child/object slots 0..5 using selectedModeIndex.

entry 6  at cselMode +0x340:
  AddUiObject(block 1)
  Creates a shared object group. The returned handle is reused by entries 7..9 and 10..13.

entries 7..9 at cselMode +(7..9)*0x50 +0x160:
  AddChildUiObject(sharedHandleFromEntry6)

entries 10..13 at cselMode +(10..13)*0x50 +0x160:
  AddChildUiObject(sharedHandleFromEntry6)

entry 2 at cselMode +0x200:
  AddUiObject(block 3)

entry 3 at cselMode +0x250:
  AddUiObject(block 4)

entry 4 at cselMode +0x2A0:
  AddUiObject(block 5)

entry 5 at cselMode +0x2F0:
  AddUiObject(block 6)
```

The mode button layout is selected by region/language:

```text
FUN_80143830(DAT_802E71B8) result -> CSelMode table variant
1 -> 0
3 -> 1
4 -> 2
2 -> 3
5 -> 4
0 -> 5
other -> 0
```

For each of the four mode buttons, the function applies the same position/animation
tables to both visual entry groups:

```text
entries 6..9  -> primary mode button/object group
entries 10..13 -> paired/secondary mode button/object group

CSelMode_PositionTable  + variant * 0x30 + modeIndex * 0x0C
CSelMode_AnimationTable + variant * 0x30 + modeIndex * 0x0C
```

Mode entry setup for entries `6..9`:

```text
modeEntryConfig[0] = 0
modeIndexForConfig = modeIndex
modeLabelOrTexture = UiManager_GetLabelOrTexture(uiManager, objectHandle, 0)
SetTransformTriplet(entry, &modeEntryConfig)
Disable child slots 1..4
Enable child slot modeIndex + 1
PlayObject slots 0, 5, and 6 with modeIndex
```

Mode entry setup for entries `10..13`:

```text
modeEntryConfig[0] = 0
modeIndexForConfig = modeIndex
modeLabelOrTexture = -1
SetTransformTriplet(entry, &modeEntryConfig)
Disable child slots 1..4
Enable child slot modeIndex + 1
PlayObject slots 0, 5, and 6 with modeIndex
```

After setup, every entry except entry `1` gets `CSelModeEntry_SetObjectFlags(entry, 0, 0x40)`.
The UI manager is then enabled and `modeState` at `+0x130` is set to `1`.

## Mode Entry Helpers

`CSelMode_Init` creates 14 list entries. Each entry is `0x50` bytes and uses the
callbacks passed into the list/controller initializer.

Known entry callbacks:

```text
0x8006309C -> CSelModeEntry_Init
0x800630D8 -> CSelModeEntry_Update
```

`CSelModeEntry_Init` calls the shared UI-entry base initializer at `0x801102DC`, then
sets the entry vtable at `+0x40` to `PTR_PTR_802BEA38`.

`CSelModeEntry_Update` calls the shared UI-entry update at `0x80110320(entry, 0)`.
If the caller passes a positive short flag/count, it also releases/frees the entry via
`FUN_80144CF0(0, entry)`.

Confirmed entry field map:

```text
+0x14 -> objectHandles[0], first stored UI/model object handle
+0x34 -> objectHandleCount
+0x3C -> uiManager/context used by FUN_80173xxx and FUN_80175xxx helpers
+0x40 -> vtable, PTR_PTR_802BEA38
+0x44 -> transform/layout/state value 0
+0x48 -> transform/layout/state value 1
+0x4C -> transform/layout/state value 2
```

Helper names from the `FUN_80110xxx` family:

```text
FUN_80110524 -> CSelModeEntry_AddUiObject
  If linkBlock != 0, calls CzanUiManager_CreateObjectGroup / FUN_80173414,
  stores the returned handle at entry + 0x14 + objectHandleCount * 4,
  then increments objectHandleCount.

FUN_8011058C -> CSelModeEntry_AddChildUiObject
  If objectId != -1, calls FUN_80173D18(entry->uiManager), stores the returned handle
  in the same handle array, then increments objectHandleCount.

FUN_80110754 -> CSelModeEntry_SetAnimationOrLayout
  If animationId == -1, calls FUN_801751B8(entry->uiManager, handle, animationData).
  Otherwise calls FUN_80175240(entry->uiManager, handle).

FUN_80110B80 -> CSelModeEntry_PlayObject
  Calls FUN_80175448(entry->uiManager, handle).

FUN_80110BF4 -> CSelModeEntry_SetTransformTriplet
  Copies three 32-bit values into entry +0x44, +0x48, +0x4C.
```

These helpers strongly suggest the mode-select background/UI is not raw texture drawing.
`CSelMode_OnEnter` creates UI/model object handles from Czan link blocks and then applies
animation/layout commands through the `FUN_80175xxx` family. The next renderer target
should be those Czan UI/object functions, not direct `selTitle` texture guessing.

## Czan UI Object Group Creation

`FUN_80173414` is the allocator/builder used by `CSelModeEntry_AddUiObject`. Suggested
name:

```text
CzanUiManager_CreateObjectGroup
```

Suggested signature while types are still incomplete:

```c
int CzanUiManager_CreateObjectGroup(void *uiManager, void *linkData, uint flags, int initialAnimIndex);
```

High-level behavior:

```text
1. Finds a free 0x28-byte object-group slot in the Czan UI manager.
2. Treats linkData as a Czan/WII link container.
3. Reads block 1 as the object-group metadata.
4. Allocates an array of object pointers using the object count at metadata +0x08.
5. Reads block 0 as the nested texture/resource link container.
6. For every 0x20-byte object descriptor at metadata +0x0C:
   - descriptor +0x14 == 2: creates an 8x8 dummy/empty object.
   - descriptor +0x14 == 0: creates a texture from a TPL block selected by descriptor +0x10.
   - otherwise: reuses/clones texture state from a previous object index at descriptor +0x12.
7. Allocates a render/object instance, attaches the sprite/texture object, stores the
   descriptor pointer at object +0x198, and stores initialAnimIndex at object +0x16C.
8. Applies initial animation/state depending on flags & 0xFF:
   - 1 or 2: start/reset animation through FUN_80172CC8 and FUN_80171B98.
   - 3 or 4: call FUN_801728E4.
9. Applies descriptor flags from descriptor +0x1A to object fields such as visibility,
   playback, loop/stop behavior, and group status bits.
10. Updates widescreen/screen-size dependent bounds from DAT_802E71B8 + 0x258.
11. Returns/stores the new group index through FUN_8012A18C.
```

Important inferred structures:

```text
CzanUiManager
+0x00 -> max object group count
+0x04 -> object group slot array

CzanUiObjectGroup slot, size 0x28
+0x00 -> current animation/state value, initialized to -1
+0x04 -> object count
+0x08 -> group flags/state byte area
+0x0C -> left/bounds float
+0x10 -> top/bounds float
+0x14 -> right/bounds float
+0x18 -> bottom/bounds float / screen height dependent
+0x1C -> object-group metadata block pointer
+0x20 -> object pointer array
+0x24 -> group status flags byte; bit 0 set after creation, bit 3 means no active visible objects
```

Object descriptor fields inside the group metadata block:

```text
+0x10 -> texture/link block index when descriptor type is 0
+0x12 -> source object index when descriptor reuses a previous object texture
+0x14 -> descriptor type
+0x18 -> two bytes copied to sprite object +0x38/+0x39
+0x1A -> behavior flags
+0x1C -> animation table pointer; initialAnimIndex selects 0x10-byte entries
```

The function creates two runtime objects per descriptor:

```text
0x1D8-byte sprite/texture object, initialized by FUN_8016BA8C
0x1B4-byte UI animation/object instance, initialized by FUN_801711DC
```

This is the path that must be reproduced before the real mode-select background and
animations can render correctly. The next missing pieces are the object instance update
and animation functions: `FUN_801711DC`, `FUN_80171B98`, `FUN_801728E4`, `FUN_80172CC8`,
and the `FUN_80175xxx` layout/animation commands.

## Czan UI Object Instance Init

`FUN_801711DC` initializes the `0x1B4`-byte UI animation/object instance allocated by
`CzanUiManager_CreateObjectGroup`. Suggested name:

```text
CzanUiObjectInstance_Init
```

Suggested signature:

```c
int CzanUiObjectInstance_Init(int objectInstance);
```

This object is paired with the `0x1D8` sprite/texture object created by `FUN_8016BA8C`.
`CzanUiManager_CreateObjectGroup` stores the sprite pointer at instance `+0x24` if it is
empty, stores the owning manager at `+0x20`, stores the descriptor pointer at `+0x198`,
and stores the requested initial animation index at `+0x16C`.

Confirmed/likely fields:

```text
+0x20  -> owning Czan UI manager pointer
+0x24  -> attached 0x1D8 sprite/texture object pointer
+0x30  -> transform/state float block start
+0x74  -> 0x10-byte color block, initialized to FF
+0x84  -> 0x20-byte secondary parameter block, initialized to 0
+0xA4  -> byte initialized to FF
+0xB0  -> playback/animation flag
+0xB1  -> enabled/playing flag, initialized to 1
+0xB2  -> animation flag
+0xB4  -> animation float/timer, initialized to 0.0
+0xC0  -> animation/state value, initialized to 0
+0xC8  -> enabled flag, initialized to 1
+0x134 -> 0x10-byte color block, initialized to FF
+0x144 -> animation counter/timer, initialized to 0
+0x148 -> current animation ID/state, initialized to -2
+0x14C -> active animation entry/index, initialized to 0
+0x150 -> left/bounds float
+0x154 -> top/bounds float
+0x158 -> right/bounds float
+0x15C -> bottom/screen-height bound
+0x16C -> initialAnimIndex/current animation selection
+0x170..0x183 -> behavior/visibility/playback flags
+0x178 -> playback rate/speed, initialized to 1.0
+0x17D -> visible/active flag, initialized to 1
+0x180 -> enabled flag, initialized to 1
+0x181 -> enabled flag, initialized to 1
+0x188 -> handle/state, initialized to -1
+0x198 -> object descriptor pointer
+0x19C -> current animation table value
```

This initializer is mostly defaults. The actual motion likely comes from:

```text
FUN_80172CC8 -> selects/starts an animation entry
FUN_80171B98 -> interprets/runs the animation command stream
FUN_801728E4 -> alternate start/stop/default animation setup
```

### Start Animation

`FUN_80172CC8` selects/starts an animation entry on a `CzanUiObjectInstance`.
Suggested name:

```text
CzanUiObjectInstance_StartAnimation
```

Suggested signature:

```c
void CzanUiObjectInstance_StartAnimation(double startFrame, int objectInstance, int animationIndex);
```

Parameter meaning:

```text
startFrame      -> starting frame/time. The function only runs when this is >= 0.0.
objectInstance  -> 0x1B4-byte Czan UI object instance.
animationIndex  -> index into the descriptor animation table; stored at object +0x16C.
```

Behavior:

```text
1. Clears playback/control flags at +0xB0, +0xB1, +0xB2 and state at +0xC0.
2. Stores animationIndex at +0x16C.
3. Reads the object descriptor at +0x198.
4. Checks animationIndex against descriptor +0x16 animation count.
5. Uses descriptor +0x1C as a table of 0x10-byte animation entries.
6. If the selected entry has a non-null frame/key count at +0x08:
   - sets active animation flag +0x171 = 1.
   - clears +0x176 and +0x172.
   - copies entry +0x0C to object +0x19C.
   - calls FUN_80170DEC(attachedSprite, entry byte +0x03).
   - copies bit 0 of entry byte +0x02 to attachedSprite +0x1C4.
   - initializes animation frame/timer floats at +0xB4, +0xB8, +0xBC.
7. If invalid/no frames, clears +0x171.
```

The timer setup changes when object flag `+0x175` is `>= 3`: in that case it initializes
`+0xB4` and `+0xB8` from the selected animation entry frame/key count plus one. Otherwise
it uses `+0xC4`, `startFrame`, and `0.0`.

This function also reveals another helper:

```text
FUN_80170DEC -> CzanSpriteObject_SetRenderMode(spriteObject, mode)
```

`FUN_80170DEC` maps the animation entry mode byte to sprite render/blend parameters.
Suggested name:

```text
CzanSpriteObject_SetRenderMode
```

Suggested signature:

```c
void CzanSpriteObject_SetRenderMode(int spriteObject, int mode);
```

Parameter meaning:

```text
spriteObject -> 0x1D8 Czan sprite/texture object.
mode         -> render mode byte copied from animation entry +0x03.
```

Known mode mappings:

```text
mode 0:
  +0x178 = 1, +0x17C = 4, +0x180 = 5, +0x184 = 5,
  +0x18C = 4, +0x190 byte = 0, +0x194 = 0, +0x198 = 7, +0x19C byte = 0

mode 8 or 0x1F:
  +0x178 = 1, +0x17C = 4, +0x180 = 1, +0x184 = 5,
  +0x18C = 4, +0x190 byte = 0, +0x194 = 0, +0x198 = 3, +0x19C byte = 0xFF

mode 4:
  +0x178 = 1, +0x17C = 0, +0x180 = 2, +0x184 = 5,
  +0x18C = 4, +0x190 byte = 0, +0x194 = 0, +0x198 = 3, +0x19C byte = 0xFF

mode 0x17:
  +0x178 = 1, +0x17C = 3, +0x180 = 3, +0x184 = 5,
  +0x18C = 4, +0x190 byte = 0, +0x194 = 0, +0x198 = 3, +0x19C byte = 0xFF

mode 10:
  +0x178 = 1, +0x17C = 3, +0x180 = 1, +0x184 = 5,
  +0x18C = 4, +0x190 byte = 0, +0x194 = 0, +0x198 = 3, +0x19C byte = 0xFF
```

All handled modes store the raw mode byte at `spriteObject +0x1A4` after applying the
render parameters. Unknown mode values only update `+0x1A4`.

### Run Animation Script

`FUN_80171B98` interprets the Czan animation command stream for a
`CzanUiObjectInstance`. Suggested name:

```text
CzanUiObjectInstance_RunAnimationScript
```

Suggested signature from call sites:

```c
void CzanUiObjectInstance_RunAnimationScript(int objectInstance, int allowUnknownOpcode);
```

Parameter meaning:

```text
objectInstance      -> 0x1B4 Czan UI object instance.
allowUnknownOpcode  -> nonzero skips/assert-suppresses unknown/default opcodes; zero asserts.
```

The decompiler for this function shows no formal params because the compiler/runtime
context helper `FUN_8012A160` recovers them internally. The call site in
`CzanUiManager_CreateObjectGroup` passes `(objectInstance, 1)`.

The active command pointer is `objectInstance +0x19C`. The stream is float-aligned; the
opcode is read as `(int)*(float *)currentCommand`.

Confirmed command behavior:

```text
0x00 -> end/restart/stop command.
       If object +0x174 == 0, marks playback finished. Otherwise resets +0x19C to the
       selected animation entry start and sets +0xB2.

0x01 -> set object +0x160 from next value.

0x04 -> set texture frame/index base at object +0xA8.
       If object +0x148 == -2 and sprite has texture data, updates sprite +0x34 and
       rebinds texture with BindTextureFromTextureSet.

0x05 -> set object +0x44, then sprite +0x80 = object +0x44 + object +0xE4.
0x06 -> set object +0x3C / sprite +0x78, with special width-based value when entry flag bit 1 is set.
0x07 -> set object +0x40 / sprite +0x7C, with special height-based value when entry flag bit 1 is set.
0x08 -> set object +0x30 / sprite +0x3C.
0x09 -> set object +0x34 / sprite +0x40.
0x0C -> set object +0x38 / sprite +0x44, with owner/flag dependent adjustment.

0x0D -> set object +0x50 / sprite +0x98, multiplied by object +0xFC.
0x0E -> set object +0x48 / sprite +0x90, multiplied by object +0xF4.
0x0F -> set object +0x4C / sprite +0x94, multiplied by object +0xF8.

0x10 -> set object +0x5C / sprite +0xA4.
0x11 -> set object +0x54 / sprite +0x9C.
0x12 -> set object +0x58 / sprite +0xA0.

0x15 -> wait command: converts next value to integer ticks, stores it at +0xC0,
        sets +0xB0 = 1, and keeps processing in wait mode.
0x16 -> wait command: converts next value to integer ticks, stores it at +0xC0,
        sets +0xB0 = 0.

0x18 -> set byte +0xA4, then calls FUN_80172FC0.
0x19 -> appends a value to a dynamic int list at +0xCC, count byte +0xCA.
0x1A -> skip one value.
0x1B -> skip two values.
0x1C -> skip three values.
0x1D -> skip three values.
0x1E -> calls CzanSpriteObject_SetRenderMode(sprite, next value).
0x1F, 0x20, 0x21 -> skip one value.

0x22 -> set object +0x60 / sprite +0xA8.
0x23 -> set object +0x64 / sprite +0xAC.
0x24 -> set object +0x68 / sprite +0xB0.

0x25 -> set all four red bytes in color block +0x74/+0x78/+0x7C/+0x80, then FUN_80172FC0.
0x26 -> set all four green bytes, then FUN_80172FC0.
0x27 -> set all four blue bytes, then FUN_80172FC0.
0x28 -> set color byte +0x74, then FUN_80172FC0.
0x29 -> set color byte +0x75, then FUN_80172FC0.
0x2A -> set color byte +0x76, then FUN_80172FC0.
0x2B -> set color byte +0x78, then FUN_80172FC0.
0x2C -> set color byte +0x79, then FUN_80172FC0.
0x2D -> set color byte +0x7A, then FUN_80172FC0.
0x2E -> set color byte +0x7C, then FUN_80172FC0.
0x2F -> set color byte +0x7D, then FUN_80172FC0.
0x30 -> set color byte +0x7E, then FUN_80172FC0.
0x31 -> set color byte +0x80, then FUN_80172FC0.
0x32 -> set color byte +0x81, then FUN_80172FC0.
0x33 -> set color byte +0x82, then FUN_80172FC0.

0x36 -> computes normalized X/pivot value from sprite width and next value, writes sprite +0xD8.
0x37 -> computes normalized Y/pivot value from sprite height and next value, writes sprite +0xDC.
0x39 -> set object +0x84 / sprite +0xE0.
0x3A -> set object +0x88 / sprite +0xE4.
0x3B -> set object +0x8C / sprite +0xE8 using width-relative adjustment.
0x3C -> set object +0x90 / sprite +0xEC.
0x3D -> set object +0x94 / sprite +0xF0.
0x3E -> set object +0x98 / sprite +0xF4 using height-relative adjustment.
0x3F -> set object +0x9C / sprite +0xF8 using width-relative adjustment.
0x40 -> set object +0xA0 / sprite +0xFC using height-relative adjustment.

0x41 -> set sprite width at sprite +0x100.
0x42 -> set sprite height at sprite +0x108.
0x43, 0x44, 0x45, 0x46 -> skip one value / currently no visible side effect.
```

When the object is in wait mode (`+0xC0 != 0`), the interpreter increments `+0xB4`,
decrements `+0xC0`, and returns the current `+0xB1` state through the context helper.

This function reveals another important helper:

```text
FUN_80172FC0 -> CzanUiObjectInstance_ApplyColorBlocks
```

### Apply Color Blocks

`FUN_80172FC0` copies the object-instance color bytes into the attached sprite's four
RGBA color blocks. Suggested name:

```text
CzanUiObjectInstance_ApplyColorBlocks
```

Suggested signature:

```c
void CzanUiObjectInstance_ApplyColorBlocks(int objectInstance);
```

Parameter meaning:

```text
objectInstance -> 0x1B4 Czan UI object instance.
```

Behavior:

```text
1. Loops 4 times, one color block per sprite vertex/corner/color slot.
2. If object +0x182 == 1, RGB comes from alternate color block at object +0x134.
   Otherwise RGB comes from primary color block at object +0x74.
3. If object +0x183 == 1, alpha is scaled:
   alpha = (alternateAlpha / 255.0) * objectAlpha
   where alternateAlpha is object +0x137 for the current block and objectAlpha is +0xA4.
4. If object +0x183 != 1, alpha is copied directly from object +0xA4.
5. Writes RGBA into attached sprite +0xB4, +0xB8, +0xBC, +0xC0.
```

The animation interpreter calls this after color opcodes `0x18`, `0x25..0x33`.

## Czan Sprite Object Init

`FUN_8016BA8C` initializes the `0x1D8`-byte sprite/texture object attached to a
`CzanUiObjectInstance`. Suggested name:

```text
CzanSpriteObject_Init
```

Suggested signature:

```c
void CzanSpriteObject_Init(int spriteObject);
```

This object holds the texture resource created from TPL data plus the render parameters
that animation commands modify.

Confirmed/likely fields:

```text
+0x20 -> enabled/active flag
+0x24 -> texture slot/texture-set pointer
+0x28 -> texture header pointer
+0x30 -> texture resource handle or texture slot index
+0x34 -> texture index/frame index used by BindTextureFromTextureSet
+0x38 -> x anchor/alignment mode, default 4
+0x39 -> y anchor/alignment mode, default 0
+0x3A -> owns/uses texture flag, default 1
+0x3B -> texture ready/bound flag
+0x3C..0x74 -> transform/channel floats, initialized to 0.0
+0x78 -> half width / x extent
+0x7C -> half height / y extent
+0x84 -> widescreen adjusted x extent in one creation path
+0x88 -> screen-height adjusted y extent in one creation path
+0x90..0x98 -> scale-like floats, initialized to 1.0
+0xB4..0xC3 -> four RGBA color blocks, all initialized to FF
+0x100 -> width
+0x108 -> height
+0x16C -> byte flag
+0x170 -> render/blend mode, default 3
+0x174 -> enabled flag, default 1
+0x178 -> render parameter, default 1
+0x17C -> render parameter, default 4
+0x180 -> render parameter, default 5
+0x184 -> render parameter, default 5
+0x188 -> byte flag
+0x18C -> render parameter, default 4
+0x198 -> render parameter, default 7
+0x1B8 -> float parameter, initialized to 0.0
+0x1BC -> float parameter, initialized to 0.0
+0x1C8 -> callback/table pointer filled with DAT_802EF3A8 by object group creation
+0x1CC -> callback/table pointer filled with DAT_802EF368 by object group creation
+0x1D4 -> dummy/special object flag; set to 1 for descriptor type 2 dummy objects
```

In `CzanUiManager_CreateObjectGroup`, descriptor type `0` fills this object with a real
TPL texture and calls `BindTextureFromTextureSet(textureSlot, spriteObject, textureIndex)`.
Descriptor type `2` creates an 8x8 dummy object and sets `+0x1D4 = 1`. Other descriptor
types reuse texture state from a previous sprite object.

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
