// fceux_dump.h - FCEUX side of the NESDUMP state dump (see common/nesdump.h, doc/state_dump.md).
// Plain C++ interface so the CPU/PPU/frame code can call it without pulling in nesdump.h.
#pragma once

#include "types.h"

extern int   g_nesdump_active;     // non-zero while a --dump run is active
extern int   g_nesdump_insn_hook;  // non-zero while the per-instruction hook must run
extern uint8 g_nesdump_apu[0x18];  // last value written to $4000-$4017

// Command line: "<exe> <rom.nes> --dump <outdir> [options]".
bool        NesDump_Requested(int argc, char** argv);
bool        NesDump_Open(int argc, char** argv, char* errbuf, int errlen);  // false on bad args
const char* NesDump_RomPath();
const char* NesDump_MoviePath();
int         NesDump_Frames();
uint32      NesDump_Input(int frame);
bool        NesDump_FourScore();
int         NesDump_InputPort(int port);
void        NesDump_Close();

// Event hooks, called from the emulation core while g_nesdump_active.
void NesDump_BeginFrame();            // start of FCEUI_Emulate's frame
void NesDump_EndFrame();              // frame fully emulated
void NesDump_BeginScanline(int sl);   // CPU window of visible scanline sl starts
int  NesDump_Frame();                 // current dump frame number (debug aid)
void NesDump_SliceEnd(int k);         // after each of the 4 CPU slices of a visible line (debug aid)
void NesDump_EndScanline(int sl);     // all work for visible scanline sl is done
void NesDump_Insn();                  // before every instruction executes
void NesDump_InitPC();                // apply --init-pc (after the game is loaded)
