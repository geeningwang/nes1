// statedump.cpp - nes1 side of the NESDUMP state dump.
#include "stdafx.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "statedump.h"

extern cpu_6502 cpu;
extern ppu_2c02 ppu;
extern apu_2a03 apu;

StateDumper* g_dumper = nullptr;

// Friend of cpu_6502 / ppu_2c02 / apu_2a03: copies their private state into the shared dump struct.
struct NesDumpAccess {
	static unsigned short pc() { return cpu.rpc; }
	// --init-pc: the nestest start state (PC given, P=$24, S=$FD).
	static void set_pc(unsigned short pc) { cpu.rpc = pc; cpu.rp = 0x24; cpu.rsp = 0xFD; }

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
		i.cycles = cpu.cycle;
	}

	static void fill(nesdump::State& s)
	{
		s.pc = cpu.rpc;
		s.a = cpu.ra;  s.x = cpu.rx;  s.y = cpu.ry;  s.s = cpu.rsp;  s.p = cpu.rp;
		s.cycles = cpu.cycle;

		s.ctrl = ppu.reg_ctrl;  s.mask = ppu.reg_mask;
		s.status = ppu.reg_status;  s.oamaddr = ppu.reg_oam_addr;
		s.v = ppu.v;  s.t = ppu.t;
		s.fine_x = ppu.fine_x;  s.w = ppu.w;  s.readbuf = ppu.data_buf;
		s.mirror = ppu.mirror_vertical ? 'V' : 'H';

		memcpy(s.apu, apu.reg_shadow, sizeof(s.apu));
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
