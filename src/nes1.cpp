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

// Run one full emulated frame matching FCEUX's FCEUPPU_Loop structure.
//
// suppress_vbl (ppudead): VBL flag never raised, NMI never fires.
//   The ROM stays in its $C03E/$C041 spin loop for the entire frame.
//   Matches FCEUX's ppudead=2/1 path: X6502_Run(scanlines_per_frame*(256+85)).
//
// Normal frame (VBL-first, matching FCEUX's ppudead=0 else-branch):
//   1. Post-render scanline  (~113 CPU = 341 PPU)
//   2. Raise VBL flag        ROM reads $2002 → exits spin loop
//   3. NMI timing window     (12 PPU)
//   4. NMI (if PPUCTRL.7)   game handler runs, writes scroll/NT/OAM
//   5. snap_nmi_scroll()    save post-NMI scroll (kept for debug export)
//   6. Rest of vblank        (6808 PPU)
//   7. snapshot_render()    capture post-NMI NT/PAL into render buffers; clear VBL
//   8. Pre-render scanline   (256 + 69 + (16-kook) PPU, mirroring FCEUX exactly)
//   9. begin_frame()         seed scanline_scroll[] from Loopy T; snap OAM
//  10. 240 visible scanlines 341 PPU each (via run_ppu, deficit carried forward)
//
// run_ppu() uses a persistent cycle_counter (carries instruction-boundary overshoot
// between calls, like FCEUX's X6502_Run) and a ppu_remainder accumulator.
// This gives per-frame CPU = exactly 29780 or 29781 (alternating via kook),
// identical to FCEUX — no cumulative drift.
static void run_one_frame(bool suppress_vbl = false)
{
	// Persistent state (survives across frames, like FCEUX's global counters)
	static int ppu_remainder = 0;  // PPU sub-cycle carry (0-2)
	static int cycle_counter = 0;  // CPU cycle budget; negative = overshoot debt
	static int kook          = 0;  // alternating pre-render length (0 → 16 PPU, 1 → 15 PPU)

	// Convert PPU cycles to CPU cycles (with accumulation) and run instructions.
	// Overshoot from a completed instruction is automatically absorbed by the
	// NEXT call (cycle_counter stays negative until the next budget replenishes it).
	auto run_ppu = [&](int ppu_cycles) {
		ppu_remainder += ppu_cycles;
		cycle_counter += ppu_remainder / 3;
		ppu_remainder  %= 3;
		while (cycle_counter > 0) {
			unsigned int before = cpu.cycle;
			cpu.step(false);
			cycle_counter -= (int)(cpu.cycle - before);
		}
	};

	if (suppress_vbl)
	{
		// ppudead: FCEUX runs X6502_Run(scanlines_per_frame * 341) = 89342 PPU,
		// no VBL set, no kook adjustment.
		run_ppu(262 * 341);
		return;
	}

	// ── Step 1: post-render scanline (scanline 240): 341 PPU ──────────────
	run_ppu(341);

	// ── Step 2: raise VBL flag ────────────────────────────────────────────
	ppu.raise_vblank();

	// ── Step 3: NMI timing window: 12 PPU ────────────────────────────────
	run_ppu(12);

	// ── Step 4: fire NMI if PPUCTRL bit 7 is set ─────────────────────────
	if (ppu.nmi_enabled())
		cpu.nmi();

	// ── Step 5: snap pre-NMI scroll (for debug export only) ──────────────
	ppu.snap_nmi_scroll();

	// ── Step 6: rest of vblank: (262-242)*341 - 12 = 6808 PPU ───────────
	run_ppu(6808);

	// ── Step 5b: snap post-NMI scroll ─────────────────────────────────────
	// The NMI handler has now completed (ran during run_ppu(6808)).
	// Capture T's vertical bits here — this is the NMI-written vertical scroll
	// for the current frame. The game-loop may overwrite T for the NEXT frame
	// during the pre-render scanline, so we must capture before that happens.
	ppu.snap_pre_render_vscroll();

	// ── Step 7: snapshot NT/PAL for rendering; clear VBL flag ────────────
	ppu.snapshot_render();

	// ── Step 8: pre-render scanline: 256 + 69 + (16-kook) PPU ───────────
	// Mirrors FCEUX's X6502_Run(256) + X6502_Run(69) + X6502_Run(16-kook).
	// begin_frame() reads Loopy T immediately after (≈ FCEUX's RefreshAddr=TempAddr
	// at pre-render dot 325) so it sees the NMI-written scroll.
	run_ppu(256);
	run_ppu(69);
	run_ppu(16 - kook);
	kook ^= 1;

	// ── Steps 9-10: visible scanlines ────────────────────────────────────
	// begin_frame() seeds scanline_scroll[] from Loopy T (= FCEUX's TempAddr
	// at pre-render dot 325) and snapshots OAM.
	ppu.begin_frame();

	// 240 visible scanlines × 341 PPU each (= FCEUX's DoLine()).
	for (int sl = 0; sl < 240; ++sl)
	{
		ppu.begin_scanline(sl);
		ppu.check_sprite0_hit(sl);
		run_ppu(341);
		ppu.end_scanline(sl);
		// Render this scanline live, reading from current mem[] so VRAM writes
		// made by the CPU during this scanline's run_ppu() are immediately visible.
		ppu.render_scanline(sl, ppu.framebuf);
	}
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
			ppu.export_frame(screen_buf, txtpath, &cpu);

			total++;
			if ((total % 64) == 0)
				printf("Progress: %d / 1024\n", total);
		}
	}

	delete[] screen_buf;
	printf("Autotest complete: %d screenshots saved to %s\\\n", total, outdir);
}

