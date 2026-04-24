#pragma once

class cpu_6502
{
public:
	cpu_6502();
	~cpu_6502();

public:
	void load_prg_rom(unsigned char *prom, int rom_size);
	//void disassemble(unsigned short start, unsigned short size);

	void reset();
	//void step_disassemble(char *pbuf);
	bool step(bool log);

	void nmi();

private:
	unsigned short int rpc;
	unsigned char rsp;
	unsigned char ra;
	unsigned char rx;
	unsigned char ry;
	unsigned char rp;

	unsigned char *mem;
	unsigned int cycle;
};
