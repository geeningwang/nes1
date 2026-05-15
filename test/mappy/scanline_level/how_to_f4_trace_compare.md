# How to Generate the Frame 4 Trace Comparison Report

This document describes the pipeline for producing
`test/mappy_out/f4_compare.html` — a per-scanline and per-NT-write
comparison between **nes1** and **FCEUX** for frame 4 of the
Mappy (Japan) ROM.

For the full frame-by-frame pixel comparison see
[how_to_dense_report.md](how_to_dense_report.md).

---

## Prerequisites

| Item | Path / version |
|---|---|
| Visual Studio 2022 | with C++ Desktop workload |
| FCEUX 2.6.6 (64-bit, custom build) | `C:\Work\fceux-2.6.6\vc\x64\Release\fceux64.exe` |
| PowerShell 5.1 | built into Windows |
| ROM | `test\Mappy (Japan).nes` |

> **Custom FCEUX build required.**  The stock FCEUX 2.6.6 release does not
> expose `ppu.getcurrentline()` or `ppu.getloopy()` in Lua, and contains
> a hard-coded `KillFCEUXonFrame` bug that forces exit after frame 4.
> Use the build from `C:\Work\fceux-2.6.6`.

---

## Step 1 — Build nes1.exe

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
    'C:\Work\nes1\src\nes1.sln' /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /v:m
```

Output binary: `src\x64\Debug\nes1.exe`

---

## Step 2 — Generate the FCEUX trace (fceux_sl_f4.txt)

Run the Lua script under the custom FCEUX build:

```
C:\Work\fceux-2.6.6\vc\x64\Release\fceux64.exe `
    -lua "C:\Work\nes1\test\mappy_f4_trace.lua" `
    "C:\Work\nes1\test\Mappy (Japan).nes"
```

The script advances silently to frame 4, records PPU state on every
scanline, then calls `emu.exit()`. Output:

```
test\mappy_out\fceux_sl_f4.txt
```

> **This file only needs to be regenerated when `mappy_f4_trace.lua`
> changes** (e.g. to capture additional PPU fields) or when the FCEUX
> build is updated. It is consumed as a static reference by the compare
> script.

### What the Lua script captures (per scanline, frame 4)

| Field | Meaning |
|---|---|
| `V_S` | Loopy V register at start of scanline |
| `V_E` | Loopy V register at end of scanline |
| `NTw` | Number of NT ($2007) writes during the scanline |
| `PC` | CPU PC sampled at end of scanline |

**PART2** of the file lists every individual `$2007` NT write in order,
with scanline, nametable address, value written, and PC.

---

## Step 3 — Run the compare script

This step runs nes1 (generating `nes1_sl_f4.txt`) and then merges both
traces into an HTML report.

```powershell
powershell.exe -ExecutionPolicy Bypass `
    -File 'C:\Work\nes1\test\compare_f4_trace.ps1'
```

The script:
1. Runs `nes1.exe --scanlinetrace nes1_sl_f4.txt 4` (overwrites on each run)
2. Reads the existing `fceux_sl_f4.txt` (must already exist from Step 2)
3. Parses both files (PART1 per-scanline, PART2 per-write)
4. Writes `test\mappy_out\f4_compare.html`

Hard-coded paths inside the script:

| Variable | Value |
|---|---|
| `$RomPath` | `C:\Work\nes1\test\Mappy (Japan).nes` |
| `$Nes1Exe` | `C:\Work\nes1\src\x64\Debug\nes1.exe` |
| `$OutDir` | `C:\Work\nes1\test\mappy_out` |
| `$TargetFrame` | `4` |

---

## Step 4 — Open the report

```powershell
Start-Process 'C:\Work\nes1\test\mappy_out\f4_compare.html'
```

---

## Interpreting the report

### Part 1 — Per-Scanline Summary (240 rows)

Each row is one rendered scanline (0–239). Columns are grouped under
colour-coded headers:

| Group | Colour | Fields |
|---|---|---|
| **nes1** | blue border | V_S, V_E, NTw, PC, SSX, SS_Y, CYC |
| **FCEUX** | orange border | V_S, V_E, NTw, PC |
| **match?** | grey | `=` / `≠` indicator for V_E and NTw |

- **Red cell** — this field differs between nes1 and FCEUX
- **Green cell** — match (`=` indicator column)
- Check **"Show Part 1 diffs only"** to hide matching scanlines

### Part 2 — NT Write Log

Each row is one `$2007` (nametable) write, aligned by sequence number
across both emulators. Grouped by scanline with a header showing the
write count per emulator for that scanline.

- **DIFF** — at least one of SL / NTADDR / VAL differs
- **MISS** — the write exists in nes1 but has no corresponding FCEUX entry
  (nes1 produced more writes than FCEUX for that sequence position)
- Check **"Show Part 2 diffs only"** to hide matching writes

---

## Quick re-run after a nes1 code change

```powershell
# 1. Rebuild nes1
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
    'C:\Work\nes1\src\nes1.sln' /p:Configuration=Debug /p:Platform=x64 /v:m

# 2. Re-run compare (regenerates nes1 trace and HTML in one step)
powershell.exe -ExecutionPolicy Bypass `
    -File 'C:\Work\nes1\test\compare_f4_trace.ps1'

# 3. Open report
Start-Process 'C:\Work\nes1\test\mappy_out\f4_compare.html'
```

The FCEUX trace (`fceux_sl_f4.txt`) is **not** re-generated here —
it is stable reference data. Only re-run Step 2 if the Lua script
changes.

---

## Trace file format

### nes1_sl_f4.txt / fceux_sl_f4.txt

```
=== PART1: per-scanline summary ===
# SL  V_S    V_E    NTw  ...
0  0000   01E0   0    ...
1  01E0   03C0   3    ...
...

=== PART2: NT write log ===
# SEQ  SL  V_BEF  NTADDR  VAL  PC
1      1   0340   2000    82   C2A5
2      1   0340   2020    00   C2A7
...
```

Both files use the same column layout so the compare script can parse
them with a single `Parse-TraceFile` function.
