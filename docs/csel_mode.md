# CSelMode

`CSelMode` controls the game mode selection screen inside `CSelect`.

Important resource split:

```text
select/select_cmn.bin    -> common select background/model/animation resources.
                            Confirmed blocks include ZMB GC / ZAB GC data, not the
                            CAE_WII/TPL-only sprite path used by CSelMode entries.

select/select_bin_sp.bin -> region-specific CSelect/CSelMode UI sprite groups.
                            CSelMode_OnEnter receives the nested WII resource at
                            select_bin_sp.bin + 0xA0 for the Spanish build.
```

So the main-menu background cannot be fixed by drawing every `select_bin_sp.bin`
object group. The next renderer target for the real background is the `ZMB GC` /
`ZAB GC` model-animation path from `select_cmn.bin`.

`FUN_80055DF4` is a confirmed helper in that path. Suggested name:

```text
FindZmbZabSectionByTag
```

Observed behavior:

```c
int FindZmbZabSectionByTag(int sectionBase, int outPayloadPtr);
```

It loops over seven known section/tag strings beginning at `PTR_s_DRAW__802B9684`.
For each tag, it gets the tag length with `FUN_801297D8`, compares that tag against
`sectionBase` with `FUN_8012F388`, and returns the matching index. If `outPayloadPtr`
is nonzero, it stores `sectionBase + tagLength` through `FUN_801331C0`, which means
the caller receives a pointer to the payload after the matched tag text.

This function is probably not the final model loader. Its callers are the important
next targets, because they decide what to do with the matched `DRAW`/model-animation
sections.

`FUN_8004F3BC` is one confirmed caller/parser. Suggested name:

```text
ParseZmbZabSectionNameAndFlags
```

Suggested signature while types are incomplete:

```c
uint ParseZmbZabSectionNameAndFlags(
    int *linkManager,
    int sectionIndex,
    int *sectionSpanOut,
    int *sectionTagIndexOut,
    char *sectionName);
```

Confirmed behavior:

```text
1. Clears *sectionSpanOut.
2. Gets the current section/block pointer through FUN_80160618.
3. Calls FindZmbZabSectionByTag(sectionPointer, stackPayloadPtr).
4. Stores the returned tag index in *sectionTagIndexOut.
5. Copies/extracts the section name with FUN_80055D34(stackPayloadPtr, sectionName).
6. Strips known suffixes from sectionName and encodes them into high flag bits:
   first suffix group:
     DAT_802713EC -> 0x080000
     DAT_802713F0 -> 0x100000
     DAT_802713F4 -> 0x200000
   second suffix group:
     DAT_802713F8 -> 0x040000
     DAT_802713FC -> 0x020000
   four-character suffix:
     DAT_80271400 -> 0x010000
7. Advances sectionSpanOut by 1 for tag index 2, otherwise by 2.
8. Scans following sections while FUN_80055D34(nextSection, 0) returns 2.
   Each continuation increments sectionSpanOut and a 16-bit repeat/extra count.
9. Returns:
   low bits  -> base section mask, 1 for tag index 2, 3 otherwise
   bits 2-17 -> continuation count
   high bits -> stripped suffix flags
```

This looks like the function that normalizes a `ZMB/ZAB` section name, identifies the
section tag type, and computes how many following sections belong to the same logical
entry.

`FUN_80053EC0` is the larger caller that consumes those parsed sections. Suggested name:

```text
LoadZmbZabModelEntryList
```

The decompile is polluted by context-helper register recovery, but the control flow is
clear:

```text
1. Sets a CzanLinkManager link to the incoming WII container.
2. First pass over all WII blocks:
   - calls ParseZmbZabSectionNameAndFlags for each logical section
   - advances by sectionSpanOut
   - counts how many 0x270-byte model-entry records are needed
3. Allocates count * 0x270 + 0x10 bytes and initializes a list/controller with
   callbacks FUN_8004D4E8 / FUN_8004D6BC.
4. Second pass over the same sections:
   - parses section flags/name/tag again
   - if returned mask bit 0 is set, consumes the primary block
   - if returned mask bit 1 is set, consumes the secondary block
   - uses bits 2..15 as the number of following continuation blocks
   - calls the entry vtable method at +0x10 to initialize the entry from the
     primary/secondary blocks
   - calls vtable method +0x20 for each continuation block
   - optionally calls vtable method +0x24 to start/setup animation when continuation
     data exists and the entry mode flag allows it
5. Stores the normalized section name at entry +0x74.
6. Stores the original packed flags at entry +0x70.
7. Groups entries by tag index in manager arrays at the active bank selected by
   byte manager +0x2C.
8. Scans model object names for references:
   - `OBJSET_` references are matched against loaded entries by name
   - `03_XX` references are tracked separately
   - a 0x104-name table at DAT_802BD620 is indexed for many object-name matches
   - `LIGPOS_#` and `LIGTAR_#` record light position/target object indices
   - `COL_pos_` records a color-position object index
9. Builds per-entry OBJSET reference arrays at entry +0xB4/+0xB8 after counting how
   many references each entry receives.
```

Important entry fields seen here:

```text
entry size 0x270
+0x000 -> vtable/object pointer used by FUN_8015Dxxx model helpers
+0x070 -> packed flags returned by ParseZmbZabSectionNameAndFlags
+0x074 -> normalized section/name string copied from parser output
+0x0B4 -> allocated int array of OBJSET reference indices
+0x0B8 -> OBJSET reference count
+0x26C -> temporary OBJSET reference counter, reset after array allocation
+0x258 -> gManager_802E70A4
+0x25C -> gLargeResourceManager
+0x268 -> vtable / method table used for entry setup
```

This is much closer to the real `select_cmn.bin` background/model path than the Czan
UI sprite group code. The next useful targets are the entry callbacks/methods:
`FUN_8004D4E8`, `FUN_8004D6BC`, and the vtable methods reached through
`entry[0x1B] + 0x10/+0x20/+0x24`.

`FUN_8004D4E8` is the constructor/initializer for each `0x270` model entry record.
Suggested name:

```text
ZmbZabModelEntry_Init
```

Confirmed initialization:

```text
calls FUN_80058D90 first
entry +0x06C -> vtable PTR_PTR_802B9624
entry +0x070 -> packed parser flags, initialized to 0
entry +0x074 -> normalized section/model name buffer, cleared for 0x40 bytes
entry +0x0B4 -> OBJSET reference index array pointer, initialized to 0
entry +0x0B8 -> OBJSET reference count, initialized to 0
entry +0x0BC -> first 4 bytes set to FF, then next 4 bytes zeroed by overlapping clears
entry +0x0C0 -> 4 bytes set to FF
entry +0x0C4..+0x160 -> first table of 40 int slots initialized to -1
entry +0x164..+0x200 -> second table of 40 int slots initialized to -1
entry +0x204..+0x250 -> paired 10-entry vector/field table initialized to 0
entry +0x254 -> initialized to 1
entry +0x258 -> manager pointer slot, initialized to 0 then filled by loader with gManager_802E70A4
entry +0x25C -> manager pointer slot, initialized to 0 then filled by loader with gLargeResourceManager
entry +0x260/+0x264 -> initialized to 0
entry +0x268 -> 2-byte state/flags field cleared
entry +0x26C -> temporary OBJSET reference counter, initialized to 0
```

The two `-1` tables and the paired `0x204..0x250` zero table are likely per-bone,
per-object, or per-material lookup caches used after ZMB/ZAB model data is loaded.

`FUN_8004D6BC` is the paired destructor/release callback. Suggested name:

```text
ZmbZabModelEntry_Destroy
```

Confirmed behavior:

```text
if entry is non-null:
  reset entry +0x06C to PTR_PTR_802B9624
  if entry +0x0B4 is nonzero:
    MemoryPool_Free(0, entry +0x0B4)
  entry +0x0B4 = 0
  entry +0x0B8 = 0
  FUN_80058E18(entry, 0)
  if releaseMode > 0:
    MemoryPool_Free(0, entry)
return entry
```

So `+0x0B4/+0x0B8` is definitely owned temporary/reference-list storage, not a
borrowed pointer from the model resource.

`FUN_80058E18` is the deeper CtsStageObj destructor/reset wrapper. Suggested name:

```text
CtsStageObj_Destroy
```

Confirmed behavior:

```text
if entry != 0:
  entry +0x6C = PTR_PTR_802B97A8
  calls CtsStageObj_ResetModelBlocks through PTR_CtsStageObj_ResetModelBlocks_802B97BC
  if releaseMode > 0:
    MemoryPool_Free(0, entry)
return entry
```

`FUN_80058D90` initializes the base CtsStageObj fields before the derived ZMB/ZAB
entry constructor changes the vtable at `+0x6C`. Suggested name:

```text
CtsStageObj_InitBase
```

Confirmed behavior:

```text
entry[0x1B] = PTR_PTR_802B97A8
entry[0] = 0
entry[1] = -1
entry[2] = -1
entry[3] = 0
entry[4] = 0
entry[5] = FLOAT_802E84D8
entry[6] = FLOAT_802E84D8
thunk_FUN_801B0120(entry + 7)
clears 0x18 bytes at entry +0x13
entry[0x19] = 0
entry[0x1A] = 0
return entry
```

The model entry vtable is at `PTR_PTR_802B9624`. The type/name pointer shown in the
table is `PTR_s_CtsStageObj_802E6448`, so these entries appear to be the game's
`CtsStageObj` model/stage-object class.

Confirmed vtable layout from Ghidra:

```text
802B9624 +0x00 -> PTR_s_CtsStageObj_802E6448
802B962C +0x08 -> ZmbZabModelEntry_Destroy / FUN_8004D6BC
802B9630 +0x0C -> CtsStageObj_LoadModelBlocks / FUN_80058EA8
802B9634 +0x10 -> CtsStageObj_LoadPrimarySecondaryBlocks / FUN_80059058
802B9638 +0x14 -> CtsStageObj_ResetModelBlocks / FUN_80059060
802B963C +0x18 -> FUN_80059150
802B9640 +0x1C -> FUN_80059154
802B9644 +0x20 -> CtsStageObj_LoadContinuationBlock / FUN_80059158
802B9648 +0x24 -> CtsStageObj_StartAnimation / LAB_800591BC
802B964C +0x28 -> LAB_800591E0
802B9650 +0x2C -> LAB_800591FC
802B9654 +0x30 -> LAB_80059218
802B9658 +0x34 -> FUN_8004E140
802B965C +0x38 -> LAB_80059260
802B9660 +0x3C -> CtsStageObj_ApplyModelTransform / FUN_800594B8
802B9664 +0x40 -> ZmbZabModelEntry_UpdatePresentation / FUN_8004E220
802B9668 +0x44 -> PTR_s_CzanModel_802E6450
```

Confirmed method behavior:

```text
CtsStageObj_LoadPrimarySecondaryBlocks
  0x80059058 simply forwards to CtsStageObj_LoadModelBlocks. LoadZmbZabModelEntryList
  calls this with the primary block, optional secondary block, their block sizes, and
  the continuation count.

CtsStageObj_LoadModelBlocks
  calls CtsStageObj_ResetModelBlocks first
  if secondaryTextureBlock != 0:
    entry[1] = CreateTextureFromTplResource(global texture manager, secondaryTextureBlock,
                                            secondaryTextureBlockSize, -1)
  entry[0] = AllocObjectAligned(0, 0x2D0, 0x20, 0), initialized by FUN_8014BE78
  if continuationCount > 0:
    CzanModel_SetContinuationCount(entry[0], continuationCount)
  if fallbackTextureSlot != -1:
    CzanModel_SetFallbackRenderSlot(entry[0], textureManager slot fallbackTextureSlot, 6, 1, 0xFF)
  FUN_8014C67C(entry[0], primaryModelBlock, primaryModelBlockSize)
  if entry[1] != -1:
    FUN_8014E828(entry[0], textureManager slot entry[1])
  FUN_8014E7D0(entry[0], 1)
  *(entry[0] + 0x148) = 1
  *(entry[0] + 0x1A0) = 2
  if continuationCount > 0:
    entry[3] = continuationCount
    entry[4] = AllocObjectAligned(0, continuationCount * 4, 0x20, 0)
    memset(entry[4], 0, continuationCount * 4)
entry[2] = FUN_8014E8E8(entry[0], DAT_80271640)
```

Confirmed helper names:

```text
FUN_8015C540 -> CzanModel_SetContinuationCount
FUN_8015C3CC -> CzanModel_SetFallbackRenderSlot
```

`CzanModel_SetContinuationCount` stores the continuation/animation block count at
model `+0x9C`.

`CzanModel_SetFallbackRenderSlot` initializes the fallback/synthetic render slot at
model `+0x280..+0x2BC`. The call from `CtsStageObj_LoadModelBlocks` passes render mode
`6`, enabled flag `1`, and alpha `0xFF`.

`FUN_8005C540` is called from active gameplay controller update/setup code after a
position/model marker is queried. Suggested cautious name:

```text
CtsStageObj_SelectAndApplyModelSlot
```

Important Ghidra note: this may show a strange signature because the function begins
with `RuntimeContext_SpillSavedRegisters`; the recovered high word is the stage/model
object pointer and the low word is a count/slot value.

Confirmed behavior:

```text
sum per-entry counters at stageObj +0x16C.. over the recovered count
compute aspect/display scale from DAT_802E71B8 +0x258 screen fields
if modelSlotOrSpecialId < stageObj +0x15C:
  descriptor = stageObj +0x180 + (counterSum + modelSlotOrSpecialId) * 0x7C
  if FUN_8005A5F4(descriptor, 0) == 0:
    use fallback/wrapped placement behavior
else:
  resolve special ids:
    700 -> random among three configured ranges
    800 -> random in second configured range
    900 -> random in first configured range
    101..199 / 201..299 / 301..399 / 601..699 -> remapped range ids
prepare descriptor through FUN_8005A9C4
apply calculated frame/position through FUN_8005993C
apply timing/frame through FUN_800599C0
store final resolved id at stageObj +0x10C
```

This function is not the raw ZMB renderer. It is a model/stage-object slot selector and
application helper. The next useful targets are:

```text
FUN_8005993C -> applies calculated frame/position data to a descriptor/model entry
FUN_800599C0 -> applies timing/frame data to the same entry
FUN_8005A5F4 -> validates/initializes a selected slot descriptor
FUN_8005ABE8 -> reads descriptor duration/length for wrap calculations
```

Confirmed helper names:

```text
FUN_8005993C -> CtsStageObjDescriptor_SetFrameProgress
FUN_800599C0 -> CtsStageObjDescriptor_SetCurrentTime
FUN_8005A5F4 -> CtsStageObjDescriptor_GetEntryHandle
FUN_8005ABE8 -> CtsStageObjDescriptor_GetCurrentDuration
FUN_8005A9C4 -> CtsStageObjDescriptor_ActivateEntry
```

`CtsStageObjDescriptor_SetFrameProgress` clamps negative progress to zero, reads the
first float of the descriptor entry table as a duration, then writes descriptor fields
`+0x28`, `+0x2C`, and `+0x30`.

`CtsStageObjDescriptor_SetCurrentTime` simply writes the current time/frame float at
descriptor `+0x30`.

`CtsStageObjDescriptor_GetEntryHandle` bounds-checks `entryIndex` against descriptor
`+0x08`, then returns the value at entry table offset `entryIndex * 0xA8 + 0xA4`.

`CtsStageObjDescriptor_GetCurrentDuration` returns zero when no active entry exists,
otherwise returns the first float at `descriptor[0] + descriptor[1] * 0xA8`.

`CtsStageObjDescriptor_ActivateEntry` switches a descriptor to `entryIndex`. It snapshots
the previous/current transform block when a blend duration is active, resets the selected
entry at `descriptor[0] + entryIndex * 0xA8`, writes the entry handle at `+0x24`, clears
frame/position fields at `+0x20..+0x48`, resets transform defaults at `+0x4C/+0x58/+0x6C`,
and stores the active index in `descriptor[1]`.

`FUN_8014BE78` initializes the `0x2D0` model object allocated by
`CtsStageObj_LoadModelBlocks`. Suggested name:

```text
CzanModel_Init
```

The vtable at `PTR_PTR_802C0720` identifies this object as `CzanModel`:

```text
802C0720 +0x00 -> PTR_s_CzanModel_802E6940
802C0728 +0x08 -> CzanModel_Destroy / FUN_8014C0B0
```

Only one real vtable method is visible here, so most CzanModel work is performed by
helper/free functions rather than virtual methods.

`FUN_8014C0B0` is the CzanModel destructor/free pass. Suggested name:

```text
CzanModel_Destroy
```

Confirmed behavior:

```text
if model != 0:
  model[0] = PTR_PTR_802C0720
  frees runtime arrays/pointers at:
    model[0x11], [7], [8], [9], [10], [5], [6], [0x1A], [0x0B]
  if model[0x0D] exists:
    walks model[0x0D] records, count model[0x0E], stride 0x1C
    frees nested display-list/remap/helper arrays
    frees model[0x0D]
  if model[0x2D] exists:
    walks model[0x2D] records, count model[0x2C], stride 8
    frees nested per-entry arrays
    frees model[0x2D]
  clears/frees model[0x70], [0x73], [0x72]/[0x71], [0x17], [0x18]
  if releaseMode > 0:
    MemoryPool_Free(0, model)
return model
```

Confirmed high-level behavior:

```text
model[0] = PTR_PTR_802C0720
clears many model state fields
sets byte at model +0x50 = 1
sets byte at model +0x6C = 1
clears model +0x230 for 0x48 bytes
sets color fields at model +0x3C and +0x40 to 0xFFFFFFFF
model[0x27] = 1
model[0x30] = -1
model[0x4C] = -1
model[0x51] = 0x111
model[0x55] = 1
model[0x5A] = 1
model[0x67] = widescreen/display dependent float
model[0x68] = 1
model[0x6B] = 1
model[0x9E] = FLOAT_802E9DA8
model[0xA0] = -1
return model
```

This is the owned model instance stored at `CtsStageObj entry[0]`.

`FUN_8014C67C` stores the primary model resource on a `CzanModel`. Suggested name:

```text
CzanModel_SetPrimaryBlock
```

Confirmed behavior:

```text
model +0x04 = primaryModelBlock
model +0x08 = primaryModelBlockSize
return 1
```

`FUN_8014E828` attaches a texture set to a `CzanModel`. Suggested name:

```text
CzanModel_AttachTextureSet
```

Confirmed behavior:

```text
model +0x54 = textureSet
if model primary block != 0 and textureSet != 0 and *(primaryBlock +0x18) != 0:
  textureFrameTable = *(primaryBlock +0x18)
  if *(primaryBlock +0x24) == 0:
    textureFrameTable = primaryBlock + textureFrameTable
  textureCount = FUN_80146540(textureSet)
  requiredCount = textureFrameTable[0]
  if requiredCount == 0:
    requiredCount = textureFrameTable[1]
  if textureCount < requiredCount:
    error through FUN_801A5710(DAT_80294477, textureCount)
