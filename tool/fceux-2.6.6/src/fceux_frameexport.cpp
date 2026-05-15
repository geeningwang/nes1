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

// Pixel buffer: 256*256 bytes, each is a palette index
extern uint8 *XBuf;

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
}

// Called at END of visible scanline's CPU window (after X6502_Run(256)).
void FCEUX_CaptureScanlineTrace(int sl)
{
    if (sl < 0 || sl >= 240) return;
    static int capture_count = 0;
    if (++capture_count <= 5) {
        FILE* dbg = fopen("C:/Work/nes1/test/mappy_out/fceux_capture_debug.txt", "a");
        if (dbg) { fprintf(dbg, "FCEUX_CaptureScanlineTrace sl=%d CYC=%llu PC=%04X\n", sl, (unsigned long long)(timestampbase + timestamp), (unsigned int)X.PC); fclose(dbg); }
    }
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
    for (int i = 0; i < 32; ++i)
        e.palette[i] = PALRAM[i];
}

void FCEUX_ExportScanlineTrace(const char* outpath)
{
    FILE* f = fopen(outpath, "w");
    if (!f) return;

    // PART1: per-scanline summary, one row per visible scanline.
    // Columns match nes1's export_scanline_trace() PART1 format:
    //   SL V_S V_E T_E FX W CTL MSK SSX SS_Y SSNT NTw CYC PC A X Y S P PAL
    // FCEUX outputs 0 for SSX/SS_Y/SSNT (not tracked by C-side capture).
    fprintf(f, "=== PART1 ===\n");
    fprintf(f, "# %-3s  %-5s %-5s %-5s %2s %1s  %2s  %2s  %3s %6s %4s  %3s  %-8s  %-4s %-2s %-2s %-2s %-2s %-2s  PAL\n",
        "SL", "V_S", "V_E", "T_E", "FX", "W", "CTL", "MSK",
        "SSX", "SS_Y", "SSNT", "NTw", "CYC", "PC", "A", "X", "Y", "S", "P");

    for (int sl = 0; sl < 240; ++sl) {
        const FCEUXScanlineTrace& e = g_sl_trace[sl];
        char pal64[65] = {};
        for (int i = 0; i < 32; ++i)
            snprintf(pal64 + i * 2, 3, "%02X", e.palette[i]);
        fprintf(f, "  %3d  %04X  %04X  %04X  %d  %d  %02X  %02X  %3d %6d    %d  %3d  %8u  %04X %02X %02X %02X %02X %02X  %s\n",
            sl,
            e.ppu_v_start & 0x7FFF, e.ppu_v & 0x7FFF, e.ppu_t & 0x7FFF,
            e.ppu_fine_x, e.ppu_w,
            e.ppuctrl, e.ppumask,
            0, 0, 0,           // SSX, SS_Y, SSNT – not available from C-side
            0,                 // NTw – not tracked C-side; Lua script fills this
            e.cpu_cycles,
            e.cpu_pc, e.cpu_a, e.cpu_x, e.cpu_y, e.cpu_s, e.cpu_p,
            pal64);
    }

    fclose(f);
}

// Nametable read via vnapage (mirrors NTARAM for NROM)
static uint8 nt_read(uint32 addr)
{
    // addr in $2000-$23FF range (first nametable)
    uint32 a = addr - 0x2000;
    uint8 *page = vnapage[(a >> 10) & 3];
    if (page)
        return page[a & 0x3FF];
    return NTARAM[a & 0x7FF];
}

void FCEUX_ExportFrame(int framenum, const char* outdir)
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
    if (XBuf && palo) {
        for (int ty = 0; ty < 30; ++ty) {
            for (int tx = 0; tx < 32; ++tx) {
                int sum = 0;
                for (int py = 0; py < 8; ++py) {
                    for (int px = 0; px < 8; ++px) {
                        int row = ty * 8 + py;
                        int col = tx * 8 + px;
                        uint8 idx = XBuf[row * 256 + col] & 0x3F;
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

    fclose(f);

    // --- BMP output (32bpp BGRA, top-down, height=-240, matches nes1) ---
    if (!XBuf || !palo) return;

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

    // Write pixels: XBuf rows 0..239, columns 0..255
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            uint8 idx = XBuf[y * 256 + x] & 0x3F;
            uint8 r = palo[idx].r;
            uint8 g = palo[idx].g;
            uint8 b = palo[idx].b;
            unsigned char px[4] = { b, g, r, 0xFF };
            fwrite(px, 1, 4, bf);
        }
    }

    fclose(bf);
}
