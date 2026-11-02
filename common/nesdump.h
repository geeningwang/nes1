// nesdump.h - shared by nes1 and FCEUX.
//
// Header-only, no emulator dependencies.  Defines the plain-text "NESDUMP 3"
// state-dump format, the digest format, the per-instruction trace, the command
// line and the trigger logic, so both emulators produce byte-comparable output.
// Each emulator fills a nesdump::State and calls Recorder::write_event(), and
// calls Recorder::insn() before every instruction.  See doc/state_dump.md and
// doc/parity_spec.md.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <climits>
#include "nesinput.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace nesdump {

static const int kScanlines = 240;   // visible scanlines 0..239

// fopen("wb") that also satisfies MSVC's deprecation errors (/sdl).
inline FILE* open_wb(const char* path)
{
#ifdef _MSC_VER
	FILE* f = nullptr;
	return fopen_s(&f, path, "wb") == 0 ? f : nullptr;
#else
	return fopen(path, "wb");
#endif
}

// Everything that is written to one event file.  The pointers must stay valid
// for the duration of write_event().
struct State {
	const char* event;        // "frame", "scanline" or "pc"
	int         frame;        // 1-based emulated frame (frames 1-2 are the ppudead frames)
	int         scanline;     // 0..239 while a visible scanline is being emulated, else -1
	int         pc_trigger;   // PC value that fired a "pc" event, else -1
	int         pc_hit;       // 1-based hit ordinal for that PC, else 0
	int         insn_no;      // 1-based instruction number for "insn" events, else 0

	uint16_t pc;
	uint8_t  a, x, y, s, p;
	uint64_t cycles;          // total CPU cycles since power-on

	uint8_t  ctrl, mask, status, oamaddr;
	uint16_t v, t;            // Loopy V and T
	uint8_t  fine_x, w, readbuf;
	char     mirror;          // 'H' or 'V'

	int      ov[4];           // dots by which the CPU ran past the end of each of the line's 4 time slices (debug aid)
	uint8_t  db;              // CPU data-bus latch (value of the last bus access)
	uint8_t  ppu_latch;       // PPU I/O latch ("open bus" of the PPU registers)

	uint8_t  apu[0x18];       // last value written to $4000-$4017 (0 if never written)
	uint8_t  apuz[0x20];      // CPU-visible APU internals, see doc/parity_spec.md "APUX layout"

	uint8_t  ram[0x800];      // CPU RAM $0000-$07FF
	uint8_t  wram[0x2000];    // cartridge work RAM, CPU $6000-$7FFF
	uint8_t  nt[0x800];       // physical nametable RAM: page A (0-3FF), page B (400-7FF)
	uint8_t  ntv[0x1000];     // logical nametables $2000-$2FFF as the PPU addresses them (after mirroring)
	uint8_t  io[0x10];        // controller serial state, see doc/state_dump.md "IO layout"
	uint8_t  irq[8];          // pending interrupt state, see doc/state_dump.md "IRQ layout"
	uint8_t  pal[0x20];       // palette RAM
	uint8_t  oam[0x100];      // sprite RAM
	const uint8_t* chr;       // 0x2000 bytes, PPU $0000-$1FFF
	const uint8_t* prg;       // 0x8000 bytes, CPU view $8000-$FFFF
	const uint8_t* fb;        // kFbBytes palette-index pixels; only the finished rows are used
};

// One CPU instruction, reported just before it executes.
struct Insn {
	uint16_t pc;
	uint8_t  op, a, x, y, s, p;
	uint8_t  db;              // data-bus latch before the instruction
	uint8_t  pl;              // PPU I/O latch before the instruction
	uint64_t cycles;          // total CPU cycles before this instruction's cost is added
	int      frame;
	int      scanline;        // visible scanline or -1
};

// Fast 64-bit hash (8 bytes per step, little-endian words).  Not cryptographic; used to
// compare state between the two emulators, which always run on the same machine.
inline uint64_t mix64(uint64_t h, uint64_t w)
{
	h ^= w;
	h *= 0x9E3779B97F4A7C15ULL;
	h ^= h >> 32;
	return h;
}