```

So this function does not decode geometry; it links the texture set and checks that
the texture resource has enough frames for what the model block expects.

`FUN_8014E7D0` sets/normalizes enabled state on a `CzanModel`. Suggested name:

```text
CzanModel_SetEnabled
```

Confirmed behavior:

```text
enabled = enabled != 0
result = FUN_8014C6CC(model, enabled)
if result != 0:
  CzanModel_UpdateObjectTransforms(1.0, model)
return result != 0
```

`FUN_8014E780` builds runtime data with disabled/zero mode and then performs the first
transform update. Suggested name:

```text
CzanModel_BuildRuntimeDataAndUpdateTransforms
```

Confirmed behavior:

```text
result = CzanModel_BuildRuntimeData(model, 0)
if result != 0:
  CzanModel_UpdateObjectTransforms(0.0, model)
return result != 0
```

`FUN_8015EBAC` is a tiny owner-side wrapper that builds runtime data for the
`CzanModel` pointer stored at owner `+0x80`. Suggested name:

```text
CzanModelOwner_BuildRuntimeDataAt80
```

Confirmed behavior:

```text
if owner +0x80 != 0:
  CzanModel_BuildRuntimeDataAndUpdateTransforms(*(owner +0x80))
```

`FUN_8015EAE0` is the owner-side constructor for the `CzanModel` pointer at
owner `+0x80`. Suggested name:

```text
CzanModelOwner_CreateModelFromPrimaryBlock
```

Confirmed behavior:

```text
if owner +0x80 != 0:
  destroy old CzanModel with release=1
  owner +0x80 = 0

model = AllocObjectAligned(0, 0x2D0, 0x20, 0)
if model != 0:
  CzanModel_Init(model)
owner +0x80 = model
CzanModel_SetPrimaryBlock(model, primaryBlock, primaryBlockSize)
```

`FUN_8015EB98` and `FUN_8015EBC8` are the matching owner-side continuation helpers.
The decompiler can lose their arguments, but the `select_cmn` call site confirms the
real shape:

```text
CzanModelOwner_SetContinuationCount(owner, continuationCount)
CzanModelOwner_LoadContinuationBlock(owner, continuationBlock, continuationIndex)
```

`FUN_8015EB98(owner, 10)` forwards to `CzanModel_SetContinuationCount(*(owner+0x80), 10)`.
`FUN_8015EBC8(owner, block, index)` checks `owner +0x80` and then forwards to the
model continuation loader at `0x8014C68C`. In `select_cmn`, this is called for blocks
`6..0xF`, indices `0..9`, so these are the ten ZAB/continuation blocks for the model
created from block `5`.

`FUN_8014C68C` is now confirmed. Suggested name:

```text
CzanModel_LoadContinuationBlock
```

Confirmed behavior:

```text
if continuationIndex < model +0x9C:
  model +0x0C = continuationBlock
  FUN_8014E464(model, continuationIndex)
  return true
return false
```

So this function is not the real ZAB parser; it is the checked attach wrapper. The
parser/builder we need next is `FUN_8014E464`.

`FUN_8014E464` is now confirmed. Suggested name:

```text
CzanModel_ParseContinuationAnimationBlock
```

Confirmed behavior:

```text
zab = model +0x0C
duration = zab +0x10 converted from integer ticks by FLOAT_802E9E38
model +0x240 = duration
animationRecordBase =
  model +0x14 + continuationIndex * modelObjectCount * 0x74
animationRecordBase +0x34 = duration

for every model object:
  clear translation/rotation/scale key counts and cached key indices in its 0x74 record

for each ZAB channel entry:
  name = zab +0x30 + channelIndex * 0x40
  objectIndex = CzanModel_FindObjectIndexByName(model, name)
  if objectIndex exists:
    record = model +0x14 + (objectIndex + continuationIndex * modelObjectCount) * 0x74
    record +0x34 = duration

  keyTable = zab + *(channel +0x3C)
  for each key group in channel +0x34:
    type 0:
      record +0x00 = keyCount
      record +0x04 = zab + keyOffset
      if zab +0x28 == 0, convert each translation key time, stride 0x10
    type 1:
      record +0x08 = keyCount
      record +0x0C = zab + keyOffset
      if zab +0x28 == 0, convert each rotation key time, stride 0x14
    type 2:
      record +0x10 = keyCount
      record +0x14 = zab + keyOffset
      if zab +0x28 == 0, convert each scale key time, stride 0x10

zab +0x28 = 1
```

This ties the `.zab` format directly to the key evaluators already named:

```text
type 0 -> CzanModel_EvaluateTranslationKeys
type 1 -> CzanModel_EvaluateRotationKeys
type 2 -> CzanModel_EvaluateScaleKeys
```

Status after this: the ZAB attach/parse chain is identified from
`CSelectCommon_LoadResource` down to per-object key arrays. The remaining rendering gap
is host-side implementation of the runtime allocations and draw submission, not mystery
about where the ZAB blocks go.

`FUN_8015C548` starts one CzanModel animation channel. Suggested name:

```text
CzanModel_StartAnimationChannel
```

Confirmed behavior:

```text
channel = model +0x230 + channelIndex * 0x24
channel +0x00 = startFrame
channel +0x04 = animationIndex
channel +0x08 = loop flag
channel +0x0C = model +0x14 + animationIndex * modelObjectCount * 0x74
channel +0x10 = animationRecord +0x34 duration
channel +0x18/+0x1C = blend state
```

`FUN_8015EBF4` is the owner-side playback speed setter. Suggested name:

```text
CzanModelOwner_SetAnimationSpeed
```

Confirmed behavior:

```text
if owner +0x80 != 0:
  *(*(owner +0x80) + 0x250) = speed
```

In `select_cmn`, `0x80098DFC` / `0x80098EAC` call this with `1.0` before
starting one of the ten background transition animations.

`FUN_80177CA8` loads a collection/bank of `CzanModel` objects from a WII link
resource. Suggested name:

```text
CzanModelCollection_LoadFromLinkBlocks
```

Confirmed behavior:

```text
collectionIndex = param4 & 0xFF
collection[collectionIndex].modelCount = modelCount
collection[collectionIndex].models =
  AllocObjectAligned(0, modelCount * 0x2D0 + 0x10, 0x20, 0)
  initialized by FUN_80129C64(..., CzanModel_Init, CzanModel_Destroy, 0x2D0, modelCount)
collection[collectionIndex].textureSlots =
  AllocObjectAligned(0, modelCount * 4, 0x20, 0)

for i in 0..modelCount-1:
  modelBlockIndex = i * 2
  textureBlockIndex = i * 2 + 1

  textureSize = CzanLinkManager_GetBlockSize(link, textureBlockIndex)
  textureBlock = CzanLinkManager_GetBlock(link, textureBlockIndex)
  textureSlot = CreateTextureFromTplResource(global texture manager, textureBlock, textureSize, -1)
  textureSlots[i] = textureSlot

  modelSize = CzanLinkManager_GetBlockSize(link, modelBlockIndex)
  modelBlock = CzanLinkManager_GetBlock(link, modelBlockIndex)
  CzanModel_SetPrimaryBlock(models[i], modelBlock, modelSize)
  CzanModel_AttachTextureSet(models[i], globalTextureManagerBase + textureSlot * 0x14)
  CzanModel_BuildRuntimeDataAndUpdateTransforms(models[i])

return -1
```

This is important because it confirms a higher-level non-`CtsStageObj` path also
loads CzanModels directly from paired model/texture blocks.

`FUN_8017872C` is the manager-level loader for one CzanModel resource bank.
Suggested name:

```text
CzanModelManager_LoadResource
```

Confirmed behavior:

```text
bank = manager + (bankIndex & 0xFF) * 0x10
link = CzanLinkManager(linkData)

block0 = CzanLinkManager_GetBlock(link, 0)
bank +0x124 = block0
bank +0x128 = block0 +0x10

if link block count < 2:
  manager +0x18 = -1
else:
  block1 = texture/TPL resource
  textureSlot = CreateTextureFromTplResource(global texture manager, block1, block1Size, -1)
  bank +0x12C = textureSlot
  manager +0x18 = textureSlot

if link block count > 2:
  block2 = model collection WII/link resource
  modelCount = *(ushort *)(bank +0x128 +6)
  CzanModelCollection_LoadFromLinkBlocks(manager +0xAC, block2, modelCount, bankIndex)

return 1
```

Manager/bank fields seen here:

```text
manager +0x18 -> active/last texture slot, or -1
manager +0xAC -> CzanModel collection array/base
bank +0x124 -> block 0 metadata base
bank +0x128 -> block 0 metadata payload at +0x10
bank +0x12C -> texture slot for block 1
bank stride -> 0x10, selected by low byte of bankIndex
```

`FUN_80178B8C` unloads/releases one CzanModel manager bank. Suggested name:

```text
CzanModelManager_UnloadBank
```

Confirmed behavior:

```text
bankIndex = bankIndex & 0xFF
if manager[bankIndex * 4 +0x4A] == 0:
  return 0

FUN_801780FC(manager +0x2B)

for each live object pointer in manager[0x69], count manager[0]:
  if object exists and object byte +0x12C == bankIndex:
    FUN_8017E5F8(...)
    call object destructor through vtable at object +0x134, releaseMode=1
    clear object pointer

if manager[bankIndex * 4 +0x4B] != -1:
  release texture slot through FUN_80146B8C(global texture manager)

manager[bankIndex * 4 +0x4A] = 0
manager[bankIndex * 4 +0x4B] = -1
FUN_8017C760(manager +2)
return 1
```

This matches the loader fields: `+0x4A` is the bank-loaded flag/metadata pointer slot,
and `+0x4B` is the bank texture slot.

`FUN_80178A68` clears the whole CzanModel manager. Suggested name:

```text
CzanModelManager_Clear
```

Confirmed behavior:

```text
for each live object pointer in manager[0x69], count manager[0]:
  if object exists:
    CzanModelObject_UnregisterManagerEntries(object)
    call object destructor through vtable at object +0x134, releaseMode=1
    clear object pointer

for bankIndex in 0..7:
  CzanModelManager_UnloadBank(manager, bankIndex)

free manager[0x69]
manager[0x69] = 0
FUN_801A1E58(manager +0x45)
FUN_80177F98(manager +0x2B)
FUN_8017C76C(manager +2)
FUN_8017C760(manager +2)
```

So the manager tracks up to 8 banks, plus a live object pointer array at `manager[0x69]`.

`FUN_80178C8C` clears only the live object pointer array contents. Suggested name:

```text
CzanModelManager_ClearLiveObjects
```

Confirmed behavior:

```text
for each live object pointer in manager[0x69], count manager[0]:
  if object exists:
    CzanModelObject_UnregisterManagerEntries(object)
    call object destructor through vtable at object +0x134, releaseMode=1
    clear object pointer
return 1
```

Unlike `CzanModelManager_Clear`, this does not unload banks 0..7 and does not free
`manager[0x69]`.

`FUN_80178DE4` updates live manager objects filtered by object group/layer byte.
Suggested name:

```text
CzanModelManager_UpdateVisibleGroup
```

Confirmed behavior:

```text
manager = recovered from RuntimeContext_SpillSavedRegisters()
if recovered low value == 0:
  calls vtable method at manager[0x6AD] +0x30 +0x0C
  computes manager[1] as a scaled delta/aspect value
  updates/copies matrices/vectors from manager +0x6A/+0x7A into manager +0x9A
  normalizes/finalizes manager +0x9A through FUN_80180C54

  for each live object in manager[0x69], count manager[0]:
    if object exists:
      if object byte +0x12D == groupId:
        FUN_8017F418(object, 1)
        if FUN_8017F6D8(object, 3) == 0:
          CzanModelObject_Update(manager[1], object)
        else if removeFinished != 0:
          CzanModelObject_UnregisterManagerEntries(object)
          call object destructor through object +0x134 vtable, releaseMode=1
          clear object pointer
      else:
        FUN_8017F418(object, 0)

  FUN_8017C8B0(manager +2)
```

Object fields:

```text
object +0x12D -> group/layer id used by this update filter
```

This puts `FUN_8017E720` directly on the live-object update path.

`FUN_8017E720` updates one live model-manager object. Suggested name:

```text
CzanModelObject_Update
```

Confirmed behavior:

```text
if object flags & 4:
  checks all registration records at object +0x118
  for type 1:
    waits while registered manager +0x0C object has nonzero short at +0xD8
  for type 3:
    waits while FUN_801A1B48(manager +0x118 object) returns 0
  if all dependencies are ready:
    object flags |= 8
    return

if object flags & 1:
  CzanModelObject_UpdateRegistrationTarget(delta, object, object +0x118 registrations)
  if FUN_8017E120(object +1, 0x1C) != 0:
    applies FUN_8017E0EC(1.0, target) to each registered target:
      type 0 -> object +1
      type 1 -> manager +0x0C target
      type 2 -> manager +0xB0 target
      type 3 -> manager +0x118 target

  if neither fade flag 0x10 nor fade flag 0x20 is set:
    if FUN_8017E120(object +1, 0x1D) != 0:
      object flags |= 4

  if fade-out flag 0x20 is set:
    object +0x124 = fades from object +0x128 to 0 over duration object +0x11C
    when complete:
      clears flags 0x20 and 1
      sends command 0x1F,0 to each registered target through its vtable +0x10
      object flags |= 4

  if fade-in flag 0x10 is set:
    object +0x124 = fades from 0 to 1 over duration object +0x11C
    when complete:
      clears flag 0x10
      object +0x124 = 1.0
```

Object fields:

```text
object +0x000 -> state flags
object +0x110 -> metadata; byte +8 is registration count
object +0x118 -> registration records, stride 0x10
object +0x11C -> fade duration
object +0x120 -> fade elapsed timer
object +0x124 -> fade/current alpha-like value
object +0x128 -> fade source/max value
```

This still is not the draw function. It controls lifecycle, registration updates, and
fade state for live objects.

`FUN_8017F7F8` updates one registered target for a live model-manager object, then
recursively updates linked child/effect registrations. Suggested name:

```text
CzanModelObject_UpdateRegistrationTarget
```

Confirmed behavior:

```text
registration type = *(byte *)(registration[0] +2)

type 0:
  call object-local vtable at object +0xD8, method +0x0C, target object +4

type 1:
  target = manager +0x0C list entry registration[3]
  target +0xCC = object +0x124
  call target vtable at target +0xD4, method +0x0C

type 2:
  target = manager +0xB0 list entry registration[3]
  target +0xCC = object +0x124
  call target vtable at target +0xD4, method +0x0C

type 3:
  target = manager +0x118 list entry registration[3]
  target +0xCC = object +0x124
  call target vtable at target +0xD4, method +0x0C

for linked entries starting at registration[1]:
  validates high-bit/link pointers
  logs effect debug info on invalid pointers
  recursively calls CzanModelObject_UpdateRegistrationTarget(delta, object, linkedEntry)
```

This still dispatches updates into registered targets; the target vtable method `+0x0C`
is now the interesting next hop for render/update behavior.

`FUN_8017E5F8` unregisters a live model object from manager-side lists before object
destruction. Suggested name:

```text
CzanModelObject_UnregisterManagerEntries
```

Confirmed behavior:

```text
registrations = object +0x118
registrationCount = *(byte *)(*(object +0x110) +8)

for each 0x10-byte registration record:
  if record[3] >= 0:
    type = *(byte *)(record[0] +2)
    if type == 1:
      manager = FUN_80178270()
      FUN_8017C848(manager +8, record[3])
    else if type == 2:
      manager = FUN_80178270()
      FUN_80178204(manager +0xAC, record[3])
    else if type == 3:
      manager = FUN_80178270()
      FUN_801A1F2C(manager +0x114, record[3])
    record[3] = -1

free object +0x118
object +0x118 = 0
```

Type codes:

```text
1 -> unregister from manager +0x08 via FUN_8017C848
2 -> unregister from manager +0xAC via FUN_80178204
3 -> unregister from manager +0x114 via FUN_801A1F2C
```

`FUN_8004EBA8` is a small wrapper that loads model-manager bank 1 into
`gManager_802E70B8`. Suggested name:

```text
CzanModelManager_LoadBank1Resource
```

Confirmed behavior:

```text
CzanModelManager_LoadResource(gManager_802E70B8, 1, linkData)
```

`FUN_8004EC4C` bridges the CzanModel manager and the CtsStageObj wrapper path.
Suggested name:

```text
CzanModelManager_SetupBank3AndStageObjects
```

Confirmed behavior:

```text
context = recovered from FUN_8012A154()
activeGroupIndex = recovered low word/int from FUN_8012A154()

for 5 existing stage-object slots at context +0xB02C:
  if slot exists:
    call vtable +0x14, currently CtsStageObj_ResetModelBlocks
    call destructor vtable +0x08 with releaseMode=1
    clear slot

FUN_80178B8C(gManager_802E70B8, 3)
clear context +0xB018 for 0x28 bytes
context +0xB01C = -1

if bank3LinkData != 0:
  CzanModelManager_LoadResource(gManager_802E70B8, 3, bank3LinkData)

context +0xB028 = activeGroupIndex
context +0x8A05 = flagA
context +0x8A06 = flagB

if stageObjectLinkData != 0 and activeGroupIndex != -1:
  offsets = DAT_802712F0 + activeGroupIndex * 0x14
  for 5 stage objects:
    alloc 0x70 bytes
    CtsStageObj_InitBase(slot)
    first block = model block
    second block = texture block
    remaining blocks in range = continuation blocks
    vtable +0x10 -> CtsStageObj_LoadPrimarySecondaryBlocks
    vtable +0x20 -> CtsStageObj_LoadContinuationBlock for each continuation
    if continuationCount == 1:
      vtable +0x24 -> CtsStageObj_StartAnimation(FLOAT_802E8408, FLOAT_802E8410, ...)
```

This function shows one real owner-side path for five `CtsStageObj` wrappers. It uses
`DAT_802712F0` as a table of block ranges for the five stage-object slots.

`FUN_80054CF4` loads one owner-side stage/model resource group from a WII link.
Suggested name:

```text
CzanModelOwner_LoadStageResourceGroup
```

Confirmed owner fields:

```text
owner +0x002C -> current resource group index, incremented when useful blocks exist
owner +0x8A04 -> group/category byte passed to FUN_80054C00
owner +0xB324 + group -> metadata type/id byte copied from nested metadata block
owner +0xB328 + group*4 -> allocated metadata string copied from nested metadata +4
```

Confirmed resource block flow:

```text
root block 0 -> LoadZmbZabModelEntryList(owner, block0)
root block 2 -> model-manager bank 5 resource candidate
root block 3 -> nested WII link
  nested block 0 -> small metadata/name record
  nested block 1 -> extra link passed to FUN_80054C00

if nested metadata exists:
  owner[0xB324 + group] = metadata[0]
  owner[0xB328 + group*4] = copy of string at metadata +4

FUN_80054C00(owner, owner[0x8A04], rootBlock2, nestedBlock1)

