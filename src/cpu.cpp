#include "stdafx.h"
#include "cpu.h"
#include "ppu.h"

#define OPERAND8 \
	(*(mem + rpc + 1))

#define OPERAND16 \
	(*(unsigned short *)(mem + rpc + 1))

#define VAR_ZERO_PAGE(operand) \
	(*(mem + (operand)))

#define OPERAND_ADD_X(operand) \
	((operand + rx) & 0xff)

#define OPERAND_ADD_Y(operand) \
	((operand + ry) & 0xff)

#define VAR_ZERO_PAGE_X(operand) \
	(*(mem + OPERAND_ADD_X(operand)))

#define VAR_ZERO_PAGE_Y(operand) \
	(*(mem + OPERAND_ADD_Y(operand)))

#define VAR_ABSOLUTE(operand16) \
	(*(mem + (operand16)))

#define ADDRESS_ADD_X(address) \
	((address + rx) & 0xffff)

#define ADDRESS_ADD_Y(address) \
	((address + ry) & 0xffff)

#define VAR_ABSOLUTE_X(operand16) \
	(*(mem + ADDRESS_ADD_X(operand16)))

#define VAR_ABSOLUTE_Y(operand16) \
	(*(mem + ADDRESS_ADD_Y(operand16)))

#define ADDRESS_ZERO_PAGE_WRAP(address) \
	((address != 0xff) ? (*(unsigned short *)(mem + (address))) : ((*(mem + 0xff)) | ((*mem) << 8)))

#define VAR_INDIRECT_X(operand) \
	(*(mem + ADDRESS_ZERO_PAGE_WRAP(OPERAND_ADD_X(operand))))

#define VAR_INDIRECT_Y(operand) \
	(*(mem + ADDRESS_ADD_Y(ADDRESS_ZERO_PAGE_WRAP(operand))))

#define ADDRESS_INDIRECT_PAGE_WRAP(operand16) \
	(((operand16 & 0xff) != 0xff) ? (*(unsigned short *)(mem + (operand16))) : ((*(mem + operand16)) | ((*(mem + (operand16 & 0xff00))) << 8)))

#define ADDRESS_ADD_X(address) \
	((address + rx) & 0xffff)

#define SET_CARRY(r) \
	if ((r) != 0) { rp |= 0x01; } else { rp &= 0xfe; }

#define GET_CARRY() \
	(rp & 0x01)

#define SET_ZERO(r) \
	if ((r) == 0) { rp |= 0x02; } else { rp &= 0xfd; }

#define GET_ZERO() \
	((rp >> 1) & 0x01)

#define SET_INTERRUPT(r) \
	if ((r) != 0) { rp |= 0x04; } else { rp &= 0xfb; }

#define GET_INTERRUPT() \
	((rp >> 2) & 0x01)

#define SET_DECIMAL(r) \
	if ((r) != 0) { rp |= 0x08; } else { rp &= 0xf7; }

#define GET_DECIMAL(r) \
	((rp >> 3) & 0x01)

#define SET_OVERFLOW(r) \
	if ((r) != 0) { rp |= 0x40; } else { rp &= 0xbf; }

#define SET_OVERFLOW_BY_BIT(r) \
	if (((r) & 0x40) != 0) { rp |= 0x40; } else { rp &= 0xbf; }

#define GET_OVERFLOW() \
	((rp >> 6) & 0x01)

#define SET_NEGATIVE_BY_BIT(r) \
	if (((r) & 0x80) != 0) { rp |= 0x80; } else { rp &= 0x7f; }

#define GET_NEGATIVE() \
	(rp >> 7)

#define STACK_PUSH(r) \
	*(mem + 0x100 + rsp) = (r); \
	rsp--;

#define STACK_POP(r) \
	rsp ++; \
	(r) = *(mem + 0x100 + rsp);

#define STACK_PUSH_PC() \
	STACK_PUSH(rpc >> 8); \
	STACK_PUSH(rpc & 0xff); \

#define STACK_POP_PC() \
	{ \
	unsigned char _poptempl, _poptemph; \
	STACK_POP(_poptempl); \
	STACK_POP(_poptemph); \
	rpc = _poptemph << 8 | _poptempl; \
	}

#define BRANCH_NEW_PAGE() !((rpc & 0xff00) == (temp_rpc & 0xff00))

#define PAGE_CROSSED(address1,address2) ((address1 >> 8) != (address2 >> 8))

cpu_6502::cpu_6502()
{
	rpc = 0;
	rsp = 0;
	ra = 0;
	rx = 0;
	ry = 0;
	rp = 0;
	mem = new unsigned char[65536];
	memset(mem, 0, 65536);
	cycle = 0;
	ppu = nullptr;
}

cpu_6502::~cpu_6502()
{
	delete[] mem;
}

void cpu_6502::load_prg_rom(unsigned char *prom, int rom_size)
{
	// NROM (mapper 0) with just 1 16K bank is supported now. prg_rom starts at both 0x8000 and 0xC000.
	//
	if (rom_size != 16384)
	{
		printf("error in rom_size.\n");
		return;
	}

	// copy the rom
	memcpy(mem + 0x8000, prom, rom_size);
	memcpy(mem + 0xc000, prom, rom_size);

	return;
}

void cpu_6502::set_ppu(ppu_2c02* p)
{
	ppu = p;
}

unsigned char cpu_6502::mem_read(unsigned short addr)
{
	// PPU registers: $2000-$2007 mirrored throughout $2000-$3FFF
	if (addr >= 0x2000 && addr <= 0x3FFF)
	{
		if (ppu)
			return ppu->cpu_read(addr);
		return 0;
	}
	return mem[addr];
}

void cpu_6502::mem_write(unsigned short addr, unsigned char val)
{
	// PPU registers: $2000-$2007 mirrored throughout $2000-$3FFF
	if (addr >= 0x2000 && addr <= 0x3FFF)
	{
		if (ppu)
			ppu->cpu_write(addr, val);
		return;
	}
	// OAM DMA
	if (addr == 0x4014)
	{
		if (ppu)
			ppu->oam_dma(mem + (val << 8));
		return;
	}
	mem[addr] = val;
}