inline uint64_t hash64(const void* data, size_t n, uint64_t seed = 0)
{
	const uint8_t* p = static_cast<const uint8_t*>(data);
	uint64_t h = mix64(0xCBF29CE484222325ULL ^ seed, (uint64_t)n);
	while (n >= 8) {
		uint64_t w;
		memcpy(&w, p, 8);
		h = mix64(h, w);
		p += 8;
		n -= 8;
	}
	uint64_t w = 0;
	for (size_t i = 0; i < n; ++i) w |= (uint64_t)p[i] << (8 * i);
	return mix64(h, w);
}

// Architectural view of the status register: bit 5 reads 1 and bit 4 (B) is not a real flag, so both
// emulators report the same value regardless of how they store those two bits (doc/parity_spec.md).
inline uint8_t norm_p(uint8_t p) { return (uint8_t)((p & 0xEF) | 0x20); }

// The 256x240 frame buffer holds one byte per pixel; rows are 256 bytes.
static const int kFbWidth = 256;
static const int kFbHeight = 240;
static const int kFbBytes = kFbWidth * kFbHeight;

// ---- range lists: "1-10,30,60-120/30,all" ------------------------------------
class RangeList {
public:
	bool parse(const std::string& spec, std::string& err)
	{
		items_.clear();
		size_t pos = 0;
		while (pos <= spec.size()) {
			size_t end = spec.find(',', pos);
			if (end == std::string::npos) end = spec.size();
			std::string tok = spec.substr(pos, end - pos);
			pos = end + 1;
			if (tok.empty()) continue;
			Item it = { 0, INT_MAX, 1 };
			if (tok != "all") {
				size_t slash = tok.find('/');
				std::string range = tok.substr(0, slash);
				if (slash != std::string::npos) it.step = atoi(tok.c_str() + slash + 1);
				size_t dash = range.find('-');
				if (!is_num(range.substr(0, dash))) { err = "bad range '" + tok + "'"; return false; }
				it.lo = atoi(range.c_str());
				if (dash == std::string::npos) it.hi = it.lo;
				else {
					if (!is_num(range.substr(dash + 1))) { err = "bad range '" + tok + "'"; return false; }
					it.hi = atoi(range.c_str() + dash + 1);
				}
				if (it.step < 1 || it.hi < it.lo) { err = "bad range '" + tok + "'"; return false; }
			}
			items_.push_back(it);
		}
		return true;
	}
	bool contains(int n) const
	{
		for (size_t i = 0; i < items_.size(); ++i) {
			const Item& it = items_[i];
			if (n >= it.lo && n <= it.hi && (n - it.lo) % it.step == 0) return true;
		}
		return false;
	}
	bool empty() const { return items_.empty(); }
private:
	struct Item { int lo, hi, step; };
	static bool is_num(const std::string& s)
	{
		if (s.empty()) return false;
		for (size_t i = 0; i < s.size(); ++i) if (s[i] < '0' || s[i] > '9') return false;
		return true;
	}
	std::vector<Item> items_;
};

// ---- command line ------------------------------------------------------------
//   <exe> <rom.nes> --dump <outdir> [options]
struct Config {
	std::string rom;
	std::string outdir;
	int         frames = 300;       // number of frames to emulate
	RangeList   dump_frames;        // frames that get an end-of-frame dump
	RangeList   scan_frames;        // frames that get end-of-scanline dumps
	RangeList   scanlines;          // which scanlines in those frames (default 0-239)
	RangeList   pc_frames;          // frames in which PC triggers are armed (default all)
	std::vector<uint16_t> pcs;      // PC trigger addresses
	int         pc_max = 16;        // max dumps per PC address
	bool        rom_every = false;  // include PRG+CHR in every dump instead of only rom.txt
	bool        digest = false;     // one hash line per event (digest.txt) instead of one file per event
	RangeList   trace_frames;       // frames for which a text instruction trace is written
	RangeList   dump_insns;         // global instruction numbers (1-based) that get a full dump
	int         init_pc = -1;       // start execution at this address instead of the reset vector
	std::string input_file;         // --input FILE: controller state per frame (see input_at)
	std::string movie_file;         // FCEUX-only native FM2 playback, used to validate the converter
	InputFile input;

