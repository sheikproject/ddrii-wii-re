# Graphics / Video Function Map

This page tracks the executable-backed functions that control Wii display mode,
resolution-facing globals, viewport/scissor-style dimensions, and copy filtering.

## Low-Level VI Setup

`FUN_801BA980 -> RuntimeVideo_GetTimingTable`

Returns the VI timing-table pointer used by the rest of the low-level video path.
Known table selectors include NTSC/PAL/progressive/interlaced variants and the
custom fallback table at `DAT_802E74D4` for selectors `0x1C`, `0x1D`, `0x1E`,
and `0x22`.

`FUN_801BAAA0 -> RuntimeVideo_ProgramInitialViRegisters`

Writes the initial hardware VI register set from the timing table selected by
`RuntimeVideo_GetTimingTable`. It temporarily disables display output through
`cc002002`, programs the core timing registers, and then re-enables display with
the selected scan/mode bits.

`FUN_801BACA0 -> RuntimeLowLevel_InitVideoInterface`

One-time VI/video initializer called before `DAT_802E71B8` is allocated.

Confirmed behavior:

```text
if DAT_802E7480 == 0:
  initialize low-level timing/interrupt state
  write VI timing registers cc00204c..cc002070 from DAT_802D16A4..DAT_802D16D4
  derive display mode from FUN_801E6700, DAT_800000CC, cc002002, cc00206c
  DAT_802F6378 = base VI mode
  DAT_802E74F0 = active VI mode
  DAT_802E74F4 = mode timing table from FUN_801BA980(...)
  DAT_802F6350 = 0x28
  DAT_802F6354 = 0x280
  DAT_802F6356 = timingTable[+2] * 2
  DAT_802F6362 = 0x280
  DAT_802F6364 = timingTable[+2] << 1
  DAT_802F636A = 0x280
  DAT_802F636C = timingTable[+2] << 1
  DAT_802F6370 = 0
  DAT_802F6390 = 1
  DAT_802F63A4 = active timing table
  register retrace/VI callbacks
  call FUN_801BE580(previousState)
```

The host equivalents live in `RuntimeLowLevel_InitVideoInterface` and
`RuntimeLowLevelVideoState`. They should mirror these game-facing values, even though
Windows cannot write the Wii VI registers directly.

## VIConfigure-Style Mode Change

`FUN_801BB780 -> RuntimeVideo_Configure`

This is the main mode/viewport recomputation path. It is the function that corresponds
to the SDK-style `VIConfigure` branch in the export.

Inputs come from a mode struct:

```text
mode[0] bits 0..1 -> scan/interlace/progressive variant
mode[0] >> 2      -> VI mode family
mode +0x0A        -> x origin / display x
mode +0x0C        -> y origin / display y
mode +0x0E        -> display width
mode +0x04        -> framebuffer width
mode +0x08        -> framebuffer height
mode +0x14        -> anti-alias / field mode flag
```

Confirmed writes:

```text
DAT_802F6374 = mode[0] & 3
DAT_802F6378 = mode[0] >> 2, except 0 or 2 falls back to DAT_800000CC
DAT_802F6350 = mode +0x0A
DAT_802F6352 = mode +0x0C, doubled when DAT_802F6374 == 1
DAT_802F6354 = mode +0x0E
DAT_802F6362 = mode +0x04
DAT_802F6364 = mode +0x08
DAT_802F6370 = mode +0x14
DAT_802F6356 = DAT_802F6364, or doubled when not progressive/interlaced and AA flag is clear
DAT_802F636A = DAT_802F6362
DAT_802F636C = DAT_802F6364
DAT_802F63A4 = FUN_801BA980(DAT_802F6378 * 4 + DAT_802F6374)
```

It then recalculates derived horizontal/vertical bounds and writes pending VI register
bits before calling:

```text
RuntimeVideo_PackHorizontalTimingRegisters(...)
RuntimeVideo_PackVerticalTimingAndCopyFilter(..., DAT_802F6390)
```

## VI Register Packing Helpers

`FUN_801BB250 -> RuntimeVideo_PackXfbOriginRegisters`