#define UNK 0
#define ADC 1
#define AND 2
#define ASL 3
#define BCC 4
#define BCS 5
#define BEQ 6
#define BIT 7
#define BMI 8
#define BNE 9
#define BPL 10
#define BRK 11
#define BVC 12
#define BVS 13
#define CLC 14
#define CLD 15
#define CLI 16
#define CLV 17
#define CMP 18
#define CPX 19
#define CPY 20
#define DEC 21
#define DEX 22
#define DEY 23
#define EOR 24
#define INC 25
#define INX 26
#define INY 27
#define JMP 28
#define JSR 29
#define LDA 30
#define LDX 31
#define LDY 32
#define LSR 33
#define NOP 34
#define ORA 35
#define PHA 36
#define PHP 37
#define PLA 38
#define PLP 39
#define ROL 40
#define ROR 41
#define RTI 42
#define RTS 43
#define SBC 44
#define SEC 45
#define SED 46
#define SEI 47
#define STA 48
#define STX 49
#define STY 50
#define TAX 51
#define TAY 52
#define TSX 53
#define TXA 54
#define TXS 55
#define TYA 56
#define SNOP 57
#define SLAX 58
#define SSAX 59
#define SSBC 60
#define SDCP 61
#define SISB 62
#define SSLO 63
#define SRLA 64
#define SSRE 65
#define SRRA 66

char instruction_name[][5] = {
	" ???", " ADC", " AND", " ASL", " BCC", " BCS", " BEQ", " BIT", " BMI", " BNE", " BPL", " BRK", " BVC", " BVS", " CLC", " CLD",
	" CLI", " CLV", " CMP", " CPX", " CPY", " DEC", " DEX", " DEY", " EOR", " INC", " INX", " INY", " JMP", " JSR", " LDA", " LDX",
	" LDY", " LSR", " NOP", " ORA", " PHA", " PHP", " PLA", " PLP", " ROL", " ROR", " RTI", " RTS", " SBC", " SEC", " SED", " SEI",
	" STA", " STX", " STY", " TAX", " TAY", " TSX", " TXA", " TXS", " TYA", "*NOP", "*LAX", "*SAX", "*SBC", "*DCP", "*ISB", "*SLO",
	"*RLA", "*SRE", "*RRA"
};

#define ADDR_NONE			0
#define ADDR_IMPLICIT		1
#define ADDR_ACCUMULATOR	2
#define ADDR_IMMEDIATE		3
#define ADDR_ZERO_PAGE		4
#define ADDR_ZERO_PAGE_X	5
#define ADDR_ZERO_PAGE_Y	6
#define ADDR_RELATIVE		7
#define ADDR_ABSOLUTE		8
#define ADDR_ABSOLUTE_X		9
#define ADDR_ABSOLUTE_Y		10
#define ADDR_INDIRECT_X		11
#define ADDR_INDIRECT_Y		12
#define ADDR_JMP_ABSOLUTE	13
#define ADDR_JMP_INDIRECT	14

struct opcode
{
	int instruction;  // NULL is invalid opcode
	int instruction_size;
	int addressing_mode;
	int cycles;
	int page_crossed_extra_cycles;
};

#define BRANCH_NEW_PAGE_EXTRA_CYCLE 1
#define BRANCH_EXTRA_CYCLE 1

