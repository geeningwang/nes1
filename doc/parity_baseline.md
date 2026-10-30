# Parity baseline (end of plan phase 0)

State of nes1 against FCEUX when the comparison tooling was finished, before any nes1 behaviour change.
Produced by `tool/run_parity.ps1 -Frames 300 -TimeoutSec 40` (Release builds, frame-level digests, then the
automatic drill-down). Nothing here is a fix; it is the starting point that later phases are measured against.

## Result

`PARITY FAILED: 10 of 10 differ`

| Suite entry | Result | First divergence found automatically |
|-------------|--------|---------------------------------------|
| `nestest_auto` (`--init-pc C000`) | ERROR: nes1 hung | nes1 never finishes: after nestest's last logged instruction it reaches an undefined opcode / BRK, `step()` returns without consuming cycles and the scheduler loops forever (spec O-5) |
| `nestest` | differs | frame 1, instruction 1: `S` is `00` in nes1, `FD` in FCEUX (O-3) |
| `mappy` | differs | same |
| `color_test` | differs | same |
| `probe_cpu_flags_brk` | ERROR: nes1 hung | BRK (O-5) |
| `probe_mem_map` | differs | same `S` difference; also `RAM`, `PRG` differ at the end of frame 1 (ROM space writable in nes1) |
| `probe_oam_dma` | differs | same |
| `probe_ppu_open_bus` | differs | same; `RAM` differs at the end of frame 1 (reads of write-only PPU registers) |
| `probe_ppu_scroll_idle` | differs | same |
| `probe_ppu_status_poll` | differs | same |

Every entry also differs in `FB` from frame 1 (nes1 does not draw the power-up frames, O-4).

## Mappy, 10 frames, every scanline (digest, 1930 events)

First divergence: frame 1 (`FB`, `INSN`). Differing fields over all events (number of events):
`PPU` 1561, `INSN` 1545, `CPU` 495, `FB` 156, `RAM` 30, `PAL` 2. First scanline-level differences:
CPU at frame 3 scanline 4, PPU at frame 3 scanline 189, RAM at frame 3 scanline 13, PAL at frame 4 scanline 87.

## Checks of the tooling itself

| Check | Result |
|-------|--------|
| FCEUX trace against `nestest.log.txt` (`--init-pc C000`) | `OK: 8991 of 8991` instructions match (PC, A, X, Y, P, S, cycles) |
| Determinism, 600 frames, 3256 events | nes1 run twice: identical; FCEUX run twice: identical |
| Legacy nes1 modes `--frametest`, `--scanlinetrace` | still produce their output (4 and 481 files) |
| Interactive window | not testable from a non-interactive session; the only changes on that path are a null-guarded hook and an extra buffer write |

## What this tells us about the next phase

Plan phase 1 starts with: reset register state (`S`), undefined opcodes and BRK (the hangs), the memory map
(ROM space), and `nestest.log` equality for nes1.
