#pragma once
#include "types.h"

// Per-frame txt+bmp export for FCEUX instrumentation.
void FCEUX_ExportFrame(int framenum, const char* outdir);

// Per-scanline CPU+PPU state trace.
// Call FCEUX_EnableScanlineTrace() before FCEUPPU_Loop every frame.
// FCEUX_CaptureBeginScanline(sl) must be called at the START of each visible
// scanline's CPU window (before X6502_Run(256)).
// FCEUX_CaptureScanlineTrace(sl) is called automatically from DoLine() if active.
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
};

void FCEUX_EnableScanlineTrace();
void FCEUX_DisableScanlineTrace();
bool FCEUX_ScanlineTraceActive();
void FCEUX_CaptureBeginScanline(int sl);
void FCEUX_CaptureScanlineTrace(int sl);
void FCEUX_ExportScanlineTrace(const char* outpath);
const FCEUXScanlineTrace* FCEUX_GetScanlineTrace();
