# Plan: make nes1 and FCEUX produce identical output (every frame, scanline and instruction)

Status: APPROVED. Implementation in progress.

## 1. Goal and definition of done

**Goal.** For the same ROM, the same command line and no input, nes1 and FCEUX produce byte-identical
state at every observation point. FCEUX is the ground truth: nes1 changes to match it (including where
FCEUX deviates from real hardware). FCEUX only receives instrumentation, never behaviour changes.

**Done when** all of these hold, for each of `test/nestest.nes`, `test/mappy/Mappy (Japan).nes`,
`test/ppu/color_test.nes`, for 3000 frames from power-on:

1. **Every frame:** the end-of-frame state is identical.
2. **Every scanline:** the end-of-scanline state of all 240 visible scanlines is identical.
3. **Every instruction:** the per-instruction trace is identical (PC, opcode, A, X, Y, S, P, cycle count).
4. The compared state includes (decided): CPU registers and cycles, 2 KB RAM, PPU registers and internal
   latches, nametables, palette, OAM, CHR/PRG, **rendered pixels (256x240 palette indices)**, and
   **internal APU state including DMC and IRQ state**.
5. `tool/run_parity.ps1` (new) runs the whole suite on a clean Windows machine with one command and
   exits 0. It is the regression gate for every later change.

**Out of scope:** controller input, mappers other than 0, PAL/Dendy, audio sample output (state, not the
waveform, is compared), the interactive GUI (must keep working, but is not compared).

## 2. What we know today (from the 120-frame Mappy comparison)

- Both emulators are deterministic. The first three frame dumps are identical (power-up frames).
- Everything after that differs. First divergence is at frame 4 (PC trigger at $C06F, scanline 148).
- Differing fields: PPU `V` (362 events), PPU `STATUS` (241), CPU `PC`/`CYCLES` (77), then A, P, X, S and 2 RAM
  bytes (probably consequences of the cycle drift).
- Matching in every event: nametables, palette, OAM, ROM CRCs, APU register shadow (for this ROM and range).

Reading nes1's source shows why patching will not be enough: its PPU is a **render-time approximation**.

| Area | What nes1 does now | Consequence |
|------|--------------------|-------------|
| `V` register | Only changed by $2006/$2007 writes; never advanced while rendering | Wrong `V` at every point inside a rendered frame |
| `STATUS` | Sprite-0 hit is a "sprite Y is on this line" heuristic; no sprite-overflow bit | Bits 6 and 5 differ |
| Register reads | $2000/1/3/5/6 and $4017 reads return 0; no PPU open-bus latch | Wrong values reach the CPU |
| Memory map | Writes to ROM space and $4018-$7FFF are stored; reads of unmapped space return stored data | PRG can be corrupted; wrong open-bus values |
| Instruction timing | Starts an instruction only when at least one whole CPU cycle of budget is left; adds the cost after executing | 2-3 cycle drift at scanline ends (hypothesis, to be proven) |
| CPU | No IRQ, BRK treated as an error, unofficial opcodes partly covered | Diverges on any ROM using them |
| APU | No DMC, no frame/DMC IRQ, no $4015 IRQ bits | CPU-visible differences for ROMs using them |
| Rendering | Per-scanline snapshots (`render_nt`, `scanline_scroll`, ...) instead of a real pipeline | Pixel parity only by accident |

These are hypotheses until each is confirmed by an experiment in Phase 1-4 (see the protocol in section 4).

## 3. Method: the divergence-driven loop

Every phase uses the same loop; the tooling in Phase 0 automates steps 1-3.

1. Run both emulators with identical arguments in **digest mode** (cheap, full length).
2. Find the first differing event (frame, then scanline, then instruction).
3. Re-run only that frame with **full dumps** and **instruction trace**; the comparator names the field/address.
4. Classify: (a) nes1 bug, (b) the two emulators observe at different moments (fix the observation point
   in the contract), (c) capture bug (a state field is read wrongly in one of them).
