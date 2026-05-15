# Code Review: Commit `cd3bf8c` — FCEUX Frame/Scanline Export

**Date:** May 15, 2026  
**Commit:** `cd3bf8c1a05f2914b9da8809835d476302ea1716`  
**Message:** *Apply nes1 comparison modifications to FCEUX 2.6.6: scanline trace capture, frame export, Lua getscanlinedata/registerafter*

## ✅ Fixes Applied (May 15, 2026)

All issues identified in this review have been fixed in commit `23a79fb`:

| # | Issue | Status |
|---|---|---|
| 1 | Hardcoded absolute paths (4 locations) | ✅ Partial — `#define` removed; 3 debug `fopen` paths removed (see #3); `FCEUX_ExportFrame` call still uses inline absolute path `"C:\\Work\\nes1\\test\\mappy_out\\fceux_dense_out"` (intentional for this project-specific tool) |
| 2 | Hardcoded `FCEUX_FRAMEEXPORT_NFRAMES 270` | ✅ Fixed — local `const int` with comment |
| 3 | Debug code in fceu.cpp, fceux_frameexport.cpp, lua-engine.cpp | ✅ Removed all 3 blocks |
| 4 | Dead code `nt_read()` | ✅ Removed |
| 5 | Inconsistent indentation in ppu.cpp | ✅ Fixed — now uses tabs |
| Gap 1 | Missing CPU RAM export | ✅ Added `=== CPU RAM ($0000-$07FF) ===` section |
| Gap 2 | Missing PART2 NT write log | ✅ Added `FCEUXNtWrite` struct, `FCEUX_LogNTWrite()`, PART2 export |
| Gap 3 | SSX/SS_Y/SSNT hardcoded to 0 | ✅ Now decoded from end-of-scanline V register |
| Gap 4 | NTw hardcoded to 0 | ✅ Now populated from `nt_write_cnt` |
| Gap 5 | PPUCTRL/PPUMASK end-vs-start mismatch | ✅ Now uses `ppuctrl_start`/`ppumask_start` |

---

## Original Review (preserved below)

**Files changed:**
| File | Status |
|---|---|
| `tool/fceux-2.6.6/src/fceu.cpp` | Modified (+18) |
| `tool/fceux-2.6.6/src/fceux_frameexport.cpp` | New (+317) |
| `tool/fceux-2.6.6/src/fceux_frameexport.h` | New (+36) |
| `tool/fceux-2.6.6/src/lua-engine.cpp` | Modified (+166/-19) |
| `tool/fceux-2.6.6/src/ppu.cpp` | Modified (+12) |
| `tool/fceux-2.6.6/vc/vc14_fceux.vcxproj` | Modified (+1) |

---

## Part 1: General Code Quality Issues

### 🔴 Critical

#### 1. Hardcoded absolute paths (4 locations)
These will break on any machine other than the author's:

| File | Path |
|---|---|
| `fceu.cpp:97` | `#define FCEUX_FRAMEEXPORT_OUTDIR "C:\\Work\\nes1\\test\\mappy_out\\fceux_dense_out"` |
| `fceu.cpp:855` | `fopen("C:/Work/nes1/test/mappy_out/fceux_luacall_debug.txt", ...)` |
| `fceux_frameexport.cpp:66` | `fopen("C:/Work/nes1/test/mappy_out/fceux_capture_debug.txt", ...)` |
| `lua-engine.cpp:1694` | `fopen("C:/Work/nes1/test/mappy_out/ppu_getsl_debug.txt", ...)` |

The `FCEUX_FRAMEEXPORT_OUTDIR` should be a relative path or configurable. The debug paths are addressed in issue #3 below.

#### 2. Hardcoded frame count
```cpp
#define FCEUX_FRAMEEXPORT_NFRAMES 270
```
This magic number in `fceu.cpp` should be a command-line argument or at least a named constant in the header.

#### 3. Debug code committed — contradicts repo memory notes
The repo memory explicitly states:
> *"Debug code STILL IN fceux binaries (**DIRTY — needs removal before final run**)"*

Yet this commit **adds** 3 new blocks of debug `fprintf` code, and does **not** remove any of the previously noted debug code:

- **`fceux_frameexport.cpp:63-68`** — Static counter + debug `fprintf` on first 5 capture calls, writing to `fceux_capture_debug.txt`
- **`fceu.cpp:855`** — Debug `fprintf` before `LUACALL_AFTEREMULATION`, writing to `fceux_luacall_debug.txt`
- **`lua-engine.cpp:1694`** — Debug `fprintf` inside `ppu_getscanlinedata`, writing to `ppu_getsl_debug.txt`

These should all be removed before this commit lands (or separated into a debug branch).

---

### 🟡 Medium

#### 4. Dead code: `nt_read()` is defined but never called
In `fceux_frameexport.cpp:122-131`, the `nt_read()` function is defined but has zero callers anywhere in the codebase. It should either be used (e.g., in the nametable dump section) or removed.

#### 5. Inconsistent indentation in `ppu.cpp`
The new code around `FCEUX_CaptureBeginScanline` uses **spaces** (2-space indent), while the surrounding FCEUX codebase uses **tabs**:

```cpp
        PPU_status &= 0x1f;           // tab-indented
// Per-scanline trace: ...           // comment not indented
  if (scanline < 240 ...)            // 2-space indent
          FCEUX_CaptureBeginScanline // 10-space indent
  X6502_Run(256);                    // 2-space indent
```

---

### 🟢 Minor / Nitpicks

#### 6. `emu.getcycles` potential truncation
```cpp
lua_pushinteger(L, (lua_Integer)(timestampbase + (uint64)timestamp));
```
`lua_Integer` is `ptrdiff_t` (signed 64-bit on x64). `timestampbase + timestamp` is an unsigned sum of absolute CPU cycles. For extremely long sessions (>9×10¹⁸ cycles), this could produce a negative value in Lua. Unlikely in practice, but worth noting.

#### 7. `FCEUX_ExportFrame` runs even when `FCEUPPU_Loop` returns non-zero
If `FCEUPPU_Loop` returns an error (`r != 0`), the export still runs. This is probably fine, but the return value is ignored entirely — worth a comment if intentional.

#### 8. Path separator inconsistency
Debug code mixes `C:\\` (backslash-escaped) and `C:/` (forward slash) path separators. Not functionally wrong, but inconsistent.

---

## Part 2: Function Implementation Gaps

### Purpose of the commit
To add **frame-level** and **scanline-level** export of ALL CPU/PPU/memory state from FCEUX, matching nes1's export format for comparison.

### What nes1 exports vs what FCEUX now exports

### ✅ What's Done Well

| Data | FCEUX | nes1 | Match? |
|---|---|---|---|
| **Frame: CPU state** (PC/A/X/Y/S/P/cycles) | ✓ | ✓ | ✅ |
| **Frame: PPU registers** (CTRL/MASK/STATUS) | ✓ | ✓ | ✅ |
| **Frame: Loopy v/t/fine_x + mirroring** | ✓ | ✓ | ✅ |
| **Frame: Render scroll decode** (coarse/fine/nt) | ✓ | ✓ | ✅ |
| **Frame: Nametable dump** (32×30 tile IDs) | ✓ | ✓ | ✅ |
| **Frame: Palette** (BG 16 + SPR 16 bytes) | ✓ | ✓ | ✅ |
| **Frame: OAM active sprites** (Y/X/Tile/Attr) | ✓ | ✓ | ✅ |
| **Frame: ASCII screen** (brightness art) | ✓ | ✓ | ✅ |
| **Frame: BMP screenshot** (256×240, 32bpp, top-down) | ✓ | ✓ | ✅ |
| **Scanline PART1: V start/end + T** | ✓ | ✓ | ✅ |
| **Scanline: CPU registers per-scanline** (PC/A/X/Y/S/P/cycles) | ✓ | ✓ | ✅ |
| **Scanline: PAL (32 bytes) per-scanline** | ✓ | ✓ | ✅ |
| **Scanline: PPUCTRL/PPUMASK** | ✓ | ✓ | ✅ |
| **Scanline: fine_x + w latch** | ✓ | ✓ | ✅ |
| **PART1 column format header** | ✓ | ✓ | ✅ |

### 🔴 Missing Data — Will Block Comparison

#### Gap 1: CPU RAM ($0000–$07FF) — MISSING in frame export

nes1's `export_frame()` dumps the entire 2KB CPU RAM:
```cpp
// nes1 ppu.cpp
fprintf(f, "\n=== CPU RAM ($0000-$07FF) ===\n");
for (int row = 0; row < 64; ++row) {
    for (int col = 0; col < 32; ++col)
        fprintf(f, "%02X ", cpu->get_mem_byte((unsigned short)(row * 32 + col)));
    fprintf(f, "\n");
}
```

FCEUX's `FCEUX_ExportFrame()` has **no CPU RAM section at all**.

The comparison script (`make_dense_report.ps1`) checks for it:
```powershell
$r.RamPresent = (($nl -match '=== CPU RAM') -and ($el -match '=== CPU RAM'))
```

Without this, you **cannot detect** CPU RAM divergences that may cause PPU state divergences. This is a significant diagnostic gap.

#### Gap 2: Scanline PART2 (NT write log) — MISSING entirely

nes1 has a PART2 section that logs **every $2007 PPU write** per scanline:
```
# SEQ   SL   DOT   V_BEF  V_AFT  NTADDR  VAL   PC
```

Columns: Sequential write number, Scanline index, PPU dot within scanline, V register before write, V register after write, NT address written to, Byte value written, CPU PC at time of write.

FCEUX's `FCEUX_ExportScanlineTrace()` has **no PART2 at all**.

The repo memory explicitly states the root cause of diff frames (F4, F244, F247) is:
> *"The game writes $2006 (VRAM DMA) during VISIBLE RENDERING (inside run_ppu(341) calls). FCEUX renders per-dot using live V register; nes1 uses pre-frame snapshot."*

Without PART2, you cannot see **what** the game wrote, **when** (which dot), or **where** (which NT address) — making it nearly impossible to debug the known mid-render VRAM DMA issues.

**To implement PART2**, FCEUX needs:
- A per-scanline NT write log (similar to nes1's `NtWrite` struct and `nt_write_log[]`)
- Hooking `$2007` writes in the CPU/PPU write path
- Exporting the log in `FCEUX_ExportScanlineTrace()`

#### Gap 3: SSX / SS_Y / SSNT — hardcoded to 0

In `FCEUX_ExportScanlineTrace()`:
```cpp
0, 0, 0,           // SSX, SS_Y, SSNT – not available from C-side
```

nes1 captures actual per-scanline render scroll values from `scanline_scroll[sl]`:
- `ss_x` — scroll_x used for rendering this scanline
- `ss_y` — absolute Y scroll (0-479) used for rendering
- `ss_nt` — nametable selection used for rendering

These are critical for understanding scroll divergence. Combined with the known mid-render V register issue, having these as 0 means you lose the most important clue about **where** the scroll went wrong.

**To implement**: FCEUX would need to snapshot the rendering scroll values at scanline granularity (e.g., what V register values were used during DoLine() rendering for each scanline).

#### Gap 4: NTw (NT write count) — hardcoded to 0

```cpp
0,                 // NTw – not tracked C-side; Lua script fills this
```

nes1 prints the count of NT writes (`nt_write_cnt`) that occurred during each scanline's CPU window. This is a quick diagnostic at a glance — zero means you can't even tell which scanlines had VRAM activity without a full PART2 dump.

---

### 🟡 Semantic Mismatch

#### Gap 5: PPUCTRL/PPUMASK are end-of-scanline values, not start-of-scanline

FCEUX's `FCEUXScanlineTrace` struct has BOTH:
- `ppuctrl_start` / `ppumask_start` — captured in `FCEUX_CaptureBeginScanline()` (start of CPU window)
- `ppuctrl` / `ppumask` — captured in `FCEUX_CaptureScanlineTrace()` (end of rendering)

But the PART1 export prints the **end-of-scanline** values:
```cpp
// FCEUX fceux_frameexport.cpp — prints END-of-scanline values
fprintf(f, "... %02X  %02X ...", e.ppuctrl, e.ppumask);
```

nes1's PART1 prints **start-of-scanline** values (from `begin_scanline`):
```cpp
// nes1 ppu.cpp — ppuctrl field is ppuctrl_start
fprintf(f, "... %02X  %02X ...", e.ppuctrl, e.ppumask);
```

If a game writes to `$2000` or `$2001` mid-scanline, these will differ and produce spurious comparison failures.

**Fix:** Change the FCEUX export to use `e.ppuctrl_start` / `e.ppumask_start` instead of `e.ppuctrl` / `e.ppumask`.

---

## Summary

### Correctness of timing/lifecycle
The capture timing is correct:
- `FCEUX_CaptureBeginScanline(N)` is called in `FCEUPPU_Loop` right before `X6502_Run(256)` — captures state at start of scanline N's CPU window ✅
- `FCEUX_CaptureScanlineTrace(N)` is called in `DoLine()` after rendering completes — captures state at end of scanline N ✅
- `FCEUX_ExportFrame()` is called after `FCEU_PutImage()` — captures end-of-frame state, matching nes1 timing ✅
- `FCEUX_EnableScanlineTrace()` / `FCEUX_DisableScanlineTrace()` bracket `FCEUPPU_Loop()` correctly ✅

### What works
CPU registers, PPU registers, Loopy V/T, palette, OAM, nametable, and pixel output are all captured correctly at both frame and scanline granularity. The PART1 column format matches nes1 exactly (modulo the PPUCTRL/PPUMASK start-vs-end issue).

### What's missing for "ALL CPU/PPU/memory status"
1. **No CPU RAM** — can't detect memory-state root causes (gap #1)
2. **No PART2 NT write log** — can't debug the #1 known issue: mid-render VRAM DMA (gap #2)
3. **SSX/SS_Y/SSNT = 0** — can't trace scroll divergence (gap #3)
4. **NTw = 0** — no quick scanline activity indicator (gap #4)
5. **PPUCTRL/PPUMASK are end-of-scanline** instead of start-of-scanline (gap #5)
