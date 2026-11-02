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
extern uint8  UPALRAM[0x03];   // hidden $3F04/$3F08/$3F0C (PALRAM mirrors $3F00 there for rendering)
extern uint8  SPRAM[0x100];
extern uint32 RefreshAddr;
extern uint32 TempAddr;
extern uint8  XOffset;
extern uint8  vtoggle;
extern uint8  VRAMBuffer;
extern uint8  PPUGenLatch;
extern uint8 *vnapage[4];
extern uint8 *RAM;
extern uint64 timestampbase;
extern uint8 *XBuf;   // 256x256 palette-index frame buffer (video.cpp)

int   g_nesdump_active   = 0;
int   g_nesdump_insn_hook = 0;
uint8 g_nesdump_apu[0x18] = { 0 };
extern void NesDump_ApuState(uint8* out);   // sound.cpp
extern void NesDump_IoState(uint8* out);    // input.cpp

static int g_ov[4] = { 0, 0, 0, 0 };

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
	g_nesdump_insn_hook = 1;   // the chained instruction hash needs every instruction
	return true;
}

int NesDump_Frame() { return g_frame; }
const char* NesDump_RomPath() { return g_cfg.rom.c_str(); }
const char* NesDump_MoviePath() { return g_cfg.movie_file.c_str(); }
int         NesDump_Frames()  { return g_cfg.frames; }
uint32      NesDump_Input(int frame) { return g_cfg.input_at(frame); }
int         NesDump_Command(int frame) { return g_cfg.command_at(frame); }
bool        NesDump_FourScore() { return g_cfg.input.fourscore; }
int         NesDump_InputPort(int port) { return g_cfg.input.ports[port]; }

// CPU-visible PRG ($8000-$FFFF) and PPU CHR ($0000-$1FFF) through FCEUX's page tables.
static uint8 g_prg[0x8000];
static uint8 g_chr[0x2000];

static void fill(nesdump::State& s)
{
	s.pc = (uint16)X.PC;
	s.a = X.A;  s.x = X.X;  s.y = X.Y;  s.s = X.S;  s.p = X.P;
	s.cycles = timestampbase + timestamp;

	for (int k = 0; k < 4; ++k) s.ov[k] = g_ov[k];
	s.db = X.DB;
	s.ppu_latch = PPUGenLatch;
	s.ctrl = PPU[0];  s.mask = PPU[1];  s.status = PPU[2];  s.oamaddr = PPU[3];
	s.v = (uint16)RefreshAddr;  s.t = (uint16)TempAddr;
	s.fine_x = XOffset;  s.w = vtoggle;  s.readbuf = VRAMBuffer;
	s.mirror = (vnapage[0] == vnapage[2] && vnapage[0] != vnapage[1]) ? 'V' : 'H';

	memcpy(s.apu, g_nesdump_apu, sizeof(s.apu));
	NesDump_ApuState(s.apuz);
	if (RAM) memcpy(s.ram, RAM, 0x800);
	for (uint32 a = 0x6000; a < 0x8000; ++a) {
		uint8* pg = Page[a >> 11];
		s.wram[a - 0x6000] = pg ? pg[a] : 0xFF;
	}
	memcpy(s.nt, NTARAM, 0x800);
	for (uint32 a = 0; a < 0x1000; ++a) {
		uint8* pg = vnapage[a >> 10];
		s.ntv[a] = pg ? pg[a & 0x3FF] : 0xFF;
	}
	NesDump_IoState(s.io);
	{
		// FCEUX's NMI request is delayed in two steps (IQNMI2 -> IQNMI); report them as armed / due.
		const uint32 q = X.IRQlow;
		s.irq[0] = (q & FCEU_IQNMI) ? 2 : (q & FCEU_IQNMI2) ? 1 : 0;
		s.irq[1] = (q & ~(uint32)(FCEU_IQNMI | FCEU_IQNMI2 | FCEU_IQRESET | FCEU_IQTEMP)) ? 1 : 0;
		s.irq[2] = X.jammed ? 1 : 0;
	}
	// Architectural palette RAM: $3F04/$3F08/$3F0C are separate entries, kept in UPALRAM.
	memcpy(s.pal, PALRAM, 0x20);
	s.pal[0x04] = UPALRAM[0];  s.pal[0x08] = UPALRAM[1];  s.pal[0x0C] = UPALRAM[2];
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
	s.fb  = XBuf;
}

static void emit(const char* event, int pc_trigger, int pc_hit, int insn_no)
{
	nesdump::State s;
	memset(&s, 0, sizeof(s));
	s.event = event;
	s.frame = g_frame;
	s.scanline = g_scanline;
	s.pc_trigger = pc_trigger;
	s.pc_hit = pc_hit;
	s.insn_no = insn_no;
	fill(s);
	g_rec.write_event(s);
}

void NesDump_BeginFrame()
{
	++g_frame;
	g_scanline = -1;
	g_rec.begin_frame(g_frame);
	if (g_frame == 1) {   // ROM is static for NROM: write rom.txt once, before frame 1 runs
		nesdump::State s;
		memset(&s, 0, sizeof(s));
		fill(s);
		g_rec.write_rom(s);
	}
}

void NesDump_EndFrame()
{
	if (g_rec.want_frame(g_frame)) emit("frame", -1, 0, 0);
	g_rec.end_frame();
}

void NesDump_BeginScanline(int sl) { g_scanline = sl; }

// Called after each of the four X6502_Run() slices of a visible line; X.count is the leftover budget
// in 1/16 dot (<= 0 after a run).
void NesDump_SliceEnd(int k) { g_ov[k] = (int)(-(int32)X.count / 16); }

void NesDump_EndScanline(int sl)
{
	g_scanline = sl;
	if (g_rec.want_scanline(g_frame, sl)) emit("scanline", -1, 0, 0);
	g_scanline = -1;
}

// The opcode at PC as the contract defines it: RAM mirrors below $2000, the page tables above $8000.
static uint8 opcode_at(unsigned pc)
{
	if (pc < 0x2000) return RAM ? RAM[pc & 0x7FF] : 0xFF;
	if (pc >= 0x8000) { uint8* pg = Page[pc >> 11]; return pg ? pg[pc] : 0xFF; }
	return 0xFF;
}

void NesDump_Insn()
{
	nesdump::Insn i;
	i.pc = (uint16_t)X.PC;  i.op = opcode_at(X.PC);
	i.a = X.A;  i.x = X.X;  i.y = X.Y;  i.s = X.S;  i.p = X.P;  i.db = X.DB;  i.pl = PPUGenLatch;
	i.cycles = timestampbase + timestamp;
	i.frame = g_frame;
	i.scanline = g_scanline;
	int r = g_rec.insn(i);
	if (r & 1) emit("pc", i.pc, g_rec.pc_hit(i.pc), 0);
	if (r & 2) emit("insn", -1, 0, g_rec.insn_number());
}

void NesDump_InitPC()
{
	if (g_cfg.init_pc < 0) return;
	X.PC = (uint16)g_cfg.init_pc;
	X.IRQlow &= ~FCEU_IQRESET;   // do not take the reset vector on the first instruction
	X.P = 0x24;                  // same power-on flags as nes1's reset (nestest start state)
	X.S = 0xFD;
}

void NesDump_Close()
{
	g_rec.close(g_frame);
	g_nesdump_active = 0;
	g_nesdump_insn_hook = 0;
}
