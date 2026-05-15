# Generating Frame-Level & Scanline-Level Snapshots (FCEUX and nes1)

This guide covers building the modified FCEUX and nes1, and running them to export
per-frame txt+bmp and per-scanline CPU+PPU state files for comparison.

Sections 1–6 cover the **FCEUX** side. Section 7 covers the **nes1** side.

---

## 1. Prerequisites

- **Visual Studio 2022** (Community or higher) with C++ desktop workload
- **MSBuild** at `C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`
- **ROM file** (e.g., `Mappy (Japan).nes`)
- **PowerShell**

---

## 2. One-Time Environment Setup

### 2.1 Build FCEUX (Release, x64)

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
$proj    = "C:\Work\nes1\tool\fceux-2.6.6\vc\vc14_fceux.vcxproj"

Start-Process -FilePath $msbuild -ArgumentList @(
    $proj,
    "/p:Configuration=Release",
    "/p:Platform=x64",
    "/p:PlatformToolset=v143",
    "/t:Build",
    "/m",
    "/v:minimal"
) -NoNewWindow -Wait
```

Output binary: `C:\Work\nes1\tool\fceux-2.6.6\vc\x64\Release\fceux64.exe`

> **Note:** The project targets v142 (VS2019) toolset by default. The `/p:PlatformToolset=v143`
> override is required when building with VS2022. The build warnings (D9035, C4002, C4018)
> are harmless and pre-existing in the FCEUX codebase.

### 2.2 Verify DLLs are present

After the build, confirm these files exist alongside the executable:

```
C:\Work\nes1\tool\fceux-2.6.6\vc\x64\Release\
├── fceux64.exe
├── lua51.dll        ← required for Lua scripting (linked via lua51.lib)
├── 7z.dll
├── 7z_64.dll
├── luascripts/      ← Lua standard library directory
├── fcs/
├── cheats/
├── snaps/
├── movies/
├── sav/
└── tools/
```

The `lua51.dll` ships with FCEUX's source at:
- `src\drivers\win\lua\x64\lua51.dll`

If missing after a first-time build, copy from the source location above.

### 2.3 Create output directories

```powershell
# Frame-level dump output (must exist before running)
New-Item -ItemType Directory -Force `
    -Path "C:\Work\nes1\test\mappy_out\fceux_dense_out"

# Scanline-level dump output is created automatically by FCEUX
# (directory name: fceux_sl_dump_fN, where N = export_sl_frame in fceu.cpp)
```

### 2.4 Prepare the Lua script

Save the following as `auto_export.lua`:

```lua
-- Auto-run Mappy for 270 frames, then exit.
-- Frame/scanline trace export happens automatically in fceu.cpp (C-side).
-- This script just needs to advance emulation far enough.

local NFRAMES = 270

emu.speedmode("nothrottle")

for frame = 1, NFRAMES do
    emu.frameadvance()
    if frame % 30 == 0 then
        print(string.format("[auto_export] frame %4d / %d", frame, NFRAMES))
    end
end

print(string.format("[auto_export] Done. %d frames.", NFRAMES))
emu.exit()
```

Place it at: `C:\Work\nes1\test\mappy\frame_level\auto_export.lua`

---

## 3. Running the Export

### 3.1 Run via batch file (recommended)

Use the provided batch file, which passes the ROM path directly — no copy needed.
FCEUX's argument parser has been patched to handle filenames containing spaces and
parentheses.

```batch
C:\Work\nes1\test\mappy\run_fceux_export.bat
```

Or run it from the VS Code task **"Run FCEUX export"**.

The batch file runs:

```batch
fceux64.exe -lua auto_export.lua "Mappy (Japan).nes"
```

### 3.2 What happens during the run

