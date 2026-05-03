// nes1.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include <xaudio2.h>
#include <vector>
#pragma comment(lib, "xaudio2.lib")

unsigned char prg_rom[32768];   // up to NROM-256 (2x16KB)
unsigned char chr_rom[8192];

#define FRAME_FREQUENCY 60

LARGE_INTEGER timer_frequency;
LARGE_INTEGER timer_last_count;

HWND hMainWindow;

HBITMAP hDibSection;
unsigned char* pScreenBits;

std::deque<LONGLONG> time_paint;

cpu_6502 cpu;
ppu_2c02 ppu;
apu_2a03 apu;

// ─── XAudio2 audio output ───────────────────────────────
// 44100 Hz mono 16-bit, double-buffered (735 samples per NTSC frame)
static const int AUDIO_SAMPLE_RATE = 44100;
static const int AUDIO_SAMPLES_PER_FRAME = 735;  // 44100/60
static const int AUDIO_BUFFER_COUNT = 3;         // triple-buffer

struct AudioSystem {
    IXAudio2*               xaudio  = nullptr;
    IXAudio2MasteringVoice* master  = nullptr;
    IXAudio2SourceVoice*    source  = nullptr;
    // Ring of PCM buffers, each holds one frame of samples
    short buf[AUDIO_BUFFER_COUNT][AUDIO_SAMPLES_PER_FRAME * 2]; // *2 for safety
    int   buf_idx = 0;

