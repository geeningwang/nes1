#include "stdafx.h"
#include "ppu.h"

unsigned char nes_color[64 * 3] = {
	0x75, 0x75, 0x75,
	0x27, 0x1b, 0x8f,
	0x00, 0x00, 0xab,
	0x47, 0x00, 0x9f,
	0x8f, 0x00, 0x77,
	0xab, 0x00, 0x13,
	0xa7, 0x00, 0x00,
	0x7f, 0x0b, 0x00,
	0x43, 0x2f, 0x00,
	0x00, 0x47, 0x00,
	0x00, 0x51, 0x00,
	0x00, 0x3f, 0x17,
	0x1b, 0x3f, 0x5f,
	0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
	0xbc, 0xbc, 0xbc,
	0x00, 0x73, 0xef,
	0x23, 0x3b, 0xef,
	0x83, 0x00, 0xf3,
	0xbf, 0x00, 0xbf,
	0xe7, 0x00, 0x5b,
	0xdb, 0x2b, 0x00,
	0xcb, 0x4f, 0x0f,
	0x8b, 0x73, 0x00,
	0x00, 0x97, 0x00,
	0x00, 0xab, 0x00,
	0x00, 0x93, 0x3b,
	0x00, 0x83, 0x8b,
	0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
	//
	0xff, 0xff, 0xff,
	0x3f, 0xbf, 0xff,
	0x5f, 0x97, 0xff,
	0xa7, 0x8b, 0xfd,
	0xf7, 0x7b, 0xff,
	0xff, 0x77, 0xb7,
	0xff, 0x77, 0x63,
	0xff, 0x9b, 0x3b,
	0xf3, 0xbf, 0x3f,
	0x83, 0xd3, 0x13,
	0x4f, 0xdf, 0x4b,
	0x58, 0xf8, 0x98,
	0x00, 0xeb, 0xdb,
	0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
	0xff, 0xff, 0xff,
	0xab, 0xe7, 0xff,
	0xc7, 0xd7, 0xff,
	0xd7, 0xcb, 0xff,
	0xff, 0xc7, 0xff,
	0xff, 0xc7, 0xdb,
	0xff, 0xbf, 0xb3,
	0xff, 0xdb, 0xab,
	0xff, 0xe7, 0xa3,
	0xe3, 0xff, 0xa3,
	0xab, 0xf3, 0xbf,
	0xb3, 0xff, 0xcf,
	0x9f, 0xff, 0xf3,
	0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
};

ppu_2c02::ppu_2c02()
{
	mem = new unsigned char[65536];
	memset(mem, 0, 65536);

	oam = new unsigned char[256];
	memset(oam, 0, 256);

	reg_ctrl     = 0;
	reg_mask     = 0;
	reg_status   = 0;
	reg_oam_addr = 0;
	v       = 0;
	t       = 0;
	fine_x  = 0;
	w       = 0;
	data_buf = 0;
}

ppu_2c02::~ppu_2c02()
{
	delete[] mem;
	delete[] oam;
}

void ppu_2c02::load_chr_rom(unsigned char* prom, int rom_size)
{
	// NROM (mapper 0) with just 1 8K bank is supported now. char_rom starts at 0x0000
	//
	if (rom_size != 8192)
	{
		printf("error in rom_size.\n");
		return;
	}

	// copy the rom
	memcpy(mem + 0x0000, prom, rom_size);

	return;
}

void ppu_2c02::draw_chr(unsigned char* chr, int x, int y, unsigned char color_bit23, unsigned char *pScreenBits)
{
	if (x == 0xff && y == 0xff)
	{
		return;
	}

	for (int j = 0; j < 8; ++j)
	{
		for (int i = 0; i < 8; ++i)
		{
			unsigned char color_bit0 = (*(chr + j) >> (7 - i)) & 1;
			unsigned char color_bit1 = ((*(chr + 8 + j) >> (7 - i)) & 1) << 1;

			unsigned char color = color_bit23 | color_bit1 | color_bit0;  // 4 bit color in palette
			color = mem[0x3f00 + color];  // 6 bit NES color

			unsigned index = (y + j) * 256 + (x + i);
			pScreenBits[index * 4 + 0] = nes_color[color * 3 + 2];
			pScreenBits[index * 4 + 1] = nes_color[color * 3 + 1];
			pScreenBits[index * 4 + 2] = nes_color[color * 3 + 0];
			pScreenBits[index * 4 + 3] = 0;
		}
	}

	return;
}