if rootBlock2 != 0 and nestedBlock1 != 0 and group == 0:
  CzanModelManager_LoadResource(gManager_802E70B8, 5, rootBlock2)

FUN_800554B0(owner, group)
if any useful block was found:
  group++
```

This explains how the model owner prepares ZMB/ZAB entries and bank-5 resources
before the later live-object creation path. It still is not a draw function. The
next functions that matter from this loader are:

```text
FUN_80054C00 -> CzanModelOwner_SetupResourceGroupEntries
FUN_800554B0 -> CzanModelOwner_EnsureResourceGroupHandle
```

`FUN_80054C00` stores the current resource group's category, bank/resource pointer,
and optional 0x0C-byte entry records. Suggested name:

```text
CzanModelOwner_SetupResourceGroupEntries
```

Confirmed parameters:

```text
param_1 -> owner
param_2 -> group/category byte
param_3 -> resource pointer, stored for this group
param_4 -> optional entry-list link parsed by FUN_8018CD38/FUN_8018CDBC
```

Confirmed fields:

```text
owner +0x002C -> current group index
owner +0xB0D0 + group -> group/category byte from param_2
owner +0xB0D4 + group*4 -> resource pointer from param_3
owner +0xB0EC + group*0xC0 + entry*0x0C +0 -> source record byte 1
owner +0xB0EC + group*0xC0 + entry*0x0C +1 -> source record byte 2
owner +0xB0EC + group*0xC0 + entry*0x0C +2 -> source record byte 0
```

Confirmed flow:

```text
if entryListLinkData != 0:
  parse it with FUN_8018CD38/FUN_8018CDBC
  count = FUN_8018CFA0(parsed)
  for entry in 0..count-1:
    record = FUN_8018D004(parsed, entry)
    store record[1], record[2], record[0] into the group table

owner[0xB0D0 + group] = groupCategory
owner[0xB0D4 + group*4] = bank/resource pointer
release parsed list
```

This confirms the group entry stride is `0x0C`, and each group gets a `0xC0`-byte
table, so the table can hold up to 16 compact entries.

`FUN_800554B0` creates a per-group handle/id when the loaded group needs one.
Suggested name:

```text
CzanModelOwner_EnsureResourceGroupHandle
```

Confirmed fields:

```text
owner +0xB324 + group -> metadata byte copied by CzanModelOwner_LoadStageResourceGroup
owner +0xB340 + group*4 -> normal per-group handle/id, initialized to -1
owner +0xB34C -> special handle/id for group 3
owner +0xB354 + group*4 -> per-group resource/link presence check
owner +0xB36C -> lock/disable flag; when nonzero this returns 0
```

Confirmed behavior:

```text
if owner +0xB36C != 0:
  return 0

if group == 3 and owner +0xB34C != -1:
  return 0

if normal handle table entry owner +0xB340 + group*4 == -1:
  if group == 3:
    owner +0xB34C = ResourceSlotManager_AllocateSlot(gManager_802E70A8, 0)
    return 1
  else if owner +0xB354 + group*4 != 0 or owner +0xB324 + group != 0:
    owner +0xB340 + group*4 = ResourceSlotManager_AllocateSlot(gManager_802E70A8, 0)
    return 1

return 0
```

This function still does not draw. It decides whether the owner needs an auxiliary
handle for a loaded resource group.

`FUN_80024EA8` allocates one slot/handle from `gManager_802E70A8`. Suggested name:

```text
ResourceSlotManager_AllocateSlot
```

Confirmed behavior:

```text
slotIndex = -1
if slotManager[0] != 0:
  slotIndex = ResourceSlotManager_ClaimFreeSlot(slotManager[0])
  if slotIndex != -1:
    slotObject = ResourceSlotManager_GetClaimedSlot(slotManager[0], slotIndex)
    if setupData != 0:
      FUN_801843CC(slotObject, setupData)
return slotIndex
```

In the model-owner path, `setupData` is passed as `0`, so this only allocates or
reserves a slot index. To know exactly what kind of slot this is, the next targets are
`FUN_80186F4C`, `FUN_801871A8`, and `FUN_801843CC`.

`FUN_80186F4C` claims the first free slot from that slot pool. Suggested name:

```text
ResourceSlotManager_ClaimFreeSlot
```

Confirmed pool fields:

```text
slotPool +0x56B8 -> slot record array base
slotPool +0x56BC -> slot record count
slot record stride -> 0x290
slot record +0x04 bit 0 -> in-use flag
```

Confirmed behavior:

```text
for slotIndex in 0..slotCount-1:
  slot = slotArray + slotIndex * 0x290
  if (slot[+0x04] & 1) == 0:
    slot[+0x04] |= 1
    return slotIndex
return -1
```

`FUN_801871A8` validates a slot index and returns the actual claimed record pointer.
Suggested name:

```text
ResourceSlotManager_GetClaimedSlot
```

Confirmed behavior:

```text
if slotIndex < 0:
  return 0
if slotIndex >= slotPool +0x56BC count:
  return 0
slot = *(slotPool +0x56B8) + slotIndex * 0x290
if (slot[+0x04] & 1) == 0:
  return 0
return slot
```

So `FUN_801871A8` is not creating or loading anything. It is only a safe accessor for
already-claimed `0x290` slot records.

`FUN_80025668` is a wrapper that releases an old slot, allocates a new one, and binds
payload/resource data into it. Suggested name:

```text
ResourceSlotHandle_Rebind
```

Confirmed behavior:

```text
if handle[0] != 0:
  FUN_80186FB8(handle[0], handle[1])
handle[1] = -1

if handle[0] != 0:
  slotIndex = ResourceSlotManager_ClaimFreeSlot(handle[0])
  if slotIndex != -1 and setupData != 0:
    slotObject = ResourceSlotManager_GetClaimedSlot(handle[0], slotIndex)
    FUN_801843CC(slotObject, setupData)
handle[1] = slotIndex

if handle[0] != 0:
  slotObject = ResourceSlotManager_GetClaimedSlot(handle[0], slotIndex)
if slotObject != 0:
  CzanMovieObj_Reset(slotObject)
  CzanMovieObj_LoadResource(slotObject, resourceOrPayload)
```

`FUN_80024FA4` checks one flag state on the current slot record. Suggested name:

```text
ResourceSlotHandle_IsActivePending
```

Confirmed behavior:

```text
slot = 0
if handle[0] != 0:
  slot = ResourceSlotManager_GetClaimedSlot(handle[0], handle[1])
if slot != 0:
  if (slot[+0x230] & 1) != 0 and (slot[+0x230] & 2) == 0:
    return 1
return 0
```

New slot-record field:

```text
slot +0x230 bit 0 -> active/started flag
slot +0x230 bit 1 -> finished/blocked flag
```

This tells us `gManager_802E70A8` exposes reusable `0x290`-byte records, but not yet
what every record field means.

`FUN_801843CC`, `FUN_80184678`, and `FUN_801844E8` identify the slot record as a
`CzanMovieObj` through assert strings from `zanMovie.cpp`.

```text
FUN_801843CC -> CzanMovieObj_AllocBuffer
FUN_80184678 -> CzanMovieObj_Reset
FUN_801844E8 -> CzanMovieObj_LoadResource
FUN_80184108 -> CzanMovieObj_InitDefaults
FUN_80197DD0 -> CzanSndRead_Open
```

`CzanMovieObj_AllocBuffer` confirmed fields:

```text
movieObj +0x220 -> allocated buffer pointer
movieObj +0x224 -> allocated buffer size
movieObj +0x230 bit 0 -> active/allocated flag; AllocBuffer asserts if already set
```

Confirmed behavior:

```text
if movieObj +0x230 bit 0 is set:
  assert "CzanMovieObj::AllocBuffer() already..."

if inactive:
  if movieObj +0x220 != 0:
    MemoryPool_Free(0, movieObj +0x220)
    movieObj +0x220 = 0
    movieObj +0x224 = 0

  if bufferSize != 0:
    movieObj +0x224 = bufferSize
    movieObj +0x220 = AllocObjectAligned(0, bufferSize, 0x20, 0)
```

`CzanMovieObj_Reset` confirmed fields and flags:

```text
movieObj +0x008 -> subobject with vtable at +0x8C
movieObj +0x0E4 -> subobject reset by FUN_801A016C/FUN_801A01EC
movieObj +0x114 -> subobject registered through DAT_802E71B8 +0x268
movieObj +0x228 -> optional allocation freed during reset
movieObj +0x230 -> main flags
movieObj +0x234/+0x238/+0x23C/+0x240 -> runtime state cleared by reset

+0x230 bit 0x00001 -> active/loaded
+0x230 bit 0x00002 -> cleanup-needed state
+0x230 bit 0x10000 -> +0x08 subobject registered/started
+0x230 bit 0x20000 -> +0xE4 subobject active
+0x230 bit 0x40000 -> +0x114 subobject active/registered
```

Confirmed reset flow:

```text
if active and cleanup-needed:
  if bit 0x40000:
    FUN_8018FB20(movieObj +0x114, 0)
    FUN_8019EAEC(movieObj +0x114)
    clear bit 0x40000
  if bit 0x10000:
    call vtable method +0x18 on movieObj +0x08
    FUN_8019D96C(movieObj +0x08)
    clear bit 0x10000
  if bit 0x20000:
    FUN_801A01EC(movieObj +0xE4)
    clear bit 0x20000

clear +0x238, +0x23C, +0x240, +0x234
movieObj +0x230 &= 0xFFFFC3FF

FUN_8019DFE8(movieObj +0x114)
call vtable method +0x14 on movieObj +0x08
FUN_801A016C(movieObj +0xE4)
free +0x228 if present
FUN_80169B0C(*(DAT_802E71B8 +0x268), movieObj +0x114)
CzanMovieObj_InitDefaults(movieObj)
```

`CzanMovieObj_LoadResource` does that same reset first, then:

```text
FUN_80169AC4(*(DAT_802E71B8 +0x268), movieObj +0x114)
CzanSndRead_Open(movieObj +0x08, resourceOrPayload)
movieObj +0x230 |= 1
FUN_8019CE38(movieObj +0x08)
```

`CzanMovieObj_InitDefaults` confirmed fields:

```text
movieObj +0x228 -> cleared
movieObj +0x22C -> cleared
movieObj +0x230 -> flags cleared
movieObj +0x234/+0x238/+0x23C/+0x240 -> runtime state cleared
movieObj +0x244 -> 0x10-byte block cleared
movieObj +0x254/+0x25C/+0x264/+0x26C -> 8-byte blocks cleared
movieObj +0x274 -> default float
movieObj +0x278 -> set to 1
movieObj +0x27C -> 4 bytes set to 0xFF
movieObj +0x280/+0x284/+0x288 -> cleared
movieObj +0x14C..+0x180 -> clamped config values from FUN_80166D54
movieObj +0x170/+0x188/+0x18C/+0x190 -> config words copied from FUN_80166D54
```

The clamp writes show `FUN_80166D54` supplies playback/config defaults, but the exact
meaning of each float should stay unnamed until that function is mapped.

`CzanSndRead_Open` is identified by `zanSndRead.cpp` assert strings. Confirmed flow:

```text
if pathOrResource == 0:
  assert dvd_Open error

fileHandle = FUN_801B1620(pathOrResource)
if fileHandle == -1:
  assert dvd_Open error "%s"

call sndRead vtable +0x14 reset/destructor-like method
if FUN_801B1930(fileHandle, sndRead +1) == 0:
  assert dvd_Open error

sndRead[0x10] = sndRead[0x0E]
sndRead[0x0C] = sndRead
sndRead[0x00] = fileHandle
if fileHandle != -1:
  validate sndRead[0x1B] reset-block state
  FUN_80198220(sndRead, 0, sndRead[0x0E], 1, sndRead[0x0E], sndRead[0x0E], 1, 1)
```

So `movieObj +0x08` is a `CzanSndRead`-like subobject that opens/reads a DVD/file
resource. That supports the idea that `gManager_802E70A8` is auxiliary playback
plumbing, not the ZMB model renderer.

This means the `gManager_802E70A8` handles created by the model-owner setup are movie
object slots. For the main-menu background/model path this is probably auxiliary
movie/screen resource plumbing, not the ZMB model draw path itself. The next most useful
functions here are `FUN_80166D54` for the movie config defaults and `FUN_80198220` for
the actual read scheduling.

`CzanSndRead_Open` has been split into its own source/header in the host because the
assert strings identify it as `zanSndRead.cpp`, separate from `zanMovie.cpp`.

`FUN_80004350` is a plain clear/fill wrapper. Suggested name:

```text
ClearMemory
```

Confirmed behavior:

```text
FUN_8000429C(dest, value, size)
return dest
```

Call sites use it like `memset(dest, value, size)`.

`FUN_801B0120` initializes a 3x4 matrix to identity. Suggested name:

```text
Matrix34_SetIdentity
```

Confirmed layout:

```text
[1, 0, 0, 0]
[0, 1, 0, 0]
[0, 0, 1, 0]
```

`FUN_801459EC` is now named:

```text
Matrix34_Copy
```

It is a tiny wrapper around `FUN_801B0150(src, dest)` and is used anywhere the game
copies a 3x4 transform matrix without composition.

Additional matrix helper names:

```text
FUN_80145A0C -> Matrix34_Multiply
FUN_80145CA4 -> Matrix34_GetTranslation
FUN_80145C88 -> Matrix34_SetTranslation
```

`Matrix34_Multiply` wraps `FUN_801B0190(lhs, rhs, dest)` and composes parent/object
3x4 transforms. `Matrix34_GetTranslation` reads offsets `+0x0C/+0x1C/+0x2C` into a
compact vec3, and `Matrix34_SetTranslation` writes the same three offsets.

`FUN_801568FC` initializes visible-part UV runtime state for a `CzanModel`. Confirmed
name:

```text
CzanModel_InitVisiblePartUvRuntime
```

Important Ghidra note: this decompiles as `void(void)` because it begins with the
saved-register helper. The recovered object is the `CzanModel` pointer.

Confirmed behavior:

```text
partTable = *(model +0x04) +0x1C
partFormatOrVersion = partTable != 0 ? *(partTable +4) : default
runtimePart = model +0x44
runtimePartCount = model +0x48

for each visible runtime part, stride 0x50:
  clear +0x0C, +0x1C, +0x40, +0x44
  clear bytes +0x4A/+0x4B
  sourcePart = runtimePart +0x30
  if sourcePart exists and part format supports UV keys:
    keyCount = *(short *)(sourcePart +0x3A)
    keyTable = *(sourcePart +0x3C)
    choose starting key indices into runtime +0x4A/+0x4B
    compute starting offsets at runtime +0x0C/+0x1C
    initialize timers/ranges at runtime +0x34/+0x38/+0x3C
```

This is the setup pair for `CzanModel_UpdatePartUvAnimation`: `InitVisiblePartUvRuntime`
initializes the per-part UV state, and `UpdatePartUvAnimation` advances it each frame.

`FUN_8013340C` is a bounded byte/string compare. Suggested name:

```text
BoundedStringCompare
```

Confirmed behavior:

```text
compare left and right byte-by-byte
stop when maxLength bytes were checked -> return 0
stop when bytes differ -> return leftByte - rightByte
stop when matching null byte is reached -> return 0
```

This is equivalent to `strncmp(left, right, maxLength)`.

`FUN_801A48A0` flushes a PowerPC data-cache range. Suggested name:

```text
FlushDataCacheRange
```

Confirmed behavior:

```text
if size == 0:
  return address

lineCount = (size + (address & 0x1F) + 0x1F) >> 5
for each 0x20-byte cache line:
  dataCacheBlockFlush(address)
  address += 0x20
syscall/sync
return address after the flushed range
```

The PC host does not need a real cache flush, but the helper is useful to keep the
original code flow recognizable. `CzanModel_BuildRuntimeData` calls it after relocating
or preparing model block data.

`FUN_8012A17C` is another saved-register/runtime return helper. It has no meaningful
game logic body and should not be named as model/menu behavior.

`FUN_80053B90` switches `gManager_802E70B8` bank 5 when an owner mode byte changes.
Suggested name:

```text
CzanModelManager_SwitchBank5ForMode
```

Confirmed owner fields:

```text
owner +0x002D -> current mode/index byte
owner +0x002E -> previous mode/index byte
owner +0xB0B8 -> cached value copied from +0xB378 during switch
owner +0xB0D4 + mode*4 -> per-mode bank-5 link/resource pointer
owner +0xB340 + mode*4 -> per-mode gManager_802E70A8 handle/id
owner +0xB34C -> alternate handle/id when mode == 3
owner +0xB36C -> boolean flag passed to MovieSlotHandle_SetObjectEnabled after reload
owner +0xB370 -> pending/transition flag
owner +0xB374 -> requested mode/index byte
owner +0xB378 -> cached value copied to +0xB0B8
owner +0xB37C -> transition/lock flag
```

Confirmed flow:

```text
if +0xB370 is set and +0xB37C is clear:
  clear +0xB370

if requestedMode != currentMode and +0xB370 == 0:
  if current mode has a bank-5 link/resource pointer:
    CzanModelManager_UnloadBank(gManager_802E70B8, 5)
  CzanModelManager_UnloadBank(gManager_802E70B8, 5)
  disable/clear old mode handle with MovieSlotHandle_SetObjectEnabled when handle != -1
  owner +0xB0B8 = owner +0xB378
  previousMode = currentMode
  currentMode = requestedMode
  if requested mode has a bank-5 link/resource pointer:
    CzanModelManager_LoadResource(gManager_802E70B8, 5, ...)
    FUN_80053124(owner)
  enable/update new mode handle with MovieSlotHandle_SetObjectEnabled when handle != -1
```

This is a dynamic model bank switcher. It is useful for finding which resources feed
bank 5, but it still does not render geometry directly.

`FUN_80053124` initializes the live model objects after bank 5 is loaded for the active
mode. Suggested name:

```text
CzanModelManager_InitBank5LiveObjectsForMode
```

Important Ghidra note: this may decompile as `void(void)` because
`RuntimeContext_SpillSavedRegisters` recovers the owner pointer.

Confirmed owner fields:

```text
owner +0x002D -> current mode/index byte
owner +0x8A04 -> group/category byte passed to FUN_8017911C
owner +0xB0E0 + mode -> count of 0x0C-byte entries for that mode
owner +0xB0E4 + mode*0xC0 + entry*0x0C -> per-mode entry table

entry +0x00 -> transform/position id, must not be -1
entry +0x04 -> live model object handle, created when -1
entry +0x08 -> signed model/resource id byte, must not be -1
entry +0x0A -> signed bank/model slot byte, passed to FUN_8017911C
```

Confirmed flow:

```text
if owner +0xB370 == 0:
  for each entry in current mode:
    if entry[0] != -1 and entry[4] == -1 and entry[8] != -1 and entry[0x0A] != -1:
      handle = FUN_8017911C(1.0, gManager_802E70B8, entry[0x0A], 5, owner +0x8A04)
      entry[4] = handle
      if handle != -1:
        slotIndex = FUN_80054F88(owner)
        if ownerSlot[slotIndex] has transform data:
          FUN_8005941C(ownerSlot +0xAC, stackTransform, entry[0])
        FUN_801791F0(gManager_802E70B8, handle, stackTransform)