opcode opcode_items[] = 
{
	// 0x00
	{ BRK, 1, ADDR_IMPLICIT, 7, 0 },{ ORA, 2, ADDR_INDIRECT_X, 6, 0 },{},{ SSLO, 2, ADDR_INDIRECT_X, 8, 0 },{ SNOP, 2, ADDR_ZERO_PAGE, 3, 0 },{ ORA, 2, ADDR_ZERO_PAGE, 3, 0 },{ ASL, 2, ADDR_ZERO_PAGE, 5, 0 },{ SSLO, 2, ADDR_ZERO_PAGE, 5, 0 },
	// 0x08
	{ PHP, 1, ADDR_IMPLICIT, 3, 0 },{ ORA, 2, ADDR_IMMEDIATE, 2, 0 },{ ASL, 1, ADDR_ACCUMULATOR, 2, 0 },{},{ SNOP, 3, ADDR_ABSOLUTE, 4, 0 },{ ORA, 3, ADDR_ABSOLUTE, 4, 0 },{ ASL, 3, ADDR_ABSOLUTE, 6, 0 },{ SSLO, 3, ADDR_ABSOLUTE, 6, 0 },
	// 0x10
	{ BPL, 2, ADDR_RELATIVE, 2, 2 },{ ORA, 2, ADDR_INDIRECT_Y, 5, 1 },{},{ SSLO, 2, ADDR_INDIRECT_Y, 8, 0 },{ SNOP, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ ORA, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ ASL, 2, ADDR_ZERO_PAGE_X, 6, 0 },{ SSLO, 2, ADDR_ZERO_PAGE_X, 6, 0 },
	// 0x18
	{ CLC, 1, ADDR_IMPLICIT, 2, 0 },{ ORA, 3, ADDR_ABSOLUTE_Y, 4, 1 },{ SNOP, 1, ADDR_IMPLICIT, 2, 0 },{ SSLO, 3, ADDR_ABSOLUTE_Y, 7, 0 },{ SNOP, 3, ADDR_ABSOLUTE_X, 4, 1 },{ ORA, 3, ADDR_ABSOLUTE_X, 4, 1 },{ ASL, 3, ADDR_ABSOLUTE_X, 7, 0 }, { SSLO, 3, ADDR_ABSOLUTE_X, 7, 0 },
	// 0x20
	{ JSR, 3, ADDR_JMP_ABSOLUTE, 6, 0 },{ AND, 2, ADDR_INDIRECT_X, 6, 0 },{},{ SRLA, 2, ADDR_INDIRECT_X, 8, 0 },{ BIT, 2, ADDR_ZERO_PAGE, 3, 0 },{ AND, 2, ADDR_ZERO_PAGE, 3, 0 },{ ROL, 2, ADDR_ZERO_PAGE, 5, 0 },{ SRLA, 2, ADDR_ZERO_PAGE, 5, 0 },
	// 0x28
	{ PLP, 1, ADDR_IMPLICIT, 4, 0 },{ AND, 2, ADDR_IMMEDIATE, 2, 0 },{ ROL, 1, ADDR_ACCUMULATOR, 2, 0 },{},{ BIT, 3, ADDR_ABSOLUTE, 4, 0 },{ AND, 3, ADDR_ABSOLUTE, 4, 0 },{ ROL, 3, ADDR_ABSOLUTE, 6, 0 },{ SRLA, 3, ADDR_ABSOLUTE, 6, 0 },
	// 0x30
	{ BMI, 2, ADDR_RELATIVE, 2, 2 },{ AND, 2, ADDR_INDIRECT_Y, 5, 1 },{},{ SRLA, 2, ADDR_INDIRECT_Y, 8, 0 },{ SNOP, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ AND, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ ROL, 2, ADDR_ZERO_PAGE_X, 6, 0 },{ SRLA, 2, ADDR_ZERO_PAGE_X, 6, 0 },
	// 0x38
	{ SEC, 1, ADDR_IMPLICIT, 2, 0 },{ AND, 3, ADDR_ABSOLUTE_Y, 4, 1 },{ SNOP, 1, ADDR_IMPLICIT, 2, 0 },{ SRLA, 3, ADDR_ABSOLUTE_Y, 7, 0 },{ SNOP, 3, ADDR_ABSOLUTE_X, 4, 1 },{ AND, 3, ADDR_ABSOLUTE_X, 4, 1 },{ ROL, 3, ADDR_ABSOLUTE_X, 7, 0 },{ SRLA, 3, ADDR_ABSOLUTE_X, 7, 0 },
	// 0x40
	{ RTI, 1, ADDR_IMPLICIT, 6, 0 },{ EOR, 2, ADDR_INDIRECT_X, 6, 0 },{},{ SSRE, 2, ADDR_INDIRECT_X, 8, 0 },{ SNOP, 2, ADDR_ZERO_PAGE, 3, 0 },{ EOR, 2, ADDR_ZERO_PAGE, 3, 0 },{ LSR, 2, ADDR_ZERO_PAGE, 5, 0 },{ SSRE, 2, ADDR_ZERO_PAGE, 5, 0 },
	// 0x48
	{ PHA, 1, ADDR_IMPLICIT, 3, 0 },{ EOR, 2, ADDR_IMMEDIATE, 2, 0 },{ LSR, 1, ADDR_ACCUMULATOR, 2, 0 },{},{ JMP, 3, ADDR_JMP_ABSOLUTE, 3, 0 },{ EOR, 3, ADDR_ABSOLUTE, 4, 0 },{ LSR, 3, ADDR_ABSOLUTE, 6, 0 },{ SSRE, 3, ADDR_ABSOLUTE, 6, 0 },
	// 0x50
	{ BVC, 2, ADDR_RELATIVE, 2, 2 },{ EOR, 2, ADDR_INDIRECT_Y, 5, 1 },{},{ SSRE, 2, ADDR_INDIRECT_Y, 8, 0 },{ SNOP, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ EOR, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ LSR, 2, ADDR_ZERO_PAGE_X, 6, 0 },{ SSRE, 2, ADDR_ZERO_PAGE_X, 6, 0 },
	// 0x58
	{ CLI, 1, ADDR_IMPLICIT, 2, 0 },{ EOR, 3, ADDR_ABSOLUTE_Y, 4, 1 },{ SNOP, 1, ADDR_IMPLICIT, 2, 0 },{ SSRE, 3, ADDR_ABSOLUTE_Y, 7, 0 },{ SNOP, 3, ADDR_ABSOLUTE_X, 4, 1 },{ EOR, 3, ADDR_ABSOLUTE_X, 4, 1 },{ LSR, 3, ADDR_ABSOLUTE_X, 7, 0 },{ SSRE, 3, ADDR_ABSOLUTE_X, 7, 0 },
	// 0x60
	{ RTS, 1, ADDR_IMPLICIT, 6, 0 },{ ADC, 2, ADDR_INDIRECT_X, 6, 0 },{},{ SRRA, 2, ADDR_INDIRECT_X, 8, 0 },{ SNOP, 2, ADDR_ZERO_PAGE, 3, 0 },{ ADC, 2, ADDR_ZERO_PAGE, 3, 0 },{ ROR, 2, ADDR_ZERO_PAGE, 5, 0 },{ SRRA, 2, ADDR_ZERO_PAGE, 5, 0 },
	// 0x68
	{ PLA, 1, ADDR_IMPLICIT, 4, 0 },{ ADC, 2, ADDR_IMMEDIATE, 2, 0 },{ ROR, 1, ADDR_ACCUMULATOR, 2, 0 },{},{ JMP, 3, ADDR_JMP_INDIRECT, 5, 0 },{ ADC, 3, ADDR_ABSOLUTE, 4, 0 },{ ROR, 3, ADDR_ABSOLUTE, 6, 0 },{ SRRA, 3, ADDR_ABSOLUTE, 6, 0 },
	// 0x70
	{ BVS, 2, ADDR_RELATIVE, 2, 2 },{ ADC, 2, ADDR_INDIRECT_Y, 5, 1 },{},{ SRRA, 2, ADDR_INDIRECT_Y, 8, 0 },{ SNOP, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ ADC, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ ROR, 2, ADDR_ZERO_PAGE_X, 6, 0 },{ SRRA, 2, ADDR_ZERO_PAGE_X, 6, 0 },
	// 0x78
	{ SEI, 1, ADDR_IMPLICIT, 2, 0 },{ ADC, 3, ADDR_ABSOLUTE_Y, 4, 1 },{ SNOP, 1, ADDR_IMPLICIT, 2, 0 },{ SRRA, 3, ADDR_ABSOLUTE_Y, 7, 0 },{ SNOP, 3, ADDR_ABSOLUTE_X, 4, 1 },{ ADC, 3, ADDR_ABSOLUTE_X, 4, 1 },{ ROR, 3, ADDR_ABSOLUTE_X, 7, 0 },{ SRRA, 3, ADDR_ABSOLUTE_X, 7, 0 },
	// 0x80
	{ SNOP, 2, ADDR_IMMEDIATE, 2, 0 },{ STA, 2, ADDR_INDIRECT_X, 6, 0 },{},{ SSAX, 2, ADDR_INDIRECT_X, 6, 0 },{ STY, 2, ADDR_ZERO_PAGE, 3, 0 },{ STA, 2, ADDR_ZERO_PAGE, 3, 0 },{ STX, 2, ADDR_ZERO_PAGE, 3, 0 },{ SSAX, 2, ADDR_ZERO_PAGE, 3, 0 },
	// 0x88
	{ DEY, 1, ADDR_IMPLICIT, 2, 0 },{},{ TXA, 1, ADDR_IMPLICIT, 2, 0 },{},{ STY, 3, ADDR_ABSOLUTE, 4, 0 },{ STA, 3, ADDR_ABSOLUTE, 4, 0 },{ STX, 3, ADDR_ABSOLUTE, 4, 0 },{ SSAX, 3, ADDR_ABSOLUTE, 4, 0 },
	// 0x90
	{ BCC, 2, ADDR_RELATIVE, 2, 2 },{ STA, 2, ADDR_INDIRECT_Y, 6, 0 },{},{},{ STY, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ STA, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ STX, 2, ADDR_ZERO_PAGE_Y, 4, 0 },{ SSAX, 2, ADDR_ZERO_PAGE_Y, 4, 0 },
	// 0x98
	{ TYA, 1, ADDR_IMPLICIT, 2, 0 },{ STA, 3, ADDR_ABSOLUTE_Y, 5, 0 },{ TXS, 1, ADDR_IMPLICIT, 2, 0 },{},{},{ STA, 3, ADDR_ABSOLUTE_X, 5, 0 },{},{},
	// 0xa0
	{ LDY, 2, ADDR_IMMEDIATE, 2, 0 },{ LDA, 2, ADDR_INDIRECT_X, 6, 0 },{ LDX, 2, ADDR_IMMEDIATE, 2, 0 },{ SLAX, 2, ADDR_INDIRECT_X, 6, 0 },{ LDY, 2, ADDR_ZERO_PAGE, 3, 0 },{ LDA, 2, ADDR_ZERO_PAGE, 3, 0 },{ LDX, 2, ADDR_ZERO_PAGE, 3, 0 },{ SLAX, 2, ADDR_ZERO_PAGE, 3, 0 },
	// 0xa8
	{ TAY, 1, ADDR_IMPLICIT, 2, 0 },{ LDA, 2, ADDR_IMMEDIATE, 2, 0 },{ TAX, 1, ADDR_IMPLICIT, 2, 0 },{},{ LDY, 3, ADDR_ABSOLUTE, 4, 0 },{ LDA, 3, ADDR_ABSOLUTE, 4, 0 },{ LDX, 3, ADDR_ABSOLUTE, 4, 0 },{ SLAX, 3, ADDR_ABSOLUTE, 4, 0 },
	// 0xb0
	{ BCS, 2, ADDR_RELATIVE, 2, 2 },{ LDA, 2, ADDR_INDIRECT_Y, 5, 1 },{},{ SLAX, 2, ADDR_INDIRECT_Y, 5, 1 },{ LDY, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ LDA, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ LDX, 2, ADDR_ZERO_PAGE_Y, 4, 0 },{ SLAX, 2, ADDR_ZERO_PAGE_Y, 4, 0 },
	// 0xb8
	{ CLV, 1, ADDR_IMPLICIT, 2, 0 },{ LDA, 3, ADDR_ABSOLUTE_Y, 4, 1 },{ TSX, 1, ADDR_IMPLICIT, 2, 0 },{},{ LDY, 3, ADDR_ABSOLUTE_X, 4, 1 },{ LDA, 3, ADDR_ABSOLUTE_X, 4, 1 },{ LDX, 3, ADDR_ABSOLUTE_Y, 4, 1 },{ SLAX, 3, ADDR_ABSOLUTE_Y, 4, 1 },
	// 0xc0
	{ CPY, 2, ADDR_IMMEDIATE, 2, 0 },{ CMP, 2, ADDR_INDIRECT_X, 6, 0 },{},{ SDCP, 2, ADDR_INDIRECT_X, 8, 0 },{ CPY, 2, ADDR_ZERO_PAGE, 3, 0 },{ CMP, 2, ADDR_ZERO_PAGE, 3, 0 },{ DEC, 2, ADDR_ZERO_PAGE, 5, 0 },{ SDCP, 2, ADDR_ZERO_PAGE, 5, 0 },
	// 0xc8
	{ INY, 1, ADDR_IMPLICIT, 2, 0 },{ CMP, 2, ADDR_IMMEDIATE, 2, 0 },{ DEX, 1, ADDR_IMPLICIT, 2, 0 },{},{ CPY, 3, ADDR_ABSOLUTE, 4, 0 },{ CMP, 3, ADDR_ABSOLUTE, 4, 0 },{ DEC, 3, ADDR_ABSOLUTE, 6, 0 },{ SDCP, 3, ADDR_ABSOLUTE, 6, 0 },
	// 0xd0
	{ BNE, 2, ADDR_RELATIVE, 2, 2 },{ CMP, 2, ADDR_INDIRECT_Y, 5, 1 },{},{ SDCP, 2, ADDR_INDIRECT_Y, 8, 0 },{ SNOP, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ CMP, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ DEC, 2, ADDR_ZERO_PAGE_X, 6, 0 },{ SDCP, 2, ADDR_ZERO_PAGE_X, 6, 0 },
	// 0xd8
	{ CLD, 1, ADDR_IMPLICIT, 2, 0 },{ CMP, 3, ADDR_ABSOLUTE_Y, 4, 1 },{ SNOP, 1, ADDR_IMPLICIT, 2, 0 },{ SDCP, 3, ADDR_ABSOLUTE_Y, 7, 0 },{ SNOP, 3, ADDR_ABSOLUTE_X, 4, 1 },{ CMP, 3, ADDR_ABSOLUTE_X, 4, 1 },{ DEC, 3, ADDR_ABSOLUTE_X, 7, 0 },{ SDCP, 3, ADDR_ABSOLUTE_X, 7, 0 },
	// 0xe0
	{ CPX, 2, ADDR_IMMEDIATE, 2, 0 },{ SBC, 2, ADDR_INDIRECT_X, 6, 0 },{},{ SISB, 2, ADDR_INDIRECT_X, 8, 0 },{ CPX, 2, ADDR_ZERO_PAGE, 3, 0 },{ SBC, 2, ADDR_ZERO_PAGE, 3, 0 },{ INC, 2, ADDR_ZERO_PAGE, 5, 0 },{ SISB, 2, ADDR_ZERO_PAGE, 5, 0 },
	// 0xe8
	{ INX, 1, ADDR_IMPLICIT, 2, 0 },{ SBC, 2, ADDR_IMMEDIATE, 2, 0 },{ NOP, 1, ADDR_IMPLICIT, 2, 0 },{ SSBC, 2, ADDR_IMMEDIATE, 2, 0 },{ CPX, 3, ADDR_ABSOLUTE, 4, 0 },{ SBC, 3, ADDR_ABSOLUTE, 4, 0 },{ INC, 3, ADDR_ABSOLUTE, 6, 0 },{ SISB, 3, ADDR_ABSOLUTE, 6, 0 },
	// 0xf0
	{ BEQ, 2, ADDR_RELATIVE, 2, 2 },{ SBC, 2, ADDR_INDIRECT_Y, 5, 1 },{},{ SISB, 2, ADDR_INDIRECT_Y, 8, 0 },{ SNOP, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ SBC, 2, ADDR_ZERO_PAGE_X, 4, 0 },{ INC, 2, ADDR_ZERO_PAGE_X, 6, 0 },{ SISB, 2, ADDR_ZERO_PAGE_X, 6, 0 },
	// 0xf8
	{ SED, 1, ADDR_IMPLICIT, 2, 0 },{ SBC, 3, ADDR_ABSOLUTE_Y, 4, 1 },{ SNOP, 1, ADDR_IMPLICIT, 2, 0 },{ SISB, 3, ADDR_ABSOLUTE_Y, 7, 0 },{ SNOP, 3, ADDR_ABSOLUTE_X, 4, 1 },{ SBC, 3, ADDR_ABSOLUTE_X, 4, 1 },{ INC, 3, ADDR_ABSOLUTE_X, 7, 0 },{ SISB, 3, ADDR_ABSOLUTE_X, 7, 0 },
};

