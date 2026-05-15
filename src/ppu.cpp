#include "stdafx.h"
#include "ppu.h"
#include "cpu.h"
#include <stdio.h>

// Emphasis-aware palette: nes_color_emph[emph_idx][nesColor*3 + {R,G,B}]
// emph_idx = (PPUMASK >> 5) & 7  (bits 7-5 of PPUMASK = B/G/R emphasis)
// Values measured from FCEUX 2.6.6 via emu.getscreenpixel().
// Index 0 = no emphasis, 1 = R, 2 = G, 3 = RG, 4 = B, 5 = RB, 6 = GB, 7 = RGB.
unsigned char nes_color_emph[8][64 * 3] = {
    { // emph=none
        0x75,0x75,0x75, 0x24,0x18,0x8E, 0x00,0x00,0xAA, 0x45,0x00,0x9E,
        0x8E,0x00,0x75, 0xAA,0x00,0x10, 0xA6,0x00,0x00, 0x7D,0x08,0x00,
        0x41,0x2C,0x00, 0x00,0x45,0x00, 0x00,0x51,0x00, 0x00,0x3C,0x14,
        0x18,0x3C,0x5D, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xBE,0xBE,0xBE, 0x00,0x71,0xEF, 0x20,0x38,0xEF, 0x82,0x00,0xF3,
        0xBE,0x00,0xBE, 0xE7,0x00,0x59, 0xDB,0x28,0x00, 0xCB,0x4D,0x0C,
        0x8A,0x71,0x00, 0x00,0x96,0x00, 0x00,0xAA,0x00, 0x00,0x92,0x38,
        0x00,0x82,0x8A, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xFF,0xFF,0xFF, 0x3C,0xBE,0xFF, 0x5D,0x96,0xFF, 0xCF,0x8A,0xFF,
        0xF7,0x79,0xFF, 0xFF,0x75,0xB6, 0xFF,0x75,0x61, 0xFF,0x9A,0x38,
        0xF3,0xBE,0x3C, 0x82,0xD3,0x10, 0x4D,0xDF,0x49, 0x59,0xFB,0x9A,
        0x00,0xEB,0xDB, 0x79,0x79,0x79, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xFF,0xFF,0xFF, 0xAA,0xE7,0xFF, 0xC7,0xD7,0xFF, 0xD7,0xCB,0xFF,
        0xFF,0xC7,0xFF, 0xFF,0xC7,0xDB, 0xFF,0xBE,0xB2, 0xFF,0xDB,0xAA,
        0xFF,0xE7,0xA2, 0xE3,0xFF,0xA2, 0xAA,0xF3,0xBE, 0xB2,0xFF,0xCF,
        0x9E,0xFF,0xF3, 0xC7,0xC7,0xC7, 0x00,0x00,0x00, 0x00,0x00,0x00,
    },
    { // emph=R
        0x90,0x6B,0x56, 0x2C,0x15,0x69, 0x00,0x00,0x7E, 0x55,0x00,0x75,
        0xAF,0x00,0x56, 0xD2,0x00,0x0B, 0xCD,0x00,0x00, 0x9A,0x07,0x00,
        0x50,0x28,0x00, 0x00,0x3F,0x00, 0x00,0x4A,0x00, 0x00,0x36,0x0E,
        0x1D,0x36,0x45, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xEB,0xAD,0x8D, 0x00,0x67,0xB1, 0x27,0x33,0xB1, 0xA1,0x00,0xB4,
        0xEB,0x00,0x8D, 0xFF,0x00,0x42, 0xFF,0x24,0x00, 0xFB,0x46,0x08,
        0xAA,0x67,0x00, 0x00,0x89,0x00, 0x00,0x9B,0x00, 0x00,0x85,0x29,
        0x00,0x76,0x66, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xFF,0xE9,0xBD, 0x4A,0xAD,0xBD, 0x73,0x89,0xBD, 0xFF,0x7E,0xBD,
        0xFF,0x6E,0xBD, 0xFF,0x6B,0x87, 0xFF,0x6B,0x48, 0xFF,0x8C,0x29,
        0xFF,0xAD,0x2C, 0xA1,0xC1,0x0B, 0x5F,0xCC,0x36, 0x6E,0xE5,0x72,
        0x00,0xD7,0xA2, 0x95,0x6E,0x59, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xFF,0xE9,0xBD, 0xD2,0xD3,0xBD, 0xF6,0xC4,0xBD, 0xFF,0xB9,0xBD,
        0xFF,0xB6,0xBD, 0xFF,0xB6,0xA2, 0xFF,0xAD,0x84, 0xFF,0xC8,0x7E,
        0xFF,0xD3,0x78, 0xFF,0xE9,0x78, 0xD2,0xDE,0x8D, 0xDC,0xE9,0x99,
        0xC3,0xE9,0xB4, 0xF6,0xB6,0x93, 0x00,0x00,0x00, 0x00,0x00,0x00,
    },
    { // emph=G
        0x5C,0x7F,0x67, 0x1C,0x1A,0x7D, 0x00,0x00,0x95, 0x36,0x00,0x8B,
        0x70,0x00,0x67, 0x86,0x00,0x0E, 0x83,0x00,0x00, 0x63,0x08,0x00,
        0x33,0x2F,0x00, 0x00,0x4A,0x00, 0x00,0x57,0x00, 0x00,0x41,0x11,
        0x13,0x41,0x52, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0x96,0xCE,0xA7, 0x00,0x7A,0xD2, 0x19,0x3C,0xD2, 0x67,0x00,0xD6,
        0x96,0x00,0xA7, 0xB7,0x00,0x4E, 0xAD,0x2B,0x00, 0xA1,0x53,0x0A,
        0x6D,0x7A,0x00, 0x00,0xA2,0x00, 0x00,0xB8,0x00, 0x00,0x9E,0x31,
        0x00,0x8D,0x79, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xCA,0xFF,0xE0, 0x2F,0xCE,0xE0, 0x49,0xA2,0xE0, 0xA4,0x95,0xE0,
        0xC4,0x83,0xE0, 0xCA,0x7F,0xA0, 0xCA,0x7F,0x55, 0xCA,0xA7,0x31,
        0xC0,0xCE,0x34, 0x67,0xE5,0x0E, 0x3D,0xF2,0x40, 0x46,0xFF,0x87,
        0x00,0xFF,0xC1, 0x60,0x83,0x6A, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xCA,0xFF,0xE0, 0x86,0xFA,0xE0, 0x9E,0xE9,0xE0, 0xAA,0xDC,0xE0,
        0xCA,0xD8,0xE0, 0xCA,0xD8,0xC1, 0xCA,0xCE,0x9C, 0xCA,0xED,0x95,
        0xCA,0xFA,0x8E, 0xB4,0xFF,0x8E, 0x86,0xFF,0xA7, 0x8D,0xFF,0xB6,
        0x7D,0xFF,0xD6, 0x9E,0xD8,0xAF, 0x00,0x00,0x00, 0x00,0x00,0x00,
    },
    { // emph=RG
        0x77,0x72,0x4C, 0x24,0x17,0x5C, 0x00,0x00,0x6F, 0x46,0x00,0x67,
        0x90,0x00,0x4C, 0xAD,0x00,0x0A, 0xA9,0x00,0x00, 0x7F,0x07,0x00,
        0x42,0x2B,0x00, 0x00,0x43,0x00, 0x00,0x4F,0x00, 0x00,0x3A,0x0D,
        0x18,0x3A,0x3C, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xC1,0xBA,0x7C, 0x00,0x6E,0x9C, 0x20,0x36,0x9C, 0x84,0x00,0x9E,
        0xC1,0x00,0x7C, 0xEB,0x00,0x3A, 0xDF,0x27,0x00, 0xCE,0x4B,0x07,
        0x8C,0x6E,0x00, 0x00,0x92,0x00, 0x00,0xA6,0x00, 0x00,0x8F,0x24,
        0x00,0x7F,0x5A, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xFF,0xF9,0xA6, 0x3D,0xBA,0xA6, 0x5E,0x92,0xA6, 0xD2,0x87,0xA6,
        0xFB,0x76,0xA6, 0xFF,0x72,0x76, 0xFF,0x72,0x3F, 0xFF,0x96,0x24,
        0xF7,0xBA,0x27, 0x84,0xCE,0x0A, 0x4E,0xDA,0x2F, 0x5A,0xF5,0x64,
        0x00,0xE6,0x8F, 0x7B,0x76,0x4F, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xFF,0xF9,0xA6, 0xAD,0xE2,0xA6, 0xCA,0xD2,0xA6, 0xDB,0xC6,0xA6,
        0xFF,0xC3,0xA6, 0xFF,0xC3,0x8F, 0xFF,0xBA,0x74, 0xFF,0xD6,0x6F,
        0xFF,0xE2,0x69, 0xE7,0xF9,0x69, 0xAD,0xEE,0x7C, 0xB5,0xF9,0x87,
        0xA0,0xF9,0x9E, 0xCA,0xC3,0x81, 0x00,0x00,0x00, 0x00,0x00,0x00,
    },
    { // emph=B
        0x69,0x78,0x95, 0x20,0x18,0xB5, 0x00,0x00,0xD9, 0x3E,0x00,0xC9,
        0x80,0x00,0x95, 0x99,0x00,0x14, 0x96,0x00,0x00, 0x71,0x08,0x00,
        0x3A,0x2D,0x00, 0x00,0x46,0x00, 0x00,0x53,0x00, 0x00,0x3D,0x19,
        0x15,0x3D,0x76, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xAB,0xC2,0xF2, 0x00,0x73,0xFF, 0x1C,0x39,0xFF, 0x75,0x00,0xFF,
        0xAB,0x00,0xF2, 0xD1,0x00,0x71, 0xC6,0x29,0x00, 0xB7,0x4E,0x0F,
        0x7C,0x73,0x00, 0x00,0x99,0x00, 0x00,0xAE,0x00, 0x00,0x95,0x47,
        0x00,0x85,0xB0, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xE6,0xFF,0xFF, 0x36,0xC2,0xFF, 0x54,0x99,0xFF, 0xBB,0x8D,0xFF,
        0xDF,0x7C,0xFF, 0xE6,0x78,0xE8, 0xE6,0x78,0x7B, 0xE6,0x9D,0x47,
        0xDB,0xC2,0x4C, 0x75,0xD8,0x14, 0x45,0xE4,0x5D, 0x50,0xFF,0xC4,
        0x00,0xF1,0xFF, 0x6D,0x7C,0x9A, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xE6,0xFF,0xFF, 0x99,0xEC,0xFF, 0xB4,0xDC,0xFF, 0xC2,0xD0,0xFF,
        0xE6,0xCC,0xFF, 0xE6,0xCC,0xFF, 0xE6,0xC2,0xE3, 0xE6,0xE0,0xD9,
        0xE6,0xEC,0xCE, 0xCD,0xFF,0xCE, 0x99,0xF9,0xF2, 0xA1,0xFF,0xFF,
        0x8E,0xFF,0xFF, 0xB4,0xCC,0xFE, 0x00,0x00,0x00, 0x00,0x00,0x00,
    },
    { // emph=RB
        0x77,0x6A,0x72, 0x24,0x15,0x8B, 0x00,0x00,0xA6, 0x46,0x00,0x9A,
        0x91,0x00,0x72, 0xAD,0x00,0x0F, 0xA9,0x00,0x00, 0x7F,0x07,0x00,
        0x42,0x27,0x00, 0x00,0x3E,0x00, 0x00,0x49,0x00, 0x00,0x36,0x13,
        0x18,0x36,0x5B, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xC2,0xAC,0xBA, 0x00,0x66,0xE9, 0x20,0x32,0xE9, 0x84,0x00,0xED,
        0xC2,0x00,0xBA, 0xEC,0x00,0x57, 0xE0,0x24,0x00, 0xCF,0x45,0x0B,
        0x8D,0x66,0x00, 0x00,0x88,0x00, 0x00,0x9A,0x00, 0x00,0x84,0x36,
        0x00,0x76,0x87, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xFF,0xE7,0xF9, 0x3D,0xAC,0xF9, 0x5F,0x88,0xF9, 0xD3,0x7D,0xF9,
        0xFC,0x6D,0xF9, 0xFF,0x6A,0xB2, 0xFF,0x6A,0x5E, 0xFF,0x8B,0x36,
        0xF8,0xAC,0x3A, 0x84,0xBF,0x0F, 0x4E,0xCA,0x47, 0x5B,0xE3,0x96,
        0x00,0xD5,0xD6, 0x7B,0x6D,0x76, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xFF,0xE7,0xF9, 0xAD,0xD1,0xF9, 0xCB,0xC3,0xF9, 0xDB,0xB8,0xF9,
        0xFF,0xB4,0xF9, 0xFF,0xB4,0xD6, 0xFF,0xAC,0xAE, 0xFF,0xC6,0xA6,
        0xFF,0xD1,0x9E, 0xE8,0xE7,0x9E, 0xAD,0xDC,0xBA, 0xB6,0xE7,0xCA,
        0xA1,0xE7,0xED, 0xCB,0xB4,0xC2, 0x00,0x00,0x00, 0x00,0x00,0x00,
    },
    { // emph=GB
        0x56,0x73,0x0B, 0x1A,0x17,0x0E, 0x00,0x00,0x11, 0x33,0x00,0x0F,
        0x69,0x00,0x0B, 0x7D,0x00,0x01, 0x7B,0x00,0x00, 0x5C,0x07,0x00,
        0x30,0x2B,0x00, 0x00,0x44,0x00, 0x00,0x4F,0x00, 0x00,0x3B,0x02,
        0x11,0x3B,0x09, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0x8C,0xBB,0x13, 0x00,0x6F,0x18, 0x17,0x37,0x18, 0x60,0x00,0x18,
        0x8C,0x00,0x13, 0xAB,0x00,0x08, 0xA2,0x27,0x00, 0x96,0x4B,0x01,
        0x66,0x6F,0x00, 0x00,0x94,0x00, 0x00,0xA7,0x00, 0x00,0x90,0x05,
        0x00,0x80,0x0D, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xBC,0xFB,0x19, 0x2C,0xBB,0x19, 0x44,0x94,0x19, 0x99,0x88,0x19,
        0xB7,0x77,0x19, 0xBC,0x73,0x12, 0xBC,0x73,0x09, 0xBC,0x97,0x05,
        0xB4,0xBB,0x06, 0x60,0xD0,0x01, 0x39,0xDC,0x07, 0x41,0xF7,0x0F,
        0x00,0xE7,0x16, 0x59,0x77,0x0C, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xBC,0xFB,0x19, 0x7D,0xE3,0x19, 0x93,0xD4,0x19, 0x9F,0xC8,0x19,
        0xBC,0xC4,0x19, 0xBC,0xC4,0x16, 0xBC,0xBB,0x11, 0xBC,0xD8,0x11,
        0xBC,0xE3,0x10, 0xA8,0xFB,0x10, 0x7D,0xEF,0x13, 0x83,0xFB,0x14,
        0x75,0xFB,0x18, 0x93,0xC4,0x14, 0x00,0x00,0x00, 0x00,0x00,0x00,
    },
    { // emph=RGB
        0x57,0x57,0x57, 0x1B,0x12,0x6A, 0x00,0x00,0x7F, 0x33,0x00,0x76,
        0x6A,0x00,0x57, 0x7F,0x00,0x0C, 0x7C,0x00,0x00, 0x5D,0x06,0x00,
        0x30,0x21,0x00, 0x00,0x33,0x00, 0x00,0x3C,0x00, 0x00,0x2D,0x0F,
        0x12,0x2D,0x45, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0x8E,0x8E,0x8E, 0x00,0x54,0xB3, 0x18,0x2A,0xB3, 0x61,0x00,0xB6,
        0x8E,0x00,0x8E, 0xAD,0x00,0x42, 0xA4,0x1E,0x00, 0x98,0x39,0x09,
        0x67,0x54,0x00, 0x00,0x70,0x00, 0x00,0x7F,0x00, 0x00,0x6D,0x2A,
        0x00,0x61,0x67, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xBF,0xBF,0xBF, 0x2D,0x8E,0xBF, 0x45,0x70,0xBF, 0x9B,0x67,0xBF,
        0xB9,0x5A,0xBF, 0xBF,0x57,0x88, 0xBF,0x57,0x48, 0xBF,0x73,0x2A,
        0xB6,0x8E,0x2D, 0x61,0x9E,0x0C, 0x39,0xA7,0x36, 0x42,0xBC,0x73,
        0x00,0xB0,0xA4, 0x5A,0x5A,0x5A, 0x00,0x00,0x00, 0x00,0x00,0x00,
        0xBF,0xBF,0xBF, 0x7F,0xAD,0xBF, 0x95,0xA1,0xBF, 0xA1,0x98,0xBF,
        0xBF,0x95,0xBF, 0xBF,0x95,0xA4, 0xBF,0x8E,0x85, 0xBF,0xA4,0x7F,
        0xBF,0xAD,0x79, 0xAA,0xBF,0x79, 0x7F,0xB6,0x8E, 0x85,0xBF,0x9B,
        0x76,0xBF,0xB6, 0x95,0x95,0x95, 0x00,0x00,0x00, 0x00,0x00,0x00,
    },
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
	scroll_x_reg = 0;
	scroll_y_reg = 0;
	scroll_nt    = 0;
	render_scroll_x  = 0;
	render_fine_x    = 0;
	render_scroll_y  = 0;
	render_scroll_nt = 0;
	memset(render_oam, 0xFF, 256);  // 0xFF = hide all sprites (y=256, off-screen)
	memset(render_nt,  0x00, sizeof(render_nt));
	memset(render_pal, 0x00, sizeof(render_pal));
	memset(scanline_scroll, 0, sizeof(scanline_scroll));
	mid_render_v_reset = false;
	mid_render_v_new   = 0;
	nmi_scroll_x  = 0;
	nmi_fine_x    = 0;
	nmi_scroll_y  = 0;
	nmi_scroll_nt = 0;
	pre_render_scroll_y    = 0;
	pre_render_scroll_nt_y = 0;
	memset(scanline_pal, 0, sizeof(scanline_pal));
	framebuf = nullptr;
	cpu_ptr           = nullptr;
	cur_scanline      = -1;
	nt_write_count    = 0;
	sl_cpu_cycle_base = 0;
	memset(nt_snapshot, 0, sizeof(nt_snapshot));
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

void ppu_2c02::draw_chr(unsigned char* chr, int x, int y, unsigned char color_bit23, unsigned char *pScreenBits, bool is_sprite, unsigned char* bg_opaque)
{
	if (x == 0xff && y == 0xff)
	{
		return;
	}

	unsigned emph_idx = (reg_mask >> 5) & 7;
	for (int j = 0; j < 8; ++j)
	{
		for (int i = 0; i < 8; ++i)
		{
			unsigned char color_bit0 = (*(chr + j) >> (7 - i)) & 1;
			unsigned char color_bit1 = ((*(chr + 8 + j) >> (7 - i)) & 1) << 1;

			unsigned char color = color_bit23 | color_bit1 | color_bit0;  // 4 bit color in palette
			bool opaque = (color & 0x03) != 0;
			// When both CHR bit-planes are 0 the pixel is transparent
			if (!opaque) {
				if (is_sprite) continue;  // sprite transparent pixel: leave background as-is
				color = 0;                // background: use universal BG color ($3F00)
			}
			// Read palette from snapshot (pre-NMI). Apply same mirrors as hardware.
			unsigned char pal_idx = (unsigned char)(color & 0x1F);
			if (pal_idx == 0x10) pal_idx = 0x00;
			if (pal_idx == 0x14) pal_idx = 0x04;
			if (pal_idx == 0x18) pal_idx = 0x08;
			if (pal_idx == 0x1C) pal_idx = 0x0C;
			color = render_pal[pal_idx];  // 6-bit NES palette index
			if (reg_mask & 0x01) color &= 0x30;    // greyscale: force to grey ramp
			int px = x + i;
			int py = y + j;
			if (px < 0 || px >= SCREEN_WIDTH || py < 0 || py >= SCREEN_HEIGHT) continue;
			unsigned index = (unsigned)(py * SCREEN_WIDTH + px);
			pScreenBits[index * 4 + 0] = nes_color_emph[emph_idx][color * 3 + 2];
			pScreenBits[index * 4 + 1] = nes_color_emph[emph_idx][color * 3 + 1];
			pScreenBits[index * 4 + 2] = nes_color_emph[emph_idx][color * 3 + 0];
			pScreenBits[index * 4 + 3] = 0;
			// Mark opaque BG pixels so behind-BG sprites can check priority
			if (bg_opaque && opaque)
				bg_opaque[index] = 1;
		}
	}

	return;
}

void ppu_2c02::render(unsigned char* pScreenBits)
{
	// When per-scanline live rendering is active, pixels were already written
	// to framebuf by render_scanline() during the visible scanline loop.
	if (framebuf != nullptr) return;

	if (!pScreenBits)
		return;

	// When rendering is completely disabled (PPUMASK bits 3 & 4 both 0),
	// output the backdrop color ($3F00) — same as real hardware and FCEUX.
	if ((reg_mask & 0x18) == 0)
	{
		unsigned char bg = render_pal[0];
		if (reg_mask & 0x01) bg &= 0x30;  // greyscale
		unsigned emph = (reg_mask >> 5) & 7;
		for (int p = 0; p < SCREEN_WIDTH * SCREEN_HEIGHT; ++p)
		{
			pScreenBits[p*4+0] = nes_color_emph[emph][bg*3+2];
			pScreenBits[p*4+1] = nes_color_emph[emph][bg*3+1];
			pScreenBits[p*4+2] = nes_color_emph[emph][bg*3+0];
			pScreenBits[p*4+3] = 0;
		}
		return;
	}

	// Fill with universal background color first
	// Use render_pal snapshot (pre-NMI palette)
	auto snap_pal = [&](unsigned short addr) -> unsigned char {
		addr &= 0x1F;
		if (addr == 0x10) addr = 0x00;
		if (addr == 0x14) addr = 0x04;
		if (addr == 0x18) addr = 0x08;
		if (addr == 0x1C) addr = 0x0C;
		return render_pal[addr];
	};
	unsigned char bg_color = snap_pal(0);
	if (reg_mask & 0x01) bg_color &= 0x30;  // greyscale: force to grey ramp
	unsigned emph_idx = (reg_mask >> 5) & 7;
	// Initial fill with BG color (covers pixels not drawn by the BG tile loop,
	// e.g. leftmost column if PPUMASK bit 1 is clear, or sprite-only areas).
	for (int p = 0; p < SCREEN_WIDTH * SCREEN_HEIGHT; ++p)
	{
		pScreenBits[p * 4 + 0] = nes_color_emph[emph_idx][bg_color * 3 + 2];
		pScreenBits[p * 4 + 1] = nes_color_emph[emph_idx][bg_color * 3 + 1];
		pScreenBits[p * 4 + 2] = nes_color_emph[emph_idx][bg_color * 3 + 0];
		pScreenBits[p * 4 + 3] = 0;
	}

	// bg_opaque[px + py*W] = 1 where a non-transparent BG tile pixel was drawn.
	// Used to implement sprite behind-BG priority (sprite only shows on transparent BG).
	unsigned char bg_opaque[SCREEN_WIDTH * SCREEN_HEIGHT];
	memset(bg_opaque, 0, sizeof(bg_opaque));

	// Draw background (PPUMASK bit 3) — rendered per-scanline so that per-scanline
	// palette updates (scanline_pal[sl]) and horizontal scroll changes are applied
	// correctly, matching FCEUX's live-palette rendering behaviour.
	if (reg_mask & 0x08)
	{
		unsigned short bg_base = (reg_ctrl & 0x10) ? 0x1000 : 0x0000;

		for (int sl = 0; sl < SCREEN_HEIGHT; ++sl)
		{
			// Per-scanline palette: update render_pal so draw_chr uses the correct colours.
			memcpy(render_pal, scanline_pal[sl], 32);

			// Horizontal scroll for this scanline (can change per-scanline after sprite-0-hit)
			int fine_x_r = (int)scanline_scroll[sl].fine_x;
			int coarse_x = (int)(scanline_scroll[sl].scroll_x >> 3);
			int nt_sel   = (int)scanline_scroll[sl].scroll_nt;

			// Vertical position: per-scanline abs_y stored by begin_frame() /
			// end_scanline().  Normally = seed_scroll_y + sl; after a mid-render
			// $2006 write it resets to the new V position (V-reset reseeding).
			int abs_y        = (int)scanline_scroll[sl].scroll_y % 480;
			int extra_nt_y   = abs_y / 240;                 // 0 or 1
			int local_y      = abs_y % 240;
			int tile_row     = local_y / 8;                  // nametable tile row (0-29)
			int pixel_in_tile = local_y % 8;                 // pixel row within tile (0-7)
			int nt_y_base    = (((nt_sel >> 1) & 1) + extra_nt_y) & 1;

			for (int col = 0; col <= 32; ++col)
			{
				int abs_col   = coarse_x + col;
				int nt_h      = ((nt_sel & 0x01) + (abs_col / 32)) & 0x01;
				int nt_v      = nt_y_base;
				int cur_nt    = nt_v * 2 + nt_h;
				int local_col = abs_col % 32;

				unsigned short nt_base = 0x2000 + cur_nt * 0x400;
				unsigned char  name    = render_nt[(nt_base - 0x2000) + tile_row * 32 + local_col];

				unsigned short att_base   = nt_base + 0x3C0;
				unsigned char  att        = render_nt[(att_base - 0x2000) + (tile_row / 4) * 8 + (local_col / 4)];
				int            square     = (tile_row & 0x02) + ((local_col >> 1) & 0x01);
				unsigned char  color_bit23 = ((att >> (square * 2)) & 0x03) << 2;

				unsigned char* chr = mem + bg_base + name * 16;

				// Render the single pixel row (pixel_in_tile) of this tile onto screen row sl
				int screen_x = col * 8 - fine_x_r;
				for (int i = 0; i < 8; ++i)
				{
					int px = screen_x + i;
					if (px < 0 || px >= SCREEN_WIDTH) continue;

					unsigned char bit0 = (chr[pixel_in_tile]     >> (7 - i)) & 1;
					unsigned char bit1 = ((chr[8 + pixel_in_tile] >> (7 - i)) & 1) << 1;
					unsigned char color = color_bit23 | bit1 | bit0;
					bool opaque = (color & 0x03) != 0;

					if (!opaque) color = 0;  // transparent: use universal BG colour ($3F00)

					unsigned char pal_idx = color & 0x1F;
					if (pal_idx == 0x10) pal_idx = 0x00;
					if (pal_idx == 0x14) pal_idx = 0x04;
					if (pal_idx == 0x18) pal_idx = 0x08;
					if (pal_idx == 0x1C) pal_idx = 0x0C;
					unsigned char nes_color = render_pal[pal_idx];
					if (reg_mask & 0x01) nes_color &= 0x30;

					unsigned idx = (unsigned)(sl * SCREEN_WIDTH + px);
					pScreenBits[idx * 4 + 0] = nes_color_emph[emph_idx][nes_color * 3 + 2];
					pScreenBits[idx * 4 + 1] = nes_color_emph[emph_idx][nes_color * 3 + 1];
					pScreenBits[idx * 4 + 2] = nes_color_emph[emph_idx][nes_color * 3 + 0];
					pScreenBits[idx * 4 + 3] = 0;

					if (opaque) bg_opaque[idx] = 1;
				}
			}
		}
		// Restore render_pal to the last scanline's snapshot for sprite rendering below.
		memcpy(render_pal, scanline_pal[SCREEN_HEIGHT - 1], 32);
	}

	// Draw sprites (PPUMASK bit 4), back-to-front so sprite 0 ends up on top.
	// Implements H-flip (attribute bit 6), V-flip (attribute bit 7), and
	// behind-BG priority (attribute bit 5: sprite only shows on transparent BG pixels).
	if (reg_mask & 0x10)
	{
		unsigned short spr_base = (reg_ctrl & 0x08) ? 0x1000 : 0x0000;
		for (int spr = 63; spr >= 0; --spr)
		{
		int            sx         = (int)render_oam[spr * 4 + 3];
		int            sy         = (int)render_oam[spr * 4 + 0] + 1;  // int prevents 0xFF+1 wrap
		unsigned char  name_index = render_oam[spr * 4 + 1];
		unsigned char  attribute  = render_oam[spr * 4 + 2];
			unsigned char  color_bit23 = ((attribute & 0x03) << 2) | 0x10;  // sprite palette ($3F10)
			bool           h_flip     = (attribute & 0x40) != 0;
			bool           v_flip     = (attribute & 0x80) != 0;
			bool           behind_bg  = (attribute & 0x20) != 0;

			if (sy >= SCREEN_HEIGHT) continue;  // off-screen (int arithmetic catches 0xFF+1=256)

			unsigned char* chr = mem + spr_base + name_index * 16;

			for (int j = 0; j < 8; ++j)
			{
				int j_chr = v_flip ? (7 - j) : j;
				for (int i = 0; i < 8; ++i)
				{
					int i_chr = h_flip ? (7 - i) : i;
					unsigned char color_bit0 = (chr[j_chr]     >> (7 - i_chr)) & 1;
					unsigned char color_bit1 = ((chr[8 + j_chr] >> (7 - i_chr)) & 1) << 1;
					unsigned char color = color_bit23 | color_bit1 | color_bit0;
					if ((color & 0x03) == 0) continue;  // transparent sprite pixel

					int px = sx + i;
					int py = sy + j;
					if (px < 0 || px >= SCREEN_WIDTH || py < 0 || py >= SCREEN_HEIGHT) continue;

					unsigned index = (unsigned)(py * SCREEN_WIDTH + px);
					if (behind_bg && bg_opaque[index]) continue;  // hidden behind opaque BG

				unsigned char col = snap_pal(color);
					if (reg_mask & 0x01) col &= 0x30;
					pScreenBits[index * 4 + 0] = nes_color_emph[emph_idx][col * 3 + 2];
					pScreenBits[index * 4 + 1] = nes_color_emph[emph_idx][col * 3 + 1];
					pScreenBits[index * 4 + 2] = nes_color_emph[emph_idx][col * 3 + 0];
					pScreenBits[index * 4 + 3] = 0;
				}
			}
		}
	}

	return;
}

// Per-scanline live rendering: renders one visible scanline directly into fb,
// reading tile data from live mem[] (not the frozen render_nt[] snapshot).
// Called inside the visible scanline loop AFTER end_scanline() so it sees VRAM
// writes the CPU made during this scanline's run_ppu(341) call.
// Palette comes from scanline_pal[sl] (captured BEFORE the CPU run by begin_scanline).
// Scroll comes from scanline_scroll[sl] (captured AFTER the CPU run by end_scanline).
void ppu_2c02::render_scanline(int sl, unsigned char* fb)
{
	if (!fb || sl < 0 || sl >= SCREEN_HEIGHT) return;

	unsigned emph_idx = (reg_mask >> 5) & 7;

	// When rendering is completely disabled, fill with backdrop.
	if ((reg_mask & 0x18) == 0) {
		unsigned char bg = scanline_pal[sl][0];
		if (reg_mask & 0x01) bg &= 0x30;
		for (int px = 0; px < SCREEN_WIDTH; ++px) {
			unsigned i = (unsigned)(sl * SCREEN_WIDTH + px);
			fb[i*4+0] = nes_color_emph[emph_idx][bg*3+2];
			fb[i*4+1] = nes_color_emph[emph_idx][bg*3+1];
			fb[i*4+2] = nes_color_emph[emph_idx][bg*3+0];
			fb[i*4+3] = 0;
		}
		return;
	}

	// Per-scanline palette snapshot (captured before CPU ran this scanline).
	const unsigned char* pal = scanline_pal[sl];

	// Fill scanline with universal BG color first.
	unsigned char pal0 = pal[0] & 0x3F;
	if (pal[0x10] == pal[0]) pal0 = pal[0];  // normal: $3F00 is BG
	// Apply mirrors for $3F10/$3F14/$3F18/$3F1C
	{
		unsigned char idx = 0;
		if (pal[0x10] == 0) idx = 0;  // transparent — unused
		pal0 = pal[0];
	}
	if (reg_mask & 0x01) pal0 &= 0x30;
	for (int px = 0; px < SCREEN_WIDTH; ++px) {
		unsigned i = (unsigned)(sl * SCREEN_WIDTH + px);
		fb[i*4+0] = nes_color_emph[emph_idx][pal0*3+2];
		fb[i*4+1] = nes_color_emph[emph_idx][pal0*3+1];
		fb[i*4+2] = nes_color_emph[emph_idx][pal0*3+0];
		fb[i*4+3] = 0;
	}

	// bg_opaque[x]: true where a non-transparent BG pixel was drawn (for sprite priority).
	bool bg_opaque[SCREEN_WIDTH] = {};

	// ── Background ───────────────────────────────────────────────────────────
	if (reg_mask & 0x08) {
		unsigned short bg_base = (reg_ctrl & 0x10) ? 0x1000 : 0x0000;

		int fine_x_r   = (int)scanline_scroll[sl].fine_x;
		int coarse_x   = (int)(scanline_scroll[sl].scroll_x >> 3);
		int nt_sel     = (int)scanline_scroll[sl].scroll_nt;
		int abs_y      = (int)scanline_scroll[sl].scroll_y % 480;
		int extra_nt_y = abs_y / 240;
		int local_y    = abs_y % 240;
		int tile_row   = local_y / 8;
		int pix_row    = local_y % 8;
		int nt_y_base  = (((nt_sel >> 1) & 1) + extra_nt_y) & 1;

		// Start from the pre-CPU NT snapshot and apply logged $2007 writes at each
		// tile boundary.  This matches FCEUX's per-dot rendering: a write at PPU dot D
		// affects tiles whose fetch dot > D but not tiles already fetched.
		// Writes are logged in CPU-execution order so nt_write_log[] is sorted by dot.
		// When no writes occurred (the common case) work_nt == nt_snapshot == live mem[].
		unsigned char work_nt[0x800];
		memcpy(work_nt, nt_snapshot, 0x800);
		int wlog_idx = 0;

		for (int col = 0; col <= 32; ++col) {
			// Apply any logged NT writes whose dot falls before this tile's fetch.
			// Tile col fetches at approximately dot col*8 (before fine_x adjustment).
			int tile_dot = col * 8;
			while (wlog_idx < nt_write_count && nt_write_log[wlog_idx].dot <= tile_dot) {
				unsigned short wa = nt_write_log[wlog_idx].addr;
				if (wa >= 0x2000 && wa <= 0x27FF)
					work_nt[wa - 0x2000] = nt_write_log[wlog_idx].val;
				++wlog_idx;
			}

			int abs_col = coarse_x + col;
			int nt_h    = ((nt_sel & 0x01) + (abs_col / 32)) & 0x01;
			int nt_v    = nt_y_base;
			int cur_nt  = nt_v * 2 + nt_h;
			int loc_col = abs_col % 32;

			unsigned short nt_base = (unsigned short)(0x2000 + cur_nt * 0x400);

			// Read tile name and attribute from the per-dot working NT copy.
			unsigned char name = work_nt[mirror_nt_addr((unsigned short)(nt_base + tile_row * 32 + loc_col)) - 0x2000];

			unsigned short at_base = (unsigned short)(nt_base + 0x3C0);
			unsigned char  att     = work_nt[mirror_nt_addr((unsigned short)(at_base + (tile_row / 4) * 8 + (loc_col / 4))) - 0x2000];
			int square             = (tile_row & 0x02) + ((loc_col >> 1) & 0x01);
			unsigned char color_bit23 = ((att >> (square * 2)) & 0x03) << 2;

			unsigned char* chr = mem + bg_base + name * 16;
			int screen_x = col * 8 - fine_x_r;

			for (int i = 0; i < 8; ++i) {
				int px = screen_x + i;
				if (px < 0 || px >= SCREEN_WIDTH) continue;

				unsigned char bit0 = (chr[pix_row]       >> (7 - i)) & 1;
				unsigned char bit1 = ((chr[8 + pix_row]  >> (7 - i)) & 1) << 1;
				unsigned char color = color_bit23 | bit1 | bit0;
				bool opaque = (color & 0x03) != 0;
				if (!opaque) color = 0;

				unsigned char pal_idx = color & 0x1F;
				if (pal_idx == 0x10) pal_idx = 0x00;
				if (pal_idx == 0x14) pal_idx = 0x04;
				if (pal_idx == 0x18) pal_idx = 0x08;
				if (pal_idx == 0x1C) pal_idx = 0x0C;
				unsigned char nes_color = pal[pal_idx];
				if (reg_mask & 0x01) nes_color &= 0x30;

				unsigned idx = (unsigned)(sl * SCREEN_WIDTH + px);
				fb[idx*4+0] = nes_color_emph[emph_idx][nes_color*3+2];
				fb[idx*4+1] = nes_color_emph[emph_idx][nes_color*3+1];
				fb[idx*4+2] = nes_color_emph[emph_idx][nes_color*3+0];
				fb[idx*4+3] = 0;
				if (opaque) bg_opaque[px] = true;
			}
		}
	}

	// ── Sprites ──────────────────────────────────────────────────────────────
	// Use render_oam[] (OAM snapshot from begin_frame) and live CHR mem[].
	// Iterate back-to-front so sprite 0 wins over higher-numbered sprites.
	if (reg_mask & 0x10) {
		unsigned short spr_base = (reg_ctrl & 0x08) ? 0x1000 : 0x0000;
		for (int spr = 63; spr >= 0; --spr) {
			int sy = (int)render_oam[spr * 4 + 0] + 1;
			if (sl < sy || sl >= sy + 8) continue;

			int            sx     = (int)render_oam[spr * 4 + 3];
			unsigned char  nm     = render_oam[spr * 4 + 1];
			unsigned char  att    = render_oam[spr * 4 + 2];
			unsigned char  cb23   = ((att & 0x03) << 2) | 0x10;
			bool h_flip           = (att & 0x40) != 0;
			bool v_flip           = (att & 0x80) != 0;
			bool behind_bg        = (att & 0x20) != 0;

			if (sy >= SCREEN_HEIGHT) continue;

			unsigned char* chr = mem + spr_base + nm * 16;
			int j     = sl - sy;
			int j_chr = v_flip ? (7 - j) : j;

			for (int i = 0; i < 8; ++i) {
				int i_chr = h_flip ? (7 - i) : i;
				unsigned char b0 = (chr[j_chr]      >> (7 - i_chr)) & 1;
				unsigned char b1 = ((chr[8 + j_chr] >> (7 - i_chr)) & 1) << 1;
				unsigned char color = cb23 | b1 | b0;
				if ((color & 0x03) == 0) continue;

				int px = sx + i;
				if (px < 0 || px >= SCREEN_WIDTH) continue;
				if (behind_bg && bg_opaque[px]) continue;

				unsigned char pal_idx = color & 0x1F;
				if (pal_idx == 0x10) pal_idx = 0x00;
				if (pal_idx == 0x14) pal_idx = 0x04;
				if (pal_idx == 0x18) pal_idx = 0x08;
				if (pal_idx == 0x1C) pal_idx = 0x0C;
				unsigned char nes_color = pal[pal_idx];
				if (reg_mask & 0x01) nes_color &= 0x30;

				unsigned idx = (unsigned)(sl * SCREEN_WIDTH + px);
				fb[idx*4+0] = nes_color_emph[emph_idx][nes_color*3+2];
				fb[idx*4+1] = nes_color_emph[emph_idx][nes_color*3+1];
				fb[idx*4+2] = nes_color_emph[emph_idx][nes_color*3+0];
				fb[idx*4+3] = 0;
			}
		}
	}
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
		scroll_nt = val & 0x03;  // save NT bits for render
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
			scroll_x_reg = val;             // save raw X for render
			w = 1;
		}
		else
		{
			t = (t & 0x8FFF) | ((val & 0x07) << 12);  // fine Y into t
			t = (t & 0xFC1F) | ((val >> 3) << 5);      // coarse Y into t
			scroll_y_reg = val;                        // save raw Y for render
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
			// Record mid-render V resets so end_scanline() can reseed abs_y
			// for the remaining scanlines of this frame.
			mid_render_v_reset = true;
			mid_render_v_new   = (unsigned short)(t & 0x7FFF);
		}
		break;
	case 0x2007:  // PPUDATA
	{
		unsigned short write_v = v;
		ppu_mem_write(v, val);
		v += (reg_ctrl & 0x04) ? 32 : 1;
		// Log NT writes that happen during a visible scanline's CPU window so
		// render_scanline() can apply them at the correct tile boundary.
		if (cur_scanline >= 0 && nt_write_count < kMaxNtWrites && cpu_ptr) {
			unsigned short nt_addr = write_v & 0x3FFF;
			if (nt_addr >= 0x2000 && nt_addr <= 0x3EFF) {
				int dot = (int)((cpu_ptr->cycle - sl_cpu_cycle_base) * 3);
				NtWrite nw = { dot, mirror_nt_addr(nt_addr), val, cpu_ptr->get_pc() };
				nt_write_log[nt_write_count++] = nw;
			}
		}
		break;
	}
	}
}

