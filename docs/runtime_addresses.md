# Runtime Addresses

Source: `input/DDRII.dmw`

Full export: `outputs/dmw_watch_labels.csv`

Boot/select function status map: [`boot_cselect_function_map.md`](boot_cselect_function_map.md)

Important early candidates:

```text
0x8053EB78  Song ID
0x8054040C  DDR POINTS
0x805753D4  Character ID
0x8068A8E8  Song Score
0x8068A90C  Song MISSES
0x8068A914  Song GOODS
0x8068A918  Song GREATS
0x8068A91C  Song PERFECTS
0x8068A920  Song MARVELOUS
0x808A20A8  Song Timer?
0x8148EC00  Arrow Highlighted
```

## Runtime Context Helpers

These are PowerPC compiler/runtime helpers, not game systems. Ghidra often treats
functions that enter through them as `void(void)` because the real arguments or return
value are recovered from the saved-register context.

```text
FUN_8012A130 -> RuntimeContext_SpillSavedRegistersR14ToR31
  stores r14..r31 into the implicit r11 context at -0x48..-0x04

FUN_8012A134 -> RuntimeContext_SpillSavedRegistersR15ToR31
  stores r15..r31 into the implicit r11 context at -0x44..-0x04

FUN_8012A144 -> RuntimeContext_SpillSavedRegistersR19ToR31
  stores r19..r31 into the implicit r11 context at -0x34..-0x04

FUN_8012A150 -> RuntimeContext_SpillSavedRegistersR22ToR31
  stores r22..r31 into the implicit r11 context at -0x28..-0x04

FUN_8012A154 -> RuntimeContext_SpillSavedRegistersR23ToR31
  stores r23..r31 into the implicit r11 context at -0x24..-0x04

FUN_8012A158 -> RuntimeContext_SpillSavedRegistersR24ToR31
  stores r24..r31 into the implicit r11 context at -0x20..-0x04

FUN_8012A15C -> RuntimeContext_SpillSavedRegistersR25ToR31
  stores r25..r31 into the implicit r11 context at -0x1C..-0x04

FUN_8012A164 -> RuntimeContext_SpillSavedRegistersR27ToR31
  stores r27..r31 into the implicit r11 context at -0x14..-0x04

FUN_8012A17C -> RuntimeContext_ReturnMarkerA17C
  empty paired return/restore marker for the saved-register helper family

FUN_8012A180 -> RuntimeContext_ReturnMarkerA180
  empty paired return/restore marker for the saved-register helper family

FUN_8012A184 -> RuntimeContext_ReturnMarkerA184
  empty paired return/restore marker for the saved-register helper family

FUN_8012A188 -> RuntimeContext_ReturnMarkerA188
  empty paired return/restore marker for the saved-register helper family

FUN_8012A18C -> RuntimeContext_ReturnMarkerA18C
  empty paired return/restore marker for the saved-register helper family

FUN_8012A190 -> RuntimeContext_ReturnMarkerA190
  empty paired return/restore marker for the saved-register helper family

FUN_8012A19C -> RuntimeContext_ReturnMarkerA19C
  empty paired return/restore marker for the saved-register helper family

FUN_8012A1A0 -> RuntimeContext_ReturnMarkerA1A0
  empty paired return/restore marker for the saved-register helper family

FUN_8012A1A4 -> RuntimeContext_ReturnMarkerA1A4
  empty paired return/restore marker for the saved-register helper family

FUN_8012A1A8 -> RuntimeContext_ReturnMarkerA1A8
  empty paired return/restore marker for the saved-register helper family

FUN_8012A1B0 -> RuntimeContext_ReturnMarkerA1B0
  empty paired return/restore marker for the saved-register helper family
```

Use these names only to annotate the decompiler/runtime ABI boundary. They should not
be folded into menu, model, ZMB, ZAB, or controller ownership logic.

## Critical Section And Timebase Helpers

```text
FUN_801A9430 -> Runtime_EnterCriticalSection
  captures the PowerPC MSR and returns a token with selected MSR state mirrored in
  the high word

FUN_801A9470 -> Runtime_LeaveCriticalSection
  restores/leaves the captured MSR state and returns the previous external interrupt
  enable bit: (MSR >> 0x0F) & 1

FUN_801AD410 -> Runtime_GetTimebase
  raw timebase read; Ghidra can show this as an empty void function because the value
  is returned directly in registers

FUN_801AD440 -> Runtime_GetBootTime
  enters a critical section, reads Runtime_GetTimebase, adds the 64-bit boot offset
  from DAT_800030D8/DAT_800030DC, leaves the critical section, and returns the sum
```

## Debug Report / Formatter Helpers

