# Parity specification: observation contract and observed behaviour

This is the behaviour reference for making nes1 identical to FCEUX (see [parity_plan.md](parity_plan.md)).
It has two parts:

1. **Observation contract** (section 1): exactly what each emulator reports, and when. Both emulators
   must implement it identically; it is not about emulation accuracy.
2. **Observed behaviour** (section 2): behaviour statements about FCEUX, each with the evidence that
   supports it. nes1 changes cite an entry here (clean-room protocol, plan section 4).

## 1. Observation contract

### 1.1 Pinned configuration

| Item | Value |
|------|-------|
| Region | NTSC |
| Cartridge | mapper 0 (NROM), 16 or 32 KB PRG, 8 KB CHR-ROM |
| Input | none (controllers never pressed) |
| FCEUX | the patched 2.6.6 in `tool/fceux-2.6.6`, run through its headless `--dump` mode before any config file is read, so all settings are the built-in defaults: old PPU (not "new PPU"), sprite limit 8 per scanline, default power-on RAM pattern (`00 00 00 00 FF FF FF FF` repeated), no cheats, no game genie |
| Power-on | frames 1 and 2 are "power-up" frames (see 1.3) |

### 1.2 Counting

- **Frame number** counts frames emulated since power-on, starting at 1.
- **Instruction number** counts instructions executed since power-on, starting at 1 (`--dump-insn`).
- **Cycles** is the total CPU cycle count since power-on, read **before** the cost of the instruction at
  PC is added. An interrupt entry (NMI) counts its 7 cycles before the first handler instruction is reported.

### 1.3 Events

| Event | Moment |
|-------|--------|
| `pc` / `insn` | At an instruction boundary: the CPU is about to fetch the opcode at PC. A pending interrupt has already been entered, so a trigger on a handler address fires at the handler's first instruction. |
| `scanline` (0-239) | All CPU and PPU work for that visible scanline (341 PPU dots) is finished and its pixels are final. |
| `frame` | The whole frame, including scanline 239, has been emulated. Same moment as the end of scanline 239. |

The `scanline` field in every dump is the visible scanline being emulated at that moment, or `-1`
outside the visible area (vblank, pre-render, power-up frames, end of frame).

Power-up frames 1 and 2 have no vblank and no visible scanlines: the CPU just runs 262 x 341 PPU dots.
They are still counted as frames 1 and 2.

### 1.4 What is reported

| Section | Content |
|---------|---------|
| `[CPU]` | PC, A, X, Y, S, P, CYCLES. **P is architectural**: bit 5 reads 1 and bit 4 (B) reads 0, because B is not a real flag and FCEUX also stores stray values in those bits (observation O-2). |
| `[PPU]` | CTRL, MASK, STATUS, OAMADDR, V, T (15-bit), fine-X, W, read buffer, mirroring |
| `[APU]` | last value written to each of $4000-$4017 (until the APU schema of plan phase 4 replaces it) |
| `[RAM]` | CPU RAM $0000-$07FF |
| `[NT]` | physical nametable RAM, 2 KB |
| `[PAL]` | palette RAM, canonical: $3F10/14/18/1C show $3F00/04/08/0C, entries masked to 6 bits |
| `[OAM]` | sprite RAM, 256 bytes |
| `[CHR]` | PPU $0000-$1FFF as the PPU reads it |
| `[FB]` | 256 x 240 palette-index pixels, see 1.5 |
| `PRG_HASH`, `CHR_HASH` | hash of the PRG ($8000-$FFFF as the CPU sees it) and of CHR; the ROM itself is in `rom.txt` |
| `INSN_HASH`, `INSN_COUNT` | chained hash and count of every instruction executed so far in this frame (1.6) |

### 1.5 Frame buffer (`[FB]`)

- One byte per pixel, in the representation FCEUX keeps: bits 0-5 are the NES colour index, bits 6-7
  record the PPUMASK emphasis state (exact encoding is defined by observation, O-4 onwards).