5. Write down the observed behaviour in `doc/parity_spec.md` (the spec, see section 4), fix nes1 from the spec.
6. Re-run the whole suite; the first divergence must move later. Repeat until none is left.

## 4. Clean-room protocol (decided: keep nes1 MIT)

- nes1's implementation is derived **only** from: public hardware documentation (`doc/nestech.txt`,
  `doc/6502_cpu.txt`, `doc/NESDoc.pdf`, the nesdev wiki), the nestest reference log, and **observed FCEUX
  behaviour** captured in dumps, traces and probe-ROM experiments.
- Observed behaviour is written into `doc/parity_spec.md` as behaviour statements with their evidence
  (event name, field, values). nes1 changes cite a spec entry.
- FCEUX source is used only to place instrumentation and to read the *meaning of the state variables*
  that the dump must capture. It is not a reference for how nes1 should implement a behaviour.
- **Disclosure.** While researching this plan I read parts of FCEUX's PPU and CPU source. The hypotheses
  above are therefore informed by it. If you need strict separation, the implementation phases should be
  done by someone (or a fresh session) that receives only `doc/parity_spec.md`, not the FCEUX source.
  Tell me if you want that; it changes who does Phases 1-4, not the plan.
- Probe ROMs (Phase 0) are our own tiny programs, generated by a script, so they carry no third-party licence.

## 5. Phases and work items

### Phase 0 - Tooling and contract (both emulators; no behaviour change in nes1 or FCEUX)

| ID | Work item | Notes |
|----|-----------|-------|
| p0-contract | Write `doc/parity_spec.md` section "Observation contract" | Exact definitions: cycle count is read **before** the instruction's cost is added; end-of-scanline moment; end-of-frame moment; frame 1-2 power-up; what `scanline=-1` means; pinned FCEUX configuration (NTSC, no config file, default RAM pattern, sprite limit on, old PPU) recorded in `run.txt` |
| p0-state | Extend `nesdump::State` and both capture functions | Add everything that influences future behaviour: PPU open-bus latch, OAM address internals, sprite-0/overflow state, VBL/NMI pending, IRQ lines, joypad shift state, CPU data-bus latch, DMA state, internal APU state (schema decided in Phase 4) |
| p0-fb | Add `[FB]` section (256x240 palette indices) to frame and scanline dumps | nes1 renders BGRA today; add an index buffer. FCEUX side reads its 8-bit buffer. Only the pixels of scanlines already finished are valid in scanline dumps; later rows are written as `--` |
| p0-digest | Digest mode: `--dump-mode digest` writes one line per event with a hash of the whole state and of each section | Needed for scale: 3000 frames x 240 scanlines = 720,000 scanline events per ROM; full dumps would be ~100 GB. Replace the bitwise CRC32 with a fast 64-bit hash (the current one is too slow for this) |
| p0-trace | Per-instruction trace: `--trace FILE` with `--trace-frames LIST`; trace digest chained per segment | Line: `seq frame scanline PC opcode A X Y S P cycles`. Chained hash covers every instruction for the full run; the text trace is only written for chosen frames. Optional `--trace-bus` adds every CPU read/write for tight debugging |
| p0-insn | `--dump-insn LIST` full dump before the Nth instruction; `--init-pc ADDR` start state option | `--init-pc C000` lets nestest run its automatic mode so the CPU is tested with all official and many unofficial opcodes (nestest.log is an independent reference) |
| p0-compare | Extend `tool/compare_dumps.py`; add `tool/find_divergence.py` | Compares digests and traces streaming (no memory blow-up); automates the loop: first bad frame -> re-run it with scanline dumps -> first bad scanline -> trace -> first bad instruction -> full dump diff |
| p0-probe | `tool/make_probe_rom.py` (mini assembler + iNES writer) and `test/probe/*.nes` | Small ROMs that isolate one behaviour (e.g. read $2002 at chosen cycle offsets) so FCEUX can be observed black-box |
| p0-suite | `tool/run_parity.ps1` | Runs nes1 + FCEUX for the 3 ROMs x 3000 frames in digest mode; prints first divergence per ROM; exit code 0 only if all identical. Initially it **fails**; that is the baseline |