    bool init() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return false;
        if (FAILED(XAudio2Create(&xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
        if (FAILED(xaudio->CreateMasteringVoice(&master))) return false;

        WAVEFORMATEX wfx = {};
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = 1;
        wfx.nSamplesPerSec  = AUDIO_SAMPLE_RATE;
        wfx.wBitsPerSample  = 16;
        wfx.nBlockAlign     = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        if (FAILED(xaudio->CreateSourceVoice(&source, &wfx))) return false;
        source->Start();
        return true;
    }

    void submit(const std::vector<short>& samples) {
        if (!source) return;
        XAUDIO2_VOICE_STATE state;
        source->GetState(&state);
        // If too many buffers queued, drop this frame (avoid runaway latency)
        if (state.BuffersQueued >= AUDIO_BUFFER_COUNT) return;

        int count = (int)samples.size();
        if (count > AUDIO_SAMPLES_PER_FRAME * 2) count = AUDIO_SAMPLES_PER_FRAME * 2;
        memcpy(buf[buf_idx], samples.data(), count * sizeof(short));

        XAUDIO2_BUFFER xbuf = {};
        xbuf.AudioBytes = count * sizeof(short);
        xbuf.pAudioData = (const BYTE*)buf[buf_idx];
        source->SubmitSourceBuffer(&xbuf);
        buf_idx = (buf_idx + 1) % AUDIO_BUFFER_COUNT;
    }

    void destroy() {
        if (source) { source->DestroyVoice(); source = nullptr; }
        if (master) { master->DestroyVoice(); master = nullptr; }
        if (xaudio) { xaudio->Release(); xaudio = nullptr; }
        CoUninitialize();
    }
} g_audio;

void create_dibsection()
{
	// Fill in the BITMAPINFOHEADER
	LPBITMAPINFO lpbi;
	lpbi = (LPBITMAPINFO) new BYTE[sizeof(BITMAPINFOHEADER)];
	lpbi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	lpbi->bmiHeader.biWidth = SCREEN_WIDTH;
	lpbi->bmiHeader.biHeight = -SCREEN_HEIGHT;
	lpbi->bmiHeader.biPlanes = 1;
	lpbi->bmiHeader.biBitCount = 32;
	lpbi->bmiHeader.biCompression = BI_RGB;
	lpbi->bmiHeader.biSizeImage = SCREEN_WIDTH * 4 * SCREEN_HEIGHT;
	lpbi->bmiHeader.biXPelsPerMeter = 0;
	lpbi->bmiHeader.biYPelsPerMeter = 0;
	lpbi->bmiHeader.biClrUsed = 0;
	lpbi->bmiHeader.biClrImportant = 0;

	hDibSection = CreateDIBSection(NULL, lpbi, DIB_RGB_COLORS, (void **)&pScreenBits, NULL, 0);
	assert(hDibSection != 0);

	delete[](BYTE*)lpbi;

	return;
}

void destroy_dibsection()
{
	DeleteObject(hDibSection);
	hDibSection = NULL;
	pScreenBits = NULL;

	return;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:
		create_dibsection();
		return 0;

	case WM_DESTROY:
		destroy_dibsection();
		PostQuitMessage(0);
		return 0;

	case WM_KEYDOWN:
	{
		switch (wParam)
		{
		case 'F':           cpu.joypad1_state |= 0x01; break;  // A
		case 'D':           cpu.joypad1_state |= 0x02; break;  // B
		case 'S':           cpu.joypad1_state |= 0x04; break;  // Select
		case VK_RETURN:     cpu.joypad1_state |= 0x08; break;  // Start
		case VK_UP:         cpu.joypad1_state |= 0x10; break;  // Up
		case VK_DOWN:       cpu.joypad1_state |= 0x20; break;  // Down
		case VK_LEFT:       cpu.joypad1_state |= 0x40; break;  // Left
		case VK_RIGHT:      cpu.joypad1_state |= 0x80; break;  // Right
		}
		return 0;
	}
	case WM_KEYUP:
	{
		switch (wParam)
		{
		case 'F':           cpu.joypad1_state &= ~0x01; break;
		case 'D':           cpu.joypad1_state &= ~0x02; break;
		case 'S':           cpu.joypad1_state &= ~0x04; break;
		case VK_RETURN:     cpu.joypad1_state &= ~0x08; break;
		case VK_UP:         cpu.joypad1_state &= ~0x10; break;
		case VK_DOWN:       cpu.joypad1_state &= ~0x20; break;
		case VK_LEFT:       cpu.joypad1_state &= ~0x40; break;
		case VK_RIGHT:      cpu.joypad1_state &= ~0x80; break;
		}
		return 0;
	}

	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);

		// Create memory DC
		HDC memorydc = CreateCompatibleDC(hdc);
		HGDIOBJ oldBitmap = SelectObject(memorydc, hDibSection);
		StretchBlt(hdc, 0, 0, SCREEN_WIDTH * DISPLAY_SCALE, SCREEN_HEIGHT * DISPLAY_SCALE, memorydc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SRCCOPY);
		SelectObject(memorydc, oldBitmap);
		DeleteDC(memorydc);

		// calculate fps
		LARGE_INTEGER timer;
		BOOL result = QueryPerformanceCounter(&timer);
		assert(result != 0);

		time_paint.push_back(timer.QuadPart);
		while (true)
		{
			LONGLONG t = time_paint.front();
			if (t < timer.QuadPart - timer_frequency.QuadPart)
			{
				// longer than 1 second
				time_paint.pop_front();
			}
			else
			{
				break;
			}
		}

		// FPS
		HFONT font = CreateFont(16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, FIXED_PITCH | FF_DONTCARE, TEXT("FixedSys"));
		HGDIOBJ oldFont = SelectObject(hdc, font);

		wchar_t buffer[100];
		swprintf(buffer, 100, L"%dfps", (int)time_paint.size());
		TextOut(hdc, 0, 0, buffer, (int)wcslen(buffer));

		SelectObject(hdc, oldFont);
		DeleteObject(font);
		EndPaint(hwnd, &ps);
	}
	return 0;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void initMainWindow()
{
	// Register the window class.
	const wchar_t CLASS_NAME[] = L"NES1 Window Class";

	WNDCLASS wc = { };

	wc.lpfnWndProc = WindowProc;
	wc.hInstance = NULL;
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);

	/*
	int cxedge = GetSystemMetrics(SM_CXEDGE);
	int cyedge = GetSystemMetrics(SM_CYEDGE);
	int cxsizeframe = GetSystemMetrics(SM_CXSIZEFRAME);
	int cysizeframe = GetSystemMetrics(SM_CYSIZEFRAME);
	int cycaption = GetSystemMetrics(SM_CYCAPTION);
	*/

	// Create the window.

	hMainWindow = CreateWindowEx(
		0,                              // Optional window styles.
		CLASS_NAME,                     // Window class
		L"NES1 main window",            // Window text
		WS_OVERLAPPEDWINDOW,            // Window style

		// Size and position
		100, 100, SCREEN_WIDTH, SCREEN_HEIGHT,

		NULL,       // Parent window    
		NULL,       // Menu
		NULL,       // Instance handle
		NULL        // Additional application data
	);

	if (hMainWindow == NULL)
	{
		return;
	}

	RECT rectWindow;
	GetWindowRect(hMainWindow, &rectWindow);
	RECT rectClient;
	GetClientRect(hMainWindow, &rectClient);

	int xdiff = (rectWindow.right - rectWindow.left) - (rectClient.right - rectClient.left);
	int ydiff = (rectWindow.bottom - rectWindow.top) - (rectClient.bottom - rectClient.top);

