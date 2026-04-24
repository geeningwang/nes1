#include "stdafx.h"
#include "ppu.h"
#include <stdio.h>

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
	mirror_vertical = false;
}

ppu_2c02::~ppu_2c02()
{
	delete[] mem;
	delete[] oam;
}

void ppu_2c02::set_mirroring(bool vertical)
{
	mirror_vertical = vertical;
}

// Map a nametable address ($2000-$2FFF) to the physical 2KB VRAM
unsigned short ppu_2c02::mirror_nt_addr(unsigned short addr)
{
	addr &= 0x2FFF;  // ignore $3000-$3EFF mirror
	unsigned short nt = (addr - 0x2000) / 0x400;  // which nametable (0-3)
	unsigned short off = (addr - 0x2000) % 0x400;  // offset within nametable
	if (mirror_vertical)
	{
		// Vertical: $2000=$2800, $2400=$2C00
		return 0x2000 + (nt & 0x01) * 0x400 + off;
	}
	else
	{
		// Horizontal: $2000=$2400, $2800=$2C00
		return 0x2000 + ((nt >> 1) & 0x01) * 0x400 + off;
	}
}

unsigned char ppu_2c02::ppu_mem_read(unsigned short addr)
{
	addr &= 0x3FFF;
	if (addr >= 0x2000 && addr <= 0x3EFF)
		return mem[mirror_nt_addr(addr)];
	if (addr >= 0x3F00)
	{
		addr = 0x3F00 + (addr & 0x1F);
		// Mirrored palette entries
		if (addr == 0x3F10) addr = 0x3F00;
		if (addr == 0x3F14) addr = 0x3F04;
		if (addr == 0x3F18) addr = 0x3F08;
		if (addr == 0x3F1C) addr = 0x3F0C;
		return mem[addr];
	}
	return mem[addr];
}