void ppu_2c02::render(unsigned char* pScreenBits)
{
	if (!pScreenBits)
		return;

	// Draw background, per tile
	for (int y = 0; y < 30; ++y)
	{
		for (int x = 0; x < 32; ++x)
		{
			unsigned char name = mem[0x2000 + y * 32 + x];

			// always use attribute table 0
			unsigned char att = mem[0x23c0 + ((y * 32) / 4) + (x / 4)];
			unsigned int square = (y & 0x02) + ((x >> 1) & 0x01);
			unsigned char color_bit23 = ((att >> (square * 2)) & 0x03) << 2;

			// always use pattern table 0
			unsigned char* chr = mem + 0x0000 + name * 16LL;

			draw_chr(chr, x * 8, y * 8, color_bit23, pScreenBits);
		}
	}

	// Draw sprites
	for (int i = 63; i >= 0; --i)
	{
		unsigned char x = oam[i * 4 + 3];
		unsigned char y = oam[i * 4 + 0] + 1;
		unsigned char name_index = oam[i * 4 + 1];
		unsigned char attribute = oam[i * 4 + 2];
		unsigned char color_bit23 = (attribute & 0x03) << 2;

		unsigned char* chr = mem + 0x0000 + name_index * 16LL;

		draw_chr(chr, x, y, color_bit23, pScreenBits);
	}

	return;
}

bool ppu_2c02::nmi_enabled()
{
	return (reg_ctrl & 0x80) != 0;
}

unsigned char ppu_2c02::cpu_read(unsigned short addr)
{
	addr = 0x2000 + (addr & 0x0007);

	switch (addr)
	{
	case 0x2002:  // PPUSTATUS
	{
		unsigned char result = reg_status;
		reg_status &= 0x7F;  // clear vblank flag on read
		w = 0;               // reset address latch
		return result;
	}
	case 0x2004:  // OAMDATA
		return oam[reg_oam_addr];
	case 0x2007:  // PPUDATA
	{
		unsigned char result = data_buf;
		data_buf = mem[v & 0x3FFF];
		// Palette reads are not buffered
		if ((v & 0x3FFF) >= 0x3F00)
			result = data_buf;
		v += (reg_ctrl & 0x04) ? 32 : 1;
		return result;
	}
	default:
		return 0;
	}
}

void ppu_2c02::cpu_write(unsigned short addr, unsigned char val)
{
	addr = 0x2000 + (addr & 0x0007);

	switch (addr)
	{
	case 0x2000:  // PPUCTRL
		reg_ctrl = val;
		t = (t & 0xF3FF) | ((val & 0x03) << 10);
		break;
	case 0x2001:  // PPUMASK
		reg_mask = val;
		break;
	case 0x2003:  // OAMADDR
		reg_oam_addr = val;
		break;
	case 0x2004:  // OAMDATA
		oam[reg_oam_addr++] = val;
		break;
	case 0x2005:  // PPUSCROLL (2 writes)
		if (w == 0)
		{
			t = (t & 0xFFE0) | (val >> 3);  // coarse X into t
			fine_x = val & 0x07;            // fine X
			w = 1;
		}
		else
		{
			t = (t & 0x8FFF) | ((val & 0x07) << 12);  // fine Y into t
			t = (t & 0xFC1F) | ((val >> 3) << 5);      // coarse Y into t
			w = 0;
		}
		break;
	case 0x2006:  // PPUADDR (2 writes)
		if (w == 0)
		{
			t = (t & 0x00FF) | ((val & 0x3F) << 8);
			w = 1;
		}
		else
		{
			t = (t & 0xFF00) | val;
			v = t;
			w = 0;
		}
		break;
	case 0x2007:  // PPUDATA
		mem[v & 0x3FFF] = val;
		v += (reg_ctrl & 0x04) ? 32 : 1;
		break;
	}
}

void ppu_2c02::set_vblank(bool vb)
{
	if (vb)
		reg_status |= 0x80;
	else
		reg_status &= 0x7F;
}

void ppu_2c02::oam_dma(unsigned char* page_data)
{
	for (int i = 0; i < 256; i++)
		oam[(reg_oam_addr + i) & 0xFF] = page_data[i];
}
