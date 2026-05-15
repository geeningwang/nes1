#pragma once

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240
#define DISPLAY_SCALE 2

struct ScanlineScroll {
	unsigned char scroll_x;
	unsigned char fine_x;
	short         scroll_y;    // abs_y (0..479): per-scanline vertical position
	unsigned char scroll_nt;
};

// A single $2007 (PPUDATA) write captured during a visible scanline's CPU window.
// Stored in nt_write_log[] so render_scanline() can replay writes at per-tile
// granularity instead of using a whole-frame VRAM snapshot.
struct NtWrite {
	int            dot;     // approximate PPU dot within scanline (col*8 units)
	unsigned short addr;    // mirrored NT address ($2000-$27FF)
	unsigned char  val;
	unsigned short cpu_pc;  // CPU PC at time of write (for trace/debug)
};

// Per-scanline CPU+PPU state.  Two sub-snapshots per entry:
//   START fields: captured in begin_scanline() BEFORE the scanline's CPU run.
//   END   fields: captured in capture_scanline_trace() AFTER the CPU run.
struct ScanlineTraceEntry {
	// --- START-OF-SCANLINE snapshot (begin_scanline, before CPU run) ---
	unsigned short v_start;    // V register before this scanline's CPU window
	unsigned char  ppuctrl;    // PPUCTRL ($2000) at start of scanline
	unsigned char  ppumask;    // PPUMASK ($2001) at start of scanline
	// Actual render scroll values nes1 will use for this scanline
	unsigned char  ss_x;       // scanline_scroll[sl].scroll_x
	short          ss_y;       // scanline_scroll[sl].scroll_y  (abs_y 0-479)
	unsigned char  ss_nt;      // scanline_scroll[sl].scroll_nt

	// --- END-OF-SCANLINE snapshot (capture_scanline_trace, after CPU run) ---
	// CPU state
	unsigned short cpu_pc;
	unsigned char  cpu_a, cpu_x, cpu_y, cpu_s, cpu_p;
	unsigned int   cpu_cycles;   // total CPU cycle counter

	// PPU Loopy registers (live, not the render snapshots)
	unsigned short ppu_v;
	unsigned short ppu_t;
	unsigned char  ppu_fine_x;
	unsigned char  ppu_w;

	// Raw scroll registers ($2005 write values and $2000 NT bits)
	unsigned char  scroll_x_reg;
	unsigned char  scroll_y_reg;
	unsigned char  scroll_nt;

// Full palette snapshot (32 bytes: $3F00-$3F1F)
    unsigned char  palette[32];

	// --- NT writes that occurred during this scanline's CPU window ---
	int            nt_write_cnt;    // number of writes logged (capped at 64)
	NtWrite        nt_writes[64];   // copy of nt_write_log[] at end of scanline

	// Full memory snapshots at end of scanline
	unsigned char  nametable[0x800]; // physical NT RAM ($2000-$27FF, 2KB)
	unsigned char  cpu_ram[0x800];   // CPU RAM ($0000-$07FF, 2KB)
	unsigned char  oam_snap[0x100];  // OAM (256 bytes)
	unsigned char  mirror_v;         // 1=vertical, 0=horizontal
	// Additional PPU registers at end of scanline
	unsigned char  ppustatus;        // $2002
	unsigned char  oamaddr;          // $2003
};

