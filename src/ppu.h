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

private:
	void draw_chr(unsigned char* chr, int x, int y, unsigned char color_bit23, unsigned char* pScreenBits);

private:
	unsigned char* mem;
	unsigned char* oam;
};
