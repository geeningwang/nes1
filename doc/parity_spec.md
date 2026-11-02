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
| `[APU]` | last value written to each of $4000-$4017 |
| `[APUX]` | CPU-visible APU internals, 32 bytes, layout in 1.9 |
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

### 1.9 APUX layout

32 bytes, little endian: `0-3` length counters of pulse 1, pulse 2, triangle, noise; `4` next frame-counter step; `5` frame mode (`$4017 >> 6`);
`6` IRQ flags (bit 6 frame, bit 7 DMC); `7` `$4015` enable bits (5 bits); `8-11` frame step timer (1/48 cycle units);
`12-13` DMC address offset; `14-15` DMC bytes remaining; `16` DMC shift register; `17` DMC bit counter; `18-19` DMC timer;
`20-21` DMC period; `22` DMC sample-valid flag; `23` DMC buffer-full flag; `24` DMC buffer; `25` DMC output level; `26` `$4010`; `27-31` zero.

Not compared: the audio-only state of the channels (pulse/noise/triangle timers and sequencers, envelopes, sweeps, the
triangle linear counter, noise shift register, mixer). It cannot be observed by the CPU, and FCEUX computes it lazily at
sound-flush time in a form that depends on its sample-rate settings, so it has no hardware-meaningful counterpart to compare.

## 2. Observed behaviour

Each entry: statement, evidence (how to reproduce), status. IDs are stable and cited by code comments.

