# Headless state dumps: nes1 vs FCEUX

Both emulators can run a ROM without a window or sound device and write machine state at chosen events.
They accept the same dump options and format; deterministic controller replay is optional.

Parity scope: mapper 0 (NROM), NTSC. With no input file, pads are neutral.
See [input_replay.md](input_replay.md) for supported replay devices and current limitations.

## Command line

```
<exe> <rom.nes> --dump <outdir> [options]
```

`<exe>` is `nes1.exe` or the patched `fceux64.exe` (see [build_environment.md](build_environment.md)). The ROM is the first argument; `--dump` selects the headless mode.

| Option | Meaning |
|--------|---------|
| `--frames N` | Emulate N frames from power-on (default 300). |
| `--dump-frames LIST` | Dump the state at the **end of each listed frame**. |
| `--dump-scanline-frames LIST` | Frames in which end-of-scanline dumps are made. |
| `--dump-scanlines LIST` | Which scanlines (0-239) to dump in those frames (default: all 240). |
| `--dump-pc ADDR[,ADDR...]` | Hex addresses. Dump the state **just before the instruction at that PC executes**. |
| `--pc-frames LIST` | Frames in which PC triggers are armed (default: all). |
| `--pc-max N` | Maximum dumps per PC address (default 16). |
| `--rom-every` | Include PRG in every dump (default: ROM written once to `rom.txt`). |
| `--digest` | Write `digest.txt` (one line of hashes per event) instead of one file per event. Needed for long runs. |
| `--trace-frames LIST` | Write `trace_fNNNNNN.txt`, one line per instruction, for these frames (columns `PC OP A X Y S P CYCLES SCANLINE DB PL`; `DB` and `PL` are the data-bus and PPU latches before the instruction). |
| `--dump-insn LIST` | Full dump just before these global instruction numbers (1-based). |
| `--init-pc ADDR` | Hex. Start at ADDR instead of the reset vector, with P=$24 and S=$FD (nestest: `C000`). |
| `--input FILE` | Deterministic controller replay, legacy two-pad bytes or NESINPUT 1. |
| `--movie FILE` | FCEUX-only native FM2 playback for converter verification; incompatible with `--input` and `--init-pc`. |

`LIST` is comma separated: `N`, `A-B`, `A-B/STEP` (every STEP-th value from A), or `all`. Frames are 1-based.

Example: frames 1-120, all 240 scanlines of frame 5, and the first 5 times PC reaches `$C06F`:

```
nes1.exe  "Mappy (Japan).nes" --dump out_nes1  --frames 120 --dump-frames 1-120 --dump-scanline-frames 5 --dump-pc C06F --pc-max 5
fceux64.exe "Mappy (Japan).nes" --dump out_fceux --frames 120 --dump-frames 1-120 --dump-scanline-frames 5 --dump-pc C06F --pc-max 5
python tool/compare_dumps.py out_nes1 out_fceux --summary
```

A bad option prints the usage text and exits with code 2. Dumps are about 43 KB each, so 240 scanline dumps are about 10 MB.

## Event definitions

These are defined identically in both emulators:

- **Frame numbering.** `frame` counts frames emulated since power-on, starting at 1. Frames 1 and 2 are the "ppudead" power-up frames (no vblank, no scanlines), exactly as in FCEUX.
- **Instruction number** (`insn_nNNNNNNNNN.txt`): global count of instructions since power-on; the dump is taken at the same boundary as a PC trigger.
- **End of frame** (`frame_fNNNNNN.txt`): the whole frame, including scanline 239, has been emulated. It is the same moment as the end of scanline 239.
- **End of scanline** (`scanline_fNNNNNN_sSSS.txt`): all CPU and PPU work for visible scanline `S` (341 PPU dots) is finished. Only visible scanlines 0-239 are supported; vblank and the pre-render line are not split into scanlines in either emulator.
- **PC trigger** (`pc_AAAA_nNNNN.txt`): the CPU is at an instruction boundary, about to fetch the opcode at `AAAA`. If an interrupt is pending it has already been taken, so a trigger on an NMI handler address fires at its first instruction. `nNNNN` is the 1-based hit number for that address.
- **`scanline` field.** In every file it is the visible scanline (0-239) being emulated when the dump was taken, or `-1` outside the visible area (vblank, pre-render, power-up frames, end-of-frame).

## Output folder

```
rom.txt          PRG ($8000-$FFFF as the CPU sees it) and CHR (8 KB), written once
events.txt       one line per dump, in the order they happened (digest.txt instead, with --digest)
trace_f000010.txt  with --trace-frames: every instruction of that frame
run.txt          format (NESDUMP 3 / NESDIGEST 3), emulator name, full command line, frames run, event counts, PC hit counts
frame_f000010.txt
scanline_f000005_s100.txt
pc_C06F_n0001.txt
```

