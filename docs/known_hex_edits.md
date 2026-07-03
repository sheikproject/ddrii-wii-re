# Known Hex Edits

Record previous manual edits here so they can become repeatable patches.

## HP5 Debug Viewer Gecko Codes

Source codes supplied for the European version:

```text
HP5 SSQ Viewer experimental
04021B3C 3BC00003
0403C2D0 2C1E0003

HP5 MOTION Viewer experimental
04021B3C 3BC00003
0403C2D8 2C1E0003
```

These are Gecko 32-bit writes for PAL HP5. In the PAL `main_HP5.dol`, they map as:

```text
0x80021B3C -> file 0x00003E5C, original 3BC00002, patched 3BC00003
0x8003C2D0 -> file 0x0001E5F0, original 2C1E0005, patched 2C1E0003
0x8003C2D8 -> file 0x0001E5F8, original 2C1E0006, patched 2C1E0003
```

The equivalent locations in this DDRII DOL are:

```text
0x80021ADC -> file 0x00003E3C, original 3BC00002, patched 3BC00003
0x8003C23C -> file 0x0001E59C, original 2C1E0005, patched 2C1E0003
0x8003C244 -> file 0x0001E5A4, original 2C1E0006, patched 2C1E0003
```

Patch definitions were added disabled:

```text
patches/hp5_ssq_viewer_experimental_pal_offsets.json
patches/hp5_motion_viewer_experimental_pal_offsets.json
```

The DOL already contains strings for `SSQ VIEWER`, `MOTION VIEWER`, and `VIEWER`,
so the hidden/debug viewer code appears to exist in this build. The exact PAL offsets
still need runtime testing before enabling permanently.

```text
Name:
File offset:
RAM address, if known:
Original bytes:
Patched bytes:
Observed effect:
Tested in:
Notes:
```