	// Controller bits for the 1-based emulated frame (A=1 B=2 Select=4 Start=8 Up=0x10 Down=0x20 Left=0x40 Right=0x80,
	// Pads 1..4 occupy successive bytes. Legacy EOF is neutral; versioned runs reject early EOF.
	uint32_t input_at(int frame) const
	{
		return input.at(frame).pads;
	}

	// Movie command bits for the frame (1 = soft reset, 2 = power cycle), applied before the frame runs, power first.
	uint8_t command_at(int frame) const
	{
		return input.at(frame).commands;
	}

	// Legacy "P1 P2" hex bytes or the versioned NESINPUT 1 contract in doc/input_replay.md.
	bool load_input(std::string& err)
	{
		return input.load(input_file, err);
	}
	std::string cmdline;            // echoed to run.txt

	// Returns true if argv requests a dump run (contains --dump).
	static bool requested(int argc, char** argv)
	{
		for (int i = 1; i < argc; ++i) if (strcmp(argv[i], "--dump") == 0) return true;
		return false;
	}

	static const char* usage()
	{
		return
			"Usage: <exe> <rom.nes> --dump <outdir> [options]\n"
			"  --frames N                  frames to emulate (default 300)\n"
			"  --dump-frames LIST          end-of-frame dumps, e.g. 1-10,60,120-600/60\n"
			"  --dump-scanline-frames LIST frames that get end-of-scanline dumps\n"
			"  --dump-scanlines LIST       scanlines 0-239 to dump in those frames (default all)\n"
			"  --dump-pc ADDR[,ADDR...]    hex PCs; dump before the instruction at PC executes\n"
			"  --pc-frames LIST            frames in which PC triggers are armed (default all)\n"
			"  --pc-max N                  max dumps per PC address (default 16)\n"
			"  --rom-every                 include PRG in every dump (default: rom.txt only)\n"
			"  --digest                    write digest.txt (one hash line per event) instead of files\n"
			"  --trace-frames LIST         write trace_fNNNNNN.txt (every instruction) for these frames\n"
			"  --dump-insn LIST            full dump before these global instruction numbers (1-based)\n"
			"  --init-pc ADDR              hex; start at ADDR instead of the reset vector (e.g. C000 for nestest)\n"
			"  --input FILE                controller state per frame: lines 'P1 P2' (hex bytes, A=01 B=02 Sel=04 Start=08 U=10 D=20 L=40 R=80)\n"
			"  --movie FILE                FCEUX only: native FM2 playback (converter verification)\n"
			"LIST = N | A-B | A-B/STEP | all, comma separated (inclusive, 1-based frames).\n";
	}