Computes the VI XFB origin register values from a mode/display struct and marks the
pending VI register dirty mask. It supports the alternate/dual-field origin path
when `mode +0x44` is nonzero.

Confirmed writes:

```text
DAT_802F62F4/DAT_802F62F6 = first XFB origin high/low words
DAT_802F62FC/DAT_802F62FE = second XFB origin high/low words
DAT_802F62F8/DAT_802F62FA = alternate first origin when mode +0x44 is set
DAT_802F6300/DAT_802F6302 = alternate second origin when mode +0x44 is set
DAT_802E74B8 |= 0x33000 or 0x3FC00
```

`FUN_801BB500 -> RuntimeVideo_PackHorizontalTimingRegisters`

Packs the horizontal timing/display bounds from the active timing table, x origin,
and display width.

Confirmed writes:

```text
DAT_802F62DE = timingTable[+0x1A]
DAT_802F62DC = timingTable[+0x1D]
DAT_802F62E2 = packed horizontal end/start bits
DAT_802F62E0 = packed horizontal start/width bits
DAT_802E74B8 |= 0x3C000000
```

`FUN_801BB5E0 -> RuntimeVideo_PackVerticalTimingAndCopyFilter`

Packs the vertical timing registers and copy-filter/AA-facing pending VI state.
This is the real function behind the copy-filter toggle, not just a generic GX
state setter.

Confirmed behavior:

```text
uses DAT_802F6374 scan mode and param_9 copy-filter enable
writes pending vertical timing/copy-filter shadow registers DAT_802F62E4..DAT_802F62EA
writes DAT_802F62D8
DAT_802E74B8 |= 0x83C00000
```

## Framebuffer Height / Display Size Update

`FUN_801BBE50 -> RuntimeVideo_SetFramebufferHeightAndViewport`

This updates the active framebuffer/display height while preserving the active timing
table at `DAT_802F63A4`.

Confirmed fields:

```text
DAT_802F636C = param_4
DAT_802F6356 = param_4, or doubled when AA/field mode requires it
DAT_802F6358/DAT_802F635A/DAT_802F635C recomputed
DAT_802F6366 = x offset / crop value
DAT_802F6368 = derived y offset
DAT_802F636A = param_3
RuntimeVideo_PackVerticalTimingAndCopyFilter(..., DAT_802F6390)
```

## Copy Filter / AA Toggle

`FUN_801BC340 -> RuntimeVideo_SetCopyFilterEnabled`

Confirmed behavior:

```text
DAT_802F6390 = param_1
RuntimeVideo_PackVerticalTimingAndCopyFilter(
  DAT_802F635A,
  DAT_802F6356,
  DAT_802F63A4[0],
  DAT_802F63A4[+2],
  DAT_802F63A4[+4],
  DAT_802F63A4[+6],
  DAT_802F63A4[+8],
  DAT_802F63A4[+10],
  param_1)
```

This is the currently identified anti-alias/copy-filter-facing control.

## Pending VI Register Flush / Alternate XFB State

`FUN_801BC1B0 -> RuntimeVideo_FlushPendingViRegisters`

Flushes the dirty pending VI shadow registers to the hardware register block. It
walks `DAT_802E74B8/DAT_802E74BC` by bit index, copies the matching word from the
pending register array at `DAT_802F62D8`, then marks retrace/update flags.

`FUN_801BC2D0 -> RuntimeVideo_EnableAlternateXfbOriginState`

Enables the alternate XFB-origin state and immediately recomputes the XFB origin
registers through `RuntimeVideo_PackXfbOriginRegisters`.

Confirmed behavior:

```text
DAT_802E74D0 = 1
DAT_802F6380 = param_1
RuntimeVideo_PackXfbOriginRegisters(&DAT_802F6350, &DAT_802F6384, &DAT_802F6388,
                                    &DAT_802F639C, &DAT_802F63A0)
```

`FUN_801BE580 -> RuntimeVideo_ApplyDisplayState`

Programs the low-level external video/display state after VI init. It sends command
bytes through `FUN_801BCDC0`/`FUN_801BC9E0`, derives `DAT_802E754C` from
`DAT_800000CC`, and applies the current display/region output mode.