`run.txt` is the only file that names the emulator; the dump files contain no emulator-specific text, so equal state gives byte-identical files.

## Dump file format (`NESDUMP 3`)

Plain text, `\n` line endings. Memory is printed 16 bytes per row as `ADDR: xx xx ...`, so a `diff` line identifies the address.

```
NESDUMP 3
event: scanline            frame | scanline | pc | insn
frame: 5
scanline: 100
pc_trigger: C06F hit=1     only in pc events
[CPU]
PC=C06F A=00 X=5D Y=00 S=5F P=26 CYCLES=133103
[PPU]
CTRL=88 MASK=1E STATUS=40 OAMADDR=00
V=2073 T=2000 FINEX=0 W=0 READBUF=00 MIRROR=V
[BUS]
DB=02 PPULATCH=00    CPU data-bus latch and PPU I/O latch (open-bus values)
[APU] 4000-4017      last value written to each register
[APUX] 0000-001F     CPU-visible APU internals (length counters, frame counter, IRQ flags, DMC); layout in parity_spec.md 1.9
[RAM] 0000-07FF      CPU RAM
[WRAM] 6000-7FFF     cartridge work RAM as the CPU sees it
[NT] 2000-27FF       physical nametable RAM: page A (first 1 KB), page B (second 1 KB)
[NTV] 2000-2FFF      logical nametables as the PPU addresses them, after mirroring
[IO] 0000-000F       controller serial state; layout below
[IRQ] 0000-0007      pending interrupt state; layout below
[PAL] 3F00-3F1F
[OAM] 0000-00FF
[CHR] 0000-1FFF
[FB]  0000-EFFF      256x240 palette-index pixels, 16 per row; unfinished rows are 00
PRG_HASH=XXXXXXXXXXXXXXXX
CHR_HASH=XXXXXXXXXXXXXXXX
INSN_HASH=XXXXXXXXXXXXXXXX   chained hash of every instruction executed so far this frame
INSN_COUNT=N
END
```

Notes on what is and is not compared:

- `CYCLES` is the total CPU cycle count since power-on, before the instruction at `PC` executes.
- `V` and `T` are the 15-bit Loopy registers.
- `[APU]` is a register shadow; `[APUX]` holds the CPU-visible internals (see `parity_spec.md` 1.9). The audio-only channel state (timers, envelopes, sweeps) is not dumped. `$4014` and `$4016` are not shadowed and always read 00.
- `[PAL]` is canonical: `$3F10/14/18/1C` show the values of `$3F00/04/08/0C` and entries are masked to 6 bits.
- `[NT]` is physical RAM, so it does not depend on how each emulator addresses the four logical nametables; `MIRROR` records which mirroring is active.
- PRG and CHR are checked through the hash lines in every dump, and in full in `rom.txt` (or in every dump with `--rom-every`).
- `[NTV]` shows what the PPU reads at `$2000-$2FFF`, read straight through each emulator's mirroring tables without any access side effects. Together with `MIRROR` it catches a wrong mirroring that `[NT]` alone cannot.
- `[WRAM]` is the whole `$6000-$7FFF` range (see parity_spec.md O-32).
- `[BUS]` holds the two latches that decide what reads of unmapped addresses and of write-only PPU registers return.
- `P` is architectural (bit 5 = 1, bit 4 = 0). `[FB]` and the instruction hash are defined in [parity_spec.md](parity_spec.md).

### `[IO]` layout (bytes, bit-for-bit between the emulators)

| Offset | Meaning |
|-------:|---------|
| 0, 1 | read counter of the pad on `$4016` / `$4017` (standard pad and Four Score; wraps like an 8-bit counter, O-39) |
| 2, 3 | read counter of the Famicom four-player adapter on `$4016` / `$4017` |
| 4 | bit 0 of the last `$4016` write (the strobe) |
| 5-15 | 0 |

### `[IRQ]` layout

| Offset | Meaning |
|-------:|---------|
| 0 | NMI request: 0 none, 1 armed by a `$2000` write (becomes due at the next instruction boundary, O-38), 2 due |
| 1 | 1 while the frame-counter or DMC IRQ line is asserted (not masked by the I flag; P is in `[CPU]`) |
| 2 | 1 if the CPU is jammed by a KIL opcode |
| 3-7 | 0 |

A reset request (FCEUX `IQRESET`) is serviced before the next instruction in both emulators and is never visible in a dump.

## Comparing

```
python tool/compare_dumps.py <dirA> <dirB> [--max N] [--all] [--summary] [--sections CPU,PPU,RAM,...]
```

