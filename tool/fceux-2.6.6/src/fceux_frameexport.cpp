// Per-frame txt+bmp export for FCEUX instrumentation.
// Outputs fceux_frame_NNNN.txt + .bmp (32bpp top-down) to match nes1 export format.

#include "types.h"
#include "x6502.h"
#include "x6502struct.h"
#include "ppu.h"
#include "palette.h"
#include "video.h"
#include "fceux_frameexport.h"

#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

// FCEUX PPU globals (from ppu.cpp)
extern uint8  NTARAM[0x800];
extern uint8  PALRAM[0x20];
extern uint8  SPRAM[0x100];
extern uint32 RefreshAddr;   // Loopy v
extern uint32 TempAddr;      // Loopy t
extern uint8  XOffset;       // fine_x (3 bits)
extern uint8  vtoggle;       // w toggle
extern uint8 *vnapage[4];    // nametable page pointers

// PPU registers are in PPU[0..3]; PPU[2] == PPU_status
extern uint8 PPU[4];

// CPU state
extern X6502 X;
extern uint32 timestamp;
extern uint64 timestampbase;

// CPU RAM (2KB at $0000-$07FF)
extern uint8 *RAM;

// Pixel buffer: 256*256 bytes, each is a palette index.
// XBackBuf is saved before OSD/messages are drawn, so it has the clean frame.
extern uint8 *XBuf;
extern uint8 *XBackBuf;

// ── Per-scanline trace ────────────────────────────────────────────────────────
// FCEUXScanlineTrace struct is defined in fceux_frameexport.h

static FCEUXScanlineTrace g_sl_trace[240];
static bool               g_sl_trace_active = false;

bool FCEUX_ScanlineTraceActive() { return g_sl_trace_active; }

void FCEUX_EnableScanlineTrace()  { g_sl_trace_active = true; }
void FCEUX_DisableScanlineTrace() { g_sl_trace_active = false; }

// Returns pointer to the 240-entry trace array (valid until next frame).
const FCEUXScanlineTrace* FCEUX_GetScanlineTrace() { return g_sl_trace; }

// Called at START of visible scanline's CPU window (before X6502_Run(256)).
void FCEUX_CaptureBeginScanline(int sl)
{
    if (sl < 0 || sl >= 240) return;
    FCEUXScanlineTrace& e = g_sl_trace[sl];
    e.ppu_v_start    = RefreshAddr;
    e.ppuctrl_start  = PPU[0];
    e.ppumask_start  = PPU[1];
    e.nt_write_cnt   = 0;     // reset NT write log for this scanline
}

// Called at END of visible scanline's CPU window (after X6502_Run(256)).
void FCEUX_CaptureScanlineTrace(int sl)
{
    if (sl < 0 || sl >= 240) return;
    FCEUXScanlineTrace& e = g_sl_trace[sl];
    e.cpu_pc     = (unsigned short)X.PC;
    e.cpu_a      = X.A;
    e.cpu_x      = X.X;
    e.cpu_y      = X.Y;
    e.cpu_s      = X.S;
    e.cpu_p      = X.P;
    e.cpu_cycles = (unsigned int)(timestampbase + timestamp);
    e.ppu_v      = RefreshAddr;
    e.ppu_t      = TempAddr;
    e.ppu_fine_x = XOffset;
    e.ppu_w      = vtoggle;
    e.ppuctrl    = PPU[0];
    e.ppumask    = PPU[1];
    e.ppustatus  = PPU[2];
    e.oamaddr    = PPU[3];
    for (int i = 0; i < 32; ++i)
        e.palette[i] = PALRAM[i];
    memcpy(e.nametable, NTARAM, 0x800);
    if (RAM) memcpy(e.cpu_ram, RAM, 0x800);
    else     memset(e.cpu_ram, 0, 0x800);
    memcpy(e.oam, SPRAM, 0x100);
    e.mirror_vertical = (vnapage[0] == vnapage[2] && vnapage[1] == vnapage[3]) ? 1 : 0;

    // Decode render scroll from end-of-scanline V register
    {
        uint32 vr = RefreshAddr;
        int coarse_x = vr & 0x1F;
        int coarse_y = (vr >> 5) & 0x1F;
        int fine_y   = (vr >> 12) & 0x7;
        e.ss_x  = (short)(coarse_x * 8 + XOffset);
        e.ss_y  = (short)(coarse_y * 8 + fine_y);
        e.ss_nt = (unsigned char)((vr >> 10) & 3);
    }
}

