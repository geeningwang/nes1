# Deterministic headless input

Both `nes1` and the patched FCEUX accept `--input FILE`. Apply each record before
its one-based emulated frame, including the two power-up frames. GUI keyboard input
is unchanged. Headless FCEUX disables battery loading and uses deterministic RAM
initialization, rather than inheriting desktop input configuration.

## Legacy two-controller files

One or two hexadecimal bytes per line; a missing second byte is zero:

```
# A on pad 1; B on pad 2
01 02
00 00
```

Bit order: A=01, B=02, Select=04, Start=08, Up=10, Down=20, Left=40, Right=80.
Blank lines and whole-line `#` comments do not consume frames. After EOF, legacy
input is neutral. Empty files, invalid bytes/fields, NUL bytes, oversized lines
and duplicate `--input` options are errors.

## NESINPUT 1

Versioned replay preserves attachments and four pad reports, and binds them to a
specific ROM. All seven metadata fields are required exactly once:

```
NESINPUT 1
rom_size=24592
rom_hash=0123456789ABCDEF
fourscore=0
port0=1
port1=1
port2=0
ram_init=0
data
00 01 02 00 00
00 00 00 00 00
```

The example identity is a placeholder, not a valid Mappy identity. `rom_hash` is
64-bit FNV-1a over the entire ROM file, including its header, and `rom_size` is
decimal. These detect stale/mismatched build artifacts; they are not security
authentication. The converter separately checks the original FM2 ROM MD5.

- `fourscore`: 0 or 1. Four Score requires both standard ports.
- `port0`, `port1`: 0 (disconnected) or 1 (gamepad).
- `port2`: 0 (none) or 3 (Famicom four-player adapter). Preserve this separately
  from Four Score; some movies attach both.
- `ram_init`: currently only 0.
- Records: five hex bytes, `COMMAND P1 P2 P3 P4`.
- Command bits: reset=01, power=02, applied at the start of their frame, power before
  reset (spec O-41). Any other bit, such as the FDS disk commands, is rejected when the
  file is read, never ignored. A command frame still counts as an ordinary replay frame,
  and the frame numbers of dumps and traces keep counting through a reset or power cycle.
- Versioned EOF is an error if `--frames` exceeds the record count.

The state dump is **NESDUMP 3** (work RAM, logical nametables, controller serial state and
pending-interrupt state were added; see [state_dump.md](state_dump.md)). It is versioned separately
from the NESINPUT format.

Reset and power execution is verified by `tool/check_reset_replay.py` (the `reset_state`
probe, 90 frames with four command frames) and by movies with commands, which must equal
native FCEUX playback: see `doc/parity_spec.md` O-41.

## Conversion

The `nesrecomp1` project supplies `tools/make_input.py`:

```
python tools/make_input.py movie.fm2 --rom game.nes --output replay.in --frames 3000
```

It accepts text FM2 version 3, NTSC, ordinary pads/Four Score/Famicom four-player
reports and default RAM initialization. It validates `RLDUTSBA` positions and ROM
MD5, preserves command bits and frame order, and rejects unsupported devices,
FDS commands, savestate/save-RAM starts, binary FM2 and malformed input. Explicit
`--frames` selects a prefix; it never pads a short movie.

## Verification

Use FCEUX's native FM2 path to verify the converter independently:

```
python tool/check_movie_replay.py --fceux fceux64.exe --rom game.nes \
  --movie movie.fm2 --input replay.in --frames 3000 --work native_check
```

This runs FCEUX twice, once with `--movie` and once with `--input`, checks every
frame digest and selected text traces, and writes a manifest with hashes and
comparison counts. `--movie` is FCEUX-only and cannot combine with `--input` or
`--init-pc`. It is a verification path for supported movies, not an interactive UI.

Then compare the two emulators:

```
python tool/find_divergence.py --nes1 nes1.exe --fceux fceux64.exe --rom game.nes \
  --input replay.in --frames 3000 --work input_comparison
```

Replay is forwarded through every drill-down rerun and through `compare_traces.py`.
`run_parity.ps1 -InputFile replay.in` forwards it too; use a compatible ROM
selection because normalized files are ROM-specific.

`check_input_replay.py` validates synthetic serial reports for two pads, Four
Score, the Famicom adapter, both adapters together and a disconnected second pad.
It checks known report bytes and line-by-line traces, rather than only matching
two idle games. `test/CMakeLists.txt` builds a standalone parser checker and runs
parser/comparison contract tests:

```
cmake -S test -B <test-build> -A x64 -DPython3_EXECUTABLE=<python.exe>
cmake --build <test-build> --config Release
ctest --test-dir <test-build> -C Release --output-on-failure
```

The run manifest reports supplied/applied input frame counts, EOF policy and
controller attachments. Reset/power execution and NESDUMP 3 are done; the next step is the
new runtime's replay adapter.