	SetWindowPos(hMainWindow, HWND_TOP, 100, 100, SCREEN_WIDTH * DISPLAY_SCALE + xdiff, SCREEN_HEIGHT * DISPLAY_SCALE + ydiff, SWP_SHOWWINDOW);
//	ShowWindow(hMainWindow, SW_SHOWNORMAL);
}

void prepareTimer()
{
	BOOL result = QueryPerformanceFrequency(&timer_frequency);
	assert(result != 0);

	result = QueryPerformanceCounter(&timer_last_count);
	assert(result != 0);

	return;
}

bool frameIntervalElapsed()
{
	LARGE_INTEGER timer_now;
	BOOL result = QueryPerformanceCounter(&timer_now);

	if (timer_now.QuadPart - timer_last_count.QuadPart > timer_frequency.QuadPart / FRAME_FREQUENCY)
	{
		timer_last_count = timer_now;
		return true;
	}

	return false;
}

// ─── Automated test helpers ──────────────────────────────────────────────────

// Run one full emulated frame (visible lines + vblank/NMI period).
static void run_one_frame()
{
	unsigned int frame_start = cpu.cycle;

	// Visible scanlines (~27384 CPU cycles on NTSC NES)
	while ((cpu.cycle - frame_start) < 27384u)
		cpu.step(false);

	// Raise vblank; fire NMI if enabled by PPUCTRL bit 7
	ppu.set_vblank(true);
	if (ppu.nmi_enabled())
		cpu.nmi();

	// VBlank period (remaining cycles up to ~29829)
	while ((cpu.cycle - frame_start) < 29829u)
		cpu.step(false);

	ppu.set_vblank(false);
}

// ZP addresses for color_test.s variables (from nrom.cfg + source order)
#define ZP_PPU_EMPHASIS  0x0000u   // ppu_emphasis
#define ZP_COLOR         0x0001u   // color
#define ZP_GAMEPAD       0x0003u   // gamepad
#define ZP_GAMEPAD_LAST  0x0004u   // gamepad_last

// The 16 PPU-mask (ppu_emphasis) values exercised by the test:
//   Bits 7/6/5 = blue/green/red colour emphasis (8 combos)
//   Bit 0      = greyscale mode (2 states)
//   Bits 4/3/2/1 fixed at 1 (sprites on, BG on, show leftmost columns)
//
//   No-grey:   $1E $3E $5E $7E $9E $BE $DE $FE
//   Grey mode: $1F $3F $5F $7F $9F $BF $DF $FF
static const unsigned char k_emph_values[16] = {
	0x1E, 0x3E, 0x5E, 0x7E, 0x9E, 0xBE, 0xDE, 0xFE,
	0x1F, 0x3F, 0x5F, 0x7F, 0x9F, 0xBF, 0xDF, 0xFF,
};

static void run_autotest(const char* outdir)
{
	// Create output directory (silently succeeds if it already exists)
	CreateDirectoryA(outdir, NULL);

	// Off-screen render buffer (BGRA, 256×240)
	unsigned char* screen_buf = new unsigned char[SCREEN_WIDTH * SCREEN_HEIGHT * 4];
	memset(screen_buf, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 4);

	// Let the ROM finish its reset/initialisation sequence before we start
	// overriding ZP values (~10 frames covers the two vblank waits in reset).
	printf("Initialising ROM...\n");
	for (int i = 0; i < 10; i++)
		run_one_frame();

	int total = 0;
	char txtpath[512];

	for (int ei = 0; ei < 16; ei++)
	{
		unsigned char emph = k_emph_values[ei];

		for (int c = 0x00; c <= 0x3F; c++)
		{
			// Directly set the ROM's zero-page state
			cpu.set_mem_byte(ZP_PPU_EMPHASIS, emph);
			cpu.set_mem_byte(ZP_COLOR,        (unsigned char)c);
			cpu.set_mem_byte(ZP_GAMEPAD,      0x00);   // no buttons held
			cpu.set_mem_byte(ZP_GAMEPAD_LAST, 0x00);   // allow NMI to process

			// Two frames: frame 1 = NMI picks up new values;
			//             frame 2 = rendered with those values stable
			run_one_frame();
			run_one_frame();

			// Render into the off-screen buffer and export BMP + text dump
			ppu.render(screen_buf);

			sprintf_s(txtpath, sizeof(txtpath),
			          "%s\\nes1_color%02X_emph%02X.txt",
			          outdir, (unsigned)c, (unsigned)emph);
			ppu.export_frame(screen_buf, txtpath);

			total++;
			if ((total % 64) == 0)
				printf("Progress: %d / 1024\n", total);
		}
	}

	delete[] screen_buf;
	printf("Autotest complete: %d screenshots saved to %s\\\n", total, outdir);
}