// Log a $2007 NT/AT write during a visible scanline's CPU window.
// Called from the PPU write handler (B2007) when scanline trace is active.
// Only writes in the $2000-$3EFF range (nametable/attribute) are logged.
void FCEUX_LogNTWrite(uint32 addr, uint8 val, unsigned short pc)
{
    extern int scanline;
    if (scanline < 0 || scanline >= 240) return;
    FCEUXScanlineTrace& e = g_sl_trace[scanline];
    if (e.nt_write_cnt >= 64) return;   // cap at 64 per scanline
    FCEUXNtWrite& w = e.nt_writes[e.nt_write_cnt++];
    // dot: approximate from CPU cycle offset within scanline (3 PPU dots per CPU cycle)
    extern uint32 timestamp;
    // timestamp is the CPU cycle counter since last timestampbase reset.
    // Within a scanline, X6502_Run(256) runs 256/3 ≈ 85 CPU cycles for visible portion,
    // then X6502_Run(16) for HBlank ≈ 5 cycles.  Use the lower bits of timestamp mod ~114.
    // For simplicity, use the raw timestamp (caller could refine later).
    w.dot    = (int)(timestamp % 114) * 3;  // approximate PPU dot
    w.addr   = (unsigned short)(addr & 0x3FFF);
    w.val    = val;
    w.cpu_pc = pc;
}

