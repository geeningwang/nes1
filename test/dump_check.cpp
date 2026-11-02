#include "../common/nesdump.h"

// Writes one synthetic frame event through the shared NESDUMP writer, optionally with one
// field changed, so that tool/test_dump_format.py can prove the comparison tools see every section.
//   dump_check ROM --dump DIR --frames 1 --dump-frames 1 [--digest] --mutate FIELD
int main(int argc, char** argv)
{
	std::vector<char*> args;
	std::string mutate;
	for (int i = 0; i < argc; ++i) {
		if (!strcmp(argv[i], "--mutate") && i + 1 < argc) mutate = argv[++i];
		else args.push_back(argv[i]);
	}
	nesdump::Config cfg;
	std::string err;
	if (!cfg.parse((int)args.size(), args.data(), err)) {
		fprintf(stderr, "%s\n", err.c_str());
		return 2;
	}
	std::vector<uint8_t> chr(0x2000, 0x11), prg(0x8000, 0x22), fb(nesdump::kFbBytes, 0x33);
	nesdump::State s;
	memset(&s, 0, sizeof(s));
	s.event = "frame";
	s.frame = 1;
	s.scanline = -1;
	s.pc_trigger = -1;
	s.pc = 0x8000;
	s.p = 0x24;
	s.mirror = 'V';
	for (size_t i = 0; i < sizeof(s.ram); ++i) s.ram[i] = (uint8_t)i;
	for (size_t i = 0; i < sizeof(s.wram); ++i) s.wram[i] = (uint8_t)(i * 3);
	for (size_t i = 0; i < sizeof(s.ntv); ++i) s.ntv[i] = (uint8_t)(i * 5);
	for (size_t i = 0; i < sizeof(s.nt); ++i) s.nt[i] = (uint8_t)(i * 7);
	for (size_t i = 0; i < sizeof(s.io); ++i) s.io[i] = (uint8_t)(i + 1);
	for (size_t i = 0; i < sizeof(s.irq); ++i) s.irq[i] = (uint8_t)(i & 1);
	s.chr = chr.data();
	s.prg = prg.data();
	s.fb = fb.data();

	if (mutate == "cpu") s.a ^= 1;
	else if (mutate == "ppu") s.ctrl ^= 1;
	else if (mutate == "ram") s.ram[0x123] ^= 1;
	else if (mutate == "wram") s.wram[0x1FFF] ^= 1;
	else if (mutate == "nt") s.nt[0x7FF] ^= 1;
	else if (mutate == "ntv") s.ntv[0xFFF] ^= 1;
	else if (mutate == "io") s.io[4] ^= 1;
	else if (mutate == "irq") s.irq[0] ^= 2;
	else if (mutate == "oam") s.oam[0xFF] ^= 1;
	else if (mutate == "pal") s.pal[0x1F] ^= 1;
	else if (mutate == "chr") chr[0x1FFF] ^= 1;
	else if (mutate == "fb") fb[nesdump::kFbBytes - 1] ^= 1;
	else if (!mutate.empty()) { fprintf(stderr, "unknown mutation %s\n", mutate.c_str()); return 2; }

	nesdump::Recorder rec;
	if (!rec.open(cfg, "dump_check")) return 2;
	rec.begin_frame(1);
	if (!rec.write_event(s)) return 2;
	rec.end_frame();
	rec.close(1);
	return 0;
}