int main(int argc, char* argv[])
{
	prepareTimer();
	initMainWindow();

	// Load NES ROM file
	if (argc < 2)
	{
		printf("Usage: nes1 <rom.nes>\n");
		return 0;
	}
	FILE *fp = NULL;
	fopen_s(&fp, argv[1], "rb");
	if (fp == NULL)
	{
		printf("error in opening file.\n");
		return 0;
	}
	
	unsigned char header[16];
	fread(header, 1, 16, fp);

	if (header[0] != 0x4e || header[1] != 0x45 || header[2] != 0x53 || header[3] != 0x1a)
	{
		printf("not a NES file.\n");
		fclose(fp);
		return 0;
	}

	// Check trainer bit
	if ((header[6] & 0x02) != 0)
	{
		printf("NES file contains a trainer. It's not supported now.\n");
		fclose(fp);
		return 0;
	}

	// Assert the prg_rom and chr_rom sizes (1 or 2 PRG banks, exactly 1 CHR bank)
	if (header[4] < 1 || header[4] > 2 || header[5] != 1)
	{
		printf("Only 1-2 PRG banks and 1 CHR bank (mapper 0) are supported now.\n");
		fclose(fp);
		return 0;
	}

	// Assert the mapper 0
	if ((header[6] & 0xf0) != 0 || (header[7] & 0xf0) != 0)
	{
		printf("Only mapper 0 is supported now.\n");
		fclose(fp);
		return 0;
	}

	// Check file size
	int prg_size = header[4] * 16384;
	fseek(fp, 0, SEEK_END);
	long filesize = ftell(fp);
	if (filesize != (long)(sizeof(header) + prg_size + 8192))
	{
		printf("File size incorrect.\n");
		fclose(fp);
		return 0;
	}
	fseek(fp, sizeof(header), SEEK_SET);

	// Initialize the 6502 CPU, load the PRG_ROM into CPU.
	fread(prg_rom, 1, prg_size, fp);
	fread(chr_rom, 1, 8192, fp);
	fclose(fp);

	cpu.load_prg_rom(prg_rom, prg_size);
	cpu.reset();

	ppu.load_chr_rom(chr_rom, 8192);
	ppu.set_mirroring((header[6] & 0x01) != 0);
	cpu.set_ppu(&ppu);
	cpu.set_apu(&apu);

	// ── Automated headless test mode ─────────────────────────────────────────
	// Usage: nes1.exe <rom.nes> --autotest <output_dir>
	// Runs all 1024 (64 colours × 16 emphasis/greyscale) combinations and saves
	// a BMP + text dump for each frame, then exits without opening a window.
	if (argc >= 4 && strcmp(argv[2], "--autotest") == 0)
	{
		run_autotest(argv[3]);
		return 0;
	}

	// Initialize audio
	if (!g_audio.init())
		printf("Warning: XAudio2 init failed, no sound.\n");

	// Run!
	bool result = true;
	MSG msg = { };

	while (result)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
		{
			if (GetMessage(&msg, NULL, 0, 0) == 0)
				break;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (frameIntervalElapsed())
		{
			// NTSC NES: 29829 CPU cycles per frame
			// ~27384 cycles for visible scanlines, ~2445 for vblank period
			unsigned int frame_start = cpu.cycle;
			std::vector<short> audio_samples;
			audio_samples.reserve(AUDIO_SAMPLES_PER_FRAME + 10);

			// Run visible scanlines
			while (result && (cpu.cycle - frame_start) < 27384u) {
				unsigned int cyc_before = cpu.cycle;
				result = cpu.step(false);
				apu.tick_n(cpu.cycle - cyc_before, audio_samples);
			}

			// Raise vblank, trigger NMI if enabled
			ppu.set_vblank(true);
			if (ppu.nmi_enabled())
				cpu.nmi();

			// Run vblank period
			while (result && (cpu.cycle - frame_start) < 29829u) {
				unsigned int cyc_before = cpu.cycle;
				result = cpu.step(false);
				apu.tick_n(cpu.cycle - cyc_before, audio_samples);
			}

			// End vblank, render video
			ppu.set_vblank(false);
			ppu.render(pScreenBits);
			InvalidateRect(hMainWindow, NULL, FALSE);

			// Submit audio for this frame
			g_audio.submit(audio_samples);
		}
		else
		{
			Sleep(0);
		}
	}

	g_audio.destroy();
    return 0;
}