// Export one txt+bmp file per visible scanline for the given frame.
// Output: <outdir>\fceux_frame_XXXX_sl_YYY.txt + .bmp  (240 pairs)
// Each file has the exact same sections as the frame-level output,
// using end-of-scanline captured state, plus "NT Writes During Scanline" at end.
// The BMP shows the full rendered frame with scanline sl highlighted in red.
void FCEUX_ExportScanlineLevel(int framenum, const char* outdir)
{
#ifdef _WIN32
    CreateDirectoryA(outdir, NULL);
#endif
    const char* shades = " .:-=+*#%%@";
    const int nshades = 10;

    for (int sl = 0; sl < 240; ++sl) {
        const FCEUXScanlineTrace& e = g_sl_trace[sl];

        // ---- Text file ----
        char path[512];
        snprintf(path, sizeof(path), "%s\\fceux_frame_%04d_sl_%03d.txt", outdir, framenum, sl);
        FILE* f = fopen(path, "w");
        if (!f) continue;

        // --- CPU State ---
        fprintf(f, "=== CPU State ===\n");
        fprintf(f, "PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X cycles=%u\n\n",
            e.cpu_pc, e.cpu_a, e.cpu_x, e.cpu_y, e.cpu_s, e.cpu_p, e.cpu_cycles);

        // --- PPU Registers ---
        const char* mirror_str = e.mirror_vertical ? "vertical" : "horizontal";
        fprintf(f, "=== PPU Registers ===\n");
        fprintf(f, "PPUCTRL  ($2000): %02X  (NMI:%d BGtable:%04X SPRtable:%04X inc:%d)\n",
            e.ppuctrl,
            (e.ppuctrl >> 7) & 1,
            (e.ppuctrl & 0x10) ? 0x1000 : 0x0000,
            (e.ppuctrl & 0x08) ? 0x1000 : 0x0000,
            (e.ppuctrl & 0x04) ? 32 : 1);
        fprintf(f, "PPUMASK  ($2001): %02X  (BG:%d SPR:%d)\n",
            e.ppumask, (e.ppumask >> 3) & 1, (e.ppumask >> 4) & 1);
        fprintf(f, "PPUSTATUS($2002): %02X  (VBL:%d)\n",
            e.ppustatus, (e.ppustatus >> 7) & 1);
        fprintf(f, "v=%04X  t=%04X  fine_x=%d  mirroring=%s\n",
            e.ppu_v & 0x7FFF, e.ppu_t & 0x7FFF, (int)e.ppu_fine_x, mirror_str);
        {
            uint32 ve = e.ppu_v;
            int cx = ve & 0x1F, cy = (ve >> 5) & 0x1F, fy = (ve >> 12) & 0x7, nt = (ve >> 10) & 0x3;
            int sx = cx * 8 + (int)e.ppu_fine_x, sy = cy * 8 + fy;
            fprintf(f, "render_scroll_x=%d (coarse=%d fine=%d)  render_scroll_y=%d (coarse=%d fine=%d)  render_scroll_nt=%d\n\n",
                sx, cx, (int)e.ppu_fine_x, sy, cy, fy, nt);
        }

        // --- Nametable ($2000-$23BF, 32x30 tile IDs) ---
        fprintf(f, "=== Nametable ($2000-$23BF, 32x30 tile IDs) ===\n");
        for (int row = 0; row < 30; ++row) {
            for (int col = 0; col < 32; ++col)
                fprintf(f, "%02X ", e.nametable[row * 32 + col]);
            fprintf(f, "\n");
        }

        // --- Palette ---
        fprintf(f, "\n=== Palette ($3F00-$3F1F) ===\n");
        fprintf(f, "BG:  ");
        for (int i = 0; i < 16; ++i) fprintf(f, "%02X ", e.palette[i]);
        fprintf(f, "\nSPR: ");
        for (int i = 0; i < 16; ++i) fprintf(f, "%02X ", e.palette[0x10 + i]);
        fprintf(f, "\n");

        // --- Active Sprites (OAM) ---
        fprintf(f, "\n=== Active Sprites (OAM) ===\n");
        fprintf(f, "  # |  Y   X  Tile Attr\n");
        for (int i = 0; i < 64; ++i) {
            unsigned char sy = e.oam[i * 4 + 0];
            unsigned char st = e.oam[i * 4 + 1];
            unsigned char sa = e.oam[i * 4 + 2];
            unsigned char sx = e.oam[i * 4 + 3];
            if (sy < 0xEF)
                fprintf(f, " %2d | %3d %3d   %02X   %02X\n", i, (int)sy + 1, (int)sx, st, sa);
        }

        // --- ASCII Screen (32x30 tiles) ---
        fprintf(f, "\n=== ASCII Screen (32x30 tiles) ===\n");
        if (XBackBuf && palo) {
            for (int ty = 0; ty < 30; ++ty) {
                for (int tx = 0; tx < 32; ++tx) {
                    int sum = 0;
                    for (int py = 0; py < 8; ++py)
                        for (int px = 0; px < 8; ++px) {
                            uint8 idx = XBackBuf[(ty * 8 + py) * 256 + (tx * 8 + px)] & 0x3F;
                            sum += palo[idx].r + palo[idx].g + palo[idx].b;
                        }
                    int brightness = sum / (64 * 3);
                    int si = brightness * (nshades - 1) / 255;
                    fprintf(f, "%c", shades[si]);
                }
                fprintf(f, "\n");
            }
        } else {
            fprintf(f, "(no pixel buffer)\n");
        }

        // --- CPU RAM ($0000-$07FF) ---
        fprintf(f, "\n=== CPU RAM ($0000-$07FF) ===\n");
        for (int row = 0; row < 64; ++row) {
            for (int col = 0; col < 32; ++col)
                fprintf(f, "%02X ", e.cpu_ram[row * 32 + col]);
            fprintf(f, "\n");
        }

        // --- NT Writes During This Scanline ---
        fprintf(f, "\n=== NT Writes During Scanline ===\n");
        if (e.nt_write_cnt == 0) {
            fprintf(f, "(none)\n");
        } else {
            fprintf(f, "#   DOT  NTADDR  VAL   PC\n");
            for (int i = 0; i < e.nt_write_cnt; ++i) {
                const FCEUXNtWrite& w = e.nt_writes[i];
                fprintf(f, "%2d  %4d   %04X   %02X   %04X\n",
                    i, w.dot, w.addr, w.val, w.cpu_pc);
            }
        }

        fclose(f);

        // ---- BMP file: full frame with scanline sl highlighted in red ----
        if (!XBackBuf || !palo) continue;

        char bmppath[512];
        snprintf(bmppath, sizeof(bmppath), "%s\\fceux_frame_%04d_sl_%03d.bmp", outdir, framenum, sl);
        FILE* bf = fopen(bmppath, "wb");
        if (!bf) continue;

        const int W = 256, H = 240;
        unsigned int pix_size  = (unsigned int)(W * H * 4);
        unsigned int file_size = 14 + 40 + pix_size;
        unsigned char bfh[14] = {
            'B','M',
            (unsigned char)(file_size      ), (unsigned char)(file_size >>  8),
            (unsigned char)(file_size >> 16), (unsigned char)(file_size >> 24),
            0,0,0,0, 54,0,0,0
        };
        fwrite(bfh, 1, 14, bf);

        int bih_h = -H;
        unsigned short planes = 1, bits = 32;
        unsigned int comp = 0, img_sz = 0, clr = 0, hdr = 40;
        int ppm = 2835;
        unsigned char dib[40];
        memcpy(dib +  0, &hdr,    4); memcpy(dib +  4, &W,      4);
        memcpy(dib +  8, &bih_h,  4); memcpy(dib + 12, &planes, 2);
        memcpy(dib + 14, &bits,   2); memcpy(dib + 16, &comp,   4);
        memcpy(dib + 20, &img_sz, 4); memcpy(dib + 24, &ppm,    4);
        memcpy(dib + 28, &ppm,    4); memcpy(dib + 32, &clr,    4);
        memcpy(dib + 36, &clr,    4);
        fwrite(dib, 1, 40, bf);

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                uint8 idx = XBackBuf[y * 256 + x] & 0x3F;
                uint8 r = palo[idx].r, g = palo[idx].g, b = palo[idx].b;
                if (y == sl) { r = (uint8)((r + 255) / 2); g /= 2; b /= 2; }
                unsigned char px[4] = { b, g, r, 0xFF };
                fwrite(px, 1, 4, bf);
            }
        }
        fclose(bf);
    }
}