void cpu_6502::reset()
{
	rpc = 0xc000;	// Trail run nestest.nes, starts from 0xc000
	rp = 0x24;		// Status bit 5 is always 1, the interrupt-disable bit 2 is set.

	// See https://superuser.com/a/606770
	STACK_PUSH_PC();  // Push PC to 0x100 and 0x1ff
	STACK_PUSH(rp);   // Push P to 0x1fe and finally SP get 0xfd.

	rpc = *((unsigned short *)(mem + 0xfffc));
	cycle += 7;

	return;
}

bool cpu_6502::step(bool log)
{
	unsigned char opcode = *(mem + rpc);
	int instruction = opcode_items[opcode].instruction;
	int instruction_size = opcode_items[opcode].instruction_size;
	int addressing_mode = opcode_items[opcode].addressing_mode;
	int instruction_cycles = opcode_items[opcode].cycles;
	int page_crossed_extra_cycles = opcode_items[opcode].page_crossed_extra_cycles;
	const char* name = instruction_name[instruction];

	if (instruction == UNK)
	{
		instruction_size = 1;
	}

	// output CPU status
	const int statusbuf_size = 100;
	char statusbuf[statusbuf_size];
	sprintf_s(statusbuf, statusbuf_size, "A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%d", ra, rx, ry, rp, rsp, cycle);

	// output the address
	const int pbuf_size = 100;
	char pbuf[pbuf_size];
	size_t bufc = sprintf_s(pbuf, pbuf_size, "%04X  ", rpc);

	// print the instruction hex code
	for (int j = 0; j < 3; ++j)
	{
		if (j < instruction_size)
			bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%02X ", *(mem + rpc + j));
		else
			bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "   ");
	}
