// fceux_dump.cpp - FCEUX side of the NESDUMP state dump.
//
// Fills the shared nesdump::State (common/nesdump.h) from FCEUX's globals.
// Keep this include first: it must see the standard headers before FCEUX's macros.
#include "../../../common/nesdump.h"

#include "types.h"
#include "x6502.h"
#include "x6502struct.h"
#include "fceu.h"
#include "cart.h"
#include "fceux_dump.h"

// FCEUX globals (ppu.cpp, fceu.cpp)
extern uint8  PPU[4];
extern uint8  NTARAM[0x800];
extern uint8  PALRAM[0x20];
extern uint8  SPRAM[0x100];
extern uint32 RefreshAddr;
extern uint32 TempAddr;
extern uint8  XOffset;
extern uint8  vtoggle;
extern uint8  VRAMBuffer;
extern uint8 *vnapage[4];
extern uint8 *RAM;
extern uint64 timestampbase;

int   g_nesdump_active   = 0;
int   g_nesdump_pc_watch = 0;
uint8 g_nesdump_apu[0x18] = { 0 };

static nesdump::Config   g_cfg;
static nesdump::Recorder g_rec;
static int g_frame = 0;
static int g_scanline = -1;

bool NesDump_Requested(int argc, char** argv) { return nesdump::Config::requested(argc, argv); }

bool NesDump_Open(int argc, char** argv, char* errbuf, int errlen)
{
	std::string err;
	if (!g_cfg.parse(argc, argv, err)) {
		snprintf(errbuf, errlen, "%s\n%s", err.c_str(), nesdump::Config::usage());
		return false;
	}
	if (!g_rec.open(g_cfg, "fceux")) {
		snprintf(errbuf, errlen, "cannot create output directory %s", g_cfg.outdir.c_str());
		return false;
	}
	g_nesdump_active = 1;
	g_nesdump_pc_watch = g_rec.any_pc_watch() ? 1 : 0;
	return true;
}

const char* NesDump_RomPath() { return g_cfg.rom.c_str(); }
int         NesDump_Frames()  { return g_cfg.frames; }

// CPU-visible PRG ($8000-$FFFF) and PPU CHR ($0000-$1FFF) through FCEUX's page tables.
static uint8 g_prg[0x8000];
static uint8 g_chr[0x2000];

static void fill(nesdump::State& s)
{
	s.pc = (uint16)X.PC;
	s.a = X.A;  s.x = X.X;  s.y = X.Y;  s.s = X.S;  s.p = X.P;
	s.cycles = timestampbase + timestamp;

	s.ctrl = PPU[0];  s.mask = PPU[1];  s.status = PPU[2];  s.oamaddr = PPU[3];
	s.v = (uint16)RefreshAddr;  s.t = (uint16)TempAddr;
	s.fine_x = XOffset;  s.w = vtoggle;  s.readbuf = VRAMBuffer;
	s.mirror = (vnapage[0] == vnapage[2] && vnapage[0] != vnapage[1]) ? 'V' : 'H';

	memcpy(s.apu, g_nesdump_apu, sizeof(s.apu));
	if (RAM) memcpy(s.ram, RAM, 0x800);
	memcpy(s.nt, NTARAM, 0x800);
	memcpy(s.pal, PALRAM, 0x20);
	memcpy(s.oam, SPRAM, 0x100);

	for (uint32 a = 0x8000; a < 0x10000; ++a) {
		uint8* pg = Page[a >> 11];
		g_prg[a - 0x8000] = pg ? pg[a] : 0xFF;
	}
	for (uint32 a = 0; a < 0x2000; ++a) {
		uint8* pg = VPage[a >> 10];
		g_chr[a] = pg ? pg[a] : 0xFF;
	}
	s.chr = g_chr;
	s.prg = g_prg;
}

static void emit(const char* event, int pc_trigger, int pc_hit)
{
	nesdump::State s;
	memset(&s, 0, sizeof(s));
	s.event = event;
	s.frame = g_frame;
	s.scanline = g_scanline;
	s.pc_trigger = pc_trigger;
	s.pc_hit = pc_hit;
	fill(s);
	g_rec.write_event(s);
}

void NesDump_BeginFrame()
{
	++g_frame;
	g_scanline = -1;
	if (g_frame == 1) {   // ROM is static for NROM: write rom.txt once, before frame 1 runs
		nesdump::State s;
		memset(&s, 0, sizeof(s));
		fill(s);
		g_rec.write_rom(s);
	}
}

void NesDump_EndFrame()
{
	if (g_rec.want_frame(g_frame)) emit("frame", -1, 0);
}

void NesDump_BeginScanline(int sl) { g_scanline = sl; }

void NesDump_EndScanline(int sl)
{
	g_scanline = sl;
	if (g_rec.want_scanline(g_frame, sl)) emit("scanline", -1, 0);
	g_scanline = -1;
}

void NesDump_CheckPC(unsigned pc)
{
	if (g_rec.want_pc((uint16_t)pc, g_frame)) emit("pc", (int)pc, g_rec.pc_hit((uint16_t)pc));
}

void NesDump_Close()
{
	g_rec.close(g_frame);
	g_nesdump_active = 0;
	g_nesdump_pc_watch = 0;
}
