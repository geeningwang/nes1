# Generating FCEUX Frame-Level & Scanline-Level Snapshots

This guide covers building the modified FCEUX and running it to export per-frame txt+bmp
and per-scanline CPU+PPU state traces for nes1 comparison.

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

### 2.3 Create output directory

```powershell
New-Item -ItemType Directory -Force `
    -Path "C:\Work\nes1\test\mappy_out\fceux_dense_out"
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
   - For frame 1 only: `FCEUX_ExportScanlineTrace(outpath)` — writes the per-scanline trace file
6. After 270 frames, `emu.exit()` terminates FCEUX

> **Performance:** With `nothrottle` mode on a modern machine, 270 frames complete in
> approximately 5–10 seconds.

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

### 4.2 Scanline-level trace (frame 1)

```
C:\Work\nes1\test\mappy_out\fceux_dense_out\fceux_scanline_trace_f0001.txt
```

**PART1** — One row per visible scanline (0–239):

| Column | Description |
|---|---|
| SL | Scanline index (0–239) |
| V_S | V register at START of scanline (before CPU run) |
| V_E | V register at END of scanline (after CPU run + rendering) |
| T_E | T (temp) register at end |
| FX | fine_x (3 bits) |
| W | W/latch toggle |
| CTL | PPUCTRL at START of scanline |
| MSK | PPUMASK at START of scanline |
| SSX | Render scroll X (decoded from V: coarse_x×8 + fine_x) |
| SS_Y | Render scroll Y (decoded: coarse_y×8 + fine_y) |
| SSNT | Render nametable selection (bits 10–11 of V) |
| NTw | Number of $2007 NT/AT writes during this scanline |
| CYC | Cumulative CPU cycle counter at end of scanline |
| PC A X Y S P | CPU registers at end of scanline |
| PAL | 64-char hex string: 32 palette bytes |

**PART2** — One row per $2007 NT/AT write:

| Column | Description |
|---|---|
| SEQ | Sequential write number (global, 1-based) |
| SL | Scanline the write occurred on |
| DOT | Approximate PPU dot within scanline |
| V_BEF | NT/AT address written (= V at write time) |
| V_AFT | Placeholder (----) — not tracked |
| NTADDR | Mirrored address written ($2000–$2FFF) |
| VAL | Byte value written |
| PC | CPU PC at time of write |

---

## 5. Key Source Files (for reference)

| File | Role |
|---|---|
| `tool/fceux-2.6.6/src/fceu.cpp` | Enables scanline trace, calls `FCEUX_ExportFrame` + `FCEUX_ExportScanlineTrace` after each frame |
| `tool/fceux-2.6.6/src/fceux_frameexport.cpp` | Contains `CaptureBeginScanline`, `CaptureScanlineTrace`, `LogNTWrite`, `ExportScanlineTrace`, `ExportFrame` |
| `tool/fceux-2.6.6/src/fceux_frameexport.h` | `FCEUXScanlineTrace` struct, `FCEUXNtWrite` struct, function declarations |
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
| Output files are empty or missing | Verify the output directory exists (`fceux_dense_out`). Check that the path in `fceu.cpp`'s `FCEUX_ExportFrame` call points to the correct location. |