	bool parse(int argc, char** argv, std::string& err)
	{
		std::string err2;
		RangeList all;
		all.parse("all", err2);
		pc_frames = all;
		scanlines.parse("0-239", err2);
		rom.clear();
		for (int i = 0; i < argc; ++i) { if (i) cmdline += ' '; cmdline += argv[i]; }

		for (int i = 1; i < argc; ++i) {
			std::string a = argv[i];
			auto need = [&](std::string& out) -> bool {
				if (i + 1 >= argc) { err = "missing value for " + a; return false; }
				out = argv[++i];
				return true;
			};
			std::string v;
			if (a == "--dump")                      { if (!need(outdir)) return false; }
			else if (a == "--frames")               { if (!need(v)) return false; frames = atoi(v.c_str()); }
			else if (a == "--dump-frames")          { if (!need(v) || !dump_frames.parse(v, err)) return false; }
			else if (a == "--dump-scanline-frames") { if (!need(v) || !scan_frames.parse(v, err)) return false; }
			else if (a == "--dump-scanlines")       { if (!need(v) || !scanlines.parse(v, err)) return false; }
			else if (a == "--pc-frames")            { if (!need(v) || !pc_frames.parse(v, err)) return false; }
			else if (a == "--pc-max")               { if (!need(v)) return false; pc_max = atoi(v.c_str()); }
			else if (a == "--rom-every")            { rom_every = true; }
			else if (a == "--digest")               { digest = true; }
			else if (a == "--trace-frames")         { if (!need(v) || !trace_frames.parse(v, err)) return false; }
			else if (a == "--dump-insn")            { if (!need(v) || !dump_insns.parse(v, err)) return false; }
			else if (a == "--input") {
				if (!input_file.empty()) { err = "duplicate --input option"; return false; }
				if (!need(v)) return false;
				input_file = v;
				if (!load_input(err)) return false;
			}
			else if (a == "--movie") {
				if (!movie_file.empty()) { err = "duplicate --movie option"; return false; }
				if (!need(movie_file)) return false;
			}
			else if (a == "--init-pc") {
				if (!need(v)) return false;
				char* ep = nullptr;
				unsigned long val = strtoul(v.c_str(), &ep, 16);
				if (*ep || v.empty() || val > 0xFFFF) { err = "bad --init-pc '" + v + "'"; return false; }
				init_pc = (int)val;
			}
			else if (a == "--dump-pc") {
				if (!need(v)) return false;
				size_t pos = 0;
				while (pos <= v.size()) {
					size_t end = v.find(',', pos);
					if (end == std::string::npos) end = v.size();
					std::string tok = v.substr(pos, end - pos);
					pos = end + 1;
					if (tok.empty()) continue;
					char* ep = nullptr;
					unsigned long val = strtoul(tok.c_str(), &ep, 16);
					if (*ep || val > 0xFFFF) { err = "bad PC '" + tok + "'"; return false; }
					pcs.push_back((uint16_t)val);
				}
			}
			else if (!a.empty() && a[0] == '-') { err = "unknown option " + a; return false; }
			else if (rom.empty())               { rom = a; }
			else { err = "unexpected argument " + a; return false; }
		}
		if (rom.empty())    { err = "no ROM given"; return false; }
		if (outdir.empty()) { err = "no output directory given"; return false; }
		if (frames < 1)     { err = "--frames must be >= 1"; return false; }
		if (!movie_file.empty() && (!input_file.empty() || init_pc >= 0)) {
			err = "--movie cannot be combined with --input or --init-pc"; return false;
		}
		if (!input_file.empty() && (!input.validate_rom(rom, err) || !input.validate_run(frames, err))) return false;
		return true;
	}
};

// ---- writer + trigger bookkeeping ------------------------------------------------
class Recorder {
public:
	Recorder() : seq_(0), insn_total_(0), frame_insn_(0), frame_hash_(0), watch_(65536, 0), hits_(65536, 0)
	{
		counts_[0] = counts_[1] = counts_[2] = counts_[3] = 0;
	}

	bool open(const Config& cfg, const char* emulator)
	{
		cfg_ = cfg;
		emulator_ = emulator;
		make_dir(cfg_.outdir);
		for (size_t i = 0; i < cfg_.pcs.size(); ++i) watch_[cfg_.pcs[i]] = 1;
		std::string path = join(cfg_.digest ? "digest.txt" : "events.txt");
		events_ = open_wb(path.c_str());
		if (!events_) { fprintf(stderr, "cannot create %s\n", path.c_str()); return false; }
		if (cfg_.digest) fputs("NESDIGEST 3\n", events_);
		return true;
	}

	const Config& config() const { return cfg_; }

	// ---- frame / instruction hooks -------------------------------------------
	void begin_frame(int frame)
	{
		frame_insn_ = 0;
		frame_hash_ = 0;
		if (cfg_.trace_frames.contains(frame)) {
			char name[64];
			snprintf(name, sizeof(name), "trace_f%06d.txt", frame);
			trace_ = open_wb(join(name).c_str());
			if (trace_) fputs("# PC OP A X Y S P CYCLES SCANLINE DB PL\n", trace_);
		}
	}
	void end_frame()
	{
		if (trace_) { fclose(trace_); trace_ = nullptr; }
	}

