# nes1

A work-in-progress NES (Nintendo Entertainment System) emulator written in C++ for Windows.

## Overview

nes1 emulates the core hardware of the NES: the MOS 6502-based CPU and the 2C02 PPU. It loads iNES ROM files, runs the CPU step-by-step, renders the PPU output to a Win32 window, and logs disassembly output to the console — making it useful as a development and debugging tool.

## Features

- **6502 CPU emulation** — full instruction set including all official opcodes and common unofficial/undocumented opcodes (`*NOP`, `*LAX`, `*SAX`, `*SBC`, `*DCP`, `*ISB`, `*SLO`, `*RLA`, `*SRE`, `*RRA`)
- **All addressing modes** — implicit, accumulator, immediate, zero page, zero page X/Y, relative, absolute, absolute X/Y, indirect X/Y, JMP absolute/indirect
- **Cycle-accurate counting** — including page-crossing and branch penalties
- **CPU disassembly log** — nestest-compatible output format for each executed instruction
- **2C02 PPU emulation** — background and sprite rendering at 256×240, using the 64-colour NES palette
- **Win32 display** — renders to a GDI DIBSection; the screen is painted 2×2 tiled with a live FPS counter
- **iNES ROM loading** — parses the 16-byte iNES header and loads PRG/CHR ROM data

## Limitations

- Only **NROM (Mapper 0)** is supported — exactly 1×16 KB PRG ROM and 1×8 KB CHR ROM
- ROM path is **hardcoded** to `../test/nestest.nes`
- NMI is a stub (`nmi_enabled()` always returns `false`)
- No scrolling, no sprite-0 hit, no proper PPU register I/O
- No audio emulation
- No controller input
- Trainers in ROM files are not supported

## Project Structure

```
src/
  nes1.cpp          — Win32 entry point, ROM loader, main emulation loop
  cpu.cpp / cpu.h   — MOS 6502 CPU emulation
  ppu.cpp / ppu.h   — Ricoh 2C02 PPU emulation
  stdafx.cpp/.h     — Precompiled header
  nes1.sln          — Visual Studio solution
  nes1.vcxproj      — Visual Studio project (x64)

test/
  nestest.nes       — Standard NES CPU test ROM
  nestest.log.txt   — Expected nestest disassembly log (for comparison)
  Mappy (Japan).nes — Real game ROM (Mapper 0)
  ppu/              — PPU colour test ROM and source

doc/
  6502_cpu.txt      — 6502 CPU reference
  nestech.txt       — NES technical reference
```

## Building

Open `src/nes1.sln` in Visual Studio and build the **x64 Debug** configuration. No external dependencies are required beyond the Windows SDK.

## Running

Run `nes1.exe` from the `src/x64/Debug/` directory (so that the relative path `../test/nestest.nes` resolves correctly). Disassembly output is printed to the console; the rendered PPU output appears in the Win32 window.

To test against the reference log:

1. Run the emulator and redirect stdout to a file.
2. Compare the output against `test/nestest.log.txt`.

## CPU Architecture

| Component | Detail |
|-----------|--------|
| Registers | A, X, Y, SP, PC, P (status) |
| Memory    | 64 KB flat array |
| PRG ROM   | Mapped at `$8000`–`$FFFF` (16 KB mirrored) |
| Reset     | Reads vector from `$FFFC`; startup state matches nestest expectations |
| Stack     | Page 1 (`$0100`–`$01FF`) |

## PPU Architecture

| Component | Detail |
|-----------|--------|
| Resolution | 256×240 pixels |
| Memory | 64 KB PPU address space |
| OAM | 256 bytes (64 sprites × 4 bytes) |
| CHR ROM | Mapped at `$0000` in PPU memory |
| Nametable | Nametable 0 at `$2000`, attribute table at `$23C0` |
| Palette | NES 64-colour palette, looked up from `$3F00` |
| Output | 32-bit BGRA bitmap |