- Only **finished rows** are part of an event: all 240 rows for `frame`, rows 0..S for `scanline S`,
  and rows before the current scanline for `pc` / `insn` events (none when the scanline is -1).
  Rows that are not finished are written and hashed as `00`.

### 1.6 Instruction trace and chained hash

Before every instruction both emulators report PC, the opcode byte at PC, A, X, Y, S, P (architectural),
CYCLES, frame and scanline.

- **Opcode byte:** PC below $2000 reads RAM through its 2 KB mirror, PC at $8000 or above reads PRG,
  anything else reads `FF`. (This is the contract's view, not what the CPU would do; a CPU that really
  fetches from elsewhere is an observation for the opcode work, plan phase 1.)
- **Chained hash:** at the start of each frame the hash and count are zero. Each instruction folds
  `(PC, opcode, A, X, Y, S, P)` and then CYCLES into the hash (`mix64`, see `common/nesdump.h`).
  Every `frame`, `scanline`, `pc` and `insn` event carries the running hash and count, so two runs have
  identical instruction streams if and only if these match at every event.
- `--trace-frames LIST` additionally writes every instruction of those frames as text
  (`trace_fNNNNNN.txt`, columns `PC OP A X Y S P CYCLES SCANLINE`).

### 1.7 Digest mode

`--digest` writes `digest.txt` instead of one file per event: one line per event with the event name,
frame, scanline, PC, cycles, and a 64-bit hash for each section (`CPU PPU APU RAM NT PAL OAM CHR PRG FB`),
for the instruction chain (`INSN=hash/count`) and for all of them (`ALL`). Each section hash covers the
same bytes the full dump prints. Two runs are identical at an event if and only if `ALL` is equal.

### 1.8 Independent reference

`test/nestest.log.txt` (the public nestest log) is an independent check of the CPU: starting nestest at
`C000` (`--init-pc C000`: PC given, P=$24, S=$FD) the 8,991 logged instructions must match in PC, A, X, Y,
P, S and cycles (the log counts from cycle 7, the emulators from 0). `tool/check_nestest.py` does this.

## 2. Observed behaviour

Each entry: statement, evidence (how to reproduce), status. IDs are stable and cited by code comments.

| ID | Statement | Evidence | Status |
|----|-----------|----------|--------|
| O-1 | FCEUX with `--init-pc C000` runs nestest exactly as `nestest.log.txt`: all 8,991 instructions match in PC, A, X, Y, P (architectural), S and cycles. | `check_nestest.py` on the FCEUX trace: `OK: 8991 of 8991` | confirmed |
| O-2 | FCEUX stores bit 4 (B) of P after PLP/PHP-style operations and has bit 5 clear at power-on (`P=04`), so its raw P differs from the architectural register in those bits only. | First check of the raw FCEUX trace against the log: `P=FF` vs log `EF`, and `P=04` vs `24` at start | confirmed |
| O-3 | At the first instruction after power-on (frame 1) FCEUX has `S=FD`; nes1 has `S=00`. | `find_divergence.py` stage 3 on Mappy, frame 1, instruction 1 | confirmed; nes1 to fix in plan phase 1 |
| O-4 | During power-up frames 1-2 FCEUX frame buffers differ from nes1's (nes1 never draws them). | Frame digest #1 `FB` differs; content to be inspected | open |
| O-5 | After nestest's log ends (instruction 8,992 onward) FCEUX keeps executing: `RTS` to a stack-garbage address, then `BRK`-like and unofficial opcodes at addresses $0000-$0007 etc., settling at 116 instructions per frame in frames 2-3. nes1 stops advancing at the first undefined opcode or BRK (`step()` returns false without consuming cycles), so it hangs. | FCEUX trace of frames 1-3 of nestest; nes1 run at 100% CPU for 6 minutes | confirmed; nes1 to fix in plan phase 1 |