void ppu_2c02::set_vblank(bool vb)
{
	if (vb) {
		// Snapshot scroll — captured after the game's main loop has written
		// $2005 (game-area scroll) but before NMI resets it for the HUD.
		render_scroll_x  = scroll_x_reg;
		render_fine_x    = fine_x;
		render_scroll_y  = scroll_y_reg;
		render_scroll_nt = scroll_nt;
		// Snapshot nametable ($2000-$27FF) and palette ($3F00-$3F1F) before
		// the NMI handler writes new tile columns (horizontal scroll update).
		// render() reads from these snapshots so it sees pre-NMI nametable data.
		for (int i = 0; i < 0x800; ++i)
			render_nt[i] = mem[0x2000 + i];
		for (int i = 0; i < 0x20; ++i)
			render_pal[i] = mem[0x3F00 + i];
		reg_status |= 0x80;
	} else {
		reg_status &= 0x7F;
	}
}

// Raise the VBL flag without snapshotting anything.
// Used in VBL-first frame structure: the ROM will read $2002 and exit its
// spin loop on the next instruction step.
void ppu_2c02::raise_vblank()
{
	reg_status |= 0x80;
}

// Capture NT/PAL/scroll into render buffers and clear the VBL flag.
// Called at the end of the VBL period (after NMI has written new tile data),
// just before visible scanlines begin.  This gives render() the post-NMI
// nametable and palette so the rendered frame reflects the current frame's
// NMI writes rather than the previous frame's.
void ppu_2c02::snapshot_render()
{
	// Scroll snapshot for export/debug output
	render_scroll_x  = scroll_x_reg;
	render_fine_x    = fine_x;
	render_scroll_y  = scroll_y_reg;
	render_scroll_nt = scroll_nt;
	// Nametable and palette snapshots for pixel rendering
	for (int i = 0; i < 0x800; ++i)
		render_nt[i] = mem[0x2000 + i];
	for (int i = 0; i < 0x20; ++i)
		render_pal[i] = mem[0x3F00 + i];
	// Clear VBL flag (end of vblank period)
	reg_status &= 0x7F;
}