## VI Retrace / External Video Encoder

`FUN_801BA0D0 -> RuntimeVideo_RetraceInterruptHandler`

The VI retrace interrupt handler. It clears VI interrupt status bits, increments the
retrace counter, runs registered retrace callbacks, flushes pending VI register
updates, watches display-cable/mode changes, and dispatches queued external video
encoder commands.

`FUN_801BB1F0 -> RuntimeVideo_WaitForRetrace`

Blocks until `DAT_802E752C`, the retrace counter incremented by
`RuntimeVideo_RetraceInterruptHandler`, changes.

`FUN_801BC5C0 -> RuntimeVideo_ReadDisplayCableStatusBit`

Reads `cc00206e & 1` under the critical-section wrapper. The result feeds
`RuntimeVideo_ApplyDisplayState` and the retrace handler's cable-change path.

`FUN_801BC600 -> RuntimeVideo_ConvertBeamPositionToFieldLine`

Converts current VI beam position (`cc00202c/cc00202e`) into the caller-visible
field/line pair using the active timing table at `DAT_802E74F4`.

`FUN_801BC9E0 -> RuntimeVideo_DelayMicroseconds`

Busy-waits from `Runtime_GetBootTime` until the requested small delay has elapsed.
The external video encoder command path consistently calls it with `2`.

`FUN_801BCA70 -> RuntimeVideoEncoder_SendByteWithAck`

Bit-bangs one byte through the external video encoder control registers
`cd8000c0/cd8000c4`, waits for acknowledgement through `cd8000c8`, and returns
success/failure.

`FUN_801BCDC0 -> RuntimeVideoEncoder_SendCommandBytes`

Sends a command byte stream to the external video encoder. It initializes the
encoder control polarity on first use, enters a critical section, calls
`RuntimeVideoEncoder_SendByteWithAck` for synchronization, shifts each payload byte
out bit-by-bit, and returns a success flag through the runtime return marker.

Small encoder-command wrappers confirmed so far:

```text
0x801BD300 -> RuntimeVideoEncoder_SendCableStatusModeCommand
0x801BD3A0 -> RuntimeVideoEncoder_SendModeFamilyCommand
0x801BD3E0 -> RuntimeVideoEncoder_SendColorControlCommand
0x801BD440 -> RuntimeVideoEncoder_SendGeometryControlCommand
0x801BD4B0 -> RuntimeVideoEncoder_SendPictureControlCommand
0x801BD520 -> RuntimeVideoEncoder_SendRegionPresetTables
0x801BE2A0 -> RuntimeVideoEncoder_SendGammaTableCommand
0x801BE400 -> RuntimeVideoEncoder_SendDefaultGammaTable
0x801BE410 -> RuntimeVideoEncoder_SendSelectedGammaTable
0x801BE430 -> RuntimeVideoEncoder_SendEnableFlagCommand
0x801BE490 -> RuntimeVideoEncoder_SendAspectModeCommand
0x801BE520 -> RuntimeVideoEncoder_QueueAspectModeCommand
0x801BE530 -> RuntimeVideoEncoder_SendForceRgbModeCommand
```

## Render-Side Consumers

Already-renamed host render functions that consume the video/screen setup:

```text
FUN_80170FA4 -> UiScreenProjection_UpdateGlobals
FUN_801B0C30 -> Matrix44_SetPerspective
FUN_801D7A90 -> RenderFlushViewportState
FUN_801D2B80 -> RenderClearVertexDescriptors
FUN_801D2BC0 -> RenderSetVertexAttrDescriptor
FUN_801D2730 -> RenderSetVertexAttrFormat
FUN_801D7110 -> RenderSetBlendMode
FUN_801D7240 -> RenderSetAlphaUpdate
FUN_801D6B70 -> RenderSetAlphaCompare
FUN_801D4180 -> RenderSetScissorBox
FUN_801D41C0 -> RenderSetViewportScissorBox
FUN_801D4230 -> RenderSetFramebufferCopyTexture
FUN_801D7800 -> RenderSetProjectionMatrix
FUN_801D7870 -> RenderSetRawProjectionState
FUN_801D78C0 -> RenderGetRawProjectionState
FUN_801D7B20 -> RenderSetViewport
FUN_801D7B60 -> RenderSetViewportDirect
```