Exit criteria: the suite runs end to end on the VM; both emulators produce the new sections; the
baseline divergence report is committed; determinism check (each emulator twice) passes; legacy
modes (`--frametest`, `--scanlinetrace`, interactive) still work.

### Phase 1 - CPU, memory map and cycle semantics

| ID | Work item | Evidence / test |
|----|-----------|-----------------|
| p1-nestest | Make nes1 match `nestest.log.txt` and the FCEUX trace using `--init-pc C000` | Instruction trace equality for the 8,991 log lines |
| p1-timing | Fix the instruction-start rule and when cycle cost is applied | Probe ROM plus scanline-end `CYCLES` equality in Mappy (known drift: 121736 vs 121738 at frame 5 scanline 0) |
| p1-interrupts | NMI, IRQ and BRK semantics: pushed flags, vector fetch, taken-at-boundary rule, I-flag delay after CLI/SEI/PLP, NMI-during-BRK | Probe ROMs; trace equality |
| p1-opcodes | Remaining official/unofficial opcodes and their page-cross/dummy-access behaviour | nestest plus generated per-opcode probe ROMs |
| p1-bus | Memory map: RAM mirrors, ROM writes ignored, unmapped reads (open bus value = last bus value), $4016/$4017 reads with no controller, $4018-$7FFF | Probe ROMs read/write every region and store results in RAM |
| p1-dma | OAM DMA cycle cost and alignment; DMC DMA stalls arrive in Phase 4 | Trace cycle equality across a $4014 write |

Exit: nestest, Mappy and color_test instruction traces equal for 3000 frames **or** the remaining
differences are attributable to the PPU/APU (recorded in the divergence log).

### Phase 2 - Frame and scanline scheduling

| ID | Work item |
|----|-----------|
| p2-budget | Replace the CPU budget model (`run_ppu`) so partial-cycle budget carries exactly as observed in FCEUX; same order of events inside a frame |
| p2-vblank | Exact moments for VBL flag set/clear, NMI delivery and its latency, OAM address reset at vblank, status-bit clearing before rendering |
| p2-powerup | Power-up frames 1-2 and reset state (already equal; keep under test) |
| p2-observe | Observation points implemented identically in both (spec section "contract") |

Exit: CPU `PC`/`CYCLES` equal at every scanline end and frame end for the three ROMs.

### Phase 3 - PPU (largest phase; clean-room)

Replace the approximations with a PPU whose *registers and counters* behave as FCEUX's do, and let the
framebuffer fall out of the same model instead of snapshots. Work items, each driven by dumps and probe ROMs:

| ID | Work item |
|----|-----------|
| p3-regs | Register semantics: open-bus latch on all reads/writes, $2002 low bits, $2003/$2004 behaviour, $2007 read buffer and palette reads, $2000 NMI-enable edge |
| p3-addr | `V`/`T`/fine-X/`W` evolution while rendering (vertical increment, horizontal copy points, behaviour when rendering is off); `V` equal at every scanline end |
| p3-sprites | Sprite evaluation per scanline, 8-sprite limit and overflow bit, 8x8 and 8x16, priority, flip |
| p3-sprite0 | Sprite-0 hit: pixel-exact condition and the **moment** it becomes visible to the CPU (depends on register-access timing within the line) |
| p3-render | Background and sprite rendering into an index buffer; mid-scanline register writes take effect at the same pixel as FCEUX; emphasis/greyscale bits as stored in the buffer |
| p3-mem | Nametable mirroring, palette mirrors, CHR-ROM write protection, CHR-RAM if present |
| p3-legacy | Keep `--frametest`, `--scanlinetrace`, `--autotest` and the interactive window working on the new PPU (convert the index buffer to BGRA for display) |

Exit: all PPU fields and `[FB]` identical at every frame and scanline for the three ROMs.

### Phase 4 - APU (decided: internal state, DMC and IRQ in scope)