	// Called before every instruction.  Folds it into the frame's chained hash, writes the
	// text trace if enabled, and tells the caller whether to emit a dump:
	// returns bit 0 = a --dump-pc trigger fired, bit 1 = a --dump-insn number was reached.
	int insn(const Insn& i)
	{
		++insn_total_;
		++frame_insn_;
		const uint8_t p = norm_p(i.p);
		uint64_t w = (uint64_t)i.pc | ((uint64_t)i.op << 16) | ((uint64_t)i.a << 24) | ((uint64_t)i.x << 32) |
		             ((uint64_t)i.y << 40) | ((uint64_t)i.s << 48) | ((uint64_t)p << 56);
		frame_hash_ = mix64(mix64(mix64(mix64(frame_hash_, w), i.cycles), i.db), i.pl);
		if (trace_)
			fprintf(trace_, "%04X %02X %02X %02X %02X %02X %02X %llu %d %02X %02X\n", i.pc, i.op, i.a, i.x, i.y, i.s, p,
			        (unsigned long long)i.cycles, i.scanline, i.db, i.pl);
		int r = 0;
		if (!cfg_.pcs.empty() && want_pc(i.pc, i.frame)) r |= 1;
		if (!cfg_.dump_insns.empty() && cfg_.dump_insns.contains((int)insn_total_)) r |= 2;
		return r;
	}
	int insn_number() const { return (int)insn_total_; }

	// ---- trigger queries ---------------------------------------------------
	// Counts the hit and returns true if this execution of 'pc' should be dumped.
	bool want_pc(uint16_t pc, int frame)
	{
		if (!watch_[pc] || !cfg_.pc_frames.contains(frame)) return false;
		if (hits_[pc] >= cfg_.pc_max) return false;
		++hits_[pc];
		return true;
	}
	int pc_hit(uint16_t pc) const { return hits_[pc]; }
	bool want_frame(int frame) const { return cfg_.dump_frames.contains(frame); }
	bool want_scanline(int frame, int sl) const
	{
		return cfg_.scan_frames.contains(frame) && cfg_.scanlines.contains(sl);
	}

	// ---- output ------------------------------------------------------------
	bool write_rom(const State& s)
	{
		FILE* f = open_wb(join("rom.txt").c_str());
		if (!f) return false;
		fprintf(f, "NESROM 3\n");
		fprintf(f, "PRG_HASH=%016llX\nCHR_HASH=%016llX\n", (unsigned long long)hash64(s.prg, 0x8000),
		        (unsigned long long)hash64(s.chr, 0x2000));
		put_hex(f, "PRG", 0x8000, s.prg, 0x8000);
		put_hex(f, "CHR", 0x0000, s.chr, 0x2000);
		fprintf(f, "END\n");
		fclose(f);
		return true;
	}

	// Number of finished frame-buffer rows that an event may show.  Rows that are not finished
	// are written/hashed as 00 so that stale data from the previous frame never matters.
	static int fb_rows(const State& s)
	{
		if (!strcmp(s.event, "frame")) return kFbHeight;
		if (!strcmp(s.event, "scanline")) return s.scanline + 1;
		return s.scanline > 0 ? s.scanline : 0;   // pc / insn events: rows before the current one
	}

	bool write_event(const State& s)
	{
		char name[96];
		if (!strcmp(s.event, "frame"))
			snprintf(name, sizeof(name), "frame_f%06d.txt", s.frame);
		else if (!strcmp(s.event, "scanline"))
			snprintf(name, sizeof(name), "scanline_f%06d_s%03d.txt", s.frame, s.scanline);
		else if (!strcmp(s.event, "insn"))
			snprintf(name, sizeof(name), "insn_n%09d", s.insn_no);
		else
			snprintf(name, sizeof(name), "pc_%04X_n%04d", s.pc_trigger, s.pc_hit);
		std::string fname = name;
		if (fname.size() < 4 || fname.compare(fname.size() - 4, 4, ".txt") != 0) fname += ".txt";

		// Palette is canonicalised: $3F10/14/18/1C alias $3F00/04/08/0C, and entries are 6 bits.
		uint8_t pal[0x20];
		for (int i = 0; i < 0x20; ++i)
			pal[i] = (uint8_t)((((i & 0x13) == 0x10) ? s.pal[i & 0x0F] : s.pal[i]) & 0x3F);
		const int rows = fb_rows(s);
		std::vector<uint8_t> fb(kFbBytes, 0);
		if (s.fb && rows > 0) memcpy(fb.data(), s.fb, (size_t)rows * kFbWidth);

		if (cfg_.digest) {
			write_digest(s, fname, pal, fb.data(), rows);
		} else if (!write_full(s, fname, pal, fb.data())) {
			return false;
		}
		if (!strcmp(s.event, "frame")) ++counts_[0];
		else if (!strcmp(s.event, "scanline")) ++counts_[1];
		else if (!strcmp(s.event, "insn")) ++counts_[3];
		else ++counts_[2];
		return true;
	}