## THP Movie Decode / Draw Path

The Wii SDK at
`E:\Dolphin Isos\modding\wii_development_package\wii_development_package\RVL_SDK`
confirms the low-level movie API used by the game:

```text
THPInit()
THPVideoDecode(file, tileY, tileU, tileV, work)
THPAudioDecode(buffer, audioFrame, flag)
```

`THPVideoDecode` writes three 32-byte-aligned I8 texture planes:

```text
Y size = width * height
U size = width * height / 4
V size = width * height / 4
work size = 0x1000
```

The SDK demo draw path is:

```text
THPVideoDecode(videoFrame, ytexture, utexture, vtexture, thpWork)
THPGXYuv2RgbSetup(rmode)
THPGXYuv2RgbDraw(ytexture, utexture, vtexture, x, y, textureWidth, textureHeight, polygonWidth, polygonHeight)
THPGXRestore()
```

The game uses the same shape through the Czan movie object:

```text
0x801844E8 -> CzanMovieObj_LoadResource
0x80184FE8 -> CzanMovieObj_StartPlayback
0x801847CC -> CzanMovieObj_UpdateDecodeAndPlayback
0x80185230 -> CzanMovieObj_SubmitDecodeRequests
0x8019F544 -> CzanThpVideoDecoder_DecodeFrame
0x80239040 -> THPVideoDecode
0x8019FCA8 -> CzanThpDecodedFrame_GetYPlane
0x8019FCB0 -> CzanThpDecodedFrame_GetUPlane
0x8019FCB8 -> CzanThpDecodedFrame_GetVPlane
0x8019FC14 -> CzanThpDecodedFrame_FlushPlanes
0x80185464 -> CzanMovieObj_DrawMovieFrame
0x80185DFC -> CzanMovieObj_FlushDecodedFramePlanes
0x80185F2C -> CzanMovieObj_ConfigureYcbcrTevStages
```

Important: the OP movie frame component must not be treated as a normal host JPEG
texture. The real decoder outputs Y/U/V I8 planes; the draw path converts YCbCr
to RGB at render time. The old host WIC/JPEG fallback is obsolete for
correctness. The host now ports the `0x80239040` video path far enough to decode
baseline THP video components into Y/U/V planes and draw them through a runtime
OpenGL equivalent of the Y/U/V draw. The host also ports the game-facing
`0x8023C4D0 -> THPAudioDecode` path through `THPAudioDecodeHost` and queues the
decoded ADPCM output through the WinMM backend.

Host timing notes:

```text
0x80024E94 -> ResourceSlotHandle_UpdateActiveSlot
```

The host now advances the claimed movie object's frame counter from the THP header
frame rate and caps catch-up to avoid burning through `movie/select/OP.thp` when
host decoding is slower than the Wii decoder. The OpenGL path also keeps the
current decoded Y/U/V frame resident and only converts/uploads it again when movie
object `+0x238` changes.

Next graphics functions to uncover:

```text
FUN_80239040  THPVideoDecode body; host port active for OP.thp video frames
FUN_80239310  THP SOF0/frame-header reader
FUN_80239450  THP scan-header reader
FUN_80239570  THP quantization-table reader
FUN_80239910  THP Huffman-table reader
FUN_80239CE0  THP bitstream preparation
FUN_80239F30  THP Y/U/V decompression dispatcher; host generic path active
FUN_8023A040  THP 512x448 MCU row decoder
FUN_8023ABC0  THP 640x480 MCU row decoder
FUN_8023AE10  THP generic MCU row decoder; used by 480x864 OP.thp
FUN_8023B070  THP Y component Huffman/DCT block decoder
FUN_8023B6F0  THP U component Huffman/DCT block decoder
FUN_8023BD90  THP V component Huffman/DCT block decoder
FUN_8023C430  THPInit
FUN_8023C4D0  THPAudioDecode; host port active for OP.thp audio frames
```