```

This proves bank 5 is used to spawn live model objects, and the live object transform
is applied immediately after creation. The next important calls are:

```text
FUN_8017911C -> CzanModelLiveObject_Init / live-object creation init body
FUN_801791F0 -> CzanModelManager_SetLiveObjectMatrix
FUN_8005941C -> CtsStageObj_CopyObjectTransform
FUN_80054F88 -> CzanModelOwner_SelectModeSlot
```

`FUN_8017911C` appears in one call site as the live-object creation/registration path
that returns a handle, but the pasted decompile is a tiny init body. Suggested cautious
name:

```text
CzanModelLiveObject_Init
```

Confirmed pasted body:

```text
object +0x120 = -1
FUN_80179330(...)
```

The surrounding allocator/registration wrapper still needs mapping before this is fully
understood.

`FUN_801791F0` applies matrix data to a live CzanModelManager object. Suggested name:

```text
CzanModelManager_SetLiveObjectMatrix
```

Confirmed behavior:

```text
if liveObjectHandle < 0:
  return
object = *(manager +0x1A4)[liveObjectHandle]
if object == 0:
  log "CzanEffMng::set_mtx(): NULL!!!!"
else:
  FUN_8017DA24(object +4, matrix)
```

`FUN_8017DA24` applies the model-manager global scale to a live object's matrix.
Suggested name:

```text
CzanModelLiveObject_ApplyGlobalScaleToMatrix
```

Confirmed behavior:

```text
FUN_80180C0C(liveObjectTransform +0xDC)
scale = *(FUN_80178270() +0x2A4)

multiply these 3x3 matrix floats by scale:
  +0xDC +0xE0 +0xE4
  +0xEC +0xF0 +0xF4
  +0xFC +0x100 +0x104
```

This is part of transform preparation after `CzanModelManager_SetLiveObjectMatrix`.
It helps make spawned model objects use the manager's global scale, but it does not
create or draw the model by itself.

`FUN_8005941C` copies a base or object-specific transform from a CtsStageObj/slot.
Suggested name:

```text
CtsStageObj_CopyObjectTransform
```

Confirmed behavior:

```text
if objectIndex != -1:
  objectTransform = CzanModel_GetObjectTransform(*(slot +0), objectIndex)
if objectTransform == 0:
  copy slot +0x1C to outMatrix through FUN_801459EC
  return 0
else:
compose/copy slot +0x1C with objectTransform through FUN_80145A0C
return 1
```

`FUN_801568D4` is now named:

```text
CzanModel_GetObjectTransform
```

It returns `*(model +0x20) + objectIndex * 0x30`, so `model +0x20` is the runtime
object transform array.

`FUN_80054F88` chooses which owner mode slot should supply transform data. Suggested
name:

```text
CzanModelOwner_SelectModeSlot
```

Confirmed behavior:

```text
default return owner +0x2D current mode
if owner +0xB0C4 == 2:
  may return owner +0x2E previous mode during transition/blend
  checks owner +0xE6E8, +0xB37C, and blend timer fields +0xB0BC/+0xB0C0
```

`FUN_801175B8` loads a group of CzanModels and builds lookup tables from named object
markers. Suggested name:

```text
CzanModelPositionSet_LoadFromLinkList
```

Confirmed behavior:

```text
CzanLinkManager_Init(local managers)
CzanModelPositionSet_Clear(positionSet)

positionSet[0] = linkDataCount
positionSet[1] = AllocObjectAligned(0, linkDataCount * 0x38, 0x20, 0)

for each linkData in linkDataList:
  CzanLinkManager_SetLink(linkData)
  blockCount = link.blockCount
  entry[0] = blockCount
  entry[1] = allocate blockCount CzanModels

  for each WII block:
    model = entry[1] + index * 0x2D0
    CzanModel_SetPrimaryBlock(model, blockData, blockSize)
    CzanModel_SetEnabled(model, 1)

  for six marker-name groups:
    scan every loaded model object name
    match strings from the table starting at PTR_s_s1_pos__80290C18
    build count/index tables for objects named like s1_pos...
```

The 0x38-byte entry layout begins as:

```text
entry +0x00 -> loaded model count / WII block count
entry +0x04 -> CzanModel array, count * 0x2D0
entry +0x08.. -> marker counts / lookup pointers for six marker groups
```

This function is useful for the model path because it proves another loader can build
`CzanModel` runtime data directly from WII blocks, but it still does not draw. It is a
named-position/marker lookup builder.

`FUN_8015759C` updates the runtime object transform arrays after the model block has
been built. Suggested name:

```text
CzanModel_UpdateObjectTransforms
```

Confirmed behavior:

```text
if model primary block at model +0x04 exists:
  objectTableHeader = *(model +0x04) +0x20
  objectCount = objectTableHeader[0]
  objectEntry = objectTableHeader[2]
  for each object:
    if objectEntry +0x94 < 0:
      copy/initialize model +0x1C transform into model +0x20 transform
    else:
      compose parent transform from model +0x20[parentIndex] into this object's transform

    if model +0x158 == 0:
      CzanModel_UpdateObjectAnimation(deltaOrScale, model, objectIndex)
    else:
      if byte model +0x50 != 0:
        copies/composes model +0x20 into model +0x24
      composes model +0x20/+0x24 into model +0x28
      if objectEntry +0x2C == 2 and no skip table says to skip:
        calls CzanModel_UpdateType2WeightedVectors(model, objectEntry, model +0x34 entry)
      CzanModel_UpdateObjectAnimation(deltaOrScale, model, objectIndex)

  CzanModel_FinalizeTransformUpdate(deltaOrScale, model)
  model +0x164 = 0
```

This is not the final draw function, but it is the model pose/object transform update.
It uses the object table parent index at object entry `+0x94` and the runtime transform
arrays at model `+0x1C`, `+0x20`, `+0x24`, and `+0x28`.

`FUN_8014F7D0` is now named:

```text
CzanModel_UpdateType2WeightedVectors
```

It prepares the generated float3 vector buffers used by type-2 object draw paths.
Confirmed behavior:

```text
objectEntry +0x9A -> submesh count
objectEntry +0x9C -> 0x40-byte per-submesh records
drawContext +0x04 -> per-submesh output buffer pointers, stride 0x10
model +0x28 -> source/composed object transforms, stride 0x30

for each submesh:
  output = *(drawContext +0x04 + submesh*0x10 + 4)
  source = objectEntry +0x9C + submesh*0x40

  first pass:
    clear one float3 per base weighted record
    for each weight record:
      transform source model +0x28 matrix/vector through FUN_801B0AD0
      scale by weight byte/float at source +0x3C through FUN_801B1040
      accumulate into output with FUN_801B0FE0

  second pass:
    clear one float3 per indexed/generated record
    for each index pair from drawContext submesh +0x0C:
      transform through FUN_801B0B30 using source +0x2C data
      scale by source +0x3C
      accumulate into output

  FlushDataCacheRange(output, generatedCount * 0x0C)

then FUN_801D2FF0 flushes/updates render state after the generated buffers are ready
```

So `model +0x34` is not a final vertex buffer by itself; it is draw/runtime metadata
whose submesh entries point at generated type-2 vector buffers.

`FUN_801576FC` finalizes a transform/update tick after all object transforms have
been processed. Suggested name:

```text
CzanModel_FinalizeTransformUpdate
```

Confirmed behavior:

```text
if model +0x150 == 0:
  if byte model +0x50 == 0 and primaryBlock +0x1C material/part table exists:
    for each top-level part:
      if part +0x30 frame-count != 0:
        advances runtime part timer at model +0x2C entry +0x0C
        wraps timer by last frame marker in part +0x34 table
        updates current frame index at runtime part entry +0x10 when not locked
      for each child part:
        same timer/frame update using child runtime offsets +0x38/+0x3C/+0x40

  byte model +0x50 = 0
  if model +0x94 != 0.0 and deltaOrScale != 0.0:
    deltaOrScale = model +0x94 * model +0x19C / FLOAT_802E9E38
  CzanModel_UpdatePartUvAnimation(deltaOrScale, model)
  if model +0x1B4 != 0:
    FUN_801579A8(deltaOrScale, model)
  model +0x150 = 1
```

New timing/update fields:

```text
model +0x50  -> dirty/update-needed byte
model +0x94  -> optional delta override
model +0x150 -> finalized-this-frame flag
model +0x19C -> animation/frame-rate divisor
model +0x1B4 -> optional extra update flag
model +0x2C  -> runtime part table, top-level stride 0xDC
```

`FUN_8015627C` updates one model animation channel. Suggested name:

```text
CzanModel_UpdateAnimationChannel
```

Confirmed behavior:

```text
channel = model +0x230 + channelIndex * 0x24

if holdFrame == 0:
  if channel duration at +0x240 > 0:
    channel timer +0x230 += channel speed +0x250 * model +0x19C / FLOAT_802E9E38
    handles forward/backward playback
    handles loop flag byte +0x238
    sets end/reached flags at bytes +0x244 and +0x245
  FUN_80157BCC(model)

if model +0x9C == 0 or channel target/index +0x234 < 0:
  if channelIndex == 0:
    CzanModel_UpdateObjectTransforms(delta, model)
else:
  if channel blend/transition flag +0x248 != 0 and holdFrame == 0:
    advances blend timer at +0x24C
    while blend timer < 1.0:
      CzanModel_BlendAnimationChannelFrame(delta, model, &channel timer, channelIndex)
      if channelIndex == 0:
        CzanModel_UpdateObjectTransforms(delta, model)
      return
    clears blend flag +0x248 and blend timer +0x24C

  if blend timer +0x24C == 0.0:
    CzanModel_ApplyAnimationChannelFrame(channel timer, model, channelIndex)
    if channelIndex == 0:
      CzanModel_UpdateObjectTransforms(delta, model)
```

Animation channel fields:

```text
model +0x230 + channel*0x24 -> channel timer/current frame
channel +0x238 -> loop flag
channel +0x240 -> duration/end frame
channel +0x244 -> reached/end flag
channel +0x245 -> active/ended flag
channel +0x248 -> blend/transition active flag
channel +0x24C -> blend/transition timer
channel +0x250 -> playback speed
```

`FUN_8015601C` applies one animation channel frame to the model's object local
transforms. Suggested name:

```text
CzanModel_ApplyAnimationChannelFrame
```

Confirmed behavior:

```text
if primary model block exists and byte model +0x50 == 0:
  channel = model +0x230 + channelIndex * 0x24
  duration = channel +0x240
  if duration > 0:
    frame = frame % duration, with non-looping channels clamped to duration
    objectAnimRecords =
      model +0x14 + (channel target/index +0x234) * objectCount * 0x74

    for each object:
      localTransform = model +0x1C + objectIndex * 0x30
      objectAnimRecord stride = 0x74

      if translation keyframes exist:
        CzanModel_EvaluateTranslationKeys(...)
        applies translation into localTransform through matrix helper at record +0x40

      if rotation keyframes exist:
        CzanModel_EvaluateRotationKeys(...)
        applies rotation into localTransform through matrix helper at record +0x4C

      if scale keyframes exist:
        CzanModel_EvaluateScaleKeys(...)
        builds scale matrix and multiplies it into localTransform

      stores last frame at objectAnimRecord +0x30
```

New animation record facts:

```text
model +0x14 -> per-channel/per-object animation records
object animation record stride -> 0x74
record +0x40 -> translation matrix/helper
record +0x4C -> rotation matrix/helper
record +0x5C -> scale matrix/helper
record +0x30 -> last applied frame
object local transforms -> model +0x1C, stride 0x30
```

ZAB keyframe evaluator names:

```text
FUN_8015D9C4 -> CzanModel_EvaluateTranslationKeys
FUN_8015DACC -> CzanModel_EvaluateRotationKeys
FUN_8015DBD8 -> CzanModel_EvaluateScaleKeys
```

Confirmed key layouts:

```text
translation key stride 0x10: frame/time, x, y, z
scale key stride       0x10: frame/time, x, y, z
rotation key stride    0x14: frame/time, qx, qy, qz, qw or equivalent 4-float rotation
```

All three evaluator functions start scanning at the cached key index, choose the key at
or before the current frame, interpolate to the next key when possible, hold the final
key when not looping, interpolate final-to-first over the channel duration when looping,
and return the chosen key index so the animation record can cache it.

`FUN_801564BC` applies one animation channel frame while blending into existing
object animation records. Suggested name:

```text
CzanModel_BlendAnimationChannelFrame
```

Confirmed behavior:

```text
if byte model +0x50 == 0 and channel duration > 0:
  frame = channel[0] modulo channel[4]
  if channel loop/relative flag is set, uses floor(channel[0] / channel[4])
  objectAnimRecords = model +0x14 + channelIndex * objectCount * 0x74

  for each object:
    resets cached keyframe cursors in the object animation record
    reads object local transform from model +0x1C
    blendWeight = channel[7]
    if object entry byte +0x27 is set:
      blendWeight = 1.0

    if rotation keyframes exist:
      evaluates rotation into temp matrix
      if record rotation was unused, copies temp matrix into record +0x4C
      else blends temp matrix into record +0x4C
      applies record +0x4C to local transform

    if scale keyframes exist:
      evaluates scale into temp vector/matrix
      if record scale was unused, copies into record +0x5C
      else blends into record +0x5C
      multiplies scale into local transform

    if translation keyframes exist:
      evaluates translation into temp vector
      if record translation was unused, copies into record +0x40
      else blends into record +0x40
      applies record +0x40 to local transform

    stores last frame at object animation record +0x30

  if channelIndex == 0:
    CzanModel_UpdateObjectTransforms(delta, model)
```

This is the transition/blend pair for `CzanModel_ApplyAnimationChannelFrame`.

`FUN_80156B70` updates render-part UV/scroll animation. Suggested name:

```text
CzanModel_UpdatePartUvAnimation
```

Confirmed behavior:

```text
partTable = primaryBlock +0x1C
partVersion = partTable[1]
partEntry = partTable[2]
partCount = partTable[0]

for each top-level part:
  if part +0x20 != 0 or part +0x24 != 0:
    marks part +0x1F renderable/animated
    updates runtime visible-part record at model +0x44:
      +0x0C scrolls one value backward/wrapped
      +0x1C scrolls one value forward/wrapped

  if first child part exists and child +0x1F != 0:
    updates next runtime visible-part record similarly

for each runtime visible-part record in model +0x44, stride 0x50:
  source part pointer is stored at runtimePart +0x30
  if source part +0x3C keyframe table exists and part version >= threshold:
    advances runtimePart +0x34
    wraps by the keyframe table's final time
    computes/interpolates two UV animation values into runtimePart +0x0C/+0x1C
```

New part/runtime facts:

```text
model +0x44 -> visible/renderable part runtime records, stride 0x50
model +0x48 -> visible/renderable part count
part +0x1F -> animated/renderable part flag
part +0x20/+0x24 -> UV scroll speeds
part +0x3A -> UV keyframe count
part +0x3C -> UV keyframe table
runtime part +0x30 -> source part pointer
runtime part +0x34 -> UV keyframe timer
runtime part +0x38/+0x3C -> current delta values
runtime part +0x40 -> base/offset used by interpolation
```

`FUN_801574B0` updates animation for one model object. Suggested name:

```text
CzanModel_UpdateObjectAnimation
```

Confirmed behavior:

```text
if model +0x1BC != 0:
  objectEntry = (*(model +0x04) +0x20)[2] + objectIndex * 0xA0
  if objectEntry byte +0x98 != 0:
    *(model +0x1C0 + objectIndex * 0x24 +0x10) = deltaOrScale
    CzanModel_SolveObjectAnimationTransform(stackTransform,
                                            model +0x1CC + objectIndex * 0xB4,
                                            model +0x1C0 + objectIndex * 0x24,
                                            objectEntry +0x9C)
    FUN_801459EC(model +0x1C + objectIndex * 0x30, stackTransform)
    if objectEntry parent index at +0x94 < 0:
      copy local transform into model +0x20 world transform
    else:
      compose parent world transform with local transform into this object's world transform
```

New model animation fields:

```text
model +0x1BC -> object animation enabled/state pointer or flag
model +0x1C0 -> per-object animation state, stride 0x24
model +0x1CC -> per-object animation data/context, stride 0xB4
object entry +0x98 -> has animation flag
object entry +0x9C -> object animation data pointer
```

`FUN_8018B4BC` solves the secondary per-object animation transform consumed by
`CzanModel_UpdateObjectAnimation`. Suggested name:

```text
CzanModel_SolveObjectAnimationTransform
```

Confirmed call shape:

```text
outMatrix         -> stack matrix copied into model +0x1C + objectIndex * 0x30
objectWorkspace   -> model +0x1CC + objectIndex * 0xB4
objectAnimState   -> model +0x1C0 + objectIndex * 0x24
objectAnimConfig  -> objectEntry +0x9C
```

Confirmed behavior:

```text
composes objectAnimState matrices/pointers into a current matrix
extracts and normalizes translation vectors
uses objectAnimConfig flags and counters to mirror/correct the solved direction
keeps previous/current matrix state in objectWorkspace
blends/corrects the object transform when objectWorkspace is in its transition state
writes the solved matrix back to outMatrix
copies the current composed matrix back into objectWorkspace +0x20
clears objectWorkspace +0x50 after the first frame/setup path
```

This is downstream from raw ZAB key evaluation. The ZAB keyframes populate runtime
object animation state first; this helper turns that state into the final local object
matrix used by the model transform arrays.

`FUN_8014C6CC` is the large primary model-block relocation/runtime-build function.
Suggested name:

```text
CzanModel_BuildRuntimeData
```

Confirmed high-level behavior:

```text
if primary block pointer stored at model +0x04 is missing:
  return 0

primaryBlock = *(model +0x04)

if primaryBlock +0x2C != 0:
  sets byte model +0x6D = 1

if primaryBlock +0x18 exists:
  relocates the texture/frame table when primaryBlock +0x24 == 0
  reads two shorts from the table header
  uses header[0] unless it is zero, then uses header[1] * 2
  allocates two helper arrays at model +0x5C and model +0x60
  array byte size = frameCountLikeValue * 4

if primaryBlock +0x1C exists:
  relocates material/part data when primaryBlock +0x24 == 0
  stores the material/part table at model +0x4C
  chooses part-entry stride 0x38 or 0x50 based on table version
  table[0] is top-level part/material count
  table[1] is the version/format float used for the 0x38 vs 0x50 stride test
  table[2] points to the first part/material entry
  child/subpart count is the short at part +0x2A
  child/subpart pointer is part +0x2C
  UV/keyframe pointer fields are relocated from part +0x34, +0x3C, +0x44, +0x4C
  counts visible/renderable parts and allocates model +0x44 as count * 0x50
  allocates/caches per-part data at model +0x2C