// Run nframes from reset with no input, saving a BMP every 'interval' frames.
// Output files: <outdir>\nes1_frame_NNNN.bmp
static void run_frametest(const char* outdir, int nframes, int interval)
{
	CreateDirectoryA(outdir, NULL);
	unsigned char* screen_buf = new unsigned char[SCREEN_WIDTH * SCREEN_HEIGHT * 4];
	memset(screen_buf, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 4);

	// Enable per-scanline live rendering: run_one_frame() will call render_scanline()
	// inside the visible scanline loop, writing pixels into screen_buf directly.
	ppu.framebuf = screen_buf;

	printf("Running %d frames, saving every %d...\n", nframes, interval);

	// Frames 1-2 are "ppudead": VBlank is suppressed so the ROM spins in its
	// VBlank-wait poll loop, exactly matching FCEUX's ppudead=2 behaviour.
	// These frames are exported (as grey backdrop screens) so that frame numbers
	// align 1:1 with FCEUX's exported frame numbering.
	int saved = 0;
	for (int f = 1; f <= nframes; f++)
	{
		bool dead = (f <= 2);
		run_one_frame(/*suppress_vbl=*/dead);
		if (f % interval == 0)
		{
			ppu.render(screen_buf);
			char path[512];
			sprintf_s(path, sizeof(path),
			          "%s\\nes1_frame_%04d.txt", outdir, f);
			ppu.export_frame(screen_buf, path, &cpu);
			saved++;
			printf("  Frame %4d / %d\n", f, nframes);
		}
	}

	ppu.framebuf = nullptr;  // restore: back to batch render() mode
	delete[] screen_buf;
	printf("Frametest done: %d screenshots saved to %s\\\n", saved, outdir);
}

// Run to frame 'target_frame' from reset and dump a per-scanline CPU+PPU trace
// for that frame to <outpath>.  Uses capture_scanline_trace() / export_scanline_trace().
static void run_scanlinetrace(const char* outpath, int target_frame)
{
	// A modified run_one_frame that captures per-scanline state.
	// We inline the frame loop here and intercept the visible-scanline step.
	static int ppu_remainder_tr = 0;
	static int cycle_counter_tr = 0;
	static int kook_tr          = 0;

	auto run_ppu_tr = [&](int ppu_cycles) {
		ppu_remainder_tr += ppu_cycles;
		cycle_counter_tr += ppu_remainder_tr / 3;
		ppu_remainder_tr  %= 3;
		while (cycle_counter_tr > 0) {
			unsigned int before = cpu.cycle;
			cpu.step(false);
			cycle_counter_tr -= (int)(cpu.cycle - before);
		}
	};

	printf("Running %d frames for scanline trace...\n", target_frame);

	for (int f = 1; f <= target_frame; f++)
	{
		bool is_target = (f == target_frame);
		bool dead = (f <= 2);

		if (dead) {
			run_ppu_tr(262 * 341);
			continue;
		}

		run_ppu_tr(341);
		ppu.raise_vblank();
		run_ppu_tr(12);
		if (ppu.nmi_enabled()) cpu.nmi();
		ppu.snap_nmi_scroll();
		run_ppu_tr(6808);
		ppu.snap_pre_render_vscroll();
		ppu.snapshot_render();
		run_ppu_tr(256);
		run_ppu_tr(69);
		run_ppu_tr(16 - kook_tr);
		kook_tr ^= 1;
		ppu.begin_frame();

		for (int sl = 0; sl < 240; ++sl) {
			ppu.begin_scanline(sl);
			ppu.check_sprite0_hit(sl);
			run_ppu_tr(341);
			if (is_target)
				ppu.capture_scanline_trace(sl, &cpu);
			ppu.end_scanline(sl);
		}
	}

	ppu.export_scanline_trace(outpath);
	printf("Scanline trace written to %s\n", outpath);
}

int main(int argc, char* argv[])
{
	prepareTimer();
	initMainWindow();

	// Load NES ROM file
	if (argc < 2)
	{
		printf("Usage: nes1 <rom.nes> [--autotest <outdir>] [--frametest <outdir> [nframes] [interval]]\n");
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

	// ── Automated headless test modes ───────────────────────────────────────
	if (argc >= 4 && strcmp(argv[2], "--autotest") == 0)
	{
		// Runs all 1024 (64 colours × 16 emphasis/greyscale) combinations
		run_autotest(argv[3]);
		return 0;
	}
	if (argc >= 4 && strcmp(argv[2], "--frametest") == 0)
	{
		// Runs nframes from reset, saving a screenshot every interval frames
		int nframes  = (argc >= 5) ? atoi(argv[4]) : 300;
		int interval = (argc >= 6) ? atoi(argv[5]) : 30;
		run_frametest(argv[3], nframes, interval);
		return 0;
	}
	if (argc >= 4 && strcmp(argv[2], "--scanlinetrace") == 0)
	{
		// Dumps per-scanline CPU+PPU state for frame N to <outpath>
		// Usage: nes1 <rom> --scanlinetrace <outpath> <frame_number>
		int target_frame = (argc >= 5) ? atoi(argv[4]) : 4;
		run_scanlinetrace(argv[3], target_frame);
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