| ID | Statement | Evidence | Status |
|----|-----------|----------|--------|
| O-1 | FCEUX with `--init-pc C000` runs nestest exactly as `nestest.log.txt`: all 8,991 instructions match in PC, A, X, Y, P (architectural), S and cycles. | `check_nestest.py` on the FCEUX trace: `OK: 8991 of 8991` | confirmed |
| O-2 | FCEUX stores bit 4 (B) of P after PLP/PHP-style operations and has bit 5 clear at power-on (`P=04`), so its raw P differs from the architectural register in those bits only. | First check of the raw FCEUX trace against the log: `P=FF` vs log `EF`, and `P=04` vs `24` at start | confirmed |
| O-3 | At the first instruction after power-on (frame 1) FCEUX has `S=FD`; nes1 has `S=00`. | `find_divergence.py` stage 3 on Mappy, frame 1, instruction 1 | confirmed; nes1 to fix in plan phase 1 |
| O-4 | During power-up frames 1-2 FCEUX frame buffers differ from nes1's (nes1 never draws them). | Frame digest #1 `FB` differs; content to be inspected | open |
| O-6 | BRK is the standard 6502 BRK: pushes PC+2 (high, then low), then P with bits 4 and 5 set, sets I, jumps through the vector at $FFFE, 7 cycles; RTI takes 6 cycles and restores P. PHP pushes P with bits 4 and 5 set. | FCEUX trace and RAM of `test/probe/cpu_flags_brk.nes` (e.g. `$0300=FF $0301=30 $0303=34`; BRK at 801F, handler at 802A cycle +7, RTI back at 8021) | confirmed; implemented in nes1 |
| O-7 | A KIL opcode ($02 and the other 11) leaves PC on the opcode, changes no registers, and every repeat of it costs 257 cycles (first report at the normal cycle, then +257 each time). | FCEUX trace of `test/probe/cpu_kil.nes`, frames 1-3 | confirmed for $02; other KIL opcodes assumed alike (unverified) |
| O-8 | Reads of unmapped addresses ($4000-$4014, $4018-$5FFF) return the CPU data-bus latch, which after `LDA abs` is the high byte of the address (`50` for $5000, `40` for $4018). Reads of $4016/$4017 with no button pressed return `40`: bits 5-7 are open bus. Writes to $8000-$FFFF do not change the ROM. $6000 behaves as writable RAM (reads back what was written). | FCEUX RAM results of `test/probe/mem_map.nes`: `$0300..$0306 = 78 FF 50 A5 40 40 40` | confirmed for these addresses; extent of the work RAM and bits 5-7 mask to be probed |
| O-9 | FCEUX starts an instruction whenever any CPU budget is left, including a fraction of a cycle: a frame of 89342 PPU dots is 29780.67 cycles, so at cycle 29780 it still starts one more instruction. | color_test frame 1: FCEUX reports an instruction at cycle 29780 that nes1 (whole-cycle budget) did not | confirmed; implemented in nes1 (budget kept in dots) |
| O-5 | After nestest's log ends (instruction 8,992 onward) FCEUX keeps executing: `RTS` to a stack-garbage address, then `BRK`-like and unofficial opcodes at addresses $0000-$0007 etc., settling at 116 instructions per frame in frames 2-3. nes1 stops advancing at the first undefined opcode or BRK (`step()` returns false without consuming cycles), so it hangs. | FCEUX trace of frames 1-3 of nestest; nes1 run at 100% CPU for 6 minutes | confirmed; nes1 to fix in plan phase 1 |
| O-10 | A `$2004` write goes to OAM[counter] regardless of OAMADDR while the write counter is below 8, and to OAM[OAMADDR] only if OAMADDR >= 8 afterwards; OAMADDR and the counter both increment on every write (the counter is reloaded with `OAMADDR & 7` by a `$2003` write). OAM DMA is 256 such writes. | `test/probe/oam_dma.nes` (OAMADDR=$10 then DMA; later a second DMA and writes from OAMADDR=$FD): OAM digests equal for 300 frames | confirmed by probe; counter width (wrap at 256) agrees with the second-DMA case |
| O-11 | Reads of `$2004` do not return OAM: they return the PPU I/O latch, like the other write-only registers. | `test/probe/ppu_open_bus.nes`: `$2004` reads back `1A`, the latch after the `$2002` read | confirmed |
| O-12 | While rendering is enabled (PPUMASK bits 3 or 4) V evolves like this: at dot 256 of each visible line the Y part is incremented (fine Y, then coarse Y with the 29/31 wrap and nametable toggle); at dot 262 V's horizontal bits (coarse X, nametable X) are copied from T; at dot 325 of the pre-render line V is loaded with all of T. V's coarse X does not advance tile by tile during the line. Nothing changes while rendering is off. | `ppu_scroll_idle` probe and color_test: V sequences equal on all lines except the quirk O-20 | confirmed apart from O-20 |
| O-13 | Sprite overflow (status bit 5) is set at dot 256 of a visible line when rendering is on and more than 8 sprites (8x8) have `0 <= line - Y < 8`; the VBL, sprite-0 and overflow flags are all cleared together at the end of vblank. | `ppu_scroll_idle` (OAM all zero: 64 sprites at Y=0): FCEUX status `20` from line 0 on; nes1 matches the `20` part | confirmed for 8x8; 8x16 not implemented |
| O-14 | The CPU data bus after an instruction is: the value read or written (also for stores and read-modify-write); the opcode for implied and accumulator instructions; the high byte of the new PC for JSR, BRK and indirect JMP; the high byte of the reset/NMI vector after reset/NMI entry; the opcode (not the offset) for a branch that is not taken, the offset for a taken one; the opcode for zero-page and immediate NOPs (they do not read their operand), but the value read for absolute NOPs. After reset it is the reset vector's high byte (`C0`, `82`, `80` in the repo ROMs); with `--init-pc` it is 0. OAM DMA does not change it. | traces of nestest, Mappy, color_test and `cpu_nop_modes`; `find_divergence.py` stage 3 on each | confirmed |
| O-15 | OAMADDR and the OAM write counter are reset to 0 when vblank starts. | `oam_dma` probe: OAMADDR is `00` at frame 4 although the program ended with OAMADDR=$01 | confirmed |
| O-16 | OAM DMA writes go through `$2004`, so the PPU I/O latch ends up holding the last DMA byte. | color_test frame 6: latch `FF` after `STA $4014` | confirmed |
| O-17 | The unofficial `NOP #imm` opcode `$E2` costs 3 cycles in FCEUX (`$80`, `$82`, `$89`, `$C2` cost 2). | `cpu_nop_modes` trace | confirmed |
| O-18 | PPU I/O latch: every write to `$2000-$2007` sets it to the written value; reads of `$2000/1/3/5/6` (and `$2004`) return it; a `$2002` read returns the status bits 7-5 plus the latch's low 5 bits and sets the latch to that result; a `$2007` read sets it to the returned value. | `ppu_open_bus` probe, color_test frame 6 | confirmed for these; palette reads (upper bits) not yet probed |
| O-19 | PPU register accesses change the picture inside the current line. FCEUX draws each visible line lazily: the CPU time of every access (end of the instruction, in dots counted from the line origin, which is the start of the 16-dot tail of the previous line) is converted to a position `p`, and the line's background tiles are brought up to date to `p >> 3` tiles, with the register state from before the access. This catch-up happens at line start (with the CPU overshoot as `p`), before every read of a PPU register, before writes to `$2000/$2001/$2005/$2006` (not for `$2003/$2004/$2007` writes), and at dot 256 (`p = 272`). | nestest frame 5 line 22 (palette write after a sync at p=56: pixels from x=56 change), Mappy frame 4 lines 87 and 135, frame 1749 line 95; sync positions printed by both emulators are identical (`NESDBG`, see state_dump.md) | confirmed |
| O-20 | Each background tile that a catch-up renders advances V's coarse X (with the nametable-X toggle at 31). This is why V's coarse X is ahead by one at the end of some lines: when the CPU overshoot at the start of a line is 8-15 dots, the catch-up at line start already renders one tile. (Earlier hypothesis that it depended on the overshoot at the end of the line was only a correlate.) | color_test frames 6-9: 107 differing lines explained; `ppu_scroll_idle`; both emulators' per-slice overshoot is identical on all 1680 lines | confirmed |
| O-21 | The pending Y increment of V (normally at dot 256) is performed by the first catch-up whose position is >= 268, with the register state of that moment, and then no longer happens at dot 256. A `$2001` write that turns rendering on late in the line therefore gets no Y increment for that line. | nestest frame 5 line 234 (`STX $2001` at position 283) | confirmed; the threshold is exactly 268: the `ppu_fixit_threshold` probe (rendering switched off at pseudo-random positions) is identical for 3000 frames with 268, and nes1 diverges within 3 frames with 267 and with 269 |
| O-22 | Background pipeline (refined by gameplay replay, O-40): rendering fetches nametable/attribute/pattern data into a two-tile pipeline and advances V. Output uses the two previously fetched tiles with the live fine X and palette. Fully disabled rendering writes backdrop chunks without shifting that pipeline. Sprites-only rendering and clipping follow O-35. | Original Mappy idle cases plus TAS frames 1502/1505 and `ppu_bg_toggle_fine_x` | refined; see O-40 |
| O-23 | Sprites for line n+1 are evaluated at dot 256 of line n from OAM as it is then: the first 8 sprites with `0 <= n - Y < 8` are kept (a 9th sets the overflow flag), their pattern rows are read with PPUCTRL as it is then, and their colours are resolved at the end of line n. They are drawn on line n+1 over the background (a sprite behind the background shows only where the background is transparent), the lowest sprite number winning. Sprite-0 hit is set at the first position where sprite 0's opaque pixel meets an opaque background pixel at x < 255, and becomes visible when a catch-up position exceeds that pixel by 16 dots (pixel x is "output" at dot x + 16). Line 0 has no sprites. 8x16 sprites and left-column clipping are not implemented. | `ppu_scroll_idle` (status 20/60), color_test, Mappy | confirmed for 8x8 sprites |
| O-24 | Registers at reset/power-on that the dumps compare: V and T are 0, status 0, palette RAM 0, OAM 0, RAM `00 00 00 00 FF FF FF FF` pattern; APU registers are not written. | first frames of every ROM | confirmed |
| O-25 | APU timing model. The APU is advanced once per instruction, just before the instruction executes, by the CPU cycles accumulated since the previous call plus **the instruction's base cost only** (page-cross and branch extras, interrupt entry, OAM DMA and DMC stalls are counted by the next call; a KIL counts 2, not 257). A `$4015`/`$4017` access therefore sees the APU as of the end of the instruction's base cycles. | `apu_len_status`, `apu_frame_irq`, `apu_fuzz` | confirmed |
| O-26 | Frame counter: one step every 7457.5 CPU cycles (kept in 1/48 cycle units, 14915*24 per step, decremented by 48 per cycle), 4 steps per round, starting 7457.5 cycles after power-on with step 0. Step 0 sets the frame IRQ flag (bit 6 of `$4015`) if `$4017` bit 6 (inhibit) and bit 7 (5-step mode) are both clear, so the IRQ comes once every 29830 cycles (first handler entry at cycle ~7467). Even steps (0 and 2) clock length counters and sweeps; every step clocks envelopes/linear counter. Step 3 in 5-step mode lasts twice as long. A `$4017` write sets step := 1 and restarts the step timer; with bit 7 set it first runs step 0 at once (using the previous mode for the IRQ rule); it clears the frame IRQ flag; the new mode takes effect after that. | traces of `apu_frame_irq` (IRQ entries 7467, 37296, 67125, 96956), `apu_len_status` (length 2 reaches 0 after the 2nd even step), `apu_frame_mode` (no IRQ while inhibited) | confirmed |
| O-27 | Length counters: loaded from the table on `$4003/$4007/$400B/$400F` only if the channel's `$4015` enable bit is set; a `$4015` write forces disabled channels to 0; decremented on even frame steps unless halted (`$4000/$4004/$400C` bit 5, `$4008` bit 7). `$4015` read: bits 0-3 = counter non-zero, bit 4 = DMC bytes remaining, bit 6 = frame IRQ, bit 7 = DMC IRQ; the read clears bit 6 only. | `apu_len_status`, `apu_fuzz` | confirmed |
| O-28 | IRQ line = frame IRQ flag OR DMC IRQ flag. It is checked before every instruction (while the budget lasts) against the I flag **as it was when the previous instruction started** (CLI/SEI/PLP take effect one instruction late); entry costs 7 cycles, which are counted by the next APU call, pushes P with B clear, sets I, leaves the vector's high byte on the bus. It is not serviced while the CPU is jammed. `$4015` read acknowledges the frame flag; `$4017` write clears it; `$4015` write and `$4010` write with bit 7 clear clear the DMC flag. | `apu_frame_irq`, `apu_dmc_irq`, `apu_fuzz` (random CLI/SEI/PLP) | confirmed |
| O-29 | DMC: output timer period from the `$4010` rate table (NTSC, 428 at power-on), counted in CPU cycles with the same per-instruction granularity as O-25; a byte is fetched (`$8000 + address`) whenever bytes remain and the buffer is empty, **at most once per APU call**, and stalls the CPU for 4 cycles. The fetched byte is left on the data bus before the instruction's own bus accesses (a NOP in that instruction leaves it there). After the last byte: loop flag (`$4010` bit 6) reloads address/length, else IRQ flag if `$4010` bit 7. `$4015` bit 4 set restarts the sample only when no bytes remain; clear stops it. Address starts at `$C000 + $4012*64`, length `$4013*16+1`. The 7-bit output level is changed by +-2 per bit and not allowed to leave 0..127. | `apu_dmc_irq` (4-cycle stalls every 3424 cycles, status `$50`, 6 IRQs), `apu_fuzz` | confirmed |
| O-30 | I-flag latency after `RTI` and `BRK`: `RTI` makes the restored I flag count at once (the next IRQ check already sees it), while `BRK` keeps IRQs blocked right after it even if I was clear before. (`CLI`, `SEI`, `PLP` still take effect one instruction late, O-28.) | opcode sweep (handler = bare `RTI` with the frame IRQ left pending) | confirmed |
| O-31 | Unstable opcodes, as FCEUX executes them: `$8B` XAA: `A = (A \| $EE) & X & imm`; `$AB` LXA: `A = X = imm`; `$BB` LAS is a read-modify-write (`S &= m; A = X = S`, the operand is written back, no page-cross cycle, cost 4); `$93`/`$9F` AHX store `A & X & (H+1)` and `$9B` TAS sets `S = A & X` and stores `S & (H+1)`, with H the high byte of the **base** address and the store going to the plain effective address; `$9C` SHY / `$9E` SHX store `Y` / `X` `& (H'+1)` with H' the high byte of the **indexed** address and **always** write to `(value << 8) \| low byte of the indexed address`. All 12 KIL opcodes (`$02 $12 ... $F2`) behave like `$02` (spec O-7). | opcode sweep, 120 variants per opcode, RAM and I/O targets | confirmed |
| O-32 | CHR is ROM: PPU writes to `$0000-$1FFF` (`$2007`) are ignored (all repo ROMs and probes have one 8 KB CHR bank). Work RAM is the whole `$6000-$7FFF` (8 KB, no mirror); `$4018-$5FFF` is open bus (reads return the high byte of the address last fetched, i.e. the data bus). | `mem_extent` probe, opcode sweep with `--io` | confirmed |
| O-33 | Memory accesses inside an instruction. (a) Every bus access of an instruction happens at the instruction's end time as far as the PPU is concerned (the base cost is charged first; the page-cross cycle is charged before the real read). (b) Indexed accesses (`abs,X`, `abs,Y`, `(zp),Y`) first read the address with the **uncorrected** high byte: always for stores and read-modify-write, only on a page cross for loads. (c) Read-modify-write on `abs`, `abs,X/Y`, `(zp,X)`, `(zp),Y` reads the operand, writes the **unmodified value back**, then writes the result (so `INC $2007` or `ASL $4014` act twice); zero-page RMW writes once. (d) `$4014`: the 256 bytes are read through the CPU bus from any page (`$2xxx` and `$4xxx` pages have side effects) and written one by one to `$2004`, one cycle per access; the data bus still holds its previous value while the handler runs, and the written value afterwards. | opcode sweep with `--io`, `op_8D`, `op_CE` | confirmed |
| O-34 | While rendering is on (PPUMASK bit 3 or 4) and the frame loop is inside the visible lines (FCEUX `scanline < 240`; the 16-dot tail of line 239 already counts as line 240), a `$2007` **read** does not add 1 or 32 to V but performs a Y increment (fine Y, then coarse Y with the 29/31 wrap). Writes always add 1/32. | `op_9D`, `op_CE` (dummy reads of `$2007`) | confirmed |
| O-35 | Background pipeline with sprites only: when PPUMASK bit 4 is set but bit 3 is clear, tiles are still fetched (V's coarse X advances) and the pixels written two tiles behind, then the range `[first-2, last-2)` of tiles of that catch-up is overwritten with the backdrop. Left-column masking of the background (PPUMASK bit 1 clear): the catch-up that first reaches tile 2 paints pixels 0-7 with the backdrop and makes them transparent for priority and sprite-0 hit. | `op_3B` (V off by one), `ppu_sprite_stress` | confirmed |
| O-36 | Sprites: with PPUCTRL bit 5 set they are 8x16: a sprite covers rows `0 <= line - Y < 16`, the pattern table comes from bit 0 of the tile number, the tile pair is `tile & $FE` / `+1` (rows 8-15 use the second tile), vertical flip mirrors all 16 rows; the sprite-overflow check uses the same height. PPUCTRL bit 3 selects the pattern table only for 8x8 sprites. With PPUMASK bit 2 clear sprite pixels at x < 8 are not drawn. | `ppu_sprite_stress` (size toggles every 64 frames, mask bits 1-2 cycle), `op_2F` | confirmed |
| O-37 | Sprite 0 state across lines: the hit data for a line is armed when the sprites of that line are fetched, only while PPUMASK bit 4 is set and only while the hit flag is still clear (sprites are also dropped for the next line when bit 4 is clear at that moment). The arming of line 239 survives the frame boundary: sprite 0 at Y = 239 can hit on line 0 of the next frame. A catch-up fetches one extra tile when an armed hit pixel lies in the 16-dot window being output (`x < position-16` and `x >= (lasttile-2)*8`). | `ppu_sprite_stress` (sprite 0 at Y 239), `ppu_scroll_idle` | confirmed |
| O-38 | Writing `$2000` with NMI enable going 0 to 1 while the vblank flag is set raises an NMI: it is converted at the next instruction boundary and taken at the one after (one instruction runs in between). The vblank-start NMI is taken at the first boundary. An NMI is not taken while the CPU is jammed. | opcode sweep (`$2000` as RMW target) | confirmed |
| O-39 | Controller reports use open-bus bits 6-7 (refining O-8), restart at the falling strobe edge, and advance even while strobe remains high. Ordinary pads return ones after eight reads; the read counter wraps at 256. Four Score adds pads 3/4 at bits 8-15 and signatures at positions 19/18 for ports 1/2. Famicom four-player serial data is on bit 1, with ones after eight reads, and can coexist with Four Score. Disconnected standard ports return no serial bit. | `controller_serial` and `check_input_replay.py`: five attachments, known report bytes and four text-trace frames each | confirmed |
| O-40 | Fine X selects across adjacent fetched tiles but does not shift the output pointer: each output still occupies exactly eight screen pixels. The two fetched tiles persist across lines and fully disabled rendering; re-enabling rendering can output old pipeline pixels before newly fetched tiles. Colors are resolved from palette indices at output. The dot-256 catch-up already emits the complete row, so no additional tail flush is performed. | Mappy TAS frame 1502 line 133 (rendering off at position 118, x=95); frame 1505 line 165 (rendering on at position 172, x=168-170); `ppu_bg_toggle_fine_x` | confirmed on replay and probes |