relocates object table at primaryBlock +0x20:
  object count -> model +0x98
  object entries are 0xA0 bytes
  allocates model +0x1C/+0x20/+0x24/+0x28 as objectCount * 0x30
  allocates model +0x14 as objectCount * model +0x9C * 0x74 when model +0x9C != 0
  allocates model +0x18 as objectCount * 0x10 when objectCount != 0
  scans object names for tags including trans, ZDRAW, COLLINE, and other known prefixes
  stores per-object flags in the object entries

builds additional runtime tables:
  display-list/index helper tables
  object/material remap tables
  bounding boxes per object/shape
  model +0x34 gets per-object shape/display-list helper records when needed
  model +0xB0/+0xB4 get bounding-box cache counts/pointers

calls FUN_801A48A0(primaryBlock, model +0x08)
sets primaryBlock +0x24 = 1
returns 1
```

Concrete runtime arrays allocated by this function:

```text
model +0x14 -> object animation records
              objectCount * model[0x9C] * 0x74, cleared
model +0x18 -> per-object skip/aux records
              objectCount * 0x10, cleared
model +0x1C -> object local matrices
              objectCount * 0x30
model +0x20 -> object world/composed matrices
              objectCount * 0x30
model +0x24 -> alternate/skinning transform array
              objectCount * 0x30
model +0x28 -> second alternate/skinning transform array
              objectCount * 0x30
model +0x2C -> per-part animation/render cache
              partCount * 0xDC, cleared
model +0x34 -> type-2 per-object helper records
              objectCount * 0x1C, only when needed
model +0x44 -> visible/animated part runtime records
              visiblePartCount * 0x50, cleared
model +0x48 -> visible/animated part count
model +0x5C -> texture/frame helper array
model +0x60 -> texture/frame helper array
model +0x98 -> object count
model +0xB8 -> part/material entry stride, 0x38 or 0x50
model +0x158 -> type-2 helper-present flag
```

Material/part build facts from the full paste:

```text
part +0x18 -> relocated pointer
part +0x1C/+0x1D -> first part index candidates for model +0x130
part +0x1F -> visible/animated runtime part flag
part +0x20/+0x24 -> UV scroll speeds, may be synthesized from keyframes
part +0x28 -> copied into runtimePart +0x4C
part +0x2A -> child/subpart count
part +0x2C -> child/subpart pointer
part +0x30/+0x34 -> UV key count/table used to update model +0x138
part +0x3A/+0x3C -> visible-part UV keyframe count/table
part +0x40/+0x44/+0x48/+0x4C -> newer-format extra UV/material tables
```

Object build facts from the full paste:

```text
object table header +0x00 -> object count
object table header +0x04 -> version/format float
object table header +0x08 -> object entries pointer
object entry stride -> 0xA0
object +0x27 -> name starts with "trans"
object +0x28 -> name matches the three-byte DAT_8029446A prefix
object +0x29 -> name starts with "ZDRAW"
object +0x2A -> object prefix/type id from the known prefix table
object +0x2B -> suffix/id parsed after the matched prefix
object +0x2C -> object type; type 2 triggers type-2 helper/runtime path
object +0x30/+0x34/+0x38 and +0x60 -> initial local matrix/translation inputs
object +0x94 -> parent index, forced to -1 for type-2 objects during init
object +0x98 -> object-animation flag used by CzanModel_UpdateObjectAnimation
object +0x9A -> submesh count
object +0x9C -> submesh/object animation config pointer, relocated
```

Type-2 helper facts:

```text
model +0x34 record stride -> 0x1C
record +0x00 -> submesh count copied from object +0x9A
record +0x04 -> allocated per-submesh records, submeshCount * 0x10
each submesh helper allocates remap/output buffers used by the type-2 vector path
model +0x158 is set when any object has type +0x2C == 2
```

This is the first real “make the ZMB usable at runtime” function. It still does not
directly draw, but it reveals the model block layout: material/part table at `+0x1C`,
object table at `+0x20`, relocation flag at `+0x24`, and object entries sized `0xA0`.

Important Ghidra note: if this decompiles as `void CzanModel_BuildRuntimeData(void)`,
the signature is wrong because of the `FUN_8012A130`/`FUN_8012A17C` saved-register
helper pattern. The logical signature remains:

```c
int CzanModel_BuildRuntimeData(int *model, int enabled);
```

The high 32 bits of the helper return are the `model` pointer, and the low 32 bits are
the normalized/boolean build mode forwarded by callers such as `CzanModel_SetEnabled`.

`FUN_8014E8E8` searches the object table inside the primary model block. Suggested
name:

```text
CzanModel_FindObjectIndexByName
```

Confirmed behavior:

```text
objectTableHeader = *(model +0x04) +0x20
objectCount = objectTableHeader[0]
objectEntry = objectTableHeader[2]
for index in 0..objectCount-1:
  if FUN_801332F0(objectEntry, objectName) == 0:
    return index
  objectEntry += 0xA0
return -1
```

This tells us the primary model block has an object table pointer/header at `+0x20`,
and each object entry is `0xA0` bytes. That is one of the first hard layout facts we
need for real ZMB model loading.

CtsStageObj_ResetModelBlocks
  if entry[4] != 0:
    MemoryPool_Free(0, entry[4])
  if entry[0] != 0:
    calls the loaded model object's destructor through its vtable
  if entry[1] != -1:
    releases the texture-manager slot through FUN_80146B8C(global texture manager)
  entry[0] = 0
  entry[1] = -1
  entry[2] = -1
  entry[3] = 0
  entry[4] = 0
  entry[5] = FLOAT_802E84D8
  entry[6] = FLOAT_802E84D8
  thunk_FUN_801B0120(entry + 7)
  clears 0x18 bytes at entry +0x13
  entry[0x19] = 0
  entry[0x1A] = 0

CtsStageObj_LoadContinuationBlock
  if continuationIndex < entry[3]:
    FUN_8014C68C(*entry, continuationBlock, continuationIndex)
    *(entry[4] + continuationIndex * 4) = *(*entry + 0x240)

CtsStageObj_StartAnimation
  entry[6] = (int)startFrame
  *(*entry + 0x250) = startFrame * entry[5]
  FUN_8015C548(*entry)

CtsStageObj_ApplyModelTransform
  if entry[0] != 0:
    CzanModel_DrawVisibleObjects(entry[0], arg1, entry +0x1C, arg2)

CtsStageObj_DrawModelWithFlags
  if entry[0] != 0:
    drawMode = 1 if flags bit 0 is set
    drawMode = 2 if bit 0 is clear and bit 1 is set
    drawMode = 0 otherwise
    FUN_8014ED4C(entry[0], arg1, entry +0x1C, arg2, drawMode)

ZmbZabModelEntry_UpdatePresentation
  if entry +0x254 != 0:
    if entry +0x70 has bit 0x10000:
      FUN_80059380(entry, stackMatrix)
      FUN_800259C0(gManager_802E70A8, arg1, stackMatrix)
    CtsStageObj_DrawModelWithFlags(entry, arg1, arg2, arg3)
```

`FUN_800594B8` is the direct wrapper into the CzanModel visible-object draw traversal.
Suggested name:

```text
CtsStageObj_ApplyModelTransform
```

Confirmed behavior:

```text
if stageObj[0] == 0:
  return
CzanModel_DrawVisibleObjects(stageObj[0], arg1, stageObj +0x1C, arg2)
```

The `stageObj +0x1C` argument is the base 3x4 transform initialized by
`Matrix34_SetIdentity`.

`FUN_8014F420` is now confirmed as the CzanModel draw traversal rather than a plain
transform setter. Suggested name:

```text
CzanModel_DrawVisibleObjects
```

Confirmed behavior:

```text
return unless model +0x04 exists and model +0x7C is nonzero
copy/compose the caller base matrix
store arg2 at model +0x128 and caller matrix at model +0x15C
optionally build an alternate transform through CzanModel_BuildSpecialObjectMatrix
walk the primary model object's 0xA0-byte table
skip hidden/runtime-disabled objects
for each visible object:
  compose matrices
  for each submesh:
    if object type +0x2C == 2:
      CzanModel_DrawType2PartTree(...)
    else:
      CzanModel_DrawStandardPartTree(...)
increment model +0x1A4 modulo model +0x1A0
mark model +0x164 = 1
```

`FUN_8014E96C` builds the special/alternate object matrix used by the visible-object
draw path when an external/base matrix is present. Suggested name:

```text
CzanModel_BuildSpecialObjectMatrix
```

Confirmed behavior:

```text
copy selected basis columns from baseMatrix into a temporary matrix
derive a local direction vector from DAT_802941DC/E0/E4 through baseMatrix
if model +0x140 is not mode 2, or the derived vector length is zero:
  copy baseMatrix into outMatrix
  replace outMatrix translation from objectMatrix
  outMatrix = baseMatrix * outMatrix

compute objectMatrix basis-vector lengths
build a scale matrix from those lengths
outMatrix = outMatrix * scaleMatrix

if model +0x140 == 2 and the derived vector length is nonzero:
  normalize the vector
  build a rotation angle from atan-like helper
  apply that rotation
  multiply objectMatrix/baseMatrix again
```

So this is a special draw transform/billboard-style adjustment. It is not where the
geometry or primitive data is decoded.

`FUN_80151E90` recursively walks the type-2 object part/material tree. Suggested name:

```text
CzanModel_DrawType2PartTree
```

Confirmed behavior:

```text
part = partTableBase + model[0x2E] * *partIndexSource
if childIndex != 0:
  part = parentPart[0x2C] + model[0x2E] * (childIndex - 1)

if part byte +0x13 != 0:
  if part byte +0x13 == 1:
    CzanModel_UpdateType2PartTexcoords(model, partIndexSource, drawContext, submeshIndex,
                                       drawContext[3] + submeshIndex * 0x10)
  else:
    FUN_8015BEFC(model, partIndexSource, part, drawContext, submeshIndex,
                 drawContext[3] + submeshIndex * 0x10)

for each child in part child table:
  if child byte +0x13 == 0:
    recurse only from the root call
  else if child byte +0x13 == 1:
    FUN_8015BD68(...)
  else:
    CzanModel_UpdateType2SpecialPartTexcoords(...)
```

Important fields:

```text
model +0xB8 / model[0x2E] -> part/material entry stride
part +0x13 -> render/container kind
part +0x2A -> child count
part +0x2C -> child part table pointer
drawContext +0x0C -> submesh/render metadata table, stride 0x10
```

`FUN_80155484` recursively walks the standard/non-type-2 part/material tree. Suggested
name:

```text
CzanModel_DrawStandardPartTree
```

Confirmed behavior:

```text
part = partTableBase + model[0x2E] * *partIndexSource
if childPass != 0:
  partForChildren = part[0x2C]

if part byte +0x13 != 0:
  if part byte +0x13 == 1:
    CzanModel_UpdateStandardPartTexcoords(model, partIndexSource, objectMatrix,
                                          drawContext[3] + submeshIndex * 0x10)
  else:
    CzanModel_UpdateStandardSpecialPartTexcoords(model, partIndexSource, part, objectMatrix,
                                                 drawContext[3] + submeshIndex * 0x10)

child = partForChildren[0x2C]
if child != 0:
  if child byte +0x13 == 0 and childPass == 0:
    recurse once into childPass 1
  else if child byte +0x13 == 1:
    FUN_801595BC(...)
  else:
    FUN_80159730(...)
```

The currently recovered leaf functions update per-vertex texcoord/projection buffers:

```text
type-2 path:     CzanModel_UpdateType2PartTexcoords / FUN_8015BD68
                 CzanModel_UpdateType2SpecialPartTexcoords / FUN_8015BEFC
standard path:   CzanModel_UpdateStandardPartTexcoords / FUN_801595BC
                 CzanModel_UpdateStandardSpecialPartTexcoords / FUN_80159730
```

`FUN_801595BC` is the standard path texcoord update for part byte `+0x13 == 1`.
Suggested name:

```text
CzanModel_UpdateStandardPartTexcoords
```

Confirmed behavior:

```text
baseMatrix = model +0xC8, unless model +0x12C is nonzero
vertexSlice = vertexCount / model +0x1A0
start = model +0x1A4 * vertexSlice
end = next slice, or vertexCount on the final slice

for each vertex in the active slice:
  read index pair from submesh +0x0C
  select position table entry from saved caller state +0x24
  select normal/vector table entry from saved caller state +0x2C
  transform/project through matrix helpers
  write two floats into *(submesh +0x04)

FlushDataCacheRange(*(submesh +0x04), vertexCount * 8)
```

`FUN_80159730` is the standard path texcoord update for part byte `+0x13 != 1`.
Suggested name:

```text
CzanModel_UpdateStandardSpecialPartTexcoords
```

Confirmed behavior:

```text
base vector = model +0xC8/model +0x12C
if part byte +0x13 != 4 and model +0x128 != 0:
  override base vector from *(model +0x128)

matrix = objectMatrix, unless model +0x160 != 0 then matrix = model +0x16C

if part byte +0x13 == 3:
  direct transformed-vector projection
else:
  if part byte +0x13 == 4:
    zero the projection vector
  transform position and normal/vector records
  normalize/project vector

write two floats per vertex to *(submesh +0x04)
FlushDataCacheRange(*(submesh +0x04), vertexCount * 8)
```

`FUN_8015BD68` is the type-2 path texcoord update for part byte `+0x13 == 1`.
Suggested name:

```text
CzanModel_UpdateType2PartTexcoords
```

Confirmed behavior:

```text
baseMatrix = model +0xC8, unless model +0x12C is nonzero
perSubmeshVectorTable = *(drawContext +0x04) + submeshIndex * 0x10 +4
use the same model +0x1A0/+0x1A4 active vertex slice scheme

for each vertex in the active slice:
  read position index from submesh +0x0C
  subtract baseMatrix translation from source position
  normalize the vector
  project through perSubmeshVectorTable
  write two floats into *(submesh +0x04)

FlushDataCacheRange(*(submesh +0x04), vertexCount * 8)
```

`FUN_8015BEFC` is the remaining type-2 special texcoord/projection leaf. Suggested
name:

```text
CzanModel_UpdateType2SpecialPartTexcoords
```

Confirmed behavior:

```text
base vector = model +0xC8/model +0x12C
if part byte +0x13 != 4 and model +0x128 != 0:
  override base vector from *(model +0x128)

perSubmeshVectorTable = *(drawContext +0x04) + submeshIndex * 0x10 +4
use model +0x1A0/+0x1A4 active vertex slice scheme

if model +0x15C == 0 or model +0x160 != 0:
  kind 4 / kind 2:
    transform indexed source vector against base vector, normalize/project
  other kinds:
    transform per-submesh vector directly
else if kind 3:
  direct transformed-vector projection through composed matrix
else:
  transform with external matrix model +0x15C, normalize/project

write two floats into *(submesh +0x04)
FlushDataCacheRange(*(submesh +0x04), vertexCount * 8)
```

So the four texcoord leaves are now accounted for.

`FUN_80157FE4` is the shared material/render-state setup helper used by the primitive
submit leaves. Suggested name:

```text
CzanModel_SetupPartRenderState
```

Confirmed behavior:

```text
drawArgs[0] -> part/material entry pointer
drawArgs[2] -> texture/runtime slot selector; negative uses the default model texture
drawArgs[3] -> display/config flag path through FUN_80143858 or FUN_801438A4
drawArgs[4] -> enables extra render state through FUN_801D7200
drawArgs[5]/[6] -> material/color mask inputs

binds the resolved texture through GXLoadTexObj_wrapper
applies part/material state through CzanModel_ApplyMaterialBlendMode / FUN_80177184
configures TEV, blend, alpha, texgen, and raster state through FUN_801Dxxxx wrappers
returns success when a usable texture/render state exists, otherwise zero
```

This helper is important because the primitive submitters do not just stream vertices;
they first ask this routine to choose the texture/material state. The PC renderer should
eventually turn this into a real `RenderState` object instead of treating it as a simple
boolean.

`FUN_80177150` applies material cull mode. Suggested name:

```text
CzanModel_ApplyMaterialCullMode
```

Confirmed behavior:

```text
if partMaterial != 0 and byte partMaterial +0x11 is nonzero:
  FUN_801D40E0(0)
else if forceCullBack != 0:
  FUN_801D40E0(1)
else:
  FUN_801D40E0(2)
```

This is a tiny material-state switch. The exact enum names for `FUN_801D40E0` are still
unconfirmed, but this is very likely the cull/face mode part of GX state.

`FUN_80177184` applies the material blend/alpha compare mode. Suggested name:

```text
CzanModel_ApplyMaterialBlendMode
```

Confirmed behavior:

```text
materialMode = partMaterial byte +0x12 & 0x7F
highBit      = partMaterial byte +0x12 >> 7

forceBlendEnabled forces highBit to 1

materialMode 3 -> RenderSetBlendMode(1, 4, 5, 5)
materialMode 2 -> RenderSetBlendMode(1, 0, 5, 5)
materialMode 1 -> RenderSetBlendMode(1, 4, 1, 5), then FUN_801D7200(1, 3, 0)
default        -> RenderSetBlendMode(1, 4, 5, 5)

if materialMode == 0 or forceAlphaCompare != 0:
  RenderSetAlphaUpdate(1)
  RenderSetAlphaCompare(7, 0, 1, 7, 0)
else:
  RenderSetAlphaUpdate(0)
  if highBit == 0:
    RenderSetAlphaCompare(4, 0xA0, 0, 3, 0xFF)
  else:
    RenderSetAlphaCompare(4, 0, 0, 3, 0xFF)
```

So material byte `+0x12` is a packed blend/alpha mode byte: low seven bits are the
mode, and the high bit changes the alpha compare reference from `0xA0` to `0`.

`FUN_801772E8` configures the vertex attribute layout for a material/submesh. Suggested
name:

```text
CzanModel_SetupMaterialVertexAttributes
```

Confirmed behavior:

```text
submesh +0x06 short -> position source mode
submesh +0x24       -> position array base when +0x06 != 1
submesh +0x34       -> color array base
submesh +0x30       -> generated texcoord array base
submesh +0x2C       -> normal/vector array base
part byte +0x10     -> enables normal/vector attr 10

attr 9  -> position, stride 0x0C when array-backed
attr 10 -> normal/vector, stride 0x0C when array-backed
attr 11 -> color, stride 4 when array-backed
attr 13 -> generated texcoord, stride 8 when array-backed
```

This gives the PC renderer a clearer vertex declaration for ZMB drawing. We now know
which submesh offsets back each GX attribute.

The low-level GX wrapper functions used by the primitive submitters are now named:

```text
FUN_801D3DF0 -> RenderBeginPrimitiveBatch
FUN_801D3B70 -> RenderFlushPendingState
FUN_801D5D80 -> RenderFlushTexGenState
FUN_801D6690 -> RenderFlushNoOpState
FUN_801D5CF0 -> RenderCopyTexGenState
FUN_801D29A0 -> RenderFlushVertexDescriptorState
FUN_801D2F30 -> RenderFlushVertexAttributeFormatState
FUN_801D2A50 -> RenderRecomputeVertexStride
FUN_801D77B0 -> RenderFlushProjectionState
FUN_801D7A90 -> RenderFlushViewportState
FUN_801D7D10 -> RenderFlushMatrixIndexState
FUN_801D2B80 -> RenderClearVertexDescriptors
FUN_801D2FB0 -> RenderSetVertexArray
FUN_801D2BC0 -> RenderSetVertexAttrDescriptor
FUN_801D2730 -> RenderSetVertexAttrFormat
FUN_801D7110 -> RenderSetBlendMode
FUN_801D7240 -> RenderSetAlphaUpdate
FUN_801D6B70 -> RenderSetAlphaCompare
```

`RenderBeginPrimitiveBatch` is the `GXBegin` wrapper. It flushes pending render state,
then writes the primitive command and vertex count to the GX FIFO:

```text
DAT_CC008000 = primitiveType | vertexFormat
RAM_CC008000 = vertexCount
```

In the Czan model submitters, the common primitive type is `0x98`.

`RenderFlushPendingState` consumes the render context dirty bits at context `+0x5FC`.
The bits we care about immediately are:

```text
0x01 -> texture-generator dependency state, calls RenderFlushTexGenState
0x02 -> no-op flush slot, calls RenderFlushNoOpState
0x08 -> vertex descriptor state, calls RenderFlushVertexDescriptorState
0x10 -> vertex attribute format/array state, calls RenderFlushVertexAttributeFormatState
0x18 -> recomputed vertex stride/count, calls RenderRecomputeVertexStride
0x04000000 -> matrix/index words, calls RenderFlushMatrixIndexState(0) and (5)
0x08000000 -> projection state, calls RenderFlushProjectionState
0x10000000 -> viewport state, calls RenderFlushViewportState
```

It also flushes BP/register ranges for TEV/color/texture state and clears dirty bits.

`RenderFlushVertexDescriptorState` writes the two descriptor words at context `+0x14`
and `+0x18` to GX registers `0x50` and `0x60`, then writes a derived descriptor summary
to register `0x1008`.

`RenderFlushVertexAttributeFormatState` walks the dirty format byte at context `+0x5FB`.
For each dirty vertex format slot, it writes the packed attribute format words from
context `+0x1C`, `+0x3C`, and `+0x5C` to GX registers `0x70`, `0x80`, and `0x90`.

`RenderRecomputeVertexStride` recalculates the active vertex stride/count at context
`+0x06` from the descriptor words and the small component-size lookup tables.

`RenderFlushTexGenState` validates texture-coordinate generator/source dependencies.
It reads the active texgen configuration from context `+0x254`, source pairs from
context `+0x170`, and calls `FUN_801D5CF0` for missing texgen/source state.

`RenderCopyTexGenState` copies cached texture-generator state from one slot to another.
It takes two 10-bit values from source slot `+0x564`, stores them into destination slot
`+0x108` and `+0x128`, folds in enable/type bits from source slot `+0x584`, then writes
both destination words through FIFO command `0x61`.

`RenderFlushProjectionState` writes seven projection-related words from context
`+0x528..+0x540` through FIFO command `0x10` starting at register/index `0x61020`.

`RenderFlushViewportState` writes the viewport transform through FIFO command `0x10`
at register/index `0x5101A`, deriving scale/offset values from context
`+0x544..+0x560`.

`RenderFlushMatrixIndexState` writes either context `+0x80` to register pair
`0x30/0x1018` or context `+0x84` to register pair `0x40/0x1019`. The pending-state
flush uses selector `0` and selector `5`.

`RenderClearVertexDescriptors` resets the vertex descriptor context to defaults
(`context +0x14 = 0x200`, `+0x18 = 0`) and marks dirty bit `0x08`.

`RenderSetVertexArray` is the `GXSetArray` wrapper. Attribute `0x19` is normalized to
attribute `10`; then it writes paired GX registers:

```text
0xA0 | (attribute - 9) -> arrayBase & 0x3FFFFFFF
0xB0 | (attribute - 9) -> stride
```

Known model attributes:

```text
9  -> position array
10 -> normal/vector array
11 -> color array
13 -> generated texcoord array
```

`RenderSetVertexAttrDescriptor` is the `GXSetVtxAttrFmt`-style wrapper for one vertex
format. It packs attr type/component type/component count into context words at
`+0x1C`, `+0x3C`, and `+0x5C`, then marks dirty bit `0x10`.

`RenderSetVertexAttrFormat` is the `GXSetVtxDesc`-style wrapper. It stores per-attribute
format bits and marks dirty bit `0x08`.

Observed Czan model values:

```text
format 0 -> disabled
format 1 -> direct/indexed mode without a separate array base
format 3 -> array-backed mode; caller also calls RenderSetVertexArray
```

`RenderSetBlendMode`, `RenderSetAlphaUpdate`, and `RenderSetAlphaCompare` cover the
blend/alpha half of material state. The alpha compare packed register is:

```text
0xF3000000 |
((op & 3) << 22) |
((compare1 & 7) << 19) |
((compare0 & 7) << 16) |
((reference1 & 0xFF) << 8) |
(reference0 & 0xFF)
```

`FUN_801586C0` is a real GX/display-list submit leaf. Suggested name:

```text
CzanModel_SubmitPartPrimitive
```

Confirmed behavior:

```text
validate/setup part with FUN_80157FE4
copy material/render state from model fields
configure vertex attrs:
  attr 9  -> position table
  attr 10 -> normal/vector table
  attr 11 -> color table or generated color
  attr 13 -> generated texcoord buffer at *(submesh +0x04)
if model +0x164 == 0 and model +0x168 != 0:
  regenerate texcoords for active model +0x1A4 slice
for each primitive batch:
  RenderBeginPrimitiveBatch(0x98, 0, vertexCount)
  write indices/colors/texcoords to 0xCC008000
```

This is one of the main functions needed for the PC renderer because it identifies the
actual arrays and stream order used by the GameCube/Wii GX path.

`FUN_801528B4` is a full material/part draw traversal with render-state setup.
Suggested name:

```text
CzanModel_DrawMaterialPartTree
```

Confirmed behavior:

```text
select current part from partTableBase + model[0x2E] * *partIndexSource
or select child part when childIndex != 0
apply part/material setup through CzanModel_ApplyMaterialCullMode / FUN_80177150
and CzanModel_SetupMaterialVertexAttributes / FUN_801772E8
handle special model modes model +0x280:
  5 -> FUN_80159A04
  6 -> FUN_8015A2B8
bind texture set slots through BindTextureFromTextureSet / GXLoadTexObj_wrapper
configure TEV/color/alpha state through many FUN_801Dxxxx wrappers
emit primitive batches to the GX write-gather pipe
if model +0x168 is enabled:
  kind 1 -> CzanModel_SubmitPartPrimitive
  other kinds -> CzanModel_SubmitSpecialPartPrimitive
recurse into child parts
optionally draw synthetic/extra part at model +0x28C
```

This function is more important for rendering than the previous part-tree wrappers:
it contains texture binding, TEV setup, vertex attribute setup, and primitive emission.

`FUN_80158DB8` is the non-kind-1 primitive submit leaf. Suggested name:

```text
CzanModel_SubmitSpecialPartPrimitive
```

Confirmed behavior:

```text
validate/setup part with FUN_80157FE4
configure vertex attrs:
  attr 9  -> position table
  attr 10 -> normal/vector table
  attr 11 -> color table or generated color
  attr 13 -> generated texcoord buffer at *(submesh +0x04)
if model +0x164 == 0 and model +0x168 != 0:
  regenerate texcoords for active model +0x1A4 slice
for each primitive batch:
  RenderBeginPrimitiveBatch(0x98, 0, vertexCount)
  write indices/colors/texcoords to 0xCC008000
```

It is nearly parallel to `CzanModel_SubmitPartPrimitive`, but handles part kinds other
than `+0x13 == 1`, including the kind-3 direct projection path.

`FUN_8015ABC8` is the type-2 object-path primitive submit leaf for part kind 1.
Suggested name:

```text
CzanModel_SubmitType2PartPrimitive
```

Confirmed behavior:

```text
validate/setup part with CzanModel_SetupPartRenderState / FUN_80157FE4
use per-submesh vector table from drawContext[1] + submeshIndex * 0x10 + 4
configure the same GX vertex attrs as CzanModel_SubmitPartPrimitive
if model +0x164 == 0 and model +0x168 != 0:
  regenerate texcoords for the active model +0x1A4 slice
for each primitive batch:
  RenderBeginPrimitiveBatch(0x98, 0, vertexCount)
  write position index, type-2 vector data/index, color index, and texcoord index
  to 0xCC008000
```

`FUN_8015B2D8` is the type-2 object-path primitive submit leaf for non-kind-1 parts.
Suggested name:

```text
CzanModel_SubmitType2SpecialPartPrimitive
```

Confirmed behavior:

```text
validate/setup part with CzanModel_SetupPartRenderState / FUN_80157FE4
use per-submesh vector table from drawContext[1] + submeshIndex * 0x10 + 4
configure the same GX vertex attrs as the other submit leaves
contains the type-2 special generated-texcoord formulas for kinds 2, 3, 4, and the
external-matrix path
emits primitive 0x98 batches to the GX write-gather pipe
```

So the primitive submit layer now has four known leaves:

```text
standard kind 1      -> CzanModel_SubmitPartPrimitive          / FUN_801586C0
standard other kinds -> CzanModel_SubmitSpecialPartPrimitive   / FUN_80158DB8
type-2 kind 1        -> CzanModel_SubmitType2PartPrimitive     / FUN_8015ABC8
type-2 other kinds   -> CzanModel_SubmitType2SpecialPartPrimitive / FUN_8015B2D8
```

`FUN_80153F44` is the base/fallback material tree renderer. Suggested name:

```text
CzanModel_DrawBaseMaterialPartTree
```

Confirmed behavior:

```text
accepts partTableBase == 0 for default material state
applies material state through CzanModel_ApplyMaterialCullMode,
CzanModel_SetupMaterialVertexAttributes, and CzanModel_ApplyMaterialBlendMode
configures texture/color/alpha/TEV state through FUN_801Dxxxx wrappers
binds texture set entries and emits primitive batches when texture state is available
recurses into child parts:
  child byte +0x17 clear -> CzanModel_DrawBaseMaterialPartTree
  child byte +0x17 set   -> CzanModel_DrawMaterialPartTree
if model +0x168 is enabled:
  kind 1 -> CzanModel_SubmitPartPrimitive
  other  -> CzanModel_SubmitSpecialPartPrimitive
```

So the material render layer now has two tree walkers:

```text
CzanModel_DrawBaseMaterialPartTree / FUN_80153F44
CzanModel_DrawMaterialPartTree     / FUN_801528B4
```

`FUN_800594DC` is the flag-aware companion wrapper. Suggested name:

```text
CtsStageObj_DrawModelWithFlags
```

Confirmed behavior:

```text
if stageObj[0] == 0:
  return

drawMode = 0
if flags bit 0 is set:
  drawMode = 1
else if flags bit 1 is set:
  drawMode = 2

FUN_8014ED4C(stageObj[0], arg1, stageObj +0x1C, arg2, drawMode)
```

`FUN_8004E220` is a higher-level derived ZMB/ZAB entry presentation/update method.
Suggested name:

```text
ZmbZabModelEntry_UpdatePresentation
```

Confirmed behavior:

```text
if entry +0x254 == 0:
  return

if entry +0x70 has bit 0x10000:
  FUN_80059380(entry, stackMatrix)
  FUN_800259C0(gManager_802E70A8, arg1, stackMatrix)

CtsStageObj_DrawModelWithFlags(entry, arg1, arg2, arg3)
```

This tells us bit `0x10000` in the packed parser flags at entry `+0x70` enables a
movie-manager update using a matrix from `FUN_80059380`. The actual model-side work
continues through `CtsStageObj_DrawModelWithFlags` into `FUN_8014ED4C`.

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

`FUN_80160368` is the cleanup/release helper for a `CzanLinkManager`. Suggested name:

```text
CzanLinkManager_Release
```

Confirmed behavior:

```text
if linkManager != 0 and releaseMode > 0:
  MemoryPool_Free(0, linkManager)
return linkManager
```

Most local stack-link-manager cleanup calls pass `-1`, so they do not release allocator
memory.

`FUN_80144CF0` is the memory-pool free wrapper. Suggested name:

```text
MemoryPool_Free
```

Confirmed behavior:

```text
lock(DAT_802EE174 + poolIndex * 0x34)
if allocation != 0:
  FUN_801DC5D0((&DAT_802EE158)[poolIndex * 0x0D], allocation)
unlock(DAT_802EE174 + poolIndex * 0x34)
```

Some calls pass extra zero arguments after the useful parameters. The callee decompiles
for `CSelModeEntry_AddUiObject` and `CSelModeEntry_AddChildUiObject` show only the first
two parameters are used; the extra registers appear to be caller convention/noise.

The lower allocator lock wrappers are now named:

```text
FUN_801A9430 -> Runtime_EnterCriticalSection
FUN_801A9470 -> Runtime_LeaveCriticalSection
FUN_801AD410 -> Runtime_GetTimebase
FUN_801AD440 -> Runtime_GetBootTime
FUN_801AC0A0 -> Runtime_GetCurrentThreadContext
FUN_801AA6E0 -> MemoryMutex_Lock
FUN_801AA7C0 -> MemoryMutex_Unlock
FUN_801DC520 -> MemoryPool_AllocateAligned
```

`Runtime_EnterCriticalSection` reads the PowerPC MSR and returns a token consumed by
the matching restore helper `FUN_801A9470`. The decompile shows the low word as the
original MSR and the high word as a masked/shifted copy of selected MSR state.
`Runtime_LeaveCriticalSection` returns `(MSR >> 0x0F) & 1` from the restored token.

`Runtime_GetTimebase` is the raw `FUN_801AD410` timebase read helper. `Runtime_GetBootTime`
wraps it with `Runtime_EnterCriticalSection` / `Runtime_LeaveCriticalSection` and adds
the boot offset stored at `DAT_800030D8/DAT_800030DC`.

`Runtime_GetCurrentThreadContext` returns `DAT_800000E4`, which is the current
thread/context pointer used by the allocator lock code. `MemoryMutex_Lock` reads and
writes fields around this context's `+0x2D0..+0x2F8`.

`MemoryMutex_Lock` is a recursive/thread-aware allocator lock. It records the owner at
mutex `+0x08`, increments the recursion count at `+0x0C`, and links the mutex into the
current thread/context list through fields `+0x10/+0x14`. When another context owns the
mutex, it records the pending mutex at current context `+0x2F0` and waits/yields through
`FUN_801AC390` and `FUN_801AD0F0`.

`MemoryMutex_Unlock` decrements the recursion count and, when it reaches zero, unlinks
the mutex from the owner list, clears owner `+0x08`, may select a waiter through
`FUN_801AC1A0`, and wakes/continues waiters through `FUN_801AD1E0`.

`MemoryPool_AllocateAligned` normalizes zero-size allocations to one byte, rounds the
size up to four bytes, optionally locks allocator `+0x20` when allocator flags `+0x38`
have bit `4`, then dispatches to:

```text
alignment < 0  -> FUN_801DC200(allocator, roundedSize, -alignment)
alignment >= 0 -> FUN_801DC120(allocator, roundedSize, alignment)
```

`FUN_8012A130`, `FUN_8012A134`, `FUN_8012A144`, `FUN_8012A150`, `FUN_8012A154`,
`FUN_8012A158`, `FUN_8012A15C`, `FUN_8012A164`, and `FUN_8012A1B0` are not
gameplay/runtime APIs. The named spill variants are documented in
`docs/runtime_addresses.md` as `RuntimeContext_SpillSavedRegistersR*ToR31` helpers.
They are compiler helper stubs Ghidra emits for spilling nonvolatile registers to the
implicit `r11` context area. `FUN_8012A1B0` is the paired empty return/restore marker
for that helper family.

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
`MemoryPool_Free(0, entry)`.

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
  If objectId != -1, calls CzanUiManager_CloneObjectGroup(entry->uiManager), stores the returned handle
  in the same handle array, then increments objectHandleCount.

FUN_801106C4 -> CSelModeEntry_ResetObjectAnimation
  Forwards the selected object handle to the Czan UI manager reset/clear-animation helper.

FUN_80110680 -> CSelModeEntry_IsObjectAnimationDone
  Returns CzanUiManager_IsObjectGroupAnimationDone(entry->uiManager, handle) when
  the stored object handle is valid.

FUN_801106D8 -> CSelModeEntry_GetCachedAnimationId
  Returns the cached animation id stored at entry +0x24 + objectSlot*4.

FUN_801105FC -> CSelModeEntry_StartObjectAnimation
  Configures mode/playback flags, starts the selected animation with a start frame,
  and caches the animation id at entry +0x24 + objectSlot*4.

FUN_80110754 -> CSelModeEntry_SetAnimationOrLayout
  If animationId == -1, calls FUN_801751B8(entry->uiManager, handle, animationData).
  Otherwise calls FUN_80175240(entry->uiManager, handle).

FUN_801106E8 -> CSelModeEntry_SetPositionOrLayout
  If childObjectIndex == -1, calls FUN_801750E4(entry->uiManager, handle, layoutData).
  Otherwise calls FUN_8017515C(entry->uiManager, handle).

FUN_80110B80 -> CSelModeEntry_PlayObject
  Calls FUN_80175448(entry->uiManager, handle, childObjectIndex, textureFrameOrAuto,
  updateSpriteDimensions).

FUN_80110BF4 -> CSelModeEntry_SetTransformTriplet
  Copies three 32-bit values into entry +0x44, +0x48, +0x4C.

FUN_80110C10 -> CSelModeEntry_GetTransformState1
  Returns entry +0x48.

FUN_80110C18 -> CSelModeEntry_GetTransformState2
  Returns entry +0x4C.
```

`FUN_80175448` is best named:

```text
CzanUiManager_SetObjectTextureFrame
```

Suggested signature:

```c
void CzanUiManager_SetObjectTextureFrame(
    int uiManager,
    int objectGroupHandle,
    int childObjectIndex,
    int textureFrameOrAuto,
    int updateSpriteDimensions);
