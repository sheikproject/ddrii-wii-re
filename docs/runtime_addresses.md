# Runtime Addresses

Source: `input/DDRII.dmw`

Full export: `outputs/dmw_watch_labels.csv`

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