	void close(int frames_run)
	{
		if (events_) { fclose(events_); events_ = nullptr; }
		if (trace_) { fclose(trace_); trace_ = nullptr; }
		FILE* f = open_wb(join("run.txt").c_str());
		if (!f) return;
		fprintf(f, "format: %s\n", cfg_.digest ? "NESDIGEST 3" : "NESDUMP 3");
		fprintf(f, "emulator: %s\n", emulator_.c_str());
		fprintf(f, "cmdline: %s\n", cfg_.cmdline.c_str());
		fprintf(f, "frames_run: %d\n", frames_run);
		fprintf(f, "input_file: %s\n", cfg_.input_file.empty() ? "(none)" : cfg_.input_file.c_str());
		fprintf(f, "native_movie: %s\n", cfg_.movie_file.empty() ? "(none)" : cfg_.movie_file.c_str());
		fprintf(f, "input_frames_supplied: %llu\n", (unsigned long long)cfg_.input.frames.size());
		fprintf(f, "input_frames_applied: %llu\n", (unsigned long long)(
			cfg_.input.frames.size() < (size_t)frames_run ? cfg_.input.frames.size() : (size_t)frames_run));
		fprintf(f, "input_eof: %s\n", cfg_.input.versioned ? "error" : "neutral");
		fprintf(f, "input_fourscore: %d\ninput_ports: %d,%d,%d\n", cfg_.input.fourscore ? 1 : 0,
			cfg_.input.ports[0], cfg_.input.ports[1], cfg_.input.ports[2]);
		fprintf(f, "instructions: %llu\n", (unsigned long long)insn_total_);
		fprintf(f, "events: frame=%d scanline=%d pc=%d insn=%d\n", counts_[0], counts_[1], counts_[2], counts_[3]);
		for (size_t i = 0; i < cfg_.pcs.size(); ++i)
			fprintf(f, "pc_hits %04X: %d\n", cfg_.pcs[i], hits_[cfg_.pcs[i]]);
		fclose(f);
	}

private:
	static void le(uint8_t* b, int& n, uint64_t v, int bytes) { for (int i = 0; i < bytes; ++i) b[n++] = (uint8_t)(v >> (8 * i)); }

	struct Sections { uint64_t cpu, ppu, bus, apu, ram, wram, nt, ntv, pal, oam, chr, prg, fb, io, irq, insn, all; };