void FCEUX_ExportFrame(int framenum, int nframes, const char* outdir)
{
    char txtpath[512];
    snprintf(txtpath, sizeof(txtpath), "%s\\fceux_frame_%04d.txt", outdir, framenum);

    FILE* f = fopen(txtpath, "w");
    if (!f) return;

    // --- CPU State ---
    fprintf(f, "=== CPU State ===\n");
    fprintf(f, "PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X cycles=%u\n\n",
        (unsigned)X.PC,
        (unsigned)X.A,
        (unsigned)X.X,
        (unsigned)X.Y,
        (unsigned)X.S,
        (unsigned)X.P,
        (unsigned)(timestampbase + timestamp));

    // --- PPU Registers ---
    uint8 ppuctrl   = PPU[0];
    uint8 ppumask   = PPU[1];
    uint8 ppustatus = PPU[2];

    fprintf(f, "=== PPU Registers ===\n");
    fprintf(f, "PPUCTRL  ($2000): %02X  (NMI:%d BGtable:%04X SPRtable:%04X inc:%d)\n",
        ppuctrl,
        (ppuctrl >> 7) & 1,
        (ppuctrl & 0x10) ? 0x1000 : 0x0000,
        (ppuctrl & 0x08) ? 0x1000 : 0x0000,
        (ppuctrl & 0x04) ? 32 : 1);
    fprintf(f, "PPUMASK  ($2001): %02X  (BG:%d SPR:%d)\n",
        ppumask, (ppumask >> 3) & 1, (ppumask >> 4) & 1);
    fprintf(f, "PPUSTATUS($2002): %02X  (VBL:%d)\n",
        ppustatus, (ppustatus >> 7) & 1);

    // Detect mirroring from vnapage pointers
    // Vertical: vnapage[0]==vnapage[2], vnapage[1]==vnapage[3]
    // Horizontal: vnapage[0]==vnapage[1], vnapage[2]==vnapage[3]
    const char* mirror_str = "unknown";
    if (vnapage[0] && vnapage[1] && vnapage[2] && vnapage[3]) {
        if (vnapage[0] == vnapage[2] && vnapage[1] == vnapage[3])
            mirror_str = "vertical";
        else if (vnapage[0] == vnapage[1] && vnapage[2] == vnapage[3])
            mirror_str = "horizontal";
    }

    fprintf(f, "v=%04X  t=%04X  fine_x=%d  mirroring=%s\n",
        (unsigned)RefreshAddr, (unsigned)TempAddr, (unsigned)XOffset, mirror_str);

    // Scroll decode from Loopy v (RefreshAddr)
    // coarse_x = v[4:0], fine_x = XOffset
    // coarse_y = v[9:5], fine_y = v[14:12]
    // nt_sel   = v[11:10]
    uint32 v = RefreshAddr;
    int coarse_x = v & 0x1F;
    int coarse_y = (v >> 5) & 0x1F;
    int fine_y   = (v >> 12) & 0x7;
    int nt_sel   = (v >> 10) & 0x3;
    int scroll_x = coarse_x * 8 + XOffset;
    int scroll_y = coarse_y * 8 + fine_y;
    fprintf(f, "render_scroll_x=%d (coarse=%d fine=%d)  render_scroll_y=%d (coarse=%d fine=%d)  render_scroll_nt=%d\n\n",
        scroll_x, coarse_x, (int)XOffset,
        scroll_y, coarse_y, fine_y,
        nt_sel);

    // --- Nametable dump ($2000-$23BF, 32x30) ---
    fprintf(f, "=== Nametable ($2000-$23BF, 32x30 tile IDs) ===\n");
    for (int row = 0; row < 30; ++row) {
        for (int col = 0; col < 32; ++col) {
            uint32 addr = 0x2000 + row * 32 + col;
            uint32 a    = addr - 0x2000;
            uint8 *page = vnapage[(a >> 10) & 3];
            uint8 val   = page ? page[a & 0x3FF] : 0;
            fprintf(f, "%02X ", val);
        }
        fprintf(f, "\n");
    }

    // --- Palette dump ($3F00-$3F1F) ---
    fprintf(f, "\n=== Palette ($3F00-$3F1F) ===\n");
    fprintf(f, "BG:  ");
    for (int i = 0; i < 16; ++i) fprintf(f, "%02X ", PALRAM[i]);
    fprintf(f, "\nSPR: ");
    for (int i = 0; i < 16; ++i) fprintf(f, "%02X ", PALRAM[0x10 + i]);
    fprintf(f, "\n");

    // --- Active Sprites (OAM) ---
    fprintf(f, "\n=== Active Sprites (OAM) ===\n");
    fprintf(f, "  # |  Y   X  Tile Attr\n");
    for (int i = 0; i < 64; ++i) {
        uint8 sy = SPRAM[i * 4 + 0];
        uint8 st = SPRAM[i * 4 + 1];
        uint8 sa = SPRAM[i * 4 + 2];
        uint8 sx = SPRAM[i * 4 + 3];
        if (sy < 0xEF)
            fprintf(f, " %2d | %3d %3d   %02X   %02X\n", i, (int)sy + 1, (int)sx, st, sa);
    }

    // --- ASCII Screen (32x30 tiles) ---
    fprintf(f, "\n=== ASCII Screen (32x30 tiles) ===\n");
    const char* shades = " .:-=+*#%%@";
    int nshades = 10;
    if (XBackBuf && palo) {
        for (int ty = 0; ty < 30; ++ty) {
            for (int tx = 0; tx < 32; ++tx) {
                int sum = 0;
                for (int py = 0; py < 8; ++py) {
                    for (int px = 0; px < 8; ++px) {
                        int row = ty * 8 + py;
                        int col = tx * 8 + px;
                        uint8 idx = XBackBuf[row * 256 + col] & 0x3F;
                        sum += palo[idx].r;
                        sum += palo[idx].g;
                        sum += palo[idx].b;
                    }
                }
                int brightness = sum / (64 * 3);
                int si = brightness * (nshades - 1) / 255;
                fprintf(f, "%c", shades[si]);
            }
            fprintf(f, "\n");
        }
    } else {
        fprintf(f, "(no pixel buffer)\n");
    }

    // --- CPU RAM ($0000-$07FF) ---
    if (RAM) {
        fprintf(f, "\n=== CPU RAM ($0000-$07FF) ===\n");
        for (int row = 0; row < 64; ++row) {
            for (int col = 0; col < 32; ++col)
                fprintf(f, "%02X ", RAM[row * 32 + col]);
            fprintf(f, "\n");
        }
    }

    fclose(f);

    // --- BMP output (32bpp BGRA, top-down, height=-240, matches nes1) ---
    if (!XBackBuf || !palo) return;

    char bmppath[512];
    snprintf(bmppath, sizeof(bmppath), "%s\\fceux_frame_%04d.bmp", outdir, framenum);

    FILE* bf = fopen(bmppath, "wb");
    if (!bf) return;

    const int W = 256, H = 240;
    unsigned int pix_size  = (unsigned int)(W * H * 4);
    unsigned int file_size = 14 + 40 + pix_size;

    // BMP file header (14 bytes)
    unsigned char bfh[14] = {
        'B','M',
        (unsigned char)(file_size      ), (unsigned char)(file_size >>  8),
        (unsigned char)(file_size >> 16), (unsigned char)(file_size >> 24),
        0,0,0,0,
        54,0,0,0
    };
    fwrite(bfh, 1, 14, bf);

    // BITMAPINFOHEADER (40 bytes) — negative height = top-down
    int bih_h = -H;
    unsigned short planes = 1, bits = 32;
    unsigned int comp = 0, img_sz = 0, clr = 0;
    int ppm = 2835;
    unsigned int hdr = 40;
    unsigned char dib[40];
    memcpy(dib +  0, &hdr,    4);
    memcpy(dib +  4, &W,      4);
    memcpy(dib +  8, &bih_h,  4);
    memcpy(dib + 12, &planes, 2);
    memcpy(dib + 14, &bits,   2);
    memcpy(dib + 16, &comp,   4);
    memcpy(dib + 20, &img_sz, 4);
    memcpy(dib + 24, &ppm,    4);
    memcpy(dib + 28, &ppm,    4);
    memcpy(dib + 32, &clr,    4);
    memcpy(dib + 36, &clr,    4);
    fwrite(dib, 1, 40, bf);

    // Write pixels: XBackBuf rows 0..239, columns 0..255 (clean frame, no OSD)
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            uint8 idx = XBackBuf[y * 256 + x] & 0x3F;
            uint8 r = palo[idx].r;
            uint8 g = palo[idx].g;
            uint8 b = palo[idx].b;
            unsigned char px[4] = { b, g, r, 0xFF };
            fwrite(px, 1, 4, bf);
        }
    }

    fclose(bf);
}