class cpu_6502;

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

	// VBL-first helpers (used by run_one_frame in frametest mode to match FCEUX structure):
	//   raise_vblank()     – set VBL flag only (no snapshot); ROM will exit its spin loop next read
	//   snapshot_render()  – capture NT/PAL/scroll into render buffers and clear VBL flag
	void raise_vblank();
	void snapshot_render();

	// Set nametable mirroring (false=horizontal, true=vertical)
	void set_mirroring(bool vertical);

	// Per-scanline rendering support:
	//   begin_frame()         – call before visible scanlines; clears spr0-hit, seeds scrolls
	//   check_sprite0_hit(sl) – call before each scanline's CPU cycles
	//   end_scanline(sl)      – call after each scanline's CPU cycles; snapshots scroll
	//   snap_nmi_scroll()     – call immediately after cpu.nmi() returns; saves post-NMI scroll
	void begin_frame();
	void begin_scanline(int scanline);  // call BEFORE each scanline's run_ppu — snaps palette
	void render_scanline(int sl, unsigned char* fb); // per-scanline live rendering (reads live mem[])
	void check_sprite0_hit(int scanline);
	void end_scanline(int scanline);
	void snap_nmi_scroll();
	// Call between run_ppu(256) and run_ppu(69) of the pre-render scanline.
	// Captures T's vertical bits so begin_frame() uses the pre-dot-280 vertical scroll,
	// matching the NES hardware T→V vertical copy at pre-render dots 280-304.
	void snap_pre_render_vscroll();

	// Dump nametable, palette, and ASCII screen art to a text file
	void export_frame(unsigned char* pScreenBits, const char* filename, cpu_6502* cpu = nullptr);

	// Per-scanline trace capture (called from nes1.cpp during a traced frame).
	// call capture_scanline_trace(sl, cpu) after each visible scanline's CPU run.
	// Then call export_scanline_level(framenum, outdir, screenbuf) to write
	// 240 per-scanline txt+bmp files (one pair per visible scanline).
	void capture_scanline_trace(int sl, cpu_6502* cpu);
	void export_scanline_trace(const char* filename);   // legacy single-file PART1/PART2 format
	void export_scanline_level(int framenum, const char* outdir, unsigned char* pScreenBits);

	// Active framebuffer for per-scanline live rendering.
	// When non-null, render_scanline() writes directly here and render() is a no-op.
	// Set from outside before each frame; null = use batch render().
	unsigned char* framebuf;

	// Backref to the CPU — set once via set_cpu(); used inside cpu_write($2007)
	// to compute the current PPU dot for per-tile VRAM write tracking.
	void set_cpu(cpu_6502* c) { cpu_ptr = c; }

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

	// OAM snapshot captured at begin_frame() — start of visible scanlines, before
	// the game's main loop has had any chance to advance sprite positions this frame.
	// This matches what FCEUX sees: OAM written by the *previous* NMI's DMA.
	unsigned char render_oam[256];

	// Nametable + palette snapshots captured at set_vblank(true), before NMI fires.
	// The NMI handler writes new tile columns to the nametable (horizontal scroll
	// update); render() must see the pre-NMI nametable to match FCEUX.
	unsigned char render_nt[0x800];   // $2000-$27FF (2KB, both nametables)
	unsigned char render_pal[0x20];   // $3F00-$3F1F (32 bytes)

	// Per-scanline scroll snapshot: entry [sl] is captured by end_scanline(sl)
	// after the CPU has run that scanline's ~114 cycles.  render() uses
	// scanline_scroll[row*8] for each tile row so mid-frame scroll changes
	// (e.g. sprite-0-hit scroll split) are applied correctly.
	ScanlineScroll scanline_scroll[240];

	// Per-scanline palette snapshot: entry [sl] is captured by end_scanline(sl)
	// from the live PPU palette memory (with proper $3F10/$3F14/$3F18/$3F1C mirrors).
	// render() uses scanline_pal[sl] for each screen row so mid-frame palette writes
	// (e.g. universal BG color change during visible scanning) are applied correctly.
	unsigned char scanline_pal[240][32];

	// Post-NMI scroll snapshot: saved immediately after cpu.nmi() returns,
	// so begin_frame() can seed scanline_scroll[] with the NMI-written scroll
	// rather than the live scroll_x_reg (which may have been overwritten by
	// game-loop code running during the vblank remainder of the previous frame).
	unsigned char nmi_scroll_x;
	unsigned char nmi_fine_x;
	unsigned char nmi_scroll_y;
	unsigned char nmi_scroll_nt;

	// Vertical scroll captured at pre-render dot ~280 (before game writes new
	// vertical scroll targeting the NEXT frame). begin_frame() uses this for
	// seed_scroll_y, mirroring the NES hardware T→V vertical copy at dots 280-304.
	unsigned char pre_render_scroll_y;
	unsigned char pre_render_scroll_nt_y; // bit 1 of NT (vertical NT select)

	// Mid-render V-register reset tracking.
	// Set in the $2006 second-write handler when the write occurs during visible
	// rendering; cleared by end_scanline() after it has reseeded abs_y.
	bool           mid_render_v_reset;
	unsigned short mid_render_v_new;   // value of T at the time of the reset (= new V)

	// Per-scanline trace buffer (filled by capture_scanline_trace, written by export_scanline_trace)
	ScanlineTraceEntry scanline_trace[240];

	// ── Per-dot VRAM write tracking ───────────────────────────────────────
	// begin_scanline() snapshots the 2 KB NT before the CPU runs.
	// cpu_write($2007) appends entries to nt_write_log[] when inside a visible
	// scanline. render_scanline() replays them at tile boundaries so the
	// rendered output matches FCEUX's per-dot rendering.
	unsigned char nt_snapshot[0x800];          // NT state before this scanline's CPU run
	static const int kMaxNtWrites = 64;        // max logged writes per scanline
	NtWrite        nt_write_log[kMaxNtWrites]; // writes during cur_scanline's CPU window
	int            nt_write_count;             // entries used in nt_write_log[]

	int          cur_scanline;        // visible scanline being executed; -1 if outside window
	unsigned int sl_cpu_cycle_base;   // cpu_ptr->cycle at the start of this scanline's CPU run

	cpu_6502*    cpu_ptr;             // set by set_cpu(); nullptr until then
};
