#pragma once

class ppu_2c02;
class apu_2a03;
struct NesDumpAccess;

class cpu_6502
{
	friend struct NesDumpAccess;  // state dump (statedump.cpp) reads private state
public:
	cpu_6502();
	~cpu_6502();

public:
	void load_prg_rom(unsigned char *prom, int rom_size);
	void set_ppu(ppu_2c02* p);
	void set_apu(apu_2a03* a);

	void reset();               // power-on reset: registers, S=$FD, PC from the vector
	void hot_reset();           // the RESET button: PC from the vector, P=I; A, X, Y, S, RAM and the cycle count are kept
	void power();               // a full power cycle: everything back to the power-on state, RAM pattern included
	bool step(bool log);
	bool nmi();                 // enter the NMI handler; false (and nothing happens) while jammed
	void arm_delayed_nmi();     // $2000 enabled NMI during vblank: taken after the next instruction (spec O-38)
	bool take_pending_nmi();    // service the armed NMI if it is due
	unsigned char nmi_state;    // 0 none, 1 armed, 2 due at the next instruction boundary
	bool irq_pending() const;   // IRQ line asserted, I flag (as of the previous instruction) clear, CPU not jammed
	void irq();

	// Direct RAM access for automated testing (bypasses bus mapping)
	void           set_mem_byte(unsigned short addr, unsigned char val);
	unsigned char  get_mem_byte(unsigned short addr) const;

public:
	unsigned int  cycle;

	unsigned short get_pc() const { return rpc; }
	unsigned char  get_a()  const { return ra; }
	unsigned char  get_x()  const { return rx; }
	unsigned char  get_y()  const { return ry; }
	unsigned char  get_s()  const { return rsp; }
	unsigned char  get_p()  const { return rp; }
	unsigned char  get_pending_instruction_cycles() const { return pending_instruction_cycles; }

	// Joypad 1 button state (set from outside each frame)
	// bit0=A, bit1=B, bit2=Select, bit3=Start, bit4=Up, bit5=Down, bit6=Left, bit7=Right
	unsigned char joypad1_state;
	unsigned char joypad2_state;
	unsigned char joypad3_state, joypad4_state;
	bool input_fourscore, input_famicom4;
	bool input_port_enabled[2];

private:
	unsigned char joy_readbit[2];   // next bit of each pad's report; wraps like an 8-bit counter (spec O-39)
	unsigned char joy_fc_readbit[2];
	unsigned char last_strobe;      // bit 0 of the last $4016 write

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
	unsigned char data_bus;  // value of the last bus access; returned by reads of unmapped addresses
	ppu_2c02* ppu;
	apu_2a03* apu;
	unsigned char pending_instruction_cycles;
	unsigned char rpi;          // P as it was when the previous instruction started: the I flag the IRQ check sees
	bool          jammed;       // executed a KIL opcode
	unsigned int  apu_cycle;    // CPU cycle up to which the APU has been advanced
};