| ID | Work item |
|----|-----------|
| p4-schema | Define the canonical APU state schema (length counters, envelopes, sweeps, linear counter, timers, frame-counter step/mode/IRQ flag, DMC address/length/shift/buffer/IRQ flag, $4015 view). For each field, find the matching FCEUX internal and prove the mapping with a probe ROM. **Decision gate:** if some FCEUX internals cannot be mapped to a hardware-meaningful field, list them and agree with you whether to exclude or to match FCEUX's own representation |
| p4-frame | Frame counter (4/5-step), frame IRQ and $4017 behaviour |
| p4-dmc | DMC channel, its IRQ and the CPU cycles it steals |
| p4-status | $4015 read/write semantics and IRQ acknowledge |
| p4-channels | Pulse, triangle and noise counters matched to the schema |

Exit: APU section identical at every event for the three ROMs, and CPU traces unaffected.

### Phase 5 - Soak and hand-over

| ID | Work item |
|----|-----------|
| p5-soak | 3000 frames x 3 ROMs, all three comparison levels, run twice per emulator (determinism) |
| p5-docs | Update `doc/state_dump.md`, `doc/build_environment.md`, readme; keep `doc/parity_spec.md` as the behaviour reference |
| p5-ci | Record the expected-identical result as the baseline; any future nes1 change must keep `run_parity.ps1` at exit 0 |

## 6. Risks and how the plan handles them

| Risk | Handling |
|------|----------|
| FCEUX's model is simplified (not cycle-exact hardware), so docs alone cannot define the target | Black-box probe ROMs and traces are the source of truth; every behaviour gets a spec entry with evidence |
| Sprite-0 hit / mid-line register timing depends on exactly when within a line a CPU access happens | Probe ROMs read/write at stepped cycle offsets; the instruction trace and `--trace-bus` pin the moment |
| APU internals in FCEUX may not map one-to-one onto hardware state | Decision gate in p4-schema before any APU implementation work |
| Output volume | Digest mode for the full runs; full dumps/traces only for the frames being debugged. Estimates: one full dump ~170 KB with pixels; full 3000-frame scanline dumps would be ~100 GB per ROM, so they are never written in bulk |
| Performance of hashing and tracing | Fast 64-bit hash, binary state hashed directly (not text); trace is hashed on the fly |
| nes1 regressions in legacy modes | `p3-legacy` plus a small pixel-regression check kept in the suite |
| Clean-room integrity | Section 4; disclosure of prior reading of FCEUX source |
| One divergence hides the next (everything cascades) | The loop always fixes the **first** divergence only; consequences are re-evaluated after each fix |

## 7. Working environment

- Build and run on the existing Windows VM (`nes1-win-vm`). The Mac cannot execute freshly built binaries.
- Compare on the VM (portable Python is installed); nothing is copied back to the Mac.
- Source sync to the VM and rebuild scripts already exist in the session; they become `tool/` scripts in Phase 0.
- Commits: one per work item, FCEUX instrumentation in separate commits so `git diff` against the original
  import (`4471be6`) still shows only our FCEUX changes. Pushing needs your credentials (not available here).

## 8. Assumptions to confirm during review

1. "Every instruction" is satisfied by the chained instruction-trace digest over the full 3000 frames plus
   full text traces and full-state dumps on demand (a full dump at every instruction is ~1.3 GB per frame).
2. A pixel difference counts as a failure only through the `[FB]` section (palette index incl. emphasis bits),
   not through BGRA colour conversion.
3. The pinned FCEUX configuration (NTSC, default RAM pattern, sprite limit on, no config file) is the
   reference; other FCEUX settings are not supported.
4. DMC in the repo ROMs may never be exercised; APU parity is still implemented and proven with probe ROMs.
5. If the divergence log shows a FCEUX behaviour that looks like an FCEUX bug, we still match it (FCEUX is
   the ground truth) and mark it in the spec.

## 9. Progress log

### Phase 0 - done

