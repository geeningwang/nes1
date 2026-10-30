// statedump.h - nes1 side of the NESDUMP state dump (see common/nesdump.h, doc/state_dump.md).
#pragma once

#include "../common/nesdump.h"

// Driven by the emulation loop in nes1.cpp.  A single instance is published
// through g_dumper while a dump run is active; it is nullptr otherwise.
class StateDumper {
public:
	explicit StateDumper(nesdump::Recorder& r) : rec(r), frame(0), scanline(-1) {}

	void begin_frame()           { ++frame; scanline = -1; rec.begin_frame(frame); extern int g_dbg_frame; g_dbg_frame = frame; }
	void on_instruction();                                                // before each instruction executes
	void on_scanline_end(int sl);                                         // all work for visible scanline sl is done
	void on_frame_end();                                                  // whole frame emulated
	bool write_rom();
	void init_pc(int pc);                                                 // --init-pc

	nesdump::Recorder& rec;
	int frame;      // 1-based
	int scanline;   // visible scanline being emulated, else -1

private:
	void emit(const char* event, int pc_trigger, int pc_hit, int insn_no);
};

extern StateDumper* g_dumper;