```

Confirmed behavior:

```text
uiManager +0x04                     -> object group table
objectGroupHandle * 0x28 +0x20      -> child object pointer array
childObjectIndex                    -> selects one CzanUiObjectInstance
CzanUiObjectInstance +0x148         -> requested/override texture frame
CzanUiObjectInstance +0xA8/+0x14C   -> automatic texture frame when textureFrameOrAuto == -2
CzanUiObjectInstance +0x24          -> attached CzanSpriteObject
CzanSpriteObject +0x24              -> texture set handle
CzanSpriteObject +0x34              -> active texture frame/index
CzanSpriteObject +0x28              -> active texture descriptor/header
CzanSpriteObject +0x100/+0x108      -> texture dimensions copied from the TPL descriptor
CzanSpriteObject +0x78/+0x7C        -> half width/height as floats
```

So `CSelModeEntry_PlayObject` is not a full draw/layout function. It selects or refreshes
the texture frame for a child object and optionally updates dimensions from the TPL
descriptor. The missing composition behavior is still in the helpers that apply layout,
visibility, transforms, and animation state.

These helpers strongly suggest the mode-select background/UI is not raw texture drawing.
`CSelMode_OnEnter` creates UI/model object handles from Czan link blocks and then applies
animation/layout commands through the `FUN_80175xxx` family. The next renderer target
should be those Czan UI/object functions, not direct `selTitle` texture guessing.

## Czan Sprite Draw Dispatch

`FUN_801729B4` is the per-object draw wrapper above `CzanSpriteObject_Draw`. Suggested
name:

```text
CzanUiObjectInstance_Draw
```

Important behavior:

```text
object +0x000..+0x01C -> up to eight callback/child pointers run before/after draw
object +0x020 -> owning Czan UI manager
object +0x024 -> attached CzanSpriteObject passed to CzanSpriteObject_Draw
object +0x028 -> resolved linked sprite candidate
object +0x148 -> texture-frame override; -1 affects skip/link path
object +0x160 -> object state/id used when resolving linked child sprite
object +0x16C -> current animation index
object +0x173 -> draw-suppression flag checked before drawing; 0 enters the draw path, nonzero suppresses it
object +0x198 -> CAE descriptor
object +0x1A0 -> callback ordering flag
object +0x1A4 -> owning object-group handle
```

When drawing is allowed, it writes:

```text
sprite +0x1A8 = object +0x28
```

Then it calls:

```text
CzanSpriteObject_Draw(object +0x24, 0, 0, -1)
```

or, when the current animation entry flag bit is set:

```text
CzanSpriteObject_Draw(object +0x24, &DAT_802EF3A8, &DAT_802EF368, -1)
```

`FUN_80174BA8`, `FUN_80174C58`, and `FUN_80174CF8` are the Czan draw traversal helpers:

```text
FUN_80174BA8 -> CzanUiManager_DrawObjectListReverse(objectList, drawLayerFilter)
FUN_80174C58 -> CzanUiManager_DrawObjectGroupInListOrder(uiManager, objectGroupHandle)
FUN_80174CF8 -> CzanUiManager_DrawChildObject(uiManager, objectGroupHandle, childObjectIndex)
```

`CzanUiManager_DrawObjectListReverse` draws the global object list from last to first,
skipping objects unless `object +0x181 == 1` and the optional layer filter matches
`object +0x18C`.

`CzanUiManager_DrawObjectGroupInListOrder` filters the global list down to children of
one object group, preserving the same reverse global draw order.

`CzanUiManager_DrawChildObject` directly draws one child from a group.

`FUN_801750A8` controls whether every child in an object group participates in draw
traversal. Suggested name:

```text
CzanUiManager_SetObjectGroupDrawEnabled
```

Confirmed behavior:

```text
group = uiManager + 0x04 + objectGroupHandle * 0x28
for each child in group:
  object +0x181 = drawEnabled
```

This is separate from `CzanUiManager_SetObjectGroupEnabled` / `FUN_80174F04`, which
writes `object +0x173`. Despite the older host name, `+0x173` is inverted: the
object draw wrapper suppresses drawing when it is nonzero. The draw traversal in
`CzanUiManager_DrawObjectListReverse` still requires `object +0x181 == 1`.

The global Czan UI draw is reached from `FUN_800FEB58`, suggested name:

```text
UiRootManager_DrawFrame
```

Its setup/resource-loading pair is `FUN_800FE548`, suggested name:

```text
UiRootManager_LoadResource
```

Confirmed behavior:

```text
linkData is a WII resource.

blocks 0..3:
  CzanUiManager_CreateObjectGroup(*(DAT_802E71B8 +0x270), block, 0, 0)
  stored at uiRootManager[1..4]

block 4:
  allocates uiRootManager[0x0C], then calls UiRootSubManager_LoadCzanGroups / FUN_80100944

block 5:
  allocates uiRootManager[0x0D], then calls
  UiRootSubManager_LoadCzanGroupsWithTexture / FUN_80104538

after block 5:
  allocates uiRootManager[0x0E], then calls
  UiRootSubManager_InitTextureFrameGroups / FUN_80106054
  This object is not initialized from a WII block directly.

block 6:
  allocates uiRootManager[0x0F], then calls
  UiRootSubManager_LoadLinkedObjectGroup / FUN_80106C34

block 7:
  initializes uiRootManager[0x10]

block 8:
  initializes uiRootManager[0x12]

blocks 9..13:
  CreateTextureFromTplResource(*(DAT_802E71B8 +0x26C), block, blockSize, -1)
  stored at uiRootManager[5..9]

uiRootManager[0x11]:
  initialized after block 8 using uiRootManager[8]

uiRootManager[0] = 1 when setup finishes.
```

Observed Spanish resource:

```text
input/DATA/2Dcommon/comAF_SP.bin
size = 0xA71C40
top-level WII block count = 14

block 0 -> uiRootManager[1] Czan object group
block 1 -> uiRootManager[2] Czan object group
block 2 -> uiRootManager[3] Czan object group
block 3 -> uiRootManager[4] Czan object group
block 4 -> uiRootManager[0x0C] via UiRootSubManager_LoadCzanGroups
block 5 -> uiRootManager[0x0D] via UiRootSubManager_LoadCzanGroupsWithTexture
block 6 -> uiRootManager[0x0F] via UiRootSubManager_LoadLinkedObjectGroup
block 7 -> uiRootManager[0x10] loader still pending export
block 8 -> uiRootManager[0x12] loader still pending export
block 9 -> uiRootManager[5] texture slot
block 10 -> uiRootManager[6] texture slot
block 11 -> uiRootManager[7] texture slot
block 12 -> uiRootManager[8] texture slot
block 13 -> uiRootManager[9] texture slot
```

This is the missing construction step before `UiRootManager_DrawFrame` can draw anything
from the global Czan object list.

`FUN_800FEC3C` creates a UI-root reference object group. Suggested provisional name:

```text
UiRootManager_CreateReferenceObjectGroup
```

Confirmed behavior:

```text
if uiRootManager[1] == -1:
  return -1

newGroup = CzanUiManager_CloneObjectGroup(global Czan UI manager, uiRootManager[1], 2, 0)

if referenceChildIndex != -1:
  CzanUiManager_LinkObjectGroupToReferenceObject(global Czan UI manager,
                                                 newGroup,
                                                 referenceObjectGroupHandle,
                                                 referenceChildIndex,
                                                 linkMode)

  if newGroup != -1:
    referenceObject = FUN_80175804(global Czan UI manager,
                                   referenceObjectGroupHandle,
                                   referenceChildIndex)
    FUN_8017559C(global Czan UI manager, newGroup, referenceObject - 8, 0)

  color = FUN_801761E0(global Czan UI manager,
                       referenceObjectGroupHandle,
                       referenceChildIndex,
                       0)
  FUN_80175B00(global Czan UI manager, newGroup, colorBytes, -1)
  CzanUiManager_SetChildObjectEnabled(global Czan UI manager,
                                      referenceObjectGroupHandle,
                                      referenceChildIndex,
                                      1)

return newGroup
```

So the function is not loading a resource by itself. It creates/clones a group from
the UI root base group at `uiRootManager[1]`, then optionally attaches that new group
to a reference child object and copies the child's color/settings.

`FUN_80173D18` clones/derives a Czan object group from an existing group. Suggested
name:

```text
CzanUiManager_CloneObjectGroup
```

Confirmed behavior:

```text
find first free object-group slot where group +0x20 == 0
if none found:
  return -1

sourceGroup = uiManager->groups[sourceObjectGroupHandle]
newGroup = first free group slot

newGroup[+0x1C] = sourceGroup[+0x1C] descriptor pointer
newGroup[+0x04] = *(ushort *)(descriptor + 0x08) child count
newGroup[+0x20] = AllocObjectAligned(childCount * 4, align 0x20)

for each child descriptor:
  sprite = AllocObjectAligned(0x1D8, align 0x20)
  CzanSpriteObject_Init(sprite)

  if descriptor type == 2:
    make dummy 8x8 sprite with no texture
  else:
    copy texture/resource fields from the matching source child sprite
    BindTextureFromTextureSet(sprite->textureSlot, sprite, sprite->textureFrame)

  object = AllocObjectAligned(0x1B4, align 0x20)
  CzanUiObjectInstance_Init(object)
  newGroup.childArray[index] = object

  object[+0x20] = uiManager
  object[+0x24] = sprite
  object[+0x198] = child descriptor
  object[+0x16C] = initialAnimIndex
  object[+0x1A4] = newGroup handle

  if cloneFlags low byte == 1 or 2:
    start/run animation initialAnimIndex and reset animation state
  else if cloneFlags low byte == 3 or 4:
    call alternate setup helper

  child descriptor flags at +0x1A set object enable/draw/animation fields

mark UI manager and new group active
return newGroup handle
```

This is why the PC host cannot fake this with only texture names: the original creates
real object instances and sprite objects for each child, then copies texture bindings,
animation state, flags, and descriptor pointers from a source Czan group.

`FUN_80174D14` validates and relocates a `CAE_WII` object-group metadata block.
Suggested name:

```text
CzanUiManager_ValidateAndRelocateObjectGroupMetadata
```

Confirmed behavior:

```text
if metadataBlock[0..7] != "CAE_WII\0":
  return 0

if metadataBlock[0x0B] != 0:
  return 1

metadataBlock[0x0B] = 1
metadataBlock[0x0C] = metadataBlock + *(int *)(metadataBlock + 0x0C)

for descriptorIndex in 0..*(ushort *)(metadataBlock + 0x08)-1:
  descriptor = metadataBlock[0x0C] + descriptorIndex * 0x20
  descriptor[0x1C] = metadataBlock + *(int *)(descriptor + 0x1C)

  for animIndex in 0..*(ushort *)(descriptor + 0x16)-1:
    animEntry = descriptor[0x1C] + animIndex * 0x10
    if *(int *)(animEntry + 0x04) == 0:
      *(int *)(animEntry + 0x0C) = 0
    else:
      *(int *)(animEntry + 0x0C) = metadataBlock + *(int *)(animEntry + 0x0C)

return 1
```

The first `uiManager` argument is present in the call signature, but this function only
uses the metadata block pointer.

`FUN_80100944` is the loader for the sub-manager stored at `uiRootManager[0x0C]`.
Suggested provisional name:

```text
UiRootSubManager_LoadCzanGroups
```

Confirmed behavior:

```text
subManager[0x27] = gManager_802E70B4

nested blocks 0..3:
  CzanUiManager_CreateObjectGroup(global Czan UI manager, block, flags=2, initialAnimIndex=0)
  stored at subManager[0..3]

several handles are cloned/derived from subManager[3] through CzanUiManager_CloneObjectGroup
and stored at subManager[4..8].

two temporary/root groups are allocated through FUN_801002E4(gUiRootManager),
then linked to reference children:
  subManager[9]  -> linked to subManager[1], child 0x0D, linkMode 0x1F
  subManager[10] -> linked to subManager[2], child 0x10, linkMode 0x1F

FUN_80175804 retrieves reference child/object handles.
FUN_8017559C applies those handles to the temporary/root groups.

subManager[0x1F], [0x20], and [0x21..0x26] are allocated helper objects
initialized around specific object groups/children.
```

This is another place where the global Czan object list is populated before the
`UiRootManager_DrawFrame` traversal.

`FUN_80104538` is the loader for the sub-manager stored at `uiRootManager[0x0D]`.
Suggested provisional name:

```text
UiRootSubManager_LoadCzanGroupsWithTexture
```

Confirmed behavior:

```text
nested blocks 0..3:
  CzanUiManager_CreateObjectGroup(global Czan UI manager, block, flags=0, initialAnimIndex=0)
  stored at subManager[0..3]

subManager[4]:
  created/retrieved through UiRootManager_CreateReferenceObjectGroup(gUiRootManager, -1, -1, 0x1F)

all five groups are disabled/hidden with:
  CzanUiManager_SetObjectGroupDrawEnabled(global Czan UI manager, group, 0)

block 4:
  CreateTextureFromTplResource(global texture manager, block4, block4Size, -1)
  stored at subManager[0x0B]
```

This is similar to `UiRootSubManager_LoadCzanGroups`, but uses plain Czan group flags
`0` instead of `2` and also owns one texture slot.

`FUN_80106054` initializes the `0x7C8`-byte sub-manager stored at `uiRootManager[0x0E]`.
Suggested provisional name:

```text
UiRootSubManager_InitTextureFrameGroups
```

Confirmed behavior:

```text
for row in 0..1:
  rowBase = subManager + row * 0x3E0
  for frame in 0..0x27:
    group = FUN_801002E4(gUiRootManager)
    *(rowBase + 0x08 + frame * 4) = group
    if row == 1:
      CzanUiManager_SetObjectGroupDrawEnabled(global Czan UI manager, group, 0)

for frame in 0..0x27:
  CzanUiManager_SetObjectTextureFrame(global Czan UI manager,
                                      *(subManager + 0x08 + frame * 4),
                                      0, frame, 0)

for frame in 0..0x27:
  CzanUiManager_SetObjectTextureFrame(global Czan UI manager,
                                      *(subManager + 0x3E8 + frame * 4),
                                      0, frame, 0)
```

This looks like a pair of texture-frame group banks. The second bank is created but
hidden from draw traversal immediately.

`FUN_80106C34` loads the `0x14`-byte sub-manager stored at `uiRootManager[0x0F]`.
Suggested provisional name:

```text
UiRootSubManager_LoadLinkedObjectGroup
```

Confirmed behavior:

```text
group = CzanUiManager_CreateObjectGroup(global Czan UI manager, linkData, flags=2, initialAnimIndex=0)
subManager[0] = group
subManager[1] = UiRootManager_CreateReferenceObjectGroup(gUiRootManager, group, 0, 0x1F)
```

In the Spanish `comAF_SP.bin` resource, this loader consumes top-level block 6.

`FUN_8011E0A4` loads six hidden Czan object groups and builds child lookup bytes.
Suggested provisional name:

```text
UiRootSubManager_LoadIndexedHiddenGroups
```

Confirmed behavior:

```text
subManager[0xCC6] = -1
subManager[0xCC7] = setupValue
subManager[0xCC8] = 0
subManager[0xCC9] = 1

for groupIndex in 0..5:
  block = CzanLinkManager_GetBlock(linkManager, groupIndex)
  group = CzanUiManager_CreateObjectGroup(global Czan UI manager, block, flags=2, initialAnimIndex=0)
  subManager[groupIndex] = group
  CzanUiManager_SetObjectGroupDrawEnabled(global Czan UI manager, group, 0)

  childCount = FUN_80175FB8(global Czan UI manager, group)
  for childIndex in 0..childCount-1:
    childObject = CzanUiManager_GetChildObjectInstance(global Czan UI manager, group, childIndex)
    objectId = *(childObject + 0x160)
    *(subManager + groupIndex * 0x80 + 0x17 + objectId) = childIndex

  UiRootSubManager_ConfigureIndexedHiddenGroup(subManager, groupIndex, setupValue)
```

This gives the sub-manager a fast lookup from a Czan child object ID to that child's
index inside one of six hidden object groups.

`FUN_8011E3E8` configures one group created by `UiRootSubManager_LoadIndexedHiddenGroups`.
Suggested provisional name:

```text
UiRootSubManager_ConfigureIndexedHiddenGroup
```

Confirmed behavior:

```text
if subManager[0xCC9] == 0:
  return

subManager[0xCC7] = setupValue
config = DAT_80291AB0 + groupIndex * 0x14
configuredChildCount = config[0]
baseScaleX = *(float *)(config + 0x04)
baseScaleY = *(float *)(config + 0x08)
usesNormalizedQuad = config[0x0C]
capturesOriginalChildData = config[0x10]

perChildDataBase = subManager + groupIndex * 0x800 + 0x318

for childIndex in 0..configuredChildCount-1:
  if capturesOriginalChildData != 0:
    read current child transform/color data

  query child texture size / object data
  bind/update the child sprite texture size
  compute quad bounds and UV/normalized values
  write one 0x10-byte data entry at perChildDataBase + childIndex * 0x10
  FUN_80175998(global Czan UI manager, group, childIndex, entry)

if widescreen mode is enabled:
  for every child in the group:
    apply a child transform/offset
```

Called helpers still needing separate bodies:

```text
FUN_80175FB8 -> get child count for a Czan object group
FUN_80175FCC -> read child object data into a local struct
FUN_801760EC -> query child texture size / object dimensions
FUN_801761A4 -> get child sprite/object handle
FUN_80175998 -> apply per-child quad/UV data
FUN_801752AC -> apply alternate child transform data
CzanUiManager_ApplyChildObjectOffset -> apply child transform/offset data
```

Confirmed behavior:

```text
if uiRootManager[0] != 0:
  FUN_80105BCC(uiRootManager[0x0D])
  if uiRootManager[0x0B] == 0:
    CzanUiManager_DrawObjectListReverse(*(DAT_802E71B8 + 0x270), -1)
    uiRootManager[0x0B] = 1
  FUN_80105BAC(uiRootManager[0x0D])
  FUN_80105B50(uiRootManager[0x0D])
  FUN_801067DC(uiRootManager[0x0E])
  FUN_8010D6B0(uiRootManager[0x11])
  FUN_80121228(uiRootManager[0x12])
  uiRootManager[0x0B] = 0
```

So the live UI render entry point is:

```text
UiRootManager_DrawFrame
  -> CzanUiManager_DrawObjectListReverse(global Czan UI manager, -1)
    -> CzanUiObjectInstance_Draw
      -> CzanSpriteObject_Draw
        -> CzanDrawTexturedOrColoredQuad