Delivered: observation contract and first observed-behaviour entries ([parity_spec.md](parity_spec.md)); `[FB]`
section; fast 64-bit hash and digest mode; per-instruction trace and chained instruction hash;
`--dump-insn`, `--init-pc`; `compare_dumps.py` digest support, `find_divergence.py`, `check_nestest.py`;
probe ROM generator and six probe ROMs; `run_parity.ps1`; baseline in [parity_baseline.md](parity_baseline.md).
Exit criteria checked: suite runs end to end, both emulators emit the new sections, determinism, legacy modes.

Deviations from the plan as written:

- **p0-state:** the extra internal-state fields (PPU open-bus latch, IRQ lines, joypad shift state, CPU data-bus
  latch, DMA state, APU internals) are *not* added yet. nes1 has no equivalent for most of them, so adding them now
  would make every event differ from the first one and hide the real first divergence. Each field is added in the
  phase that implements it (CPU/bus fields in phase 1, PPU fields in phase 3, APU in phase 4), together with its
  spec entry.
- **p0-compare:** the divergence tooling works on the VM where both emulators run; it is not packaged separately.
- **Observation point for P:** the dump reports the architectural status register (bit 5 = 1, bit 4 = 0) because
  FCEUX stores stray bits there that no program can observe, and because this makes `nestest.log` usable as an
  independent reference (spec O-2).

### Phases 1-3 - done for the repo ROMs (frame level, 3000 frames)

All 12 suite entries (nestest from reset and with `--init-pc C000`, Mappy, color_test, and the 8 probe ROMs) are
identical to FCEUX for 3000 frames at frame level, which includes the chained hash of every instruction (PC,
opcode, registers, cycles, data bus, PPU latch). See [parity_spec.md](parity_spec.md) O-6 to O-24 for the behaviours
that were found and the evidence.

What was implemented in nes1 (clean-room, driven by the divergence loop): BRK, KIL and the remaining illegal
opcodes; architectural P and the reset state; the open-bus memory map with work RAM; the data-bus and PPU I/O
latch models; OAM write quirk, `$2004` reads, OAM DMA through `$2004`, OAMADDR reset at vblank; the CPU budget kept
in PPU dots; V evolution; sprite overflow and pixel-accurate sprite-0 hit; and a lazy per-tile line renderer that
reproduces FCEUX's mid-line effects (catch-up at register accesses, tile pipeline with write pointer).

Deviations from the plan as written: phase 2 (scheduling) did not need separate work beyond the budget rule
(O-9); the PPU work was driven by rendering parity rather than the order in section 5.

Scanline-level result (3000 frames, every one of the 240 scanlines of every frame, plus every frame and the instruction
hash at each of those points): all 12 suite entries IDENTICAL (`tool/run_parity.ps1 -Frames 3000 -Level scanline`).

Phase 4 (APU), done for everything the CPU can observe: nes1 now has the FCEUX frame counter and frame IRQ, the IRQ
line and I-flag latency, `$4015` semantics, length counters and the DMC (fetch stalls, IRQ, loop). Spec O-25..O-29. The
dump has a new `[APUX]` section (spec 1.9). Decision-gate outcome (p4-schema): the audio-only state of the pulse,
triangle and noise channels (timers, sequencers, envelopes, sweeps, linear counter, LFSR, mixer) is **not** compared:
FCEUX computes it lazily at sound-flush time in a form that depends on its sample-rate settings, and the CPU cannot see
it. Probes: `apu_len_status`, `apu_frame_irq`, `apu_frame_mode`, `apu_dmc_irq` and the random-traffic `apu_fuzz`.

### Still open

- **Phase 5 (soak)**: scanline-level runs for all ROMs, and the instruction-level text trace comparison on chosen frames.
- Unverified in nes1: the unstable illegal opcodes (`$8B`, `$AB`, `$93`, `$9B`, `$9C`, `$9E`, `$9F`), the other KIL
  opcodes, `$6000-$7FFF` work-RAM extent, the exact `268` threshold of O-21, 8x16 sprites, left-column clipping.

