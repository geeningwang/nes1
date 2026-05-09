# nes1

A work-in-progress NES (Nintendo Entertainment System) emulator written in C++ for Windows.

## Overview

nes1 emulates the core hardware of the NES: the MOS 6502-based CPU, the Ricoh 2C02 PPU, and the 2A03 APU. It loads iNES ROM files (Mapper 0), runs the CPU and PPU in sync, renders output to a Win32 window at 60 fps, and supports audio via XAudio2. A headless frametest mode allows automated per-frame comparison against reference output (FCEUX).

## Features

- **6502 CPU emulation** — full instruction set including all official opcodes and common unofficial/undocumented opcodes (`*NOP`, `*LAX`, `*SAX`, `*SBC`, `*DCP`, `*ISB`, `*SLO`, `*RLA`, `*SRE`, `*RRA`)
- **All addressing modes** — implicit, accumulator, immediate, zero page, zero page X/Y, relative, absolute, absolute X/Y, indirect X/Y, JMP absolute/indirect
- **Cycle-accurate counting** — including page-crossing and branch penalties; per-frame CPU cycle count matches FCEUX exactly (29780/29781 alternating via odd-frame kook)
- **2C02 PPU emulation** with full Loopy-register scroll model:
  - Background rendering with per-scanline horizontal and vertical scroll (sprite-0-hit scroll split supported)
  - Sprite rendering with H-flip, V-flip, behind-BG priority, 64 sprites
  - Sprite-0 hit detection
  - Per-scanline palette — mid-frame palette writes take effect on the correct scanline
  - **Per-scanline live rendering**: pixels are written into the framebuffer one scanline at a time inside the CPU/PPU interleave loop, reading from live VRAM (`mem[]`) so mid-render `$2007` VRAM DMA writes (tile streaming) are immediately visible on the correct scanline — identical to FCEUX
  - NMI timing matches FCEUX VBL-first frame structure
  - Nametable mirroring (horizontal and vertical)
  - PPUMASK colour emphasis (all 8 combinations measured from FCEUX)
  - Greyscale mode
- **2A03 APU emulation** — pulse (×2), triangle, noise channels with XAudio2 output
- **Win32 display** — renders to a GDI DIBSection at 2× scale with a live FPS counter
- **Controller input** — Player 1 D-pad and A/B/Select/Start via keyboard
- **iNES ROM loading** — parses the 16-byte iNES header, supports 1–2 PRG banks and 1 CHR bank (Mapper 0)
- **Automated headless test modes** — `--frametest`, `--autotest`, `--scanlinetrace` (see below)

## Limitations

- Only **NROM (Mapper 0)** is supported — up to 2×16 KB PRG ROM banks and 1×8 KB CHR ROM bank
- Sprite rendering uses OAM snapshot from start of frame (no mid-frame OAM DMA update within a frame)
- 8×16 sprites not supported
- Trainers in ROM files are not supported

## Project Structure

```
src/
  nes1.cpp          — Win32 entry point, ROM loader, main emulation loop, test modes
  cpu.cpp / cpu.h   — MOS 6502 CPU emulation
  ppu.cpp / ppu.h   — Ricoh 2C02 PPU emulation
  apu.cpp / apu.h   — 2A03 APU emulation (pulse, triangle, noise)
  stdafx.cpp/.h     — Precompiled header
  nes1.sln          — Visual Studio solution
  nes1.vcxproj      — Visual Studio project (x64)

test/
  nestest.nes           — Standard NES CPU test ROM
  nestest.log.txt       — Expected nestest disassembly log (for comparison)
  Mappy (Japan).nes     — Real game ROM (Mapper 0, vertical mirroring)
  mappy_compare.ps1     — PowerShell script: diff nes1 vs FCEUX frame outputs
  frame_diff_report.ps1 — PowerShell script: generate per-frame diff summary
  mappy_fceux_dense.lua — FCEUX Lua script: export dense per-frame text snapshots
  mappy_ppu_trace.lua   — FCEUX Lua script: PPU state trace
  mappy_out/            — nes1 frametest output (gitignored)
  ppu/                  — PPU colour test ROM and source

doc/
  6502_cpu.txt      — 6502 CPU reference
  nestech.txt       — NES technical reference
```

## Building

Open `src/nes1.sln` in Visual Studio and build the **x64 Debug** configuration. No external dependencies are required beyond the Windows SDK and XAudio2 (included with the Windows SDK).

## Running

```
nes1.exe <rom.nes>
```

Controls (Player 1):

| Key | NES Button |
|-----|------------|
| F | A |
| D | B |
| S | Select |
| Enter | Start |
| Arrow keys | D-pad |

## Test Modes

### Frame test — headless rendering comparison

```
nes1.exe <rom.nes> --frametest <outdir> [nframes] [interval]
```

Runs `nframes` frames from reset (default 300), saving a text snapshot every `interval` frames (default 30) to `<outdir>`. Uses per-scanline live rendering so output is pixel-accurate for VRAM-streaming games. Compare against FCEUX reference output with `test/mappy_compare.ps1`.

### Autotest — colour/emphasis sweep

```
nes1.exe <rom.nes> --autotest <outdir>
```

Runs all 1024 combinations of NES colour × PPUMASK emphasis bits through the colour_test ROM and exports text snapshots.

### Scanline trace — per-scanline CPU+PPU state dump

```
nes1.exe <rom.nes> --scanlinetrace <outpath> <frame_number>
```

Dumps a per-scanline CPU and PPU state trace for the specified frame number.

## CPU Architecture

| Component | Detail |
|-----------|--------|
| Registers | A, X, Y, SP, PC, P (status) |
| Memory    | 64 KB flat array |
| PRG ROM   | Mapped at `$8000`–`$FFFF` (16 KB mirrored, or 32 KB) |
| Reset     | Reads vector from `$FFFC` |
| Stack     | Page 1 (`$0100`–`$01FF`) |
| Timing    | 29780/29781 CPU cycles per frame (alternating), matching FCEUX |

## PPU Architecture

| Component | Detail |
|-----------|--------|
| Resolution | 256×240 pixels |
| PPU memory | 64 KB PPU address space |
| OAM | 256 bytes (64 sprites × 4 bytes) |
| CHR ROM | Mapped at PPU `$0000`–`$1FFF` |
| Nametables | 2 KB VRAM at `$2000`–`$27FF`; mirrored horizontally or vertically |
| Palette | 32 bytes at `$3F00`; 8 emphasis variants from FCEUX measurements |
| Scroll | Full Loopy V/T register model; per-scanline scroll snapshot |
| Rendering | Per-scanline live render reads live VRAM each scanline — no frozen nametable snapshot |
| Output | 32-bit BGRA DIBSection, 2× scaled via StretchBlt |