```text
FUN_801A5710 -> RuntimeDebugReport
  builds the varargs frame used by OSReport-style logging, then calls FUN_801318BC

FUN_801318BC -> RuntimeDebugReportV / report dispatch
  checks the report sink state through FUN_801374B8 and calls FUN_80130E80 with the
  output callback when logging is allowed

FUN_80130E80 -> RuntimeFormatWrite
  shared printf-style formatter; parses literals and % conversions, formats into a
  scratch buffer, and calls the supplied writer callback for each output span

FUN_801374B8 -> RuntimeDebugSinkQueryAndSet
  queries/updates the encoded report sink mode in word +0x04

FUN_801A6220 -> RuntimeBootInfo_CopyOptional
  copies 0x1C bytes from DAT_800030F0 into the supplied buffer when that pointer is in
  the high memory range; otherwise writes zero to the first word

FUN_801A2040/FUN_801A2050/FUN_801A2060/... -> RuntimeLowLevel_ClearCallbackSlot*
  empty callback-slot setters used by OS startup

FUN_801A2170 -> RuntimeLowLevel_CheckH4AFlag
  reports when the supplied flag does not have bit 31 set

FUN_8002A33C -> InputOrMenuStateManager_Update
  updates the controller/menu records consumed by boot, CSelect, and gameplay UI

FUN_8002AE08 -> InputOrMenuStateManager_TestActiveMask
  tests `(mask & *(manager + controllerIndex * 0x20 + 0x04)) != 0`

FUN_8002AE68 -> InputOrMenuStateManager_TestActiveRepeatMask
  first checks the active/repeat candidate word at controller record +0x04, then
  dispatches through ControllerManager_TestRepeatTimerForSource for source ids
  0/1/3/4/5 and conditionally 6/7/2. Logical controller slot 4 expands across
  physical controllers 0..3.

FUN_8002B100 -> InputOrMenuStateManager_TestSourceHeldMask
  tests a held mask against a selected physical controller/source mode. The caller's
  mode argument maps logical player pairs onto physical controller indices before
  reading ControllerManager_ReadHeldMaskSource.

FUN_8002AE28 -> InputOrMenuStateManager_TestHeldMask
  tests `(mask & *(manager + controllerIndex * 0x20 + 0x08)) != 0`

FUN_8002AE48 -> InputOrMenuStateManager_TestTriggeredMask
  tests `(mask & *(manager + controllerIndex * 0x20 + 0x10)) != 0`

FUN_8002ADE0 -> InputOrMenuStateManager_IsConfirmPressed
  returns bit 11 from controller record +0x08

FUN_8002ADF4 -> InputOrMenuStateManager_IsBackPressed
  returns bit 10 from controller record +0x08

FUN_8014A6BC -> ControllerManager_ReadHeldMaskSource
  reads the held/current mask for one physical controller and one source selector:
  source 0 uses controller +0x1118, source 1 uses +0x117C when type byte +0x64 is 2,
  source 2 uses manager + controller*0x64 +0x40 when manager flag 0x400 is enabled,
  sources 3/4/5 use +0x11E0/+0x1244/+0x12A8, sources 6/7 use manager offsets
  +0x1D0/+0x360, and source 9 ORs the active sources together.

FUN_8014A860 -> ControllerManager_ReadTriggeredMaskSource
  same source-selector layout as ControllerManager_ReadHeldMaskSource, but reads the
  current-frame/triggered words: +0x111C, +0x1180, manager +0x44, +0x11E4, +0x1248,
  +0x12AC, manager +0x1D4, manager +0x364, or the ORed source 9 variant.

FUN_8014AA04 -> ControllerManager_TestRepeatTimerForSource
  checks per-bit repeat timers for one source selector against the current scaled
  frame time. This is the low-level helper behind the game's repeat/active input
  tests, not a button-name mapper.

FUN_8014B7F4 -> ControllerManager_IsAuxSourceActive
  when manager flag 0x400 is enabled, returns whether byte
  `manager + controllerIndex * 0x0C + 0x1A` is nonzero.

FUN_8014B824 -> ControllerManager_GetControllerType
  returns the controller type byte at `*(manager + controllerIndex * 4) + 0x64`.

FUN_80131B38 -> RuntimeString_FormatBuffer
  formats into a caller-provided buffer through RuntimeFormatWrite

FUN_80145060 -> DebugText_SetGlyphSize
  stores DAT_802EE1E4, the debug glyph quad size

FUN_80145104 -> DebugText_Draw
  draws bitmap debug text by emitting one quad per glyph

FUN_80145390 -> DebugText_ConfigureRenderState
  configures GX/render state for debug text quads

FUN_80144F48 -> DebugText_InitFontBacking
  initializes debug text font globals: glyph size 0x18, plane count 6, allocates the
  backing buffer, calls FUN_801A9050, and derives U/V scale ratios from header fields

FUN_801A9050 -> DebugText_LoadFontPlanes
  selects and decodes one or two debug font planes depending on video/region state

FUN_801A8460 -> DebugText_LoadFontPlane
  reads a packed "Yay" font plane from fixed Wii source offsets, decompresses it, and
  applies the plane-1 glyph pixel patch before returning the decoded header size

FUN_801A8DE0 -> DebugText_DecodePackedGlyphPlane
  expands 2-bit packed glyph data through the palette at header +0x2C; mode 0 writes
  two paletted nibbles per byte, mode 2 writes four palette bytes per packed byte

FUN_801A48D0 -> FlushDataCacheRange
  stores/flushes every 0x20-byte cache line touched by the range and returns the first
  address after the flushed line range; host implementation is a coherent-memory no-op

FUN_80188284 -> GlobalUiFrameState_CreateOnce
  lazily allocates DAT_802E71F8 as a 0xF4-byte object, clears +0x00..+0xE3, clears
  +0xE4/+0xE8/+0xEC, and stores its vtable at +0xF0

FUN_801731E4 -> CzanUiManager_AllocateObjectGroupStorage
  initializes screen projection globals, then allocates the UI manager object-group
  table: count at +0x00, group records at +0x04 with stride 0x28, a small table at
  +0x08, capacity at +0x0C, pointer table at +0x1C, and initial empty group records

FUN_80170FA4 -> UiScreenProjection_UpdateGlobals
  derives UI projection/view globals DAT_802EF368 and DAT_802EF3A8 from display config
  under DAT_802E71B8 +0x258

FUN_801B0C30 -> BuildPerspectiveProjectionMatrix
  builds the 4x4 perspective matrix used by UiScreenProjection_UpdateGlobals

FUN_801BA0D0 -> RuntimeVideo_RetraceInterruptHandler
  VI retrace interrupt handler; clears VI interrupt status bits, increments
  DAT_802E752C, invokes registered callbacks, flushes pending VI register shadows,
  watches display-cable/mode changes, and dispatches queued encoder updates

FUN_801BA980 -> RuntimeVideo_GetTimingTable
  returns the low-level VI timing-table pointer for a selector derived from VI mode
  family and scan mode; selectors also include custom fallback table DAT_802E74D4

FUN_801BAAA0 -> RuntimeVideo_ProgramInitialViRegisters
  temporarily disables VI output, writes the initial cc0020xx timing registers from
  the selected timing table, then re-enables output with the selected scan/mode bits

FUN_801BACA0 -> RuntimeLowLevel_InitVideoInterface
  one-time low-level VI initializer; derives DAT_802F6350..DAT_802F63A4 display
  globals, seeds copy-filter enable DAT_802F6390, registers retrace callbacks, then
  applies display state through FUN_801BE580

FUN_801BB1F0 -> RuntimeVideo_WaitForRetrace
  waits until DAT_802E752C changes by repeatedly sleeping on the retrace event object
  at DAT_802E7508

FUN_801BB250 -> RuntimeVideo_PackXfbOriginRegisters
  computes pending VI XFB origin register words at DAT_802F62F4..DAT_802F6302 and
  marks the pending register dirty mask

FUN_801BB500 -> RuntimeVideo_PackHorizontalTimingRegisters
  packs horizontal timing/display bounds into DAT_802F62DC..DAT_802F62E2

FUN_801BB5E0 -> RuntimeVideo_PackVerticalTimingAndCopyFilter
  packs vertical timing and copy-filter/AA-facing pending VI state into
  DAT_802F62D8 and DAT_802F62E4..DAT_802F62EA

FUN_801BB780 -> RuntimeVideo_Configure
  VIConfigure-style mode change; consumes a render-mode struct, updates
  DAT_802F6350..DAT_802F6378 and active timing table DAT_802F63A4, then repacks
  horizontal, vertical, copy-filter, and XFB-origin pending VI registers

FUN_801BBE50 -> RuntimeVideo_SetFramebufferHeightAndViewport
  updates framebuffer/display height and derived viewport offsets while preserving
  the active timing table

FUN_801BC1B0 -> RuntimeVideo_FlushPendingViRegisters
  walks dirty masks DAT_802E74B8/DAT_802E74BC and copies pending shadow VI register
  words from DAT_802F62D8 into the hardware register block

FUN_801BC2D0 -> RuntimeVideo_EnableAlternateXfbOriginState
  enables alternate XFB-origin state and recomputes the origin pending registers

FUN_801BC340 -> RuntimeVideo_SetCopyFilterEnabled
  stores DAT_802F6390 and repacks vertical/copy-filter pending VI registers

FUN_801BC5C0 -> RuntimeVideo_ReadDisplayCableStatusBit
  reads cc00206e under the critical-section wrapper and returns bit 0

FUN_801BC600 -> RuntimeVideo_ConvertBeamPositionToFieldLine
  converts current VI beam registers cc00202c/cc00202e into a field/line pair using
  DAT_802E74F4 and DAT_802F6374

FUN_801BC9E0 -> RuntimeVideo_DelayMicroseconds
  busy-waits against Runtime_GetBootTime; the video encoder command path uses this
  with a two-microsecond delay between pin transitions

FUN_801BCA70 -> RuntimeVideoEncoder_SendByteWithAck
  bit-bangs one byte through cd8000c0/cd8000c4 and checks acknowledgement through
  cd8000c8

FUN_801BCDC0 -> RuntimeVideoEncoder_SendCommandBytes
  sends a byte stream to the external video encoder; initializes encoder polarity on
  first use, enters a critical section, shifts each byte bit-by-bit, and returns a
  success flag through the runtime return marker

FUN_801BD300 -> RuntimeVideoEncoder_SendCableStatusModeCommand
  sends encoder command 1 with mode bits derived from DAT_800000CC and the current
  display-cable status bit

FUN_801BD3A0 -> RuntimeVideoEncoder_SendModeFamilyCommand
  sends encoder command 0x6E with a one-byte mode-family flag

FUN_801BD3E0 -> RuntimeVideoEncoder_SendColorControlCommand
  sends encoder command 5 using packed globals DAT_802E6C7C..DAT_802E6C7E

FUN_801BD440 -> RuntimeVideoEncoder_SendGeometryControlCommand
  sends encoder command 8 using packed globals DAT_802E6C7F..DAT_802E6C82

FUN_801BD4B0 -> RuntimeVideoEncoder_SendPictureControlCommand
  sends encoder command 0x7A using four 7-bit picture-control globals
  DAT_802E6C83..DAT_802E6C86

FUN_801BD520 -> RuntimeVideoEncoder_SendRegionPresetTables
  sends the large encoder preset table command 0x40; table data is selected from
  DAT_802D1E30/DAT_802D1E84/DAT_802D1F2C/etc. by DAT_802E6C78

FUN_801BE2A0 -> RuntimeVideoEncoder_SendGammaTableCommand
  packs 17 16-bit table entries into encoder command 0x10 and sends 0x22 bytes

FUN_801BE400 -> RuntimeVideoEncoder_SendDefaultGammaTable
  sends the default gamma table at DAT_802D1B64 through FUN_801BE2A0

FUN_801BE410 -> RuntimeVideoEncoder_SendSelectedGammaTable
  sends the selected gamma table at DAT_802D1A10 + DAT_802E7544 * 0x22

FUN_801BE430 -> RuntimeVideoEncoder_SendEnableFlagCommand
  sends encoder command 3 with the inverted DAT_802E6C87 enable byte

FUN_801BE490 -> RuntimeVideoEncoder_SendAspectModeCommand
  sends encoder command 10; when DAT_802E754C is 3 it uses DAT_802E7540 as the
  packed aspect/mode bit, otherwise it sends zero

FUN_801BE520 -> RuntimeVideoEncoder_QueueAspectModeCommand
  queues the aspect-mode encoder command by OR-ing DAT_802E7538 with 0x80

FUN_801BE530 -> RuntimeVideoEncoder_SendForceRgbModeCommand
  forces DAT_802E754C to 3 and sends encoder command 1 with value 3

FUN_801BE580 -> RuntimeVideo_ApplyDisplayState
  applies external display/video state after VI init, including DAT_802E754C derived
  from DAT_800000CC

FUN_80168060 -> Runtime_SetSoundArchiveReloadGuard
  stores DAT_802E71E0, the global observed around the sound archive reload path

FUN_80027FFC -> PlayerDataState_GetCurrentValue
  returns the first word of a small player-data/state block

FUN_80020B68 -> PlayerDataStateContainer_Init
  initializes a small five-word container, allocates a 0x10-byte PlayerDataState child
  at +0x0C, initializes it, and seeds it with value 0

FUN_80020EE4 -> PlayerDataState_Init
  initializes a 0x10-byte state object: +0x00 = 0xFFFF, +0x04 = 0xFFFF, +0x08 = 0,
  +0x0C = vtable

FUN_80021030 -> PlayerDataState_SetInitialValue
  writes state +0x00 only while state +0x04 is still 0xFFFF

FUN_80027CA8 -> PlayerDataManager_Init
  initializes the player-data manager, sets vtable at +0x2F50, constructs subblocks
  at +0x14A4/+0x1BA8/+0x1C28/+0x1C54/+0x2F1C/+0x2F40, then calls the local reset
  helper FUN_80027DE4

FUN_80027DE4 -> PlayerDataManager_Reset
  clears the player-data manager's first 0x14A4 bytes, seeds the default setup/player
  counters and timer records, resets seven 0x19C-byte player records, clears the mode
  records through FUN_8002896C, then resets the constructed subblocks

FUN_80028330 -> PlayerDataManager_ResetPlayerRecord
  clears one or all seven player records at +0x17C, each 0x19C bytes, and seeds the
  first word of each cleared record to -1

FUN_80028500 -> PlayerDataManager_ResetMenuRecords
  resets the +0xCC0 menu/setup record block: clears eight 0x44-byte records at
  +0xCC4, four words at +0xEE4, four 0x12-byte records at +0xEF4, enables +0xCC0,
  seeds each record's first word to -1, and cycles record +0x04 through DAT_8026E8B8

FUN_8002896C -> PlayerDataManager_ResetModeRecord
  clears one or all six mode/setup records, including +0xF64 mode flags and the
  0xDC-byte records at +0xF7C; row initialization is delegated to FUN_80028D4C and
  FUN_80028FA0

FUN_80028D4C -> PlayerDataManager_SetModeRecordRowValues
  writes row +0x00/+0x04 for one row across one/all six mode records at +0xF7C

FUN_80028FA0 -> PlayerDataManager_ResetModeRecordRowTail
  resets row +0x0C to 0 and row +0x10 to -1 for one row across one/all six mode
  records at +0xF7C

FUN_800F3940 -> PlayerDataManager_ResetSubBlock14A4
  resets the constructed player-data subblock at +0x14A4; clears the main ranges,
  seeds +0x15C to -1, +0x17C to 5, and marks +0x4F0 enabled

FUN_800F36C4 -> PlayerDataManager_ResetSubBlock1BA8
  seeds four 0x20-byte records at +0x1BA8 as {6, index, 0x52, 0, 0, 0, 0, 0}

FUN_800DE50C -> PlayerDataManager_ResetSubBlock1C28
  resets the compact +0x1C28 subblock; first bytes are mostly 0xFF, words
  +0x14..+0x20 are 1, +0x24 is -1, and +0x28 is 0

FUN_800DF6BC -> PlayerDataManager_ResetSubBlock1C54
  resets the large +0x1C54 subblock; clears nine 0x204-byte records with bytes
  +0x50/+0x51 set to 1, clears four 0x20-byte records at +0x1224, and clears
  six tail words at +0x12B0..+0x12C4

FUN_801206D8 -> PlayerDataManager_ResetSubBlock2F1C
  resets the 0x24-byte subblock at +0x2F1C, seeds state words, stores a byte derived
  from RuntimeRandom_Next15 at +0x19, and sets +0x20 to -1

FUN_80123574 -> PlayerDataManager_ResetSubBlock2F40
  clears the four-word subblock at +0x2F40

FUN_80141AA4 -> GlobalSubManager274_CopyRgba48
  copies four bytes from manager +0x48..+0x4B into the caller's output color buffer

FUN_801467FC -> TextureSlot_Release
  releases one zanTexture 0x14-byte slot and clears its four words

FUN_80146B8C -> TextureManager_DeleteTexture
  releases textureManager[0] + textureIndex * 0x14 and decrements textureManager[1]
  on success

FUN_801448E8 -> RuntimeFile_ReleaseOwnedMemory
  releases a zanFile-owned memory record when +0x10 is set and +0x0C == 1

FUN_80143A98 -> RuntimeDebugAssert
  release-build assert/report sink used by zanFile/zanTexture paths

FUN_80143830 -> GlobalRuntimeContext_SelectRegionVariant
  chooses the active region/language path-table index from global context +0x8C,
  falling back to +0xB8 when the +0x90 table entry is zero

FUN_80143818 -> GlobalRuntimeContext_SetRegionVariantEntry
  writes global context `+0x90 + index * 4`

FUN_80143828 -> GlobalRuntimeContext_SetFallbackRegionVariant
  writes global context `+0xB8`

FUN_80144B40 -> GlobalResourceManager260_SetModeTable
  writes manager `+0x4C`

FUN_80144EA0 -> RuntimeMemory_SetCriticalFlag
  sets DAT_802E71C8 and uRam802E71CC under the memory mutex

FUN_80144EF4 -> RuntimeMemory_ClearCriticalFlag
  clears DAT_802E71C8 under the memory mutex

FUN_80023634 -> LargeResourceManager_ActivateDefaultAudioReferences
  activates/refcounts handles from DAT_8027A570 and marks largeResourceManager +0x43C

FUN_8003C9A0 -> CGame_UpdateViewerSetupSelection
  interactive debug/viewer selector used by CGame setup; edits style/mode fields,
  draws SSQ/MOTION/VIEWER text, and returns 1 accept, -1 cancel, or 0 pending

FUN_8003D180 -> CGame_InitDefaultGameplaySetup
  seeds the CGame setup/default song block around +0xD4..+0x118; sets the default
  song table to "more than alive", magic +0xF0 to 0x12345678, +0xFC to -1, +0x10C
  to 5, and +0xD4 to 0x06880000

FUN_80028120 -> PlayerDataManager_GetSetupFieldF8
  returns the player-data manager word at +0xF8

FUN_8002813C -> PlayerDataManager_GetSetupFieldFC
  returns the player-data manager word at +0xFC

FUN_800282C4 -> PlayerDataManager_GetInactiveOrCpuPlayerCount
  returns the player-data manager word at +0x110

FUN_800282CC -> PlayerDataManager_GetPlayerCount
  returns the player-data manager word at +0x114

FUN_800F3B9C -> PlayerStats_GetField170
  returns the word at +0x170 from a player/stat record

FUN_8002754C -> GameIndexedId_GetCategory
  maps sparse gameplay/resource ids into seven compact categories:
  0x00..0x0E -> 0, 0x14..0x1F -> 1, 0x28..0x36 -> 2, 0x3C..0x4A -> 3,
  200..208 -> 4, 0x50..0x59 -> 5, and 100 -> 6; all other ids return -1

FUN_800275D8 -> GameIndexedId_ToLinearIndex
  converts a categorized id into a linear index by adding the prefix sum from
  DAT_8026E87C to `id - DAT_8026E898[category]`; the current host constants are
  derived from the ranges in FUN_8002754C

FUN_80026AC8 -> LargeResourceManager_SetTransitionSoundBanks
  ignores its first argument and applies the supplied value to sound player banks 8
  and 9 through FUN_8002458C(gManager_802E70A4, bank, value)

FUN_8002458C -> GlobalCueManager_SetSoundPlayerBankStopParam
  resolves DAT_802E71B8 +0x268 and forwards the bank/value pair to FUN_8016AFB8

FUN_8016AFB8 -> CzanSoundManager_SetPlayerBankStopParam
  walks one 0x9C-byte sound-player bank from soundManager +0x9B8, index checked
  against +0x9B4, and applies FUN_8018FB20 to active linked nodes

FUN_8018F508 -> CzanSoundPlayerBank_FindFirstActiveNode
  scans a sound-player bank's linked list at +0x18 when bank +0x04 is positive and
  returns the first node accepted by the node predicate

FUN_8018FB20 -> CzanSoundPlayerNode_SetStopOrPassive
  for an active sound node, stores stopParam at +0x2A, clears +0x28/+0x30, sets
  +0x2C to 0 or 1, and either marks state low nibble 5 or moves the node passive

FUN_80169DDC -> CzanSoundManager_ClearAuxState
  frees the optional pointer at soundManager +0x9C0, then clears +0x9BC size 0x14

FUN_80024AF4 -> GlobalCueManager_ResetRuntimeState
  clears sound manager aux state through DAT_802E71B8 +0x268, releases two optional
  cue-manager pointers at +0x484/+0x488, clears +0x480, +0x484 size 8, +0x48C,
  +0x490, and +0x494

FUN_80024F28 -> ResourceSlotHandle_ReleaseIfManagerPresent
  if slotHandle[0] exists, releases the referenced slot through ResourceSlotManager_ReleaseSlot

FUN_80024C3C -> ResourceSlotHandle_Init
  initializes a small resource slot handle with manager pointer 0, slot index -1,
  and the original vtable/type pointer slot represented as a host-null placeholder

FUN_80025DEC -> LargeResourceManager_ResetLoadedState
  outer teardown for the active DAT_802E70BC/gLargeResourceManager: clears the active
  flag and known top-level state, visits seven large banks at +0x91610 stride +0x58534,
  clears the SSQ sequence-data object, resets the stage bank, and marks
  manager[0]/[1]/[3] reset

FUN_8002F34C -> TsSeqData_ClearBankWorkBuffers
  clears ten large sequence work-buffer groups, clears the 0xA0-byte table at
  +0x9109C, then calls TsSeqData_Reset

FUN_8002F63C -> TsSeqData_Reset
  resets the `tsSeqData` object: clears timing arrays, sequence counters, per-mode
  scratch pointers, and attached per-bank 0xA000/0x4000/0x600 work buffers

FUN_800337F0 -> LargeResourceBank_InitDefaults
  initializes one 0x58534-byte large-resource bank record: clears top-level state,
  seeds mode defaults from DAT_8026F620/DAT_8026F624 tables, sets +0x6C to 1, then
  resets active state through FUN_8003552C

FUN_80033988 -> LargeResourceBank_ResetAndFreeScratch
  resets one large-resource bank record, freeing the optional scratch buffer at +0x78
  before reapplying LargeResourceBank_InitDefaults

FUN_8003552C -> LargeResourceBank_ResetActiveState
  clears one large-resource bank record's active links, stage-bank payload reference,
  animation/effect helper subrecords, and high-offset runtime fields

FUN_800A82F4 -> RuntimeSlotTable_ClearEntry
  clears one 0x30-byte entry or the whole table when index is -1; defaults +0x1C/+0x20
  to 1.0 and bytes +0x28..+0x2B to 0xFF

FUN_800A8428 -> RuntimeSlotTable_SetPayload
  writes entry +0x00, keeps active entries packed at the front of the order table, and
  bumps/sorts the key when a slot transitions from empty to active

FUN_800A85DC -> RuntimeSlotTable_SetSortKey
  writes entry +0x2C and sorts active entries by sort key, then pointer address

FUN_800B6DCC -> LargeResourceStageBank_ClearStageSlot
  clears table slot index +7 at stageBank +0x5370, then optionally restores sort key
  and payload

FUN_800B6E4C -> LargeResourceStageBank_ClearMatrixSlot
  clears table slot `rowIndex + bankIndex * 4 + 0x0B` at stageBank +0x5370, then
  optionally restores sort key and payload

FUN_800B6864 -> LargeResourceStageBank_ResetLoadedState
  resets the stage bank at gLargeResourceManager +0x2FBA7C: releases grouped stage
  handles, unloads model bank 0, clears nested runtime slot tables, deletes the
  owned texture, rebuilds the +0x5370 slot table, and clears stage-handle arrays

FUN_8012543C -> CGameTransitionSlot_Reset
  resets the transition subsystem: clears stage-bank table links, owned subsystem
  handles, texture slot, UI/model handles, nested 7x12 records, unloads model bank 2,
  clears +0x444/+0x445, and marks the slot inactive

FUN_80099F70 -> Manager802e70e0_Init
  constructs/resets the large manager stored in gManager_802E70E0, clearing +0x04
  size 0x28F38 and resetting trailing state fields at word offsets 0xA3CF..0xA3D5

FUN_8009A050 -> Manager802e70e0_DestroyNoop
  empty destructor/no-op for gManager_802E70E0

FUN_800221BC -> BootResourceBundle_Init
  constructs the boot/CGame resource bundle object: sets the vtable slot, resource
  state -1, clears the 0x438-byte body, initializes slot state around +0x43C..+0x494,
  and leaves resource handles inactive

FUN_80021D60 -> BootTempManager_Init
  clears the small 0x28-byte gBootTempManager object

FUN_80024D38 -> ResourceSlotHandle_CreateSlotPool
  rebuilds gManager_802E70A8 after ResourceSlotHandle_Init: releases the existing
  slot if needed, allocates the 0x56C8 slot manager, and initializes 10 movie/resource
  slots

FUN_80025A7C -> LargeResourceManager_Init
  constructs gLargeResourceManager (0x3010B8): initializes the common manager,
  small record controller, seven 0x58534-byte banks, stage bank at +0x2FBA7C, and
  resets active state

FUN_800C0BF8 -> Manager802e70b0_Init
  constructs the 0x50-byte gManager_802E70B0 and sets +0x44 to -1

FUN_800CC450 -> CharacterAssetManager_Init
  constructs the actual gCharacterAssetManager object allocated at size 0x284E0;
  initializes the +0x34 and +0x28168 nested managers, sets several ids to -1,
  stores default floats, sets +0x24 to 0x11, and clears +0x28164

FUN_800CC574 -> CharacterAssetManager_LoadSelectCommon
  loads select/select_cmn.bin when +0x28164 is clear: block 0 goes to
  CSelectCommon_LoadResource(+0x28168), block 1 enters the FUN_800F68FC setup
  path at +0x34, and block 2 loads model manager bank 4

FUN_800CC690 -> CharacterAssetManager_UpdateActiveAssets
  per-frame active character/select asset update; calls the pending sound/timer
  subpaths, then drives CharacterAssetSelectSupport_UpdateResourceRecords at +0x34
  when the caller does not pass skip flag 1

FUN_800CCBD0 -> CharacterAssetManager_IsSelectCommonIdle
  returns 1 only while gCharacterAssetManager +0x24 is 0x11; menu modules use it
  as the select/common manager idle predicate before starting another transition

FUN_800CD668 -> CharacterAssetManager_DestroyLiveSelectSupportObjects
  wrapper around CharacterAssetSelectSupport_DestroyLiveObjects at +0x34

FUN_800CD670 -> CharacterAssetManager_AreSelectSupportRecordsIdle
  wrapper around CharacterAssetSelectSupport_AreResourceRecordsIdle at +0x34

FUN_800CD678 -> CharacterAssetManager_UpdateSelectSupportEntries
  wrapper around CharacterAssetSelectSupport_UpdateEntries at +0x34

FUN_800CD680 -> CharacterAssetManager_SetSelectSupportEntryId
  wrapper around CharacterAssetSelectSupport_SetEntryId at +0x34

FUN_800CD688 -> CharacterAssetManager_MarkSelectSupportEntryDirty
  wrapper around CharacterAssetSelectSupport_MarkEntryDirty at +0x34

FUN_800CD690 -> CharacterAssetManager_SetSelectSupportPartValue
  wrapper around CharacterAssetSelectSupport_SetPartValue at +0x34

FUN_800CD698 -> CharacterAssetManager_SetSelectSupportPartColor
  wrapper around CharacterAssetSelectSupport_SetPartColor at +0x34

FUN_800CD6A0 -> CharacterAssetManager_SetSelectSupportEntryAnimation
  wrapper around CharacterAssetSelectSupport_SetEntryAnimation at +0x34

FUN_800CD6A8 -> CharacterAssetManager_ClearSelectSupportEntryAnimation
  wrapper around CharacterAssetSelectSupport_ClearEntryAnimation at +0x34

FUN_800F66A4 -> CharacterAssetSelectSupport_Init
  constructs the +0x34 select-support object: initializes its CzanLinkManager,
  clears entry/resource records, seeds all CHR%02d%02d and CHR%02d0 resource paths,
  and resets the four 0xC4-byte entry records

FUN_800F6848 -> CharacterAssetSelectSupport_Destroy
  destructor path for the +0x34 select-support object; releases live objects and
  the bound link manager before optional memory free

FUN_800F68FC -> CharacterAssetManager_LoadSelectCommonBlock1
  resets the +0x34 select-support object state, camera/projection helper, small
  vector fields, and binds the select_cmn block-1 WII link at +0x27B10 for the
  later live object/effect resource consumers

FUN_800F6A48 -> CharacterAssetSelectSupport_UpdateResourceRecords
  per-frame update for the +0x34 select-support records: runs the resource-load
  throttle over 77 * 24 per-player records plus 77 shared records, then advances
  object/effect reference counts and calls the ready-stage-object attach path

FUN_800F6DC4 -> CharacterAssetSelectSupport_UpdateEntries
  advances the four select-support entry records after resource record throttling,
  increments referenced CHR record counters when entries become active, and calls
  CharacterAssetSelectSupport_AttachReadyStageObject for ready live objects

FUN_800F7428 -> CharacterAssetSelectSupport_AttachReadyStageObject
  creates a 0x7DC extended CtsStageObj when both per-player and shared resource
  records have completed, attaches all blocks from the +0x27B10 link, and starts
  the configured animation/effect bindings

FUN_800F7888 -> CharacterAssetSelectSupport_ResetEntryRecord
  resets one 0xC4-byte select-support entry record to id -1, clears per-player
  flags/colors, seeds timing/default animation fields, and clears live object slots

FUN_800F79BC -> CharacterAssetSelectSupport_UpdateResourceRecordLoad
  load-throttle helper for one 0x54-byte resource record; keeps at most 0x4B active
  loaded records through +0x27AB4 and calls LoadResourceByPath only when the record
  is marked needed

FUN_800F7B60 -> CharacterAssetSelectSupport_SetEntryId
  changes one of the four 0xC4-byte select-support entry records to a new indexed
  id/sub-index pair; if the previous live object was fully active, decrements the
  referenced per-player/shared resource record counts before tearing the object down

FUN_800F7E84 -> CharacterAssetSelectSupport_MarkEntryDirty
  sets entry +0xA8 to 1 for the selected 0xC4-byte entry

FUN_800F7E98 -> CharacterAssetSelectSupport_SetPartValue
  writes `entry + partIndex * 4 + 0x08`, the special-part selector value used by
  the later attach/update paths

FUN_800F7EB0 -> CharacterAssetSelectSupport_SetPartColor
  copies four RGBA bytes into `entry + partIndex * 4 + 0x18`

FUN_800F7EE4 -> CharacterAssetSelectSupport_SetEntryAnimation
  changes entry +0x30/+0x38 and tears down the live object at +0x40 when the
  animation binding changes

FUN_800F801C -> CharacterAssetSelectSupport_ClearEntryAnimation
  clears entry animation binding +0x30/+0x38 and tears down the live object

FUN_800F814C -> CharacterAssetSelectSupport_DestroyLiveObjects
  destroys all four live select-support objects, clears attach flags +0x44..+0xA4,
  and resets entry timers to their default values

FUN_800F8348 -> CharacterAssetSelectSupport_AreResourceRecordsIdle
  returns 0 while any per-player or shared select-support resource record is still
  in state 1; returns 1 only when all 77 groups are idle

FUN_800CCB34 -> CharacterAssetManager_ResetSelectCommonState
  resets the root character-asset state at +0x10/+0x14/+0x18/+0x1C/+0x20 before
  CSelect starts state-specific preload logic

FUN_80058EA8 -> CtsStageObj_LoadModelBlocks
  loads a primary ZMB block plus optional TPL texture block into a CtsStageObj,
  allocates a 0x2D0 CzanModel, applies continuation count and texture slot, and
  enables the model; host keeps the model pointer in side storage to avoid
  truncating it through the original 32-bit entry[0] field

FUN_80059158 -> CtsStageObj_LoadContinuationBlock
  forwards a continuation/ZAB block into the CtsStageObj-owned CzanModel

FUN_800591BC -> CtsStageObj_StartAnimation
  writes the starting animation frame into the CtsStageObj-owned CzanModel

FUN_80160308 -> CzanLinkManager_Init
  initializes an empty CzanLinkManager; host side storage preserves the full
  64-bit WII resource pointer while the original 32-bit fields are kept as
  mirrors for nearby code

FUN_801603D4 -> CzanLinkManager_SetLink
  binds a WII link resource, validates the header/version, sets the block-table
  pointer to resource +0x10, relocates block offsets when needed, and marks the
  resource relocated

FUN_80160504 -> CzanLinkManager_GetBlockInfo
  returns one linked block pointer and size from the CzanLinkManager block table;
  the host path also accepts a registered raw WII link pointer so direct loaders can
  consume exported link resources without a fake 32-bit stack manager

FUN_801606AC -> CzanLinkManager_GetBlock
  returns the block pointer for a given index; the host wraps this through
  CzanLinkManager_GetBlockInfo so callers never dereference a truncated 32-bit
  manager pointer on the 64-bit host

FUN_800FE48C -> UiRootManager_Init
  constructs the 0x4C-byte gUiRootManager and initializes object group handles to -1

FUN_80105DE4 -> UiRootSubManager_InitTextureFrameGroupState
  resets the 0x7C8 menu-presentation/texture-frame object: clears both bank masks,
  initializes 0x28 per-bank entry records, seeds default offsets, and enables the
  default-visible Czan object groups

FUN_801061B4 -> UiRootSubManager_UpdateTextureFrameGroupMotion
  per-frame update for the two 0x28-entry banks at uiRoot +0x38: detects mask changes,
  seeds the motion records through FUN_801064F8, interpolates active records, and
  applies offsets through FUN_80106724

FUN_801064F8 -> UiRootSubManager_SeedTextureFrameGroupMotion
  prepares the per-entry offset targets after the two-word mask at +0xA8/+0xAC changes

FUN_80106724 -> UiRootSubManager_ApplyTextureFrameGroupMotion
  applies active menu-presentation offsets with CzanUiManager_ApplyObjectGroupPositionLayout

FUN_80106850 -> UiRootSubManager_SetMenuPresentationMode
  writes the selected bank's two mask words at +0xA8/+0xAC; the runtime update pass
  consumes them later

FUN_80106B48 -> UiRootSubManager_ResetMenuPresentationBankFlags
  clears a per-child value/flag through FUN_801757E4 for all 0x28 object groups in
  the selected presentation bank

FUN_80178278 -> ModelEffectManager_Init
  constructs gManager_802E70B8, initializes nested model/effect/list managers, clears
  transient banks, and stores the global manager back-pointer
```