void ppu_2c02::oam_dma(unsigned char* page_data)
{
	for (int i = 0; i < 256; i++)
		oam[(reg_oam_addr + i) & 0xFF] = page_data[i];
}

void ppu_2c02::begin_frame()
{
	// Clear sprite-0 hit (reset at pre-render scanline on real HW)
	reg_status &= ~0x40;
	// Snapshot OAM at the start of visible scanlines — before the game's main
	// loop has advanced any sprite positions this frame.  This matches real HW:
	// each frame's visible scanlines render with the OAM written by the previous
	// frame's NMI DMA, before this frame's CPU has touched it.
	memcpy(render_oam, oam, 256);
	// Seed all scanline scroll entries from the Loopy T register, exactly as the
	// real PPU does at pre-render dot 304 (RefreshAddr = TempAddr in FCEUX terms).
	// T is updated by both $2005 (scroll) AND $2006 (PPUADDR) writes, so it
	// correctly handles games that set scroll via PPUADDR rather than PPUSCROLL.
	// After sprite-0-hit the game writes game-area scroll; end_scanline() will
	// overwrite the affected entries with those updated values.
	unsigned char seed_scroll_x  = (unsigned char)(((t & 0x001F) << 3) | fine_x);
	unsigned char seed_fine_x    = fine_x;
	// Vertical scroll: use pre_render_scroll_y captured after run_ppu(6808) (end of vblank)
	// so the NMI handler has completed and T reflects the NMI-written vertical scroll.
	unsigned char seed_scroll_y  = pre_render_scroll_y;
	// NT: horizontal from T (valid at begin_frame time); vertical from the post-NMI capture.
	unsigned char seed_scroll_nt = (unsigned char)(((t >> 10) & 0x01) | (pre_render_scroll_nt_y << 1));
	// Clear mid-render V-reset flag at the start of each frame.
	mid_render_v_reset = false;
	mid_render_v_new   = 0;
	for (int sl = 0; sl < 240; ++sl)
	{
		scanline_scroll[sl].scroll_x  = seed_scroll_x;
		scanline_scroll[sl].fine_x    = seed_fine_x;
		// Store per-scanline abs_y = seed_scroll_y + sl (the natural per-scanline
		// vertical position if no mid-frame V reset occurs).
		scanline_scroll[sl].scroll_y  = (short)(seed_scroll_y + sl);
		scanline_scroll[sl].scroll_nt = seed_scroll_nt;
		// Seed per-scanline palette from the vblank snapshot (render_pal).
		// end_scanline() will refresh this from live PPU memory each scanline,
		// so mid-frame palette writes are captured at the correct scanline boundary.
		memcpy(scanline_pal[sl], render_pal, 32);
	}
}

