// nesdump.h - shared by nes1 and FCEUX.
//
// Header-only, no emulator dependencies.  Defines the plain-text "NESDUMP 1"
// state-dump format, the command line, and the trigger logic, so both emulators
// produce byte-comparable output.  Each emulator only has to fill a nesdump::State
// and call Recorder::write_event().  See doc/state_dump.md.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <climits>

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

	uint16_t pc;
	uint8_t  a, x, y, s, p;
	uint64_t cycles;          // total CPU cycles since power-on

	uint8_t  ctrl, mask, status, oamaddr;
	uint16_t v, t;            // Loopy V and T
	uint8_t  fine_x, w, readbuf;
	char     mirror;          // 'H' or 'V'

	uint8_t  apu[0x18];       // last value written to $4000-$4017 (0 if never written)

	uint8_t  ram[0x800];      // CPU RAM $0000-$07FF
	uint8_t  nt[0x800];       // physical nametable RAM: page A (0-3FF), page B (400-7FF)
	uint8_t  pal[0x20];       // palette RAM
	uint8_t  oam[0x100];      // sprite RAM
	const uint8_t* chr;       // 0x2000 bytes, PPU $0000-$1FFF
	const uint8_t* prg;       // 0x8000 bytes, CPU view $8000-$FFFF
};

inline uint32_t crc32(const uint8_t* d, size_t n)
{
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; ++i) {
		c ^= d[i];
		for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return ~c;
}

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
			"  --rom-every                 include PRG/CHR in every dump (default: rom.txt only)\n"
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
		return true;
	}
};

// ---- writer + trigger bookkeeping ------------------------------------------------
class Recorder {
public:
	Recorder() : seq_(0), watch_(65536, 0), hits_(65536, 0) { counts_[0] = counts_[1] = counts_[2] = 0; }

	bool open(const Config& cfg, const char* emulator)
	{
		cfg_ = cfg;
		emulator_ = emulator;
		make_dir(cfg_.outdir);
		for (size_t i = 0; i < cfg_.pcs.size(); ++i) watch_[cfg_.pcs[i]] = 1;
		std::string path = join("events.txt");
		events_ = open_wb(path.c_str());
		if (!events_) { fprintf(stderr, "cannot create %s\n", path.c_str()); return false; }
		return true;
	}

	const Config& config() const { return cfg_; }

	// ---- trigger queries (cheap; called per instruction / scanline / frame) ----
	bool any_pc_watch() const { return !cfg_.pcs.empty(); }
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
		fprintf(f, "NESROM 1\n");
		fprintf(f, "PRG_CRC32=%08X\nCHR_CRC32=%08X\n", crc32(s.prg, 0x8000), crc32(s.chr, 0x2000));
		put_hex(f, "PRG", 0x8000, s.prg, 0x8000);
		put_hex(f, "CHR", 0x0000, s.chr, 0x2000);
		fprintf(f, "END\n");
		fclose(f);
		return true;
	}

	bool write_event(const State& s)
	{
		char name[96];
		if (!strcmp(s.event, "frame"))
			snprintf(name, sizeof(name), "frame_f%06d.txt", s.frame);
		else if (!strcmp(s.event, "scanline"))
			snprintf(name, sizeof(name), "scanline_f%06d_s%03d.txt", s.frame, s.scanline);
		else
			snprintf(name, sizeof(name), "pc_%04X_n%04d.txt", s.pc_trigger, s.pc_hit);

		FILE* f = open_wb(join(name).c_str());
		if (!f) { fprintf(stderr, "cannot create %s\n", name); return false; }

		fprintf(f, "NESDUMP 1\n");
		fprintf(f, "event: %s\n", s.event);
		fprintf(f, "frame: %d\n", s.frame);
		fprintf(f, "scanline: %d\n", s.scanline);
		if (s.pc_trigger >= 0) fprintf(f, "pc_trigger: %04X hit=%d\n", s.pc_trigger, s.pc_hit);
		fprintf(f, "[CPU]\n");
		fprintf(f, "PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X CYCLES=%llu\n",
		        s.pc, s.a, s.x, s.y, s.s, s.p, (unsigned long long)s.cycles);
		fprintf(f, "[PPU]\n");
		fprintf(f, "CTRL=%02X MASK=%02X STATUS=%02X OAMADDR=%02X\n", s.ctrl, s.mask, s.status, s.oamaddr);
		fprintf(f, "V=%04X T=%04X FINEX=%d W=%d READBUF=%02X MIRROR=%c\n",
		        s.v & 0x7FFF, s.t & 0x7FFF, s.fine_x, s.w, s.readbuf, s.mirror);
		// Palette is canonicalised: $3F10/14/18/1C alias $3F00/04/08/0C, and entries are 6 bits.
		uint8_t pal[0x20];
		for (int i = 0; i < 0x20; ++i)
			pal[i] = (uint8_t)((((i & 0x13) == 0x10) ? s.pal[i & 0x0F] : s.pal[i]) & 0x3F);
		put_hex(f, "APU", 0x4000, s.apu, 0x18);
		put_hex(f, "RAM", 0x0000, s.ram, 0x800);
		put_hex(f, "NT", 0x2000, s.nt, 0x800);
		put_hex(f, "PAL", 0x3F00, pal, 0x20);
		put_hex(f, "OAM", 0x0000, s.oam, 0x100);
		put_hex(f, "CHR", 0x0000, s.chr, 0x2000);
		fprintf(f, "PRG_CRC32=%08X\nCHR_CRC32=%08X\n", crc32(s.prg, 0x8000), crc32(s.chr, 0x2000));
		if (cfg_.rom_every) put_hex(f, "PRG", 0x8000, s.prg, 0x8000);
		fprintf(f, "END\n");
		fclose(f);

		fprintf(events_, "%06d %s frame=%d scanline=%d pc=%04X cycles=%llu\n",
		        ++seq_, name, s.frame, s.scanline, s.pc, (unsigned long long)s.cycles);
		fflush(events_);
		if (!strcmp(s.event, "frame")) ++counts_[0];
		else if (!strcmp(s.event, "scanline")) ++counts_[1];
		else ++counts_[2];
		return true;
	}

	void close(int frames_run)
	{
		if (events_) { fclose(events_); events_ = nullptr; }
		FILE* f = open_wb(join("run.txt").c_str());
		if (!f) return;
		fprintf(f, "emulator: %s\n", emulator_.c_str());
		fprintf(f, "cmdline: %s\n", cfg_.cmdline.c_str());
		fprintf(f, "frames_run: %d\n", frames_run);
		fprintf(f, "events: frame=%d scanline=%d pc=%d\n", counts_[0], counts_[1], counts_[2]);
		for (size_t i = 0; i < cfg_.pcs.size(); ++i)
			fprintf(f, "pc_hits %04X: %d\n", cfg_.pcs[i], hits_[cfg_.pcs[i]]);
		fclose(f);
	}

private:
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
	int                    seq_;
	std::vector<uint8_t>   watch_;
	std::vector<int>       hits_;
	int                    counts_[3];
};

} // namespace nesdump
