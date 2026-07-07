# DDRII Wii Reverse Engineering Project

This project keeps the original game files untouched while collecting analysis notes, address maps, Dolphin Memory Engine labels, and repeatable patches.

## Layout

```text
input/      Original copied files. Treat as read-only.
docs/       Human notes, findings, known edits, function notes.
ghidra/     Ghidra import/export notes and generated symbol files.
outputs/    Generated CSVs, reports, patched DOL files.
patches/    Patch definitions.
tools/      Small scripts for DOL mapping, DME export, and patching.
```

## Current Inputs

- `input/main.dol`
- `input/DDRII.dmw`

## Useful Commands

Parse DOL sections:

```powershell
python .\tools\dol_info.py .\input\main.dol --csv .\outputs\dol_sections.csv
```

Export Dolphin Memory Engine watches:

```powershell
python .\tools\dmw_export.py .\input\DDRII.dmw --dol-sections .\outputs\dol_sections.csv --csv .\outputs\dmw_watch_labels.csv
```

Convert a RAM address to a DOL file offset:

```powershell
python .\tools\dol_addr.py 0x80006310
```

Convert a DOL file offset to a RAM address:

```powershell
python .\tools\dol_addr.py 0x00002840 --file-offset
```

Apply patch definitions:

```powershell
python .\tools\apply_patches.py .\input\main.dol .\outputs\main_patched.dol .\patches\example_patch.json
```

Build the experimental Windows console host skeleton:

```powershell
New-Item -ItemType Directory -Force outputs | Out-Null
cl /Iinclude src\host\main.c src\host\host_cselect.c src\runtime\module_system.c src\runtime\boot_logo.c src\render\render_engine.c src\platform\null_render_backend.c src\resource\resource_manager.c src\select\csel_mode.c /Fe:outputs\ddrii_host_skeleton.exe
```

Build the experimental Windows OpenGL host:

```powershell
New-Item -ItemType Directory -Force outputs | Out-Null
cl /Iinclude src\host\win32_gl_main.c src\host\host_cselect.c src\runtime\module_system.c src\runtime\boot_logo.c src\render\render_engine.c src\platform\win32_gl_backend.c src\resource\resource_manager.c src\select\csel_mode.c /Fe:outputs\ddrii_host_gl.exe user32.lib gdi32.lib opengl32.lib
```

Run the console host:

```powershell
.\outputs\ddrii_host_skeleton.exe
```

Run the OpenGL host:

```powershell
.\outputs\ddrii_host_gl.exe
```

Host skeleton controls:

```text
A / Left Arrow   previous CSelMode choice
D / Right Arrow  next CSelMode choice
Enter            confirm selected choice and exit
Esc / B          exit
```

The console host is only a control-flow experiment. A real PC port needs either a
recompiled implementation of the original DOL functions or a PowerPC/GX emulation layer;
native Win32 drawing is not equivalent to the original executable render functions.

## First Real Workflow

1. Pick a known DME address, such as score, misses, timer, or character ID.
2. Set a Dolphin debugger watchpoint on that runtime address.
3. Record the PC address when the watchpoint breaks.
4. Use `tools/dol_addr.py` to map that PC to a file offset.
5. Name the function in Ghidra and document it in `docs/function_notes.md`.
6. If modifying bytes, create a JSON patch in `patches/` and apply it with `tools/apply_patches.py`.