	Sections hash_sections(const State& s, const uint8_t* pal, const uint8_t* fb, int rows) const
	{
		Sections h;
		uint8_t b[32];
		int n = 0;
		le(b, n, s.pc, 2); le(b, n, s.a, 1); le(b, n, s.x, 1); le(b, n, s.y, 1); le(b, n, s.s, 1); le(b, n, norm_p(s.p), 1);
		le(b, n, s.cycles, 8);
		h.cpu = hash64(b, (size_t)n);
		n = 0;
		le(b, n, s.ctrl, 1); le(b, n, s.mask, 1); le(b, n, s.status, 1); le(b, n, s.oamaddr, 1);
		le(b, n, s.v & 0x7FFF, 2); le(b, n, s.t & 0x7FFF, 2);
		le(b, n, s.fine_x, 1); le(b, n, s.w, 1); le(b, n, s.readbuf, 1); le(b, n, (uint8_t)s.mirror, 1);
		h.ppu = hash64(b, (size_t)n);
		n = 0;
		le(b, n, s.db, 1); le(b, n, s.ppu_latch, 1);
		h.bus = hash64(b, (size_t)n);
		{
			uint8_t ab[sizeof(s.apu) + sizeof(s.apuz)];
			memcpy(ab, s.apu, sizeof(s.apu));
			memcpy(ab + sizeof(s.apu), s.apuz, sizeof(s.apuz));
			h.apu = hash64(ab, sizeof(ab));
		}
		h.ram = hash64(s.ram, sizeof(s.ram));
		h.wram = hash64(s.wram, sizeof(s.wram));
		h.nt  = hash64(s.nt, sizeof(s.nt));
		h.ntv = hash64(s.ntv, sizeof(s.ntv));
		h.io  = hash64(s.io, sizeof(s.io));
		h.irq = hash64(s.irq, sizeof(s.irq));
		h.pal = hash64(pal, 0x20);
		h.oam = hash64(s.oam, sizeof(s.oam));
		h.chr = hash64(s.chr, 0x2000);
		h.prg = hash64(s.prg, 0x8000);
		h.fb  = hash64(fb, (size_t)rows * kFbWidth, (uint64_t)rows);
		h.insn = mix64(frame_hash_, (uint64_t)frame_insn_);
		uint64_t all = 0;
		const uint64_t parts[] = { h.cpu, h.ppu, h.bus, h.apu, h.ram, h.wram, h.nt, h.ntv, h.pal, h.oam, h.chr, h.prg, h.fb, h.io, h.irq, h.insn };
		for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) all = mix64(all, parts[i]);
		h.all = all;
		return h;
	}

	void write_digest(const State& s, const std::string& fname, const uint8_t* pal, const uint8_t* fb, int rows)
	{
		Sections h = hash_sections(s, pal, fb, rows);
		fprintf(events_,
		        "%06d %s f=%d sl=%d pc=%04X cyc=%llu v=%04X st=%02X ov=%d,%d,%d,%d ALL=%016llX CPU=%016llX PPU=%016llX BUS=%016llX APU=%016llX RAM=%016llX "
		        "WRAM=%016llX NT=%016llX NTV=%016llX PAL=%016llX OAM=%016llX CHR=%016llX PRG=%016llX FB=%016llX IO=%016llX IRQ=%016llX "
		        "INSN=%016llX/%u\n",
		        ++seq_, fname.c_str(), s.frame, s.scanline, s.pc, (unsigned long long)s.cycles, s.v & 0x7FFF, s.status, s.ov[0], s.ov[1], s.ov[2], s.ov[3],
		        (unsigned long long)h.all, (unsigned long long)h.cpu, (unsigned long long)h.ppu,
		        (unsigned long long)h.bus, (unsigned long long)h.apu, (unsigned long long)h.ram,
		        (unsigned long long)h.wram, (unsigned long long)h.nt, (unsigned long long)h.ntv,
		        (unsigned long long)h.pal, (unsigned long long)h.oam, (unsigned long long)h.chr,
		        (unsigned long long)h.prg, (unsigned long long)h.fb, (unsigned long long)h.io, (unsigned long long)h.irq,
		        (unsigned long long)frame_hash_mix(), (unsigned)frame_insn_);
	}
	uint64_t frame_hash_mix() const { return mix64(frame_hash_, (uint64_t)frame_insn_); }

	bool write_full(const State& s, const std::string& fname, const uint8_t* pal, const uint8_t* fb)
	{
		FILE* f = open_wb(join(fname).c_str());
		if (!f) { fprintf(stderr, "cannot create %s\n", fname.c_str()); return false; }

		fprintf(f, "NESDUMP 3\n");
		fprintf(f, "event: %s\n", s.event);
		fprintf(f, "frame: %d\n", s.frame);
		fprintf(f, "scanline: %d\n", s.scanline);
		if (s.pc_trigger >= 0) fprintf(f, "pc_trigger: %04X hit=%d\n", s.pc_trigger, s.pc_hit);
		if (s.insn_no > 0) fprintf(f, "insn: %d\n", s.insn_no);
		fprintf(f, "[CPU]\n");
		fprintf(f, "PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X CYCLES=%llu\n",
		        s.pc, s.a, s.x, s.y, s.s, norm_p(s.p), (unsigned long long)s.cycles);
		fprintf(f, "[PPU]\n");
		fprintf(f, "CTRL=%02X MASK=%02X STATUS=%02X OAMADDR=%02X\n", s.ctrl, s.mask, s.status, s.oamaddr);
		fprintf(f, "V=%04X T=%04X FINEX=%d W=%d READBUF=%02X MIRROR=%c\n",
		        s.v & 0x7FFF, s.t & 0x7FFF, s.fine_x, s.w, s.readbuf, s.mirror);
		fprintf(f, "[BUS]\nDB=%02X PPULATCH=%02X\n", s.db, s.ppu_latch);
		put_hex(f, "APU", 0x4000, s.apu, 0x18);
		put_hex(f, "APUX", 0x0000, s.apuz, 0x20);
		put_hex(f, "RAM", 0x0000, s.ram, 0x800);
		put_hex(f, "WRAM", 0x6000, s.wram, 0x2000);
		put_hex(f, "NT", 0x2000, s.nt, 0x800);
		put_hex(f, "NTV", 0x2000, s.ntv, 0x1000);
		put_hex(f, "IO", 0x0000, s.io, 0x10);
		put_hex(f, "IRQ", 0x0000, s.irq, 8);
		put_hex(f, "PAL", 0x3F00, pal, 0x20);
		put_hex(f, "OAM", 0x0000, s.oam, 0x100);
		put_hex(f, "CHR", 0x0000, s.chr, 0x2000);
		put_hex(f, "FB", 0x0000, fb, kFbBytes);
		fprintf(f, "PRG_HASH=%016llX\nCHR_HASH=%016llX\n", (unsigned long long)hash64(s.prg, 0x8000),
		        (unsigned long long)hash64(s.chr, 0x2000));
		fprintf(f, "INSN_HASH=%016llX\nINSN_COUNT=%u\n", (unsigned long long)frame_hash_mix(), (unsigned)frame_insn_);
		if (cfg_.rom_every) put_hex(f, "PRG", 0x8000, s.prg, 0x8000);
		fprintf(f, "END\n");
		fclose(f);

		fprintf(events_, "%06d %s frame=%d scanline=%d pc=%04X cycles=%llu\n",
		        ++seq_, fname.c_str(), s.frame, s.scanline, s.pc, (unsigned long long)s.cycles);
		fflush(events_);
		return true;
	}

	std::string join(const std::string& name) const { return cfg_.outdir + "/" + name; }

	// Creates the directory and any missing parents (errors for existing ones are ignored).
	static void make_dir(const std::string& d)
	{
		for (size_t i = 1; i <= d.size(); ++i) {
			if (i != d.size() && d[i] != '/' && d[i] != '\\') continue;
			std::string part = d.substr(0, i);
#ifdef _WIN32
			_mkdir(part.c_str());
#else
			mkdir(part.c_str(), 0777);
#endif
		}
	}

	// 16 bytes per row, "ADDR: xx xx ..."; one row per line so line diffs point at the address.
	static void put_hex(FILE* f, const char* name, uint32_t base, const uint8_t* d, size_t n)
	{
		fprintf(f, "[%s] %04X-%04X\n", name, (unsigned)base, (unsigned)(base + n - 1));
		std::string out;
		char buf[16];
		for (size_t off = 0; off < n; off += 16) {
			snprintf(buf, sizeof(buf), "%04X:", (unsigned)(base + off));
			out += buf;
			for (size_t i = off; i < off + 16 && i < n; ++i) {
				snprintf(buf, sizeof(buf), " %02X", d[i]);
				out += buf;
			}
			out += '\n';
		}
		fwrite(out.data(), 1, out.size(), f);
	}

	Config                 cfg_;
	std::string            emulator_;
	FILE*                  events_ = nullptr;
	FILE*                  trace_ = nullptr;
	int                    seq_;
	uint64_t               insn_total_;
	uint32_t               frame_insn_;
	uint64_t               frame_hash_;
	std::vector<uint8_t>   watch_;
	std::vector<int>       hits_;
	int                    counts_[4];
};

} // namespace nesdump