1. FCEUX loads the ROM (no GUI interaction needed)
2. The `-lua` script executes immediately after ROM load
3. `emu.speedmode("nothrottle")` disables frame rate throttling for maximum speed
4. `emu.frameadvance()` is called 270 times in a loop
5. **Inside each `FCEUI_Emulate()` call** (C-side, in `fceu.cpp`):
   - `FCEUX_EnableScanlineTrace()` — starts per-scanline capture
   - `FCEUPPU_Loop()` — emulates the frame, `CaptureBeginScanline`/`CaptureScanlineTrace` called for each scanline
   - `FCEUX_DisableScanlineTrace()` — ends capture
   - `FCEUX_ExportFrame(framenum, 270, outdir)` — writes the txt + bmp for this frame
   - If this is the configured scanline-level frame: `FCEUX_ExportScanlineLevel(framenum, sl_outdir)` — creates `fceux_sl_dump_fN\` (auto-mkdir) and writes 240 txt + bmp pairs
6. After 270 frames, `emu.exit()` terminates FCEUX
>
> **Selecting the scanline-level frame:** Edit `export_sl_frame` in
> `tool/fceux-2.6.6/src/fceu.cpp` (line ~870) to choose which frame gets the
> 240 per-scanline files. Default is frame 5. The output directory is named
> `fceux_sl_dump_fN` (e.g. `fceux_sl_dump_f5`) and is created automatically.

---

## 4. Output Files

### 4.1 Frame-level snapshots

```
C:\Work\nes1\test\mappy_out\fceux_dense_out\
├── fceux_frame_0001.txt    ← CPU/PPU state dump
├── fceux_frame_0001.bmp    ← 256×240 32bpp top-down screenshot
├── fceux_frame_0002.txt
├── fceux_frame_0002.bmp
├── ...
├── fceux_frame_0270.txt
└── fceux_frame_0270.bmp
```

Each `.txt` contains:

| Section | Description |
|---|---|
| `=== CPU State ===` | PC, A, X, Y, S, P, total CPU cycles |
| `=== PPU Registers ===` | PPUCTRL, PPUMASK, PPUSTATUS (decoded) |
| `v=... t=... fine_x=...` | Live Loopy registers + mirroring detection |
| `render_scroll_x=...` | Scroll decoded from V register |
| `=== Nametable ===` | 32×30 tile IDs ($2000–$23BF) |
| `=== Palette ===` | BG (16 bytes) and SPR (16 bytes) |
| `=== Active Sprites ===` | OAM entries (Y, X, Tile, Attr) |
| `=== ASCII Screen ===` | Brightness-based ASCII art |
| `=== CPU RAM ===` | Full 2KB ($0000–$07FF), 64×32 hex dump |
| Companion `.bmp` | 256×240 32bpp BGRA, top-down |

### 4.2 Scanline-level snapshots

For the one frame configured via `export_sl_frame` (default: frame 5), a pair of
files is written for each of the 240 visible scanlines into a dedicated directory
(created automatically):

```
C:\Work\nes1\test\mappy_out\fceux_sl_dump_f5\   ← name derived from export_sl_frame
├── fceux_frame_0005_sl_000.txt    ← scanline 0 dump
├── fceux_frame_0005_sl_000.bmp    ← full frame, scanline 0 highlighted
├── fceux_frame_0005_sl_001.txt
├── fceux_frame_0005_sl_001.bmp
├── ...
├── fceux_frame_0005_sl_239.txt
└── fceux_frame_0005_sl_239.bmp
```

Each `_sl_YYY.txt` has **exactly the same 7 sections** as the frame-level `.txt`,
using the CPU/PPU/memory state captured at the **end of that scanline**, plus one
additional section at the end:

| Section | Source data |
|---|---|
| `=== CPU State ===` | CPU registers at end of scanline |
| `=== PPU Registers ===` | PPUCTRL, PPUMASK, PPUSTATUS, v, t, fine_x, mirroring, scroll |
| `=== Nametable ($2000–$23BF) ===` | Physical NT RAM (NT bank 0 tiles) at end of scanline |
| `=== Palette ($3F00–$3F1F) ===` | Palette snapshot at end of scanline |
| `=== Active Sprites (OAM) ===` | OAM snapshot at end of scanline |
| `=== ASCII Screen ===` | Brightness ASCII art from full rendered frame |
| `=== CPU RAM ($0000–$07FF) ===` | Full 2KB CPU RAM at end of scanline |
| `=== NT Writes During Scanline ===` | All $2007 NT/AT writes during this scanline's CPU window |

The `NT Writes During Scanline` section lists each write as:
```
#   DOT  NTADDR  VAL   PC
 0   123   2284   00   CC0E
```

Each `_sl_YYY.bmp` is the complete rendered frame (256×240) with row `YYY`
tinted red (mixed 50% toward red, green and blue halved) so you can immediately
identify which scanline the file describes.

---

## 5. Key Source Files (for reference)

| File | Role |
|---|---|
| `tool/fceux-2.6.6/src/fceu.cpp` | Enables scanline trace, calls `FCEUX_ExportFrame` for every frame and `FCEUX_ExportScanlineLevel` for the configured frame (`export_sl_frame`) |
| `tool/fceux-2.6.6/src/fceux_frameexport.cpp` | Contains `CaptureBeginScanline`, `CaptureScanlineTrace`, `LogNTWrite`, `ExportFrame`, `ExportScanlineLevel` |
| `tool/fceux-2.6.6/src/fceux_frameexport.h` | `FCEUXScanlineTrace` struct (includes per-scanline NT RAM, CPU RAM, OAM, palette, PPU registers), `FCEUXNtWrite` struct, function declarations |
| `tool/fceux-2.6.6/src/ppu.cpp` | `DoLine()` calls `CaptureScanlineTrace`; `FCEUPPU_Loop` calls `CaptureBeginScanline`; `B2007` hooks `LogNTWrite` |
| `tool/fceux-2.6.6/src/lua-engine.cpp` | Exposes `ppu.getscanlinedata`, `ppu.getloopy`, `ppu.getcurrentline`, `emu.getregisters`, `emu.getcycles` to Lua |
| `test/mappy/frame_level/auto_export.lua` | Lua script that advances 270 frames and exits |

---

## 6. Troubleshooting

| Problem | Solution |
|---|---|
| `lua51.dll was not found` | Copy `lua51.dll` from `src\drivers\win\lua\x64\lua51.dll` to the same directory as `fceux64.exe`. |
| `Error opening <rom path>` | The ROM file was not found. Verify the path is correct. If passing the path unquoted and it contains spaces, either quote it or the FCEUX arg parser will reconstruct it from the split fragments automatically. |
| `The build tools for v142 cannot be found` | Add `/p:PlatformToolset=v143` to the MSBuild command line. |
| FCEUX hangs / doesn't exit | Check that the Lua script calls `emu.exit()` at the end. Increase the Start-Process timeout from 60s. |
| Output files are empty or missing | (1) Verify the output directory exists (`fceux_dense_out`). (2) Check that the path in `fceu.cpp`'s `FCEUX_ExportFrame` call matches the directory. (3) **Known bug (fixed):** `emu.speedmode("nothrottle")` caused `FCEU_LuaFrameskip()` to return -1, which was treated as truthy by the caller, setting `skip=1` for every frame — the export block checked `!skip` and never fired. Fix in `fceu.cpp`: export when `skip < 2` rather than `!skip`. |

---

## 7. nes1 Frame-Level & Scanline-Level Dumps

The native nes1 emulator has the same two-tier export system as the modified FCEUX,
producing identically structured txt+bmp output for direct comparison.

### 7.1 Build nes1

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild "C:\Work\nes1\src\nes1.vcxproj" `
    /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v143 `
    /t:Build /v:minimal
```