Events are compared in the order of `events.txt`. The report names the first diverging event, then for each differing event lists the CPU/PPU fields (`A=... B=...`) and the memory addresses that differ (`ADDR: A/B`). `--summary` adds one line per field with the number of differing events and the first one. Exit status is 0 if identical, 1 if they differ, 2 on usage errors. Plain `diff -r dirA dirB` also works.

The tool accepts only version 3. It exits with status 2 (never "identical") for a dump or digest of another version, a missing or short section, a missing `END` line, a digest line with a missing field, a folder pair that mixes digests and full dumps, a missing `events.txt`, and a `run.txt` manifest (`format:` and `events:` counts) that disagrees with the files on disk. `tool/test_dump_format.py` checks each of these, and that changing one byte of every section (including the new ones) is reported as a difference in full and digest mode.

## Digest mode and finding the first divergence

Full dumps are about 230 KB each, so long runs use `--digest`: one text line per event with a 64-bit hash for
each section (see [parity_spec.md](parity_spec.md), section 1.7). The file starts with `NESDIGEST 3`. `compare_dumps.py` compares two digest
folders line by line without holding them in memory (movies run for hundreds of thousands of frames) and names the first differing event and fields. `tool/find_divergence.py` automates the whole drill-down
(first differing frame, then scanline, then instruction trace, then field-level diff), and
`tool/run_parity.ps1` runs it for all repo ROMs and probe ROMs. See [parity_plan.md](parity_plan.md).

### Digest line

```
000123 scanline_f000005_s100.txt f=5 sl=100 pc=C06F cyc=133103 v=2073 st=40 ov=4,10,1,6 ALL=... CPU=... PPU=... BUS=... APU=... RAM=... WRAM=... NT=... NTV=... PAL=... OAM=... CHR=... PRG=... FB=... IO=... IRQ=... INSN=hash/count
```

`v` and `st` (V register and PPU status) and `ov` (the CPU's overshoot in dots at the end of each of the line's four time slices, 256 + 6 + 63 + 16) are human-readable debugging aids; they are not part of any hash. The hashes cover the same bytes the full dump prints.

### Debugging aid: `NESDBG`

Both emulators print the catch-up positions of the line renderer to stderr when the environment variable `NESDBG=<frame>:<scanline>` is set during a dump run (`NES1 sync ...` / `FCEUX sync ...` lines with the position, the first unrendered tile, PPUMASK, PPUCTRL and fine X). Identical sequences mean the timing agrees and any difference is in what is drawn. This is how the mid-line rendering rules in [parity_spec.md](parity_spec.md) (O-19 to O-23) were found. `NESDBGHIT=1` additionally prints every sprite-0 hit (`NES1 hit ...` / `FCEUX hit ...` with frame, line, position and sprite data).

## Where the code is

| Piece | Location |
|-------|----------|
| Format, command line, triggers (shared by both emulators) | `common/nesdump.h` |
| nes1 side: state capture and hooks | `src/statedump.cpp`, `src/statedump.h`, hooks in `src/nes1.cpp` (`run_one_frame`, `run_dump_mode`), APU state in `src/apu.cpp` (`apu_2a03::advance`) |
| FCEUX side: state capture | `tool/fceux-2.6.6/src/fceux_dump.cpp` |
| FCEUX hooks | `x6502.cpp` (per instruction), `ppu.cpp` (`DoLine`, per scanline), `fceu.cpp` (`FCEUI_Emulate`, per frame), `sound.cpp` (APU shadow, `NesDump_ApuState`), `drivers/win/main.cpp` (`RunHeadlessDump`) |
| Comparison tools | `tool/compare_dumps.py`, `tool/find_divergence.py`, `tool/check_nestest.py`, `tool/run_parity.ps1` |
| Probe ROMs (tiny test programs, generated) | `tool/make_probe_rom.py` -> `test/probe/*.nes` |

FCEUX's old hard-coded frame export (270 frames to `C:\Work\...`) is skipped while a `--dump` run is active.

To change what is dumped, edit `nesdump::State` and `Recorder::write_event` in `common/nesdump.h` and fill the new field in both `statedump.cpp` and `fceux_dump.cpp`.

## Current status

The first comparison (Mappy, 120 frames) and the baseline of the parity suite are recorded in
[parity_baseline.md](parity_baseline.md); progress against the plan is tracked in [parity_plan.md](parity_plan.md).

## Instruction-trace comparison

`tool/compare_traces.py` runs both emulators with `--trace-frames` for chosen frames and compares **every line** of every trace
(`PC OP A X Y S P CYCLES SCANLINE DB PL`), reporting the first differing instruction with its neighbours and the differing columns.
`run_parity.ps1 -TraceFrames 1-8,100,3000` does this for the whole suite (default list: `1-8,60,120,300,600,1000,1500,2000,2500,3000`).