void ppu_2c02::snap_nmi_scroll()
{
	// Save the scroll register state immediately after the NMI handler returns.
	// begin_frame() uses these to seed scanline_scroll[] so that HUD rows
	// (before sprite-0-hit) render with the NMI-written scroll (typically 0),
	// not with scroll values written by game-loop code during the vblank remainder.
	nmi_scroll_x  = scroll_x_reg;
	nmi_fine_x    = fine_x;
	nmi_scroll_y  = scroll_y_reg;
	nmi_scroll_nt = scroll_nt;

	// Also pre-initialise the pre-render vertical capture to the NMI scroll,
	// so frame 1 (where snap_pre_render_vscroll may not be called) is correct.
	pre_render_scroll_y   = (unsigned char)((((t >> 5) & 0x1F) << 3) | ((t >> 12) & 7));
	pre_render_scroll_nt_y = (unsigned char)((t >> 11) & 1);
}

void ppu_2c02::snap_pre_render_vscroll()
{
	// Capture T's vertical bits at the moment the NES hardware would copy them
	// into V (pre-render dots 280-304). After this point the game may write new
	// vertical scroll values to prepare the NEXT frame, so we must not read T
	// for the current frame's vertical scroll after this call.
	pre_render_scroll_y   = (unsigned char)((((t >> 5) & 0x1F) << 3) | ((t >> 12) & 7));
	pre_render_scroll_nt_y = (unsigned char)((t >> 11) & 1);
}