void ppu_2c02::ppu_mem_write(unsigned short addr, unsigned char val)
{
	addr &= 0x3FFF;
	if (addr >= 0x2000 && addr <= 0x3EFF)
	{
		mem[mirror_nt_addr(addr)] = val;
		return;
	}
	if (addr >= 0x3F00)
	{
		addr = 0x3F00 + (addr & 0x1F);
		if (addr == 0x3F10) addr = 0x3F00;
		if (addr == 0x3F14) addr = 0x3F04;
		if (addr == 0x3F18) addr = 0x3F08;
		if (addr == 0x3F1C) addr = 0x3F0C;
		mem[addr] = val;
		return;
	}
	mem[addr] = val;
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
			// When both CHR bit-planes are 0 the pixel is transparent → always use universal BG color ($3F00)
			if ((color & 0x03) == 0) color = 0;
			color = ppu_mem_read(0x3f00 + color);  // 6 bit NES color

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

	// Fill with universal background color first
	unsigned char bg_color = ppu_mem_read(0x3F00);
	for (int p = 0; p < SCREEN_WIDTH * SCREEN_HEIGHT; ++p)
	{
		pScreenBits[p * 4 + 0] = nes_color[bg_color * 3 + 2];
		pScreenBits[p * 4 + 1] = nes_color[bg_color * 3 + 1];
		pScreenBits[p * 4 + 2] = nes_color[bg_color * 3 + 0];
		pScreenBits[p * 4 + 3] = 0;
	}

	// Draw background (PPUMASK bit 3)
	if (reg_mask & 0x08)
	{
		for (int y = 0; y < 30; ++y)
		{
			for (int x = 0; x < 32; ++x)
			{
				unsigned char name = ppu_mem_read(0x2000 + y * 32 + x);

				// Attribute table: each byte covers 4x4 tiles
				unsigned char att = ppu_mem_read(0x23c0 + (y / 4) * 8 + (x / 4));
				unsigned int square = (y & 0x02) + ((x >> 1) & 0x01);
				unsigned char color_bit23 = ((att >> (square * 2)) & 0x03) << 2;

				// BG pattern table: PPUCTRL bit 4 selects $0000 or $1000
				unsigned short bg_base = (reg_ctrl & 0x10) ? 0x1000 : 0x0000;
				unsigned char* chr = mem + bg_base + name * 16;

				draw_chr(chr, x * 8, y * 8, color_bit23, pScreenBits);
			}
		}
	}

	// Draw sprites (PPUMASK bit 4), back-to-front so sprite 0 is on top
	if (reg_mask & 0x10)
	{
		for (int i = 63; i >= 0; --i)
		{
			unsigned char sx = oam[i * 4 + 3];
			unsigned char sy = oam[i * 4 + 0] + 1;
			unsigned char name_index = oam[i * 4 + 1];
			unsigned char attribute = oam[i * 4 + 2];
			unsigned char color_bit23 = ((attribute & 0x03) << 2) | 0x10;  // sprite palette ($3F10)

			if (sy >= SCREEN_HEIGHT) continue;  // off-screen

			// Sprite pattern table: PPUCTRL bit 3 selects $0000 or $1000
			unsigned short spr_base = (reg_ctrl & 0x08) ? 0x1000 : 0x0000;
			unsigned char* chr = mem + spr_base + name_index * 16;

			draw_chr(chr, sx, sy, color_bit23, pScreenBits);
		}
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
		data_buf = ppu_mem_read(v);
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
		ppu_mem_write(v, val);
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

void ppu_2c02::export_frame(unsigned char* pScreenBits, const char* filename)
{
	FILE* f = nullptr;
	fopen_s(&f, filename, "w");
	if (!f) return;

	// --- PPU Registers ---
	fprintf(f, "=== PPU Registers ===\n");
	fprintf(f, "PPUCTRL  ($2000): %02X  (NMI:%d BGtable:%04X SPRtable:%04X inc:%d)\n",
		reg_ctrl,
		(reg_ctrl >> 7) & 1,
		(reg_ctrl & 0x10) ? 0x1000 : 0x0000,
		(reg_ctrl & 0x08) ? 0x1000 : 0x0000,
		(reg_ctrl & 0x04) ? 32 : 1);
	fprintf(f, "PPUMASK  ($2001): %02X  (BG:%d SPR:%d)\n",
		reg_mask, (reg_mask >> 3) & 1, (reg_mask >> 4) & 1);
	fprintf(f, "PPUSTATUS($2002): %02X  (VBL:%d)\n",
		reg_status, (reg_status >> 7) & 1);
	fprintf(f, "v=%04X  t=%04X  fine_x=%d  mirroring=%s\n\n",
		v, t, fine_x, mirror_vertical ? "vertical" : "horizontal");

	// --- Nametable dump ($2000-$23BF) ---
	fprintf(f, "=== Nametable ($2000-$23BF, 32x30 tile IDs) ===\n");
	for (int row = 0; row < 30; ++row)
	{
		for (int col = 0; col < 32; ++col)
			fprintf(f, "%02X ", ppu_mem_read(0x2000 + row * 32 + col));
		fprintf(f, "\n");
	}

	// --- Palette dump ($3F00-$3F1F) ---
	fprintf(f, "\n=== Palette ($3F00-$3F1F) ===\n");
	fprintf(f, "BG:  ");
	for (int i = 0; i < 16; ++i) fprintf(f, "%02X ", ppu_mem_read(0x3F00 + i));
	fprintf(f, "\nSPR: ");
	for (int i = 0; i < 16; ++i) fprintf(f, "%02X ", ppu_mem_read(0x3F10 + i));
	fprintf(f, "\n");

	// --- Active sprites (OAM) ---
	fprintf(f, "\n=== Active Sprites (OAM) ===\n");
	fprintf(f, "  # |  Y   X  Tile Attr\n");
	for (int i = 0; i < 64; ++i)
	{
		unsigned char sy = oam[i * 4 + 0];
		unsigned char st = oam[i * 4 + 1];
		unsigned char sa = oam[i * 4 + 2];
		unsigned char sx = oam[i * 4 + 3];
		if (sy < 0xEF)  // visible
			fprintf(f, " %2d | %3d %3d   %02X   %02X\n", i, sy + 1, sx, st, sa);
	}

	// --- ASCII art (1 char per 8x8 tile, brightness from pixel buffer) ---
	fprintf(f, "\n=== ASCII Screen (32x30 tiles) ===\n");
	const char* shades = " .:-=+*#%%@";
	int nshades = 10;
	if (pScreenBits)
	{
		for (int ty = 0; ty < 30; ++ty)
		{
			for (int tx = 0; tx < 32; ++tx)
			{
				// Average brightness of the 8x8 tile
				int sum = 0;
				for (int py = 0; py < 8; ++py)
					for (int px = 0; px < 8; ++px)
					{
						int idx = ((ty * 8 + py) * 256 + (tx * 8 + px)) * 4;
						sum += pScreenBits[idx + 0];  // B
						sum += pScreenBits[idx + 1];  // G
						sum += pScreenBits[idx + 2];  // R
					}
				int brightness = sum / (64 * 3);  // 0-255
				int si = brightness * (nshades - 1) / 255;
				fprintf(f, "%c", shades[si]);
			}
			fprintf(f, "\n");
		}
	}
	else
	{
		fprintf(f, "(no pixel buffer - render first)\n");
	}

	fclose(f);

	// Save BMP screenshot
	if (pScreenBits)
	{
		// Build .bmp filename by replacing extension
		char bmp_filename[512];
		strncpy_s(bmp_filename, filename, sizeof(bmp_filename) - 1);
		char* dot = strrchr(bmp_filename, '.');
		if (dot) strcpy_s(dot, 5, ".bmp");
		else strncat_s(bmp_filename, sizeof(bmp_filename), ".bmp", 4);

		FILE* bf = nullptr;
		fopen_s(&bf, bmp_filename, "wb");
		if (bf)
		{
			// BMP file header (14 bytes)
			unsigned int pix_size = SCREEN_WIDTH * SCREEN_HEIGHT * 4;
			unsigned int file_size = 14 + 40 + pix_size;
			unsigned char bfh[14] = {
				'B','M',
				(unsigned char)(file_size),(unsigned char)(file_size>>8),(unsigned char)(file_size>>16),(unsigned char)(file_size>>24),
				0,0,0,0,
				54,0,0,0
			};
			fwrite(bfh, 1, 14, bf);

			// BITMAPINFOHEADER (40 bytes) — negative height = top-down
			int bih_w = SCREEN_WIDTH;
			int bih_h = -SCREEN_HEIGHT;
			unsigned short planes = 1, bits = 32;
			unsigned int comp = 0, img_sz = 0, clr = 0;
			int ppm = 2835;
			unsigned int hdr = 40;
			unsigned char dib[40];
			memcpy(dib+ 0, &hdr,   4);
			memcpy(dib+ 4, &bih_w, 4);
			memcpy(dib+ 8, &bih_h, 4);
			memcpy(dib+12, &planes,2);
			memcpy(dib+14, &bits,  2);
			memcpy(dib+16, &comp,  4);
			memcpy(dib+20, &img_sz,4);
			memcpy(dib+24, &ppm,   4);
			memcpy(dib+28, &ppm,   4);
			memcpy(dib+32, &clr,   4);
			memcpy(dib+36, &clr,   4);
			fwrite(dib, 1, 40, bf);

			fwrite(pScreenBits, 1, pix_size, bf);
			fclose(bf);
		}
	}
}
