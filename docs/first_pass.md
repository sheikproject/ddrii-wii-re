# DDRII Wii RE - First Pass

## Inputs

- `input/main.dol`
  - Size: `3,040,032` bytes
  - SHA-256: `6cdc239285698500547d24227b8447585385696fb3d47c376bb78ca13269cc84`
- `input/DDRII.dmw`
  - Dolphin Memory Engine watch file
  - Plain JSON
  - Exported to `outputs/dmw_watch_labels.csv`

## DOL Layout

- Entry point: `0x80006310`
- BSS: `0x802E70A0-0x8035E7D0`
- Section table: `outputs/dol_sections.csv`

```text
text0  file 0x00000100  RAM 0x80004000-0x80006740  size 0x2740
text1  file 0x00002840  RAM 0x800204E0-0x8026BE60  size 0x24B980
data0  file 0x0024E1C0  RAM 0x80006740-0x80018F60  size 0x12820
data1  file 0x002609E0  RAM 0x80018F60-0x800204E0  size 0x7580
data2  file 0x00267F60  RAM 0x8026BE60-0x8026BEA0  size 0x40
data3  file 0x00267FA0  RAM 0x8026BEA0-0x8026BEC0  size 0x20
data4  file 0x00267FC0  RAM 0x8026BEC0-0x802A4320  size 0x38460
data5  file 0x002A0420  RAM 0x802A4320-0x802E63A0  size 0x42080
data6  file 0x002E24A0  RAM 0x802E63A0-0x802E70A0  size 0xD00
data7  file 0x002E31A0  RAM 0x802E7B60-0x802EACE0  size 0x3180
```

## DME Watch Export

- Watch entries: `271`
- Inside static DOL sections: `0`
- Outside static DOL sections: `271`

This means the documented DME addresses are runtime state addresses rather than direct `main.dol` patch addresses. They are still very useful: the next step is to use Dolphin watchpoints/breakpoints on these addresses, find which PowerPC functions read or write them, then name those functions in Ghidra.

Type guesses from the DME file:

```text
262  u32/i32
5    u8/i8
2    string
2    u64/i64
```

Early useful labels include:

```text
0x8053EB78  Song ID
0x8054040C  DDR POINTS
0x805753D4  Character ID
0x8068A8E8  Song Score
0x8068A90C  Song MISSES
0x8068A918  Song GREATS
0x8068A91C  Song PERFECTS
0x8068A920  Song MARVELOUS
0x808A20A8  Song Timer?
0x8148EC00  Arrow Highlighted
```

## Tools

Inspect DOL layout:

```powershell
python .\tools\dol_info.py .\input\main.dol --csv .\outputs\dol_sections.csv
```

Convert a RAM address to a file offset:

```powershell
python .\tools\dol_addr.py 0x80006310
```

Convert a file offset to a RAM address:

```powershell
python .\tools\dol_addr.py 0x00002840 --file-offset
```

Export DME labels:

```powershell
python .\tools\dmw_export.py .\input\DDRII.dmw --dol-sections .\outputs\dol_sections.csv --csv .\outputs\dmw_watch_labels.csv
```

## Recommended Next Step

Pick one documented runtime value, preferably something easy to trigger:

- `0x8068A8E8` Song Score
- `0x8068A90C` Song MISSES
- `0x808A20A8` Song Timer?
- `0x8054040C` DDR POINTS

In Dolphin debugger, set a write watchpoint/breakpoint on that address. When it breaks, record the current PC/instruction address and call stack if available. That PC should land inside `main.dol`, which we can map to a Ghidra function and start naming.
