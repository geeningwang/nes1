// statedump.cpp - nes1 side of the NESDUMP state dump.
#include "stdafx.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "statedump.h"

extern cpu_6502 cpu;
extern ppu_2c02 ppu;
extern apu_2a03 apu;
extern int g_ppu_budget;
extern int g_line_ov[4];

StateDumper* g_dumper = nullptr;
extern int g_dbg_frame;

// Friend of cpu_6502 / ppu_2c02 / apu_2a03: copies their private state into the shared dump struct.
struct NesDumpAccess {
	static unsigned short pc() { return cpu.rpc; }
	// --init-pc: the nestest start state (PC given, P=$24, S=$FD).
	static void set_pc(unsigned short pc) { cpu.rpc = pc; cpu.rp = 0x24; cpu.rsp = 0xFD; cpu.data_bus = 0; }

	// The opcode at PC as the contract defines it: RAM mirrors for $0000-$1FFF, flat memory otherwise.
	static unsigned char opcode()
	{
		unsigned short a = cpu.rpc;
		return a < 0x2000 ? cpu.mem[a & 0x7FF] : cpu.mem[a];
	}

	static void fill_insn(nesdump::Insn& i)
	{
		i.pc = cpu.rpc;  i.op = opcode();
		i.a = cpu.ra;  i.x = cpu.rx;  i.y = cpu.ry;  i.s = cpu.rsp;  i.p = cpu.rp;
		i.db = cpu.data_bus;
		i.pl = ppu.io_latch;
		i.cycles = cpu.cycle;
	}

	static void fill(nesdump::State& s)
	{
		s.pc = cpu.rpc;
		s.a = cpu.ra;  s.x = cpu.rx;  s.y = cpu.ry;  s.s = cpu.rsp;  s.p = cpu.rp;
		s.cycles = cpu.cycle;

		for (int k = 0; k < 4; ++k) s.ov[k] = g_line_ov[k];
		s.db = cpu.data_bus;  s.ppu_latch = ppu.io_latch;
		s.ctrl = ppu.reg_ctrl;  s.mask = ppu.reg_mask;
		s.status = ppu.reg_status;  s.oamaddr = ppu.reg_oam_addr;
		s.v = ppu.v;  s.t = ppu.t;
		s.fine_x = ppu.fine_x;  s.w = ppu.w;  s.readbuf = ppu.data_buf;
		s.mirror = ppu.mirror_vertical ? 'V' : 'H';

		memcpy(s.apu, apu.reg_shadow, sizeof(s.apu));
		memset(s.apuz, 0, sizeof(s.apuz));
		s.apuz[0] = apu.pulse[0].length;  s.apuz[1] = apu.pulse[1].length;
		s.apuz[2] = apu.tri.length;       s.apuz[3] = apu.noise.length;
		s.apuz[4] = apu.fcnt;  s.apuz[5] = apu.irq_mode;  s.apuz[6] = apu.sirq;
		s.apuz[7] = apu.ch_enable;
		for (int k = 0; k < 4; ++k) s.apuz[8 + k] = (unsigned char)((unsigned)apu.fhcnt >> (8 * k));
		s.apuz[12] = (unsigned char)apu.dmc_addr;  s.apuz[13] = (unsigned char)(apu.dmc_addr >> 8);
		s.apuz[14] = (unsigned char)apu.dmc_size;  s.apuz[15] = (unsigned char)(apu.dmc_size >> 8);
		s.apuz[16] = apu.dmc_shift;  s.apuz[17] = apu.dmc_bits;
		s.apuz[18] = (unsigned char)apu.dmc_acc;     s.apuz[19] = (unsigned char)(apu.dmc_acc >> 8);
		s.apuz[20] = (unsigned char)apu.dmc_period;  s.apuz[21] = (unsigned char)(apu.dmc_period >> 8);
		s.apuz[22] = apu.dmc_have_sample;  s.apuz[23] = apu.dmc_have_dma;
		s.apuz[24] = apu.dmc_buf;  s.apuz[25] = apu.dmc_dac;  s.apuz[26] = apu.dmc_format;
		memcpy(s.ram, cpu.mem, 0x800);
		memcpy(s.nt, ppu.mem + 0x2000, 0x800);
		memcpy(s.pal, ppu.mem + 0x3F00, 0x20);
		memcpy(s.oam, ppu.oam, 0x100);
		s.chr = ppu.mem;
		s.prg = cpu.mem + 0x8000;
		s.fb  = ppu.fb_idx;
	}
};

void StateDumper::emit(const char* event, int pc_trigger, int pc_hit, int insn_no)
{
	nesdump::State s;
	memset(&s, 0, sizeof(s));
	s.event = event;
	s.frame = frame;
	s.scanline = scanline;
	s.pc_trigger = pc_trigger;
	s.pc_hit = pc_hit;
	s.insn_no = insn_no;
	NesDumpAccess::fill(s);
	rec.write_event(s);
}

bool StateDumper::write_rom()
{
	nesdump::State s;
	memset(&s, 0, sizeof(s));
	NesDumpAccess::fill(s);
	return rec.write_rom(s);
}

void StateDumper::on_instruction()
{
	nesdump::Insn i;
	NesDumpAccess::fill_insn(i);
	i.frame = frame;
	i.scanline = scanline;
	int r = rec.insn(i);
	if (r & 1) emit("pc", i.pc, rec.pc_hit(i.pc), 0);
	if (r & 2) emit("insn", -1, 0, rec.insn_number());
}

void StateDumper::init_pc(int pc)
{
	if (pc >= 0) NesDumpAccess::set_pc((unsigned short)pc);
}

void StateDumper::on_scanline_end(int sl)
{
	scanline = sl;
	if (rec.want_scanline(frame, sl))
		emit("scanline", -1, 0, 0);
	scanline = -1;
}

void StateDumper::on_frame_end()
{
	if (rec.want_frame(frame))
		emit("frame", -1, 0, 0);
	rec.end_frame();
}
