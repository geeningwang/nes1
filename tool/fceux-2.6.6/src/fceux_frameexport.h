#pragma once
#include "types.h"

// Per-frame txt+bmp export for FCEUX instrumentation.
// framenum: 1-based frame number, nframes: total frames to export (controls filename).
// outdir: output directory (relative or absolute).
void FCEUX_ExportFrame(int framenum, int nframes, const char* outdir);

// Single NT/$2007 write during a scanline's CPU window.
struct FCEUXNtWrite {
    int            dot;     // approximate PPU dot within scanline (CPU cycle offset * 3)
    unsigned short addr;    // mirrored NT/AT address ($2000-$2FFF)
    unsigned char  val;     // byte written
    unsigned short cpu_pc;  // CPU PC at time of write
};

// Per-scanline CPU+PPU state trace.
// Call FCEUX_EnableScanlineTrace() before FCEUPPU_Loop every frame.
// FCEUX_CaptureBeginScanline(sl) must be called at the START of each visible
// scanline's CPU window (before X6502_Run(256)).
// FCEUX_CaptureScanlineTrace(sl) is called automatically from DoLine() if active.
// FCEUX_LogNTWrite(addr, val, pc) hooks $2007 writes during visible scanlines.
struct FCEUXScanlineTrace {
    // Start-of-scanline snapshot (before X6502_Run(256))
    unsigned int   ppu_v_start;
    unsigned char  ppuctrl_start;
    unsigned char  ppumask_start;
    // End-of-scanline snapshot (after X6502_Run(256))
    unsigned short cpu_pc;
    unsigned char  cpu_a, cpu_x, cpu_y, cpu_s, cpu_p;
    unsigned int   cpu_cycles;
    unsigned int   ppu_v;
    unsigned int   ppu_t;
    unsigned char  ppu_fine_x;
    unsigned char  ppu_w;
    unsigned char  ppuctrl;
    unsigned char  ppumask;
    unsigned char  palette[32];
    // Render scroll decode (from end-of-scanline V register)
    short          ss_x;     // scroll_x used for rendering (0-255)
    short          ss_y;     // scroll_y used for rendering (0-255)
    unsigned char  ss_nt;    // nametable selection used for rendering
    // NT write log during this scanline's CPU window
    int            nt_write_cnt;   // capped at 64
    FCEUXNtWrite   nt_writes[64];
};

void FCEUX_EnableScanlineTrace();
void FCEUX_DisableScanlineTrace();
bool FCEUX_ScanlineTraceActive();
void FCEUX_CaptureBeginScanline(int sl);
void FCEUX_CaptureScanlineTrace(int sl);
void FCEUX_LogNTWrite(uint32 addr, uint8 val, unsigned short pc);
const FCEUXScanlineTrace* FCEUX_GetScanlineTrace();
