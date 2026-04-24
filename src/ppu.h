#pragma once

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240

class ppu_2c02
{
public:
	ppu_2c02();
	~ppu_2c02();

public:
	void load_chr_rom(unsigned char* prom, int rom_size);
	void render(unsigned char* pScreenBits);
	bool nmi_enabled();

	// CPU-side register access ($2000-$2007, mirrored throughout $2000-$3FFF)
	unsigned char cpu_read(unsigned short addr);
	void cpu_write(unsigned short addr, unsigned char val);

	// OAM DMA: copy 256 bytes of CPU page data into OAM
	void oam_dma(unsigned char* page_data);

	// Set/clear the vblank flag in PPUSTATUS
	void set_vblank(bool vb);

	// Set nametable mirroring (false=horizontal, true=vertical)
	void set_mirroring(bool vertical);

private:
	unsigned short mirror_nt_addr(unsigned short addr);
	unsigned char  ppu_mem_read(unsigned short addr);
	void           ppu_mem_write(unsigned short addr, unsigned char val);

private:
	void draw_chr(unsigned char* chr, int x, int y, unsigned char color_bit23, unsigned char* pScreenBits);

private:
	unsigned char* mem;
	unsigned char* oam;

	// PPU registers
	unsigned char reg_ctrl;      // $2000 PPUCTRL  (write)
	unsigned char reg_mask;      // $2001 PPUMASK  (write)
	unsigned char reg_status;    // $2002 PPUSTATUS (read)
	unsigned char reg_oam_addr;  // $2003 OAMADDR  (write)

	// Internal Loopy registers
	unsigned short v;        // current VRAM address (15-bit)
	unsigned short t;        // temporary VRAM address (15-bit)
	unsigned char  fine_x;   // fine X scroll (3-bit)
	unsigned char  w;        // write latch (0 or 1)
	unsigned char  data_buf; // $2007 read buffer
	bool           mirror_vertical; // false=horizontal, true=vertical
};