Output binary: `C:\Work\nes1\src\x64\Debug\nes1.exe`

### 7.2 Frame-level dump

```powershell
& 'C:\Work\nes1\src\x64\Debug\nes1.exe' 'C:\Work\nes1\test\mappy\Mappy (Japan).nes' `
    --frametest 'C:\Work\nes1\test\mappy_out\nes1_dense_out' 270 1
```

Arguments: `--frametest <outdir> <nframes> <interval>`

- `nframes`: total frames to emulate (270 = matches FCEUX export)
- `interval`: save every Nth frame (`1` = every frame)

**Output** (270 × 2 = 540 files):
```
nes1_dense_out\
├── nes1_frame_0001.txt
├── nes1_frame_0001.bmp
├── ...
├── nes1_frame_0270.txt
└── nes1_frame_0270.bmp
```

Each `.txt` has the same 8-section format as the FCEUX frame-level file:
`CPU State`, `PPU Registers`, `Nametable`, `Palette`, `Active Sprites`,
`ASCII Screen`, `CPU RAM`.

### 7.3 Scanline-level dump

```powershell
& 'C:\Work\nes1\src\x64\Debug\nes1.exe' 'C:\Work\nes1\test\mappy\Mappy (Japan).nes' `
    --scanlinetrace 'C:\Work\nes1\test\mappy_out\nes1_sl_dump_f5' 5
```

Arguments: `--scanlinetrace <outdir> <frame_number>`

**Output** (240 × 2 = 480 files):
```
nes1_sl_dump_f5\
├── nes1_frame_0005_sl_000.txt    ← scanline 0 dump
├── nes1_frame_0005_sl_000.bmp    ← full frame, scanline 0 row tinted red
├── ...
├── nes1_frame_0005_sl_239.txt
└── nes1_frame_0005_sl_239.bmp
```

Each `_sl_YYY.txt` has the same 7 sections as the frame-level txt (using the
per-scanline captured state) plus `=== NT Writes During Scanline ===`:

| Section | Source data |
|---|---|
| `=== CPU State ===` | CPU registers at end of scanline |
| `=== PPU Registers ===` | PPUCTRL, PPUMASK, PPUSTATUS, v, t, fine_x, mirroring, scroll decoded from V |
| `=== Nametable ($2000–$23BF) ===` | Physical NT bank 0 snapshot at end of scanline |
| `=== Palette ($3F00–$3F1F) ===` | Raw palette snapshot (BG: $3F00–$3F0F, SPR: $3F10–$3F1F) |
| `=== Active Sprites (OAM) ===` | OAM snapshot at end of scanline |
| `=== ASCII Screen ===` | Brightness ASCII art from full rendered frame |
| `=== CPU RAM ($0000–$07FF) ===` | 2KB CPU RAM at end of scanline |
| `=== NT Writes During Scanline ===` | All $2007 NT/AT writes during this scanline's CPU window |

Each `_sl_YYY.bmp` is the complete rendered frame with row `YYY` tinted red
(R = (R+255)/2, G /= 2, B /= 2).

### 7.4 Key source files (nes1)

| File | Role |
|---|---|
| `src/nes1.cpp` | `run_frametest()` — calls `export_frame` every N frames; `run_scanlinetrace()` — calls `capture_scanline_trace`/`render_scanline` per scanline, then `export_scanline_level` |
| `src/ppu.cpp` | `export_frame()` — 7-section txt + BMP; `capture_scanline_trace()` — snapshots CPU/PPU/NT/OAM/RAM at end of each scanline; `export_scanline_level()` — writes 240 per-scanline txt+bmp pairs |
| `src/ppu.h` | `ScanlineTraceEntry` struct (per-scanline state including nametable, cpu\_ram, oam\_snap, palette, PPU registers); `NtWrite` struct |