```

`FUN_80170354` is the higher-level Czan sprite draw dispatcher. Suggested name:

```text
CzanSpriteObject_Draw
```

It gets the current sprite/render context through `FUN_8012A164`, checks that the sprite
is active, has a valid texture frame, and has nonzero draw dimensions, copies/adjusts
the four vertex color blocks, applies GX render state, chooses a draw mode, and then
calls a low-level quad emitter.

`FUN_8012A164` is another runtime/compiler context helper, not sprite logic. It stores
`r27..r31` to the implicit `r11` context area:

```text
*(r11 - 0x14) = r27
*(r11 - 0x10) = r28
*(r11 - 0x0C) = r29
*(r11 - 0x08) = r30
*(r11 - 0x04) = r31
```

Important fields consumed:

```text
sprite +0x020 -> active flag
sprite +0x034 -> active texture frame/index
sprite +0x090/+0x094 -> secondary dimensions/scale; both must be nonzero
sprite +0x0B4..+0x0C3 -> four RGBA vertex color blocks
sprite +0x0C8/+0x0CC/+0x0D0/+0x0D4 -> quad corner/edge inputs
sprite +0x0D8/+0x0DC -> pivot/offset added to quad X/Y inputs
sprite +0x0E0..+0x0FC -> final corner values consumed by low-level quad emitters
sprite +0x104/+0x108 -> width/height passed to draw variants
sprite +0x10C -> transform/matrix source copied before draw
sprite +0x16C/+0x170/+0x174 -> color-channel/TEV state
sprite +0x178/+0x17C/+0x180/+0x184 -> blend/render mode state
sprite +0x188/+0x18C/+0x190/+0x194/+0x198/+0x19C -> alpha/compare/blend state
sprite +0x1A0 -> extra render-state value
sprite +0x1A8/+0x1AC -> optional linked object/texture state
sprite +0x1C0 -> requested draw mode; if caller passes -1, mode can be derived
sprite +0x1D4 -> dummy/no-texture flag; when clear, passes sprite pointer to quad emitter
```

Draw mode dispatch:

```text
mode 0 -> CzanDrawTexturedOrColoredQuad / FUN_8016BE18
mode 1 -> FUN_8016D08C
mode 2 -> FUN_8016D698
mode 3 -> FUN_8016C3B8
mode 4 -> FUN_8016C768
mode 5 -> FUN_8016CC84
mode 6 -> FUN_8016DEC0
mode 7 -> FUN_8016E34C
mode 8 -> FUN_8016E9F0
```

For common draw mode `0`, the caller passes:

```text
x0 = sprite +0x0C8 + sprite +0x0D8
y0 = sprite +0x0CC + sprite +0x0DC
x1 = sprite +0x0D0 + sprite +0x0D8
y1 = sprite +0x0D4 + sprite +0x0DC
sprite pointer
sprite +0x78
sprite +0x104
sprite +0x108
adjusted vertex colors
texture/dummy pointer
```

`FUN_8016BE18` / `CzanDrawTexturedOrColoredQuad` is the low-level GX quad emitter. It
loads a texture object when one is provided, then writes four vertices/colors directly
to the GX FIFO.

## Texture Release

`FUN_801467FC` releases one `zanTexture` slot. Suggested name:

```text
TextureSlot_Release
```

Confirmed behavior:

```text
if slot +0x0C != 0:
  if slot +0x00 == 0 and slot +0x04 == 0:
    free nested GX/TPL allocation records through slot +0x08
  free slot +0x0C
  clear slot +0x00/+0x04/+0x08/+0x0C
  return 1

if slot +0x00 != 0:
  clear slot +0x00/+0x04
  return 1

return 0
```

`FUN_80146B8C` releases one indexed texture entry from a texture manager. Suggested
name:

```text
TextureManager_DeleteTexture
```

It reads the slot at `manager[0] + textureIndex * 0x14`, calls
`TextureSlot_Release`, and decrements `manager[1]` when release succeeds. If release
fails despite an active slot, it reports `zanTexture: warning DeleteTexture`.

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
3. Reads block 1 as the object-group metadata and validates/relocates it through
   `CzanUiManager_ValidateAndRelocateObjectGroupMetadata`.
   If validation fails, releases the link manager and returns `-2`.
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
   - 3 or 4: preplay the selected animation through CzanUiObjectInstance_PreplayInitialAnimation.
9. Applies descriptor flags from descriptor +0x1A to object fields such as visibility,
   playback, loop/stop behavior, and group status bits.
10. Copies the group's left/top/right/bottom bounds into each child object. In
    widescreen/screen-dependent cases it adjusts the attached sprite offsets when
    sprite bytes +0x38/+0x39 request horizontal or vertical anchoring.
11. After all children are built, sets `uiManager +0x18 = 1`, `uiManager +0x19 = 1`,
    and sets object group `+0x24` bit 0.
12. Sums each child object's `+0x17D` byte. If none are active and group `+0x00 == -1`,
    sets object group `+0x24` bit 3; otherwise clears bit 3.
13. Returns/stores the new group index through FUN_8012A18C.
```

Important inferred structures:

```text
CzanUiManager
+0x00 -> max object group count
+0x04 -> object group slot array
+0x18 -> set to 1 after a group is created
+0x19 -> draw/list-ready flag set to 1 after a group is created

CzanUiObjectGroup slot, size 0x28
+0x00 -> current animation/state value, initialized to -1
+0x04 -> object count
+0x08 -> group flags/state byte area; byte +0x08 starts at 0
+0x09 -> secondary group flags/state byte
+0x0C -> left/bounds float
+0x10 -> top/bounds float
+0x14 -> right/bounds float
+0x18 -> bottom/bounds float / screen height dependent
+0x1C -> object-group metadata block pointer
+0x20 -> object pointer array
+0x24 -> group status flags byte; bit 0 set after creation, bit 3 means no active visible objects
+0x25 -> initialized to 0
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

Descriptor `+0x1A` confirmed flag effects:

```text
0x001 -> object +0x173 = 1, suppresses the object in the draw wrapper.
0x002 -> object +0x174 = 1 and object +0x0B1 = 0, playback/end state override.
0x004 -> starts animation 0 immediately.
0x008 -> contributes 1 to object +0x175 mode value.
0x010 -> contributes 2 to object +0x175 mode value.
0x020 -> contributes 3 to object +0x175 mode value.
0x040 -> object +0x17D = 0.
0x080 -> group byte +0x08 = 0xFF if not already set.
0x100 -> group byte +0x08 |= 1 if not already set by 0x80.
0x200 -> group byte +0x08 |= 2 if not already set by 0x80.
```

The function creates two runtime objects per descriptor:

```text
0x1D8-byte sprite/texture object, initialized by FUN_8016BA8C
0x1B4-byte UI animation/object instance, initialized by FUN_801711DC
```

This is the path that must be reproduced before the real mode-select background and
animations can render correctly. The next missing pieces are the object instance update
and animation functions: `FUN_801711DC`, `FUN_80171B98`, `CzanUiObjectInstance_PreplayInitialAnimation`, `FUN_80172CC8`,
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
CzanUiObjectInstance_PreplayInitialAnimation -> preplays the selected animation then resets script state
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

### Preplay Initial Animation

`FUN_801728E4` preplays the selected animation on a `CzanUiObjectInstance` and then
resets its script state. Suggested name:

```text
CzanUiObjectInstance_PreplayInitialAnimation
```

Suggested signature:

```c
void CzanUiObjectInstance_PreplayInitialAnimation(int objectInstance);
```

Confirmed behavior:

```text
oldMode = object +0x175
object +0x175 = 3

CzanUiObjectInstance_StartAnimation(0.0, objectInstance, object +0x16C)

preplayCount = object +0xB4
object +0xB4 = 0.0

for i in 0..preplayCount-1:
  CzanUiObjectInstance_RunAnimationScript(objectInstance)

object +0x175 = oldMode

object +0x19C = *(object +0x198 descriptor +0x1C + object +0x16C * 0x10 + 0x0C)
object +0x171 = 0
object +0xB1 = 1
object +0xB2 = 0
object +0xB4 = 0.0
object +0xC0 = 0
```

This is the path used by object-group creation when the low byte of the creation flags
is `3` or `4`. In both cases it evaluates the animation for a number of script ticks,
then leaves the object reset at the selected animation entry.

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
context helper `FUN_8012A160` spills saved registers `r26..r31` into a stack/context
area at `r11 - 0x18 .. r11 - 0x04`. Ghidra then presents those context values as if
they were recovered/returned arguments. The call site in `CzanUiManager_CreateObjectGroup`
passes `(objectInstance, 1)`.

`FUN_8012A160` itself is not game UI logic:

```text
*(r11 - 0x18) = r26
*(r11 - 0x14) = r27
*(r11 - 0x10) = r28
*(r11 - 0x0C) = r29
*(r11 - 0x08) = r30
*(r11 - 0x04) = r31
```

`FUN_8012A1AC` is the paired helper seen after this kind of context handling. Its
decompiled body is only `return;`, so it should be documented as runtime/decompiler
glue, not exported as a real game/UI routine.

This function appears in xrefs to `BindTextureFromTextureSet`, but it is not the final
sprite draw function. It interprets CAE animation commands and writes derived state into
the attached `CzanSpriteObject`; a later renderer must consume the sprite fields.

The active command pointer is `objectInstance +0x19C`. The stream is float-aligned; the
opcode is read as `(int)*(float *)currentCommand`.

Confirmed command behavior:

```text
0x00 -> end/restart/stop command.
       If object +0x174 == 0, sets +0xB1. If object +0x175 == 1 in that path, it
       also sets +0x173, which suppresses drawing. If object +0x174 is nonzero,
       it resets +0x19C to the selected animation entry start and sets +0xB2.

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

`FUN_80172F30` writes one or all four RGBA color blocks on a `CzanUiObjectInstance`,
marks color state dirty at `+0x182/+0x183`, then calls
`CzanUiObjectInstance_ApplyColorBlocks`. Suggested name:

```text
CzanUiObjectInstance_SetColorBlocks
```

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

Named helper mapping:

```text
FUN_8002AE28 -> InputOrMenuStateManager_TestHeldMask
FUN_8002AE48 -> InputOrMenuStateManager_TestTriggeredMask
FUN_8002ADE0 -> InputOrMenuStateManager_IsConfirmPressed
FUN_8002ADF4 -> InputOrMenuStateManager_IsBackPressed
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

## Host `select_cmn.bin` Probe

The host must load `select/select_cmn.bin` in addition to the region-specific
`select/select_bin_sp.bin`. The latter contains the mode-select UI sprite groups, but
the real common background/model package lives in `select_cmn.bin`.

`tools/dump_select_cmn_models.py` confirms the top-level layout:

```text
select_cmn root: WII blocks=3
[0] WII, size 0x1F3D60  -> common model package
[1] WII, size 0x14D220  -> large ZAB animation package
[2] WII, size 0x56060   -> TEB + texture data
```

Top block 0 is a WII resource with 17 blocks:

```text
[00] ZMB model      size 0xABA18
[01] texture/other  size 0x20FE0
[02] ZAB animation  size 0x123934
[03] ZMB model      size 0x644
[04] texture/other  size 0x320
[05] ZMB model      size 0x220
[06..15] ZAB animation blocks
[16] nested WII resource
```

Confirmed ZMB header fields from the dump:

```text
block 0: +18=0x30, +1C=0x140, +20=0x8C0, +24=0
         object table count=0x152, entries=0x8D0
block 3: +18=0x30, +1C=0x60, +20=0xB0, +24=0
         object table count=2, entries=0xC0
block 5: +18=0, +1C=0, +20=0x30, +24=0
         object table count=3, entries=0x40
```

`FUN_800982A8` is the runtime loader for this same common package. Suggested name:

```text
CSelectCommon_LoadResource
```

Confirmed block map from the function:

```text
CzanLinkManager_SetLink(stackLink, linkData)

blocks 0/1:
  CzanLinkManager_GetBlockInfo(stackLink, 0, &modelBlock, &modelSize)
  CzanLinkManager_GetBlockInfo(stackLink, 1, &textureBlock, &textureSize)
  load CtsStageObj at selectCommon +0x48

block 2:
  attach continuation block 0 to the +0x48 CtsStageObj
  start/play it with frame/rate constants

blocks 3/4:
  CzanLinkManager_GetBlockInfo(stackLink, 3, &modelBlock, &modelSize)
  CzanLinkManager_GetBlockInfo(stackLink, 4, &textureBlock, &textureSize)
  load CtsStageObj at selectCommon +0xB8

block 5:
  CzanModelOwner_CreateModelFromPrimaryBlock(selectCommon +0x128, block5, size5)
  CzanModelOwner_SetContinuationCount(selectCommon +0x128, 10)
  CzanModelOwner_BuildRuntimeDataAt80(selectCommon +0x128)

blocks 6..0xF:
  CzanModelOwner_LoadContinuationBlock(selectCommon +0x128, block, index 0..9)
  CzanModelOwner_SetAnimationSpeed(selectCommon +0x128, 0.0)

block 0x10:
  create the shared CSelModeEntry UI object group and two mirrored entries
```

`FUN_80098BF0` / `CSelectCommon_DrawEffects` is the normal select-common draw path:

```text
if selectCommon +0x36C == 1:
  update owner/model controller at selectCommon +0x1AC
  draw CtsStageObj at selectCommon +0x48 with matrix selectCommon +0x18
  copy/draw the texture surface at selectCommon +0x234 as the half-screen reflection
  update owner/model controller at selectCommon +0x1AC again
  draw CtsStageObj at selectCommon +0xB8 with matrix selectCommon +0x18
```

The owner model at `selectCommon +0x128` is not drawn directly by this function.
It drives the matrix copied to `selectCommon +0x18`; using a separate hand-projected
`BG_Camera01` camera in the host is not the game path.

So the current model status is no longer ambiguous:

```text
.zmb primary data:
  block 5 goes through CzanModel_SetPrimaryBlock and CzanModel_BuildRuntimeData.

.zab / continuation data:
  blocks 6..0xF go through CzanModelOwner_LoadContinuationBlock ->
  CzanModel_LoadContinuationBlock after the model has continuation count 10.
  CzanModel_LoadContinuationBlock then calls FUN_8014E464(model, continuationIndex).
```

`FUN_80160504` is the link-manager helper used by this loader. Suggested name:

```text
CzanLinkManager_GetBlockInfo(linkManager, blockIndex, outBlock, outSize)
```

Confirmed behavior:

```text
if blockIndex < *(int *)(*linkManager + 8):
  *outSize = *(linkManager[1] + blockIndex * 8 + 4)
  *outBlock = (*outSize < 1) ? 0 : *(linkManager[1] + blockIndex * 8)
  return 1
return 0
```

`FUN_80098810` is the select-common movie/background update and bind step. Suggested
name:

```text
CSelectCommon_UpdateMovieBackground
```

Suggested host signature:

```c
void CSelectCommon_UpdateMovieBackground(
    int *selectCommon,
    int skipInitialUpdate,
    int allowMovieStart,
    int forceInitialBind);
```

The decompile shows the object pointer recovered through the runtime saved-register
helper, but all field offsets are relative to the `selectCommon` object loaded by
`CSelectCommon_LoadResource`.

Confirmed behavior:

```text
1. Resets/pauses select-common model state:
   - model owner at selectCommon +0x128
   - CtsStageObj layer at selectCommon +0x48
   - CtsStageObj layer at selectCommon +0xB8

2. If allowMovieStart is nonzero and selectCommon +0x358 == 1:
   - when selectCommon +0x35C == 1, selects a THP/movie path based on
     selectCommon +0x34C and calls the movie manager load function.
   - selected paths are under:
       /sound/stream/mu_bgm_999/movie/b_*
   - the movie handle used for these operations is selectCommon +0x344.

3. Polls movie readiness through the movie manager. Once ready:
   - starts/fades the movie.
   - gets the active movie object.
   - writes movie object field +0x288 = 1.
   - binds the movie object/texture into the two CSelModeEntry objects at
     selectCommon +0x254 and +0x2A4 through the Czan UI manager at
     selectCommon +0x250.

4. After binding, chooses one of two reveal/update paths:
   - selectCommon +0x368 == 1 -> CSelectCommon_RevealMovieEntriesPrimary(selectCommon, 1)
   - otherwise                -> CSelectCommon_RevealMovieEntriesAlternate(selectCommon, 0)

5. If selectCommon +0x364 == 1, releases/hides the bound movie object and clears
   the movie/background state flags.
```

Important state fields:

```text
+0x250 -> Czan UI manager pointer used to bind the movie object into CSelModeEntry
+0x254 -> first CSelModeEntry receiving the movie-backed object
+0x2A4 -> second CSelModeEntry receiving the movie-backed object
+0x344 -> movie manager handle/slot
+0x34C -> mode/category selector used to pick the b_* movie path
+0x350 -> movie load requested flag
+0x354 -> movie binding active/requested flag
+0x358 -> movie-background state enabled/loading flag
+0x35C -> request new movie path flag
+0x360 -> movie ready/bind pending flag
+0x364 -> release/hide movie binding flag
+0x368 -> reveal path selector
```

Important follow-up callees:

```text
MovieSlotHandle_LoadResource -> movie manager load/assign path
MovieSlotHandle_HasPlaybackStarted -> movie playback-started poll
MovieSlotHandle_StartPlayback -> start/fade movie playback
MovieSlotHandle_GetClaimedObject -> get active movie object/state
FUN_80025104 -> release/stop movie binding
CSelectCommon_RevealMovieEntriesPrimary -> one select-common reveal/update path
CSelectCommon_RevealMovieEntriesAlternate -> alternate select-common reveal/update path
```

`FUN_80098FA0` reveals/transitions the two movie-backed `CSelModeEntry` objects using
the primary timing/layout path. Suggested name:

```text
CSelectCommon_RevealMovieEntriesPrimary
```

`FUN_800991C4` performs the alternate reveal path, using layout ids `1` and `3` for
the two mirrored entries. Suggested name:

```text
CSelectCommon_RevealMovieEntriesAlternate
```

The reveal helpers use these lower UI helpers:

```text
FUN_801106C4 -> CSelModeEntry_ResetObjectAnimation
FUN_801105FC -> CSelModeEntry_StartObjectAnimation
FUN_80174FA4 -> CzanUiManager_ResetObjectGroupAnimationTime
FUN_801761C4 -> CzanUiManager_GetChildObjectInstance
FUN_80172F30 -> CzanUiObjectInstance_SetColorBlocks
```

The OpenGL host now stores `select_cmn.bin` on `HostCSelectModule`, logs the common
model package at CSelect startup, matches block 2 ZAB channels against block 0 ZMB
objects, and caches visible block 0 ZMB primitive vertices for a host preview.

The preview uses the recovered Czan transform path rather than host-local transform
guesses:

```text
CzanModel_ReadZmbObjectLocalMatrix:
  matches `CzanModel_BuildRuntimeData` at `0x8014C6CC`: runtime matrix row 0 is
  object `+0x30/+0x40/+0x50/+0x60`, row 1 is `+0x34/+0x44/+0x54/+0x64`, and
  row 2 is `+0x38/+0x48/+0x58/+0x68`.

CzanModel_BuildZmbObjectWorldMatrices:
  composes object local matrices through parent index +0x94, matching the
  CzanModel_BuildRuntimeData -> CzanModel_UpdateObjectTransforms split.

CzanModel_TransformPoint:
  applies the 3x4 world matrix before the host preview stores each primitive vertex.
```

This still is not the final select_cmn renderer. The remaining model work is to move
from cached preview primitives into the actual Czan draw-submit path:

```text
CzanModel_DrawVisibleObjects
  -> CzanModel_DrawStandardPartTree / CzanModel_DrawType2PartTree
  -> CzanModel_SubmitPartPrimitive / special/type2 submitters
  -> material state, vertex attribute setup, texture binding, and primitive batches
```
