-- autotest_fceux.lua
-- NES1 PPU Color Test - FCEUX Reference Data Collector
--
-- For each of 1,024 (64 colors × 16 PPUMASK) combinations:
--   1. Writes ppu_emphasis ($0000) and color ($0001) directly to CPU RAM
--   2. Advances 2 frames  (frame 1: NMI picks up ZP values and writes to PPU
--                          frame 2: PPU renders with stable state)
--   3. Samples pixel (4,4) — top-left corner, pure background (sprites start at y=24)
--   4. Logs: color_hex, emph_hex, r, g, b  to CSV
--
-- Requirements:
--   FCEUX 2.4+  with  color_test.nes  loaded before running this script.
--
-- Usage (GUI):
--   FCEUX > Script > Run Lua Script > select this file
--
-- Usage (command line, if your FCEUX build supports it):
--   fceux.exe --loadlua autotest_fceux.lua color_test.nes
--
-- Output:
--   <FCEUX working dir>\fceux_output\fceux_log.csv

-- ── Output directory ─────────────────────────────────────────────────────────
local OUTDIR  = "fceux_output"
local LOGNAME = OUTDIR .. "\\fceux_log.csv"

os.execute("mkdir \"" .. OUTDIR .. "\" 2>nul")

local logfile = io.open(LOGNAME, "w")
if not logfile then
    print("ERROR: Cannot create log file: " .. LOGNAME)
    return
end
logfile:write("color_hex,emph_hex,r,g,b\n")

-- ── ZP addresses (from nrom.cfg + color_test.s segment order) ────────────────
--   ZEROPAGE segment starts at $00:
--     $00  ppu_emphasis
--     $01  color
--     $02  temp
--     $03  gamepad
--     $04  gamepad_last
local ZP_PPU_EMPHASIS = 0x0000
local ZP_COLOR        = 0x0001
local ZP_GAMEPAD      = 0x0003
local ZP_GAMEPAD_LAST = 0x0004

-- ── 16 PPUMASK values (8 emphasis combos × 2 greyscale states) ───────────────
-- Bits 7/6/5 = blue/green/red emphasis; bit 0 = greyscale.
-- Bits 4:1 = $F (BG on, SPR on, show left 8px columns) kept constant.
local EMPH_VALUES = {
    0x1E, 0x3E, 0x5E, 0x7E, 0x9E, 0xBE, 0xDE, 0xFE,   -- no greyscale
    0x1F, 0x3F, 0x5F, 0x7F, 0x9F, 0xBF, 0xDF, 0xFF,   -- greyscale on
}

-- ── ROM initialisation — skip first 10 frames ────────────────────────────────
-- The reset sequence in color_test.s needs 2 vblank periods (~2 frames minimum).
-- We wait 10 frames to be safe and let the palette + OAM be set up fully.
print("Waiting for ROM initialisation (10 frames)...")
for i = 1, 10 do
    emu.frameadvance()
end
print("ROM ready. Starting 1024-case sweep...")

-- ── Main test loop ────────────────────────────────────────────────────────────
local total = 0

for _, emph in ipairs(EMPH_VALUES) do
    for color = 0x00, 0x3F do

        -- Inject ZP state directly (bypasses gamepad debounce & input processing)
        memory.writebyte(ZP_PPU_EMPHASIS, emph)
        memory.writebyte(ZP_COLOR,        color)
        memory.writebyte(ZP_GAMEPAD,      0x00)   -- no buttons held
        memory.writebyte(ZP_GAMEPAD_LAST, 0x00)   -- allow NMI button gate to pass through

        -- Frame 1: NMI executes, reads ZP, writes:
        --   lda ppu_emphasis  →  sta $2001   (reg_mask = emph)
        --   lda color         →  sta $2007   (VRAM[0x3F00] = color)
        emu.frameadvance()

        -- Frame 2: another NMI for stability, then the rendered state is ready
        emu.frameadvance()

        -- Sample pixel at (4, 4):
        --   • x=4: leftmost visible column (no sprite starts here)
        --   • y=4: row 4 of 240, well above the sprite overlay (sprites at y≥24)
        --   This pixel is the universal background color after PPU rendering.
        local r, g, b = emu.getscreenpixel(4, 4)

        logfile:write(string.format("%02X,%02X,%d,%d,%d\n", color, emph, r, g, b))

        total = total + 1
        if total % 64 == 0 then
            print(string.format("  Progress: %d / 1024  (emph=%02X)", total, emph))
            logfile:flush()   -- flush periodically so data is not lost on crash
        end
    end
end

logfile:close()
print(string.format("Done!  %d test cases saved to: %s", total, LOGNAME))
print("Now run compare.py to generate the HTML comparison report.")
