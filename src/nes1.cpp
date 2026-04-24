// nes1.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "cpu.h"
#include "ppu.h"

unsigned char prg_rom[16384];
unsigned char chr_rom[8192];

#define FRAME_FREQUENCY 60

LARGE_INTEGER timer_frequency;
LARGE_INTEGER timer_last_count;

HWND hMainWindow;

HBITMAP hDibSection;
unsigned char* pScreenBits;

std::deque<LONGLONG> time_paint;

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

	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);

		// Create memory DC
		HDC memorydc = CreateCompatibleDC(hdc);
		HGDIOBJ oldBitmap = SelectObject(memorydc, hDibSection);
		BitBlt(hdc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, memorydc, 0, 0, SRCCOPY);
		BitBlt(hdc, SCREEN_WIDTH + 1, 0, SCREEN_WIDTH, SCREEN_HEIGHT, memorydc, 0, 0, SRCCOPY);
		BitBlt(hdc, 0, SCREEN_HEIGHT + 1, SCREEN_WIDTH, SCREEN_HEIGHT, memorydc, 0, 0, SRCCOPY);
		BitBlt(hdc, SCREEN_WIDTH + 1, SCREEN_HEIGHT + 1, SCREEN_WIDTH, SCREEN_HEIGHT, memorydc, 0, 0, SRCCOPY);
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

	SetWindowPos(hMainWindow, HWND_TOP, 100, 100, SCREEN_WIDTH + xdiff, SCREEN_HEIGHT + ydiff, SWP_SHOWWINDOW);
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

int main(int argc, char* argv[])
{
	prepareTimer();
	initMainWindow();

	// Load test/nestest.nes file
	FILE *fp = NULL;
	fopen_s(&fp, "../test/nestest.nes", "rb");
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

	// Assert the prg_rom and chr_rom sizes
	if (header[4] != 1 || header[5] != 1)
	{
		printf("Only 1 prg_rom and 1 chr_rom NES file is supported now.\n");
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
	fseek(fp, 0, SEEK_END);
	long filesize = ftell(fp);
	if (filesize != sizeof(header) + 16384 + 8192)
	{
		printf("File size incorrect.\n");
		fclose(fp);
		return 0;
	}
	fseek(fp, sizeof(header), SEEK_SET);

	// Initialize the 6502 CPU, load the PRG_ROM into CPU.
	fread(prg_rom, 1, 16384, fp);
	fread(chr_rom, 1, 8192, fp);
	fclose(fp);

	cpu_6502 cpu;
	cpu.load_prg_rom(prg_rom, 16384);
	cpu.reset();

	ppu_2c02 ppu;
	ppu.load_chr_rom(chr_rom, 8192);
        ppu.set_mirroring((header[6] & 0x01) != 0);
        cpu.set_ppu(&ppu);

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
			ppu.set_vblank(true);
			if (ppu.nmi_enabled())
				cpu.nmi();
			ppu.render(pScreenBits);
			ppu.set_vblank(false);
			InvalidateRect(hMainWindow, NULL, FALSE);

			static int frame_count = 0;
			++frame_count;
			if (frame_count == 120)  // export at frame 120 (~2 seconds)
				ppu.export_frame(pScreenBits, "frame_dump.txt");
		}

		result = cpu.step(true);
		Sleep(0);
	}

    return 0;
}

