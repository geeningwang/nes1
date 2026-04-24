#pragma once

class ppu_2c02;

class cpu_6502
{
public:
	cpu_6502();
	~cpu_6502();

public:
	void load_prg_rom(unsigned char *prom, int rom_size);
	void set_ppu(ppu_2c02* p);

	void reset();
	bool step(bool log);
	void nmi();

public:
	unsigned int cycle;

	// Joypad 1 button state (set from outside each frame)
	// bit0=A, bit1=B, bit2=Select, bit3=Start, bit4=Up, bit5=Down, bit6=Left, bit7=Right
	unsigned char joypad1_state;

private:
	bool          joypad1_strobe;
	unsigned char joypad1_shift;

	unsigned char  mem_read(unsigned short addr);
	void           mem_write(unsigned short addr, unsigned char val);

private:
	unsigned short int rpc;
	unsigned char rsp;
	unsigned char ra;
	unsigned char rx;
	unsigned char ry;
	unsigned char rp;

	unsigned char *mem;
	ppu_2c02* ppu;
};