void ppu_2c02::check_sprite0_hit(int sl)
{
	if ((reg_mask & 0x18) == 0) return;  // rendering disabled, sprite-0-hit can't fire
	if (reg_status & 0x40) return;  // already set this frame
	// Use live OAM (post-NMI DMA) for sprite-0 position, matching real HW
	int sy = (int)oam[0] + 1;  // OAM byte 0 = Y-1; sy = first scanline sprite appears on
	if (sl >= sy && sl < sy + 8)
		reg_status |= 0x40;
}

void ppu_2c02::begin_scanline(int sl)
{
	if (sl < 0 || sl >= 240) return;
	// Snapshot BOTH palette and horizontal scroll BEFORE this scanline's CPU run.
	// This matches real hardware: the PPU copies T's horizontal bits to V at the
	// H-blank of the PREVIOUS scanline (dot 257), so a $2005/$2006 write during
	// scanline N's CPU run only affects scanline N+1's rendering — not N itself.
	// Similarly, palette writes during scanline N's CPU take effect from N+1.
	for (int i = 0; i < 0x20; ++i)
		scanline_pal[sl][i] = ppu_mem_read(0x3F00 + i);
	scanline_scroll[sl].scroll_x = scroll_x_reg;
	scanline_scroll[sl].fine_x   = fine_x;
	// Snapshot the 2 KB nametable BEFORE the CPU runs this scanline.
	// cpu_write($2007) will append entries to nt_write_log[] as the CPU writes
	// VRAM; render_scanline() replays them at per-tile boundaries.
	memcpy(nt_snapshot, mem + 0x2000, 0x800);
	nt_write_count    = 0;
	cur_scanline      = sl;
	sl_cpu_cycle_base = cpu_ptr ? cpu_ptr->cycle : 0;

	// Pre-populate start-of-scanline trace fields BEFORE the CPU runs.
	// capture_scanline_trace() fills the end-of-scanline fields afterwards.
	scanline_trace[sl].v_start = v;
	scanline_trace[sl].ppuctrl = reg_ctrl;
	scanline_trace[sl].ppumask = reg_mask;
	// ss_* reflect the render scroll that was locked in by begin_scanline
	// (scroll_x / fine_x were just written above; scroll_y was seeded earlier)
	scanline_trace[sl].ss_x  = scanline_scroll[sl].scroll_x;
	scanline_trace[sl].ss_y  = scanline_scroll[sl].scroll_y;
	scanline_trace[sl].ss_nt = scanline_scroll[sl].scroll_nt;
}