//	bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, " ");

	// print the instruction

	switch (addressing_mode)
	{
	case ADDR_NONE:
		break;
	case ADDR_IMPLICIT:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s", name);
		break;
	case ADDR_ACCUMULATOR:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s A", name);
		break;
	case ADDR_IMMEDIATE:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s #$%02X", name, OPERAND8);
		break;
	case ADDR_ZERO_PAGE:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s $%02X = %02X", name, OPERAND8, VAR_ZERO_PAGE(OPERAND8));
		break;
	case ADDR_ZERO_PAGE_X:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s $%02X,X @ %02X = %02X", name, OPERAND8, OPERAND_ADD_X(OPERAND8), VAR_ZERO_PAGE_X(OPERAND8));
		break;
	case ADDR_ZERO_PAGE_Y:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s $%02X,Y @ %02X = %02X", name, OPERAND8, OPERAND_ADD_Y(OPERAND8), VAR_ZERO_PAGE_Y(OPERAND8));
		break;
	case ADDR_RELATIVE:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s $%04X", name, rpc + (char)OPERAND8 + instruction_size);
		break;
	case ADDR_ABSOLUTE:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s $%04X = %02X", name, OPERAND16, VAR_ABSOLUTE(OPERAND16));
		break;
	case ADDR_ABSOLUTE_X:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s $%04X,X @ %04X = %02X", name, OPERAND16, ADDRESS_ADD_X(OPERAND16), VAR_ABSOLUTE_X(OPERAND16));
		break;
	case ADDR_ABSOLUTE_Y:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s $%04X,Y @ %04X = %02X", name, OPERAND16, ADDRESS_ADD_Y(OPERAND16), VAR_ABSOLUTE_Y(OPERAND16));
		break;
	case ADDR_INDIRECT_X:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s ($%02X,X) @ %02X = %04X = %02X", name, OPERAND8, OPERAND_ADD_X(OPERAND8), ADDRESS_ZERO_PAGE_WRAP(OPERAND_ADD_X(OPERAND8)), VAR_INDIRECT_X(OPERAND8));
		break;
	case ADDR_INDIRECT_Y:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s ($%02X),Y = %04X @ %04X = %02X", name, OPERAND8, ADDRESS_ZERO_PAGE_WRAP(OPERAND8), ADDRESS_ADD_Y(ADDRESS_ZERO_PAGE_WRAP(OPERAND8)), VAR_INDIRECT_Y(OPERAND8));
		break;
	case ADDR_JMP_ABSOLUTE:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s $%04X", name, OPERAND16);
		break;
	case ADDR_JMP_INDIRECT:
		bufc += sprintf_s(pbuf + bufc, pbuf_size - bufc, "%s ($%04X) = %04X", name, OPERAND16, ADDRESS_INDIRECT_PAGE_WRAP(OPERAND16));
		break;
	default:
		assert(false);
		break;
	}

	if (instruction == UNK || instruction == BRK)
	{
		// output whole line
		const size_t status_start_col = 48;
		for (size_t i = 0; i < status_start_col - bufc; ++i)
			*(pbuf + bufc + i) = ' ';
		*(pbuf + status_start_col) = '\0';

		printf("%s%s\n", pbuf, statusbuf);
		return false;
	}

	// Fetch data
	unsigned char temp = 0;
	unsigned short temp_rpc = 0;
	unsigned char op8 = 0;
	unsigned short op16 = 0;

	switch (addressing_mode)
	{
	case ADDR_NONE:
		temp = 0;
		break;
	case ADDR_ACCUMULATOR:
		temp = ra;
		break;
	case ADDR_IMMEDIATE:
		temp = OPERAND8;
		break;
	case ADDR_ZERO_PAGE:
		op8 = OPERAND8;
		temp = VAR_ZERO_PAGE(op8);
		break;
	case ADDR_ZERO_PAGE_X:
		op8 = OPERAND8;
		temp = VAR_ZERO_PAGE_X(op8);
		break;
	case ADDR_ZERO_PAGE_Y:
		op8 = OPERAND8;
		temp = VAR_ZERO_PAGE_Y(op8);
		break;
	case ADDR_RELATIVE:
		temp = OPERAND8;
		break;
	case ADDR_ABSOLUTE:
		op16 = OPERAND16;
		temp = mem_read(op16);
		break;
	case ADDR_ABSOLUTE_X:
		op16 = OPERAND16;
		temp = mem_read(ADDRESS_ADD_X(op16));
		if (PAGE_CROSSED(op16, ADDRESS_ADD_X(op16)))
			instruction_cycles += page_crossed_extra_cycles;
		break;
	case ADDR_ABSOLUTE_Y:
		op16 = OPERAND16;
		temp = mem_read(ADDRESS_ADD_Y(op16));
		if (PAGE_CROSSED(op16, ADDRESS_ADD_Y(op16)))
			instruction_cycles += page_crossed_extra_cycles;
		break;
	case ADDR_INDIRECT_X:
		op8 = OPERAND8;
		temp = mem_read(ADDRESS_ZERO_PAGE_WRAP(OPERAND_ADD_X(op8)));
		break;
	case ADDR_INDIRECT_Y:
		op8 = OPERAND8;
		temp = mem_read(ADDRESS_ADD_Y(ADDRESS_ZERO_PAGE_WRAP(op8)));
		if (PAGE_CROSSED(ADDRESS_ZERO_PAGE_WRAP(op8), ADDRESS_ADD_Y(ADDRESS_ZERO_PAGE_WRAP(op8))))
			instruction_cycles += page_crossed_extra_cycles;
		break;
	case ADDR_JMP_ABSOLUTE:
		temp_rpc = OPERAND16;
		break;
	case ADDR_JMP_INDIRECT:
		temp_rpc = ADDRESS_INDIRECT_PAGE_WRAP(OPERAND16);
		break;
	default:
		break;
	}

	rpc += instruction_size;

	// Operation
	bool store = false;
	unsigned char temp_out = 0;
	switch (instruction)
	{
	case ADC:
	{
		unsigned short sum = ra + temp + GET_CARRY();
		SET_CARRY(sum > 0xff ? 1 : 0);
		/* The overflow flag is set when the sign of the addends is the same and differs from the sign of the sum */
		/* See: https://stackoverflow.com/questions/29193303/6502-emulation-proper-way-to-implement-adc-and-sbc */
		SET_OVERFLOW(~(ra ^ temp) & (ra ^ sum) & 0x80);
		ra = (unsigned char)sum;
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
	}
	break;
	case AND:
		ra = ra & temp;
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
		break;
	case ASL:
		store = true;
		temp_out = temp << 1;
		SET_ZERO(temp_out);
		SET_NEGATIVE_BY_BIT(temp_out);
		SET_CARRY(temp >> 7);
		break;
	case BCC:
		if (GET_CARRY() == 0)
		{
			temp_rpc = rpc;
			rpc = temp_rpc + (char)temp;
			instruction_cycles += BRANCH_EXTRA_CYCLE;
			if (BRANCH_NEW_PAGE())
				instruction_cycles += BRANCH_NEW_PAGE_EXTRA_CYCLE;
		}
		break;
	case BCS:
		if (GET_CARRY() == 1)
		{
			temp_rpc = rpc;
			rpc = temp_rpc + (char)temp;
			instruction_cycles += BRANCH_EXTRA_CYCLE;
			if (BRANCH_NEW_PAGE())
				instruction_cycles += BRANCH_NEW_PAGE_EXTRA_CYCLE;
		}
		break;
	case BEQ:
		if (GET_ZERO() == 1)
		{
			temp_rpc = rpc;
			rpc = temp_rpc + (char)temp;
			instruction_cycles += BRANCH_EXTRA_CYCLE;
			if (BRANCH_NEW_PAGE())
				instruction_cycles += BRANCH_NEW_PAGE_EXTRA_CYCLE;
		}
		break;
	case BIT:
		SET_NEGATIVE_BY_BIT(temp);
		SET_OVERFLOW_BY_BIT(temp);
		SET_ZERO(ra & temp);
		break;
	case BMI:
		if (GET_NEGATIVE() == 1)
		{
			temp_rpc = rpc;
			rpc = temp_rpc + (char)temp;
			instruction_cycles += BRANCH_EXTRA_CYCLE;
			if (BRANCH_NEW_PAGE())
				instruction_cycles += BRANCH_NEW_PAGE_EXTRA_CYCLE;
		}
		break;
	case BNE:
		if (GET_ZERO() == 0)
		{
			temp_rpc = rpc;
			rpc = temp_rpc + (char)temp;
			instruction_cycles += BRANCH_EXTRA_CYCLE;
			if (BRANCH_NEW_PAGE())
				instruction_cycles += BRANCH_NEW_PAGE_EXTRA_CYCLE;
		}
		break;
	case BPL:
		if (GET_NEGATIVE() == 0)
		{
			temp_rpc = rpc;
			rpc = temp_rpc + (char)temp;
			instruction_cycles += BRANCH_EXTRA_CYCLE;
			if (BRANCH_NEW_PAGE())
				instruction_cycles += BRANCH_NEW_PAGE_EXTRA_CYCLE;
		}
		break;
		//case BRK:
			// BRK is not processed now. Use default (error) branch.
	case BVC:
		if (GET_OVERFLOW() == 0)
		{
			temp_rpc = rpc;
			rpc = temp_rpc + (char)temp;
			instruction_cycles += BRANCH_EXTRA_CYCLE;
			if (BRANCH_NEW_PAGE())
				instruction_cycles += BRANCH_NEW_PAGE_EXTRA_CYCLE;
		}
		break;
	case BVS:
		if (GET_OVERFLOW() == 1)
		{
			temp_rpc = rpc;
			rpc = temp_rpc + (char)temp;
			instruction_cycles += BRANCH_EXTRA_CYCLE;
			if (BRANCH_NEW_PAGE())
				instruction_cycles += BRANCH_NEW_PAGE_EXTRA_CYCLE;
		}
		break;
	case CLC:
		SET_CARRY(0);
		break;
	case CLD:
		SET_DECIMAL(0);
		break;
	case CLI:
		SET_INTERRUPT(0);
		break;
	case CLV:
		SET_OVERFLOW(0);
		break;
	case CMP:
	{
		SET_CARRY(ra >= temp ? 1 : 0);
		unsigned char diff = ra - temp;
		SET_ZERO(diff);
		SET_NEGATIVE_BY_BIT(diff);
	}
	break;
	case CPX:
	{
		SET_CARRY(rx >= temp ? 1 : 0);
		unsigned char diff = rx - temp;
		SET_ZERO(diff);
		SET_NEGATIVE_BY_BIT(diff);
	}
	break;
	case CPY:
	{
		SET_CARRY(ry >= temp ? 1 : 0);
		unsigned char diff = ry - temp;
		SET_ZERO(diff);
		SET_NEGATIVE_BY_BIT(diff);
	}
	break;
	case DEC:
		store = true;
		temp_out = temp - 1;
		SET_ZERO(temp_out);
		SET_NEGATIVE_BY_BIT(temp_out);
		break;
	case DEX:
		rx = rx - 1;
		SET_ZERO(rx);
		SET_NEGATIVE_BY_BIT(rx);
		break;
	case DEY:
		ry = ry - 1;
		SET_ZERO(ry);
		SET_NEGATIVE_BY_BIT(ry);
		break;
	case EOR:
		ra = ra ^ temp;
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
		break;
	case INC:
		store = true;
		temp_out = temp + 1;
		SET_ZERO(temp_out);
		SET_NEGATIVE_BY_BIT(temp_out);
		break;
	case INX:
		rx = rx + 1;
		SET_ZERO(rx);
		SET_NEGATIVE_BY_BIT(rx);
		break;
	case INY:
		ry = ry + 1;
		SET_ZERO(ry);
		SET_NEGATIVE_BY_BIT(ry);
		break;
	case JMP:
		rpc = temp_rpc;
		break;
	case JSR:
		rpc--;
		STACK_PUSH_PC();
		rpc = temp_rpc;
		break;
	case LDA:
		ra = temp;
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
		break;
	case LDX:
		rx = temp;
		SET_ZERO(rx);
		SET_NEGATIVE_BY_BIT(rx);
		break;
	case LDY:
		ry = temp;
		SET_ZERO(ry);
		SET_NEGATIVE_BY_BIT(ry);
		break;
	case LSR:
		store = true;
		temp_out = temp >> 1;
		SET_ZERO(temp_out);
		SET_NEGATIVE_BY_BIT(temp_out);
		SET_CARRY(temp & 0x01);
		break;
	case NOP:
		break;
	case ORA:
		ra = ra | temp;
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
		break;
	case PHA:
		STACK_PUSH(ra);
		break;
	case PHP:
		STACK_PUSH(rp | 0x10);  // | 0x10 is temprary for test, in future the B flag should be refined.
		break;
	case PLA:
		STACK_POP(ra);
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
		break;
	case PLP:
		STACK_POP(rp);
		// bit 4 and bit 5 have not hardware implementation.
		rp = rp | 0x20; // bit 5 is always 1
		rp = rp & 0xef; // bit 4 (B flag) is normally 0
		break;
	case ROL:
		store = true;
		temp_out = (temp << 1) | GET_CARRY();
		SET_ZERO(temp_out);
		SET_NEGATIVE_BY_BIT(temp_out);
		SET_CARRY(temp >> 7);
		break;
	case ROR:
		store = true;
		temp_out = (temp >> 1) | (GET_CARRY() ? 0x80 : 0);
		SET_ZERO(temp_out);
		SET_NEGATIVE_BY_BIT(temp_out);
		SET_CARRY(temp & 0x01);
		break;
	case RTI:
		STACK_POP(rp);
		// bit 4 and bit 5 have not hardware implementation.
		rp = rp | 0x20; // bit 5 is always 1
		rp = rp & 0xef; // bit 4 (B flag) is normally 0
		STACK_POP_PC();
		break;
	case RTS:
		STACK_POP_PC();
		rpc++;
		break;
	case SBC:
	case SSBC:
		{
			// SBC is equivalent to ADC(~temp), with reversed carry flag.
			unsigned short sum = ra + (~temp) + GET_CARRY();
			SET_CARRY(sum > 0xff ? 0 : 1);
			SET_OVERFLOW(~(ra ^ (~temp)) & (ra ^ sum) & 0x80);
			ra = (unsigned char)sum;
			SET_ZERO(ra);
			SET_NEGATIVE_BY_BIT(ra);
		}
		break;
	case SEC:
		SET_CARRY(1);
		break;
	case SED:
		SET_DECIMAL(1);
	case SEI:
		SET_INTERRUPT(1);
		break;
	case STA:
		store = true;
		temp_out = ra;
		break;
	case STX:
		store = true;
		temp_out = rx;
		break;
	case STY:
		store = true;
		temp_out = ry;
		break;
	case TAX:
		rx = ra;
		SET_ZERO(rx);
		SET_NEGATIVE_BY_BIT(rx);
		break;
	case TAY:
		ry = ra;
		SET_ZERO(ry);
		SET_NEGATIVE_BY_BIT(ry);
		break;
	case TSX:
		rx = rsp;
		SET_ZERO(rx);
		SET_NEGATIVE_BY_BIT(rx);
		break;
	case TXA:
		ra = rx;
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
		break;
	case TXS:
		rsp = rx;
		break;
	case TYA:
		ra = ry;
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
		break;
	case SNOP:
		break;
	case SLAX:
		ra = temp;
		rx = temp;
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
		break;
	case SSAX:
		store = true;
		temp_out = ra & rx;
		break;
	case SDCP:
		// DEC
		store = true;
		temp_out = temp - 1;
		SET_ZERO(temp_out);
		SET_NEGATIVE_BY_BIT(temp_out);
		// CMP
		{
			SET_CARRY(ra >= temp_out ? 1 : 0);
			unsigned char diff = ra - temp_out;
			SET_ZERO(diff);
			SET_NEGATIVE_BY_BIT(diff);
		}
		break;
	case SISB:
		// INC
		store = true;
		temp_out = temp + 1;
		//SET_ZERO(temp_out);
		//SET_NEGATIVE_BY_BIT(temp_out);
		// SBC
		{
			// SBC is equivalent to ADC(~temp), with reversed carry flag.
			unsigned short sum = ra + (~temp_out) + GET_CARRY();
			SET_CARRY(sum > 0xff ? 0 : 1);
			SET_OVERFLOW(~(ra ^ (~temp_out)) & (ra ^ sum) & 0x80);
			ra = (unsigned char)sum;
			SET_ZERO(ra);
			SET_NEGATIVE_BY_BIT(ra);
		}
		break;
	case SSLO:
		// ASL
		store = true;
		temp_out = temp << 1;
		//SET_ZERO(temp_out);
		//SET_NEGATIVE_BY_BIT(temp_out);
		SET_CARRY(temp >> 7);
		// ORA
		ra = ra | temp_out;
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
		break;
	case SRLA:
		// ROL
		store = true;
		temp_out = (temp << 1) | GET_CARRY();
		//SET_ZERO(temp_out);
		//SET_NEGATIVE_BY_BIT(temp_out);
		SET_CARRY(temp >> 7);
		// AND
		ra = ra & temp_out;
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
		break;
	case SSRE:
		// LSR
		store = true;
		temp_out = temp >> 1;
		//SET_ZERO(temp_out);
		//SET_NEGATIVE_BY_BIT(temp_out);
		SET_CARRY(temp & 0x01);
		// EOR
		ra = ra ^ temp_out;
		SET_ZERO(ra);
		SET_NEGATIVE_BY_BIT(ra);
		break;
	case SRRA:
		// ROR
		store = true;
		temp_out = (temp >> 1) | (GET_CARRY() ? 0x80 : 0);
		//SET_ZERO(temp_out);
		//SET_NEGATIVE_BY_BIT(temp_out);
		SET_CARRY(temp & 0x01);
		// ADC
		{
			unsigned short sum = ra + temp_out + GET_CARRY();
			SET_CARRY(sum > 0xff ? 1 : 0);
			/* The overflow flag is set when the sign of the addends is the same and differs from the sign of the sum */
			/* See: https://stackoverflow.com/questions/29193303/6502-emulation-proper-way-to-implement-adc-and-sbc */
			SET_OVERFLOW(~(ra ^ temp_out) & (ra ^ sum) & 0x80);
			ra = (unsigned char)sum;
			SET_ZERO(ra);
			SET_NEGATIVE_BY_BIT(ra);
		}
		break;

	default:
		assert(false);
		break;
	}

	// Store data
	if (store)
	{
		switch (addressing_mode)
		{
		case ADDR_ACCUMULATOR:
			ra = temp_out;
			break;
		case ADDR_ZERO_PAGE:
			VAR_ZERO_PAGE(op8) = temp_out;
			break;
		case ADDR_ZERO_PAGE_X:
			VAR_ZERO_PAGE_X(op8) = temp_out;
			break;
		case ADDR_ZERO_PAGE_Y:
			VAR_ZERO_PAGE_Y(op8) = temp_out;
			break;
		case ADDR_ABSOLUTE:
			mem_write(op16, temp_out);
			break;
		case ADDR_ABSOLUTE_X:
			mem_write(ADDRESS_ADD_X(op16), temp_out);
			break;
		case ADDR_ABSOLUTE_Y:
			mem_write(ADDRESS_ADD_Y(op16), temp_out);
			break;
		case ADDR_INDIRECT_X:
			mem_write(ADDRESS_ZERO_PAGE_WRAP(OPERAND_ADD_X(op8)), temp_out);
			break;
		case ADDR_INDIRECT_Y:
			mem_write(ADDRESS_ADD_Y(ADDRESS_ZERO_PAGE_WRAP(op8)), temp_out);
			break;
		default:
			break;
		}
	}

	cycle += instruction_cycles;

	// output whole line
	const size_t status_start_col = 48;
	for (size_t i = 0; i < status_start_col - bufc; ++i)
		*(pbuf + bufc + i) = ' ';
	*(pbuf + status_start_col) = '\0';

	printf("%s%s\n", pbuf, statusbuf);

	return true;
}

void cpu_6502::nmi()
{
	STACK_PUSH_PC();
	STACK_PUSH(rp & 0xEF);  // push P with B flag clear
	SET_INTERRUPT(1);
	rpc = *((unsigned short*)(mem + 0xFFFA));  // NMI vector at $FFFA/$FFFB
	cycle += 7;
}
