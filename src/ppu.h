#pragma once

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240
#define DISPLAY_SCALE 2

struct ScanlineScroll {
	unsigned char scroll_x;
	unsigned char fine_x;
	unsigned char scroll_y;
	unsigned char scroll_nt;
};

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

	// Per-scanline rendering support:
	//   begin_frame()         – call before visible scanlines; clears spr0-hit, seeds scrolls
	//   check_sprite0_hit(sl) – call before each scanline's CPU cycles
	//   end_scanline(sl)      – call after each scanline's CPU cycles; snapshots scroll
	void begin_frame();
	void check_sprite0_hit(int scanline);
	void end_scanline(int scanline);

	// Dump nametable, palette, and ASCII screen art to a text file
	void export_frame(unsigned char* pScreenBits, const char* filename);

private:
	unsigned short mirror_nt_addr(unsigned short addr);
	unsigned char  ppu_mem_read(unsigned short addr);
	void           ppu_mem_write(unsigned short addr, unsigned char val);

private:
	void draw_chr(unsigned char* chr, int x, int y, unsigned char color_bit23, unsigned char* pScreenBits, bool is_sprite = false, unsigned char* bg_opaque = nullptr);

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

	// Saved scroll for lazy rendering.
	// t is clobbered by $2006 VRAM-address writes that typically follow
	// the $2005 scroll writes in a game NMI handler.  We save the raw
	// $2005 values and the $2000 NT bits here so render() can use them
	// even after t has been overwritten.
	unsigned char scroll_x_reg; // raw value from last $2005 first-write  (X)
	unsigned char scroll_y_reg; // raw value from last $2005 second-write (Y)
	unsigned char scroll_nt;    // NT select bits 1:0 from last $2000 write

	// Scroll snapshot captured at set_vblank(true) — end of visible scanlines,
	// before NMI runs and overwrites the scroll registers.
	// render() uses these so it sees the scroll that was active during rendering.
	unsigned char render_scroll_x;
	unsigned char render_fine_x;
	unsigned char render_scroll_y;
	unsigned char render_scroll_nt;

	// OAM snapshot captured at set_vblank(true) — same timing as scroll snapshot.
	// On a real NES, visible scanlines use the OAM that was set by the *previous*
	// frame's NMI (OAM DMA).  By snapshotting before NMI fires, render() sees the
	// same OAM state that FCEUX uses for the current frame's scanlines.
	unsigned char render_oam[256];

	// Per-scanline scroll snapshot: entry [sl] is captured by end_scanline(sl)
	// after the CPU has run that scanline's ~114 cycles.  render() uses
	// scanline_scroll[row*8] for each tile row so mid-frame scroll changes
	// (e.g. sprite-0-hit scroll split) are applied correctly.
	ScanlineScroll scanline_scroll[240];
};