void ppu_2c02::end_scanline(int sl)
{
	if (sl < 0 || sl >= 240) return;
	// Scroll and palette are now captured in begin_scanline (before cpu).
	// Only clear the mid-render V-reset flag here.
	mid_render_v_reset = false;
	cur_scanline = -1;  // no longer inside a visible CPU window
}

void ppu_2c02::export_frame(unsigned char* pScreenBits, const char* filename, cpu_6502* cpu)
{
	FILE* f = nullptr;
	fopen_s(&f, filename, "w");
	if (!f) return;

	// --- CPU State ---
	if (cpu)
	{
		fprintf(f, "=== CPU State ===\n");
		fprintf(f, "PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X cycles=%u\n\n",
			cpu->get_pc(), cpu->get_a(), cpu->get_x(), cpu->get_y(),
			cpu->get_s(), cpu->get_p(), cpu->cycle);
	}

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
	fprintf(f, "v=%04X  t=%04X  fine_x=%d  mirroring=%s\n",
		v, t, fine_x, mirror_vertical ? "vertical" : "horizontal");
	// Decode render_scroll from the live V register, matching FCEUX's RefreshAddr decode.
	{
		int ex_coarse_x = (int)(v & 0x1F);
		int ex_fine_x   = (int)fine_x;
		int ex_coarse_y = (int)((v >> 5) & 0x1F);
		int ex_fine_y   = (int)((v >> 12) & 7);
		int ex_scroll_x = ex_coarse_x * 8 + ex_fine_x;
		int ex_scroll_y = ex_coarse_y * 8 + ex_fine_y;
		int ex_nt       = (int)((v >> 10) & 3);
		fprintf(f, "render_scroll_x=%d (coarse=%d fine=%d)  render_scroll_y=%d (coarse=%d fine=%d)  render_scroll_nt=%d\n\n",
			ex_scroll_x, ex_coarse_x, ex_fine_x,
			ex_scroll_y, ex_coarse_y, ex_fine_y,
			ex_nt);
	}

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
	// Read SPR palette as raw physical PPU RAM (no $3F10/$3F14/$3F18/$3F1C → $3F00 mirror).
	// Writes to $3F10/$3F14/$3F18/$3F1C are redirected to $3F00/$3F04/$3F08/$3F0C, so the
	// physical locations at $3F10 etc. retain their reset value of $00 — matching how
	// FCEUX's Lua script reads them (raw PPU RAM without the hardware mirror applied).
	for (int i = 0; i < 16; ++i) fprintf(f, "%02X ", mem[0x3F10 + i]);
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

	// --- CPU RAM ($0000-$07FF) ---
	if (cpu)
	{
		fprintf(f, "\n=== CPU RAM ($0000-$07FF) ===\n");
		for (int row = 0; row < 64; ++row)
		{
			for (int col = 0; col < 32; ++col)
				fprintf(f, "%02X ", cpu->get_mem_byte((unsigned short)(row * 32 + col)));
			fprintf(f, "\n");
		}
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

void ppu_2c02::capture_scanline_trace(int sl, cpu_6502* cpu)
{
	if (sl < 0 || sl >= 240) return;
	ScanlineTraceEntry& e = scanline_trace[sl];

	// CPU registers
	if (cpu) {
		e.cpu_pc     = cpu->get_pc();
		e.cpu_a      = cpu->get_a();
		e.cpu_x      = cpu->get_x();
		e.cpu_y      = cpu->get_y();
		e.cpu_s      = cpu->get_s();
		e.cpu_p      = cpu->get_p();
		e.cpu_cycles = cpu->cycle;
	}

	// Live PPU Loopy registers
	e.ppu_v     = v;
	e.ppu_t     = t;
	e.ppu_fine_x = fine_x;
	e.ppu_w     = w;

	// Raw scroll registers
	e.scroll_x_reg = scroll_x_reg;
	e.scroll_y_reg = scroll_y_reg;
	e.scroll_nt    = scroll_nt;

// Full palette snapshot (32 bytes, mirrors resolved)
  for (int i = 0; i < 32; ++i) e.palette[i] = ppu_mem_read(0x3F00 + i);

	// Copy the NT write log accumulated during this scanline's CPU window.
	// begin_scanline() reset nt_write_count and nt_write_log; cpu_write($2007)
	// appended to it; we snapshot it here before the next begin_scanline clears it.
	e.nt_write_cnt = nt_write_count;
	int ncopy = nt_write_count < 64 ? nt_write_count : 64;
	for (int i = 0; i < ncopy; ++i)
		e.nt_writes[i] = nt_write_log[i];
}

void ppu_2c02::export_scanline_trace(const char* filename)
{
	FILE* f = nullptr;
	fopen_s(&f, filename, "w");
	if (!f) return;

	// ----------------------------------------------------------------
	// PART 1 – Per-scanline summary (one row per visible scanline)
	// Columns:
	//   SL        – scanline index 0-239
	//   V_S       – V register at START of scanline (before CPU run)
	//   V_E       – V register at END of scanline (after CPU run)
	//   T_E       – T (temp) register at end
	//   FX W      – fine_x, w latch at end
	//   CTL MSK   – PPUCTRL, PPUMASK at start of scanline
	//   SSX SS_Y  – scanline_scroll[sl].scroll_x, .scroll_y (render values)
	//   SSNT      – scanline_scroll[sl].scroll_nt
	//   NTw       – number of NT writes during this scanline's CPU window
	//   CYC       – cumulative CPU cycle counter at end of scanline
	//   PC A X Y S P  – CPU registers at end of scanline
	//   PAL       – live palette $3F00 at end of scanline
	// ----------------------------------------------------------------
	fprintf(f, "=== PART1 ===\n");
	fprintf(f, "# %-3s  %-5s %-5s %-5s %2s %1s  %2s  %2s  %3s %6s %4s  %3s  %-8s  %-4s %-2s %-2s %-2s %-2s %-2s  PAL\n",
		"SL", "V_S", "V_E", "T_E", "FX", "W", "CTL", "MSK",
		"SSX", "SS_Y", "SSNT", "NTw", "CYC", "PC", "A", "X", "Y", "S", "P");

	for (int sl = 0; sl < 240; ++sl) {
		const ScanlineTraceEntry& e = scanline_trace[sl];
		char pal64[65] = {};
		for (int i = 0; i < 32; ++i) snprintf(pal64 + i*2, 3, "%02X", e.palette[i]);
		fprintf(f, "  %3d  %04X  %04X  %04X  %d  %d  %02X  %02X  %3d %6d    %d  %3d  %8u  %04X %02X %02X %02X %02X %02X  %s\n",
			sl,
			e.v_start, e.ppu_v, e.ppu_t,
			e.ppu_fine_x, e.ppu_w,
			e.ppuctrl, e.ppumask,
			(int)e.ss_x, (int)e.ss_y, (int)e.ss_nt,
			e.nt_write_cnt,
			e.cpu_cycles,
			e.cpu_pc, e.cpu_a, e.cpu_x, e.cpu_y, e.cpu_s, e.cpu_p,
			pal64);
	}

	// ----------------------------------------------------------------
	// PART 2 – Per-write log (every NT/$2007 write logged per scanline)
	// Columns:
	//   SEQ    – sequential write number (1-based, global across all scanlines)
	//   SL     – scanline the write occurred on
	//   DOT    – approximate PPU dot within scanline
	//   V_BEF  – V register before the write (= the PPU address written to)
	//   V_AFT  – V register after the write (= V_BEF + increment)
	//   NTADDR – mirrored NT/AT address written ($2000-$27FF)
	//   VAL    – byte value written
	//   PC     – CPU PC at time of write
	// ----------------------------------------------------------------
	fprintf(f, "\n=== PART2 ===\n");
	fprintf(f, "# %-5s %-3s %-5s %-5s %-5s %-5s %-3s  PC\n",
		"SEQ", "SL", "DOT", "V_BEF", "V_AFT", "NTADDR", "VAL");

	int seq = 0;
	for (int sl = 0; sl < 240; ++sl) {
		const ScanlineTraceEntry& e = scanline_trace[sl];
		for (int i = 0; i < e.nt_write_cnt && i < 64; ++i) {
			const NtWrite& w = e.nt_writes[i];
			// V_BEF is implicit: it's w.addr (the address written, which equals V at write time)
			// V_AFT = V_BEF + (ctrl & 4 ? 32 : 1) — but we don't re-track increment here.
			// Use 0 as V_AFT placeholder; exact V_AFT is in PART1 e.ppu_v (end of scanline).
			fprintf(f, "  %5d  %3d  %4d  %04X  ----  %04X  %02X  %04X\n",
				++seq, sl, w.dot, w.addr, w.addr, w.val, w.cpu_pc);
		}
	}

	fclose(f);
}
