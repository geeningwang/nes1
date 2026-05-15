-- mappy_f1_trace.lua
-- Capture a detailed per-scanline / per-write PPU trace for Frame 1 of
-- Mappy (Japan) running inside FCEUX 2.6.x, producing output that matches
-- the two-part format written by nes1's export_scanline_trace().
--
-- Uses ppu.getscanlinedata(sl) for authoritative CPU+PPU state per scanline.
-- NT write hooks are used only for PART2 and NTw count.
--
-- Output file: C:\Work\nes1\test\mappy_out\fceux_sl_f1.txt

local OUTFILE = "C:\\Work\\nes1\\test\\mappy_out\\fceux_sl_f1.txt"
local TARGET_FRAME = 1
local band, bor, lshift, rshift = bit.band, bit.bor, bit.lshift, bit.rshift

emu.speedmode("nothrottle")

-- -----------------------------------------------------------------------
-- NT write log (PART2) and per-scanline NT write count
-- -----------------------------------------------------------------------
local recording = false
local write_log = {}      -- array of { seq, sl, v_bef, v_aft, ntaddr, val, pc }
local sl_nt_writes = {}   -- count of NT writes per scanline
for i = 0, 239 do sl_nt_writes[i] = 0 end

-- Snapshot table: sl_data[sl] = table with all fields from ppu.getscanlinedata
local sl_data = {}

-- Loopy V/T state (tracked for PART2 v_bef/v_aft)
local v_reg    = 0
local t_reg    = 0
local fine_x   = 0
local w_latch  = 0
local ctrl_reg = 0

local function get_scanline()
    local ok, sl = pcall(function() return ppu.getcurrentline() end)
    if ok and type(sl) == "number" then return sl end
    return -1
end

local function record_write(reg_addr, val, v_bef, v_aft, ntaddr)
    if not recording then return end
    local regs = emu.getregisters()
    local sl   = get_scanline()
    if sl >= 0 and sl <= 239 then
        if ntaddr ~= nil and ntaddr >= 0x2000 and ntaddr <= 0x3EFF then
            sl_nt_writes[sl] = sl_nt_writes[sl] + 1
        end
    end
    local seq = #write_log + 1
    write_log[seq] = {
        seq    = seq,
        sl     = sl,
        v_bef  = v_bef,
        v_aft  = v_aft,
        ntaddr = ntaddr,
        val    = val,
        pc     = regs.pc or 0,
    }
end

-- PPU register write hooks
memory.registerwrite(0x2000, function(addr, val)
    ctrl_reg = val
    t_reg = bor(band(t_reg, 0xF3FF), lshift(band(val, 0x03), 10))
end)
memory.registerwrite(0x2001, function(addr, val) end)
memory.registerread(0x2002,  function()         w_latch = 0 end)
memory.registerwrite(0x2005, function(addr, val)
    if w_latch == 0 then
        t_reg   = bor(band(t_reg, 0xFFE0), rshift(val, 3))
        fine_x  = band(val, 7)
        w_latch = 1
    else
        t_reg   = bor(band(t_reg, 0x8C1F), bor(lshift(band(val, 7), 12), lshift(rshift(val, 3), 5)))
        w_latch = 0
    end
end)
memory.registerwrite(0x2006, function(addr, val)
    local v_bef = v_reg
    if w_latch == 0 then
        t_reg   = bor(band(t_reg, 0x00FF), lshift(band(val, 0x3F), 8))
        w_latch = 1
    else
        t_reg   = bor(band(t_reg, 0xFF00), val)
        v_reg   = t_reg
        w_latch = 0
        record_write(0x2006, val, v_bef, v_reg, nil)
    end
end)
memory.registerwrite(0x2007, function(addr, val)
    local v_bef    = v_reg
    local ppu_addr = band(v_reg, 0x3FFF)
    local ntaddr   = nil
    if ppu_addr >= 0x2000 and ppu_addr <= 0x3EFF then ntaddr = ppu_addr end
    v_reg = band(v_reg + (band(ctrl_reg, 0x04) ~= 0 and 32 or 1), 0x7FFF)
    record_write(0x2007, val, v_bef, v_reg, ntaddr)
end)

-- -----------------------------------------------------------------------
-- Capture trace data (called via emu.registerafter during the target frame)
-- -----------------------------------------------------------------------
local function capture_trace_callback()
    for sl = 0, 239 do
        local d = ppu.getscanlinedata(sl)
        if d then
            local snap = {
                v_start  = d.v_start  or 0,
                v        = d.v        or 0,
                t        = d.t        or 0,
                fine_x   = d.fine_x   or 0,
                w        = d.w        or 0,
                ppuctrl  = d.ppuctrl  or 0,
                ppumask  = d.ppumask  or 0,
                cycles   = d.cycles   or 0,
                pc       = d.pc       or 0,
                a        = d.a        or 0,
                x        = d.x        or 0,
                y        = d.y        or 0,
                s        = d.s        or 0,
                p        = d.p        or 0,
                palette  = {},
            }
            if d.palette then
                for i = 1, 32 do snap.palette[i] = d.palette[i] or 0 end
            else
                for i = 1, 32 do snap.palette[i] = 0 end
            end
            sl_data[sl] = snap
        end
    end
end

-- -----------------------------------------------------------------------
-- Output
-- -----------------------------------------------------------------------
local function write_output()
    local f = io.open(OUTFILE, "w")
    if not f then
        print("[mappy_f1_trace] ERROR: cannot open " .. OUTFILE)
        return
    end

    -- ----------------------------------------------------------------
    -- PART 1 - per-scanline summary using authoritative ppu.getscanlinedata
    -- ----------------------------------------------------------------
    f:write("=== PART1 ===\n")
    f:write(string.format(
        "# %-3s  %-5s %-5s %-5s %2s %1s  %2s  %2s  %3s %6s %4s  %3s  %-8s  %-4s %-2s %-2s %-2s %-2s %-2s  PAL\n",
        "SL", "V_S", "V_E", "T_E", "FX", "W",
        "CTL", "MSK", "SSX", "SS_Y", "SSNT", "NTw",
        "CYC", "PC", "A", "X", "Y", "S", "P"))

    for sl = 0, 239 do
        local d = sl_data[sl] or {}
        local pal64 = ""
        if d.palette then
            for i = 1, 32 do
                pal64 = pal64 .. string.format("%02X", d.palette[i] or 0)
            end
        else
            pal64 = string.rep("00", 32)
        end

        f:write(string.format(
            "  %3d  %04X  %04X  %04X  %d  %d  %02X  %02X  %3d %6d    %d  %3d  %8u  %04X %02X %02X %02X %02X %02X  %s\n",
            sl,
            band((d.v_start or 0), 0x7FFF), band((d.v or 0), 0x7FFF), band((d.t or 0), 0x7FFF),
            d.fine_x or 0, d.w or 0,
            d.ppuctrl or 0, d.ppumask or 0,
            0, 0, 0,
            sl_nt_writes[sl],
            d.cycles or 0,
            d.pc or 0, d.a or 0, d.x or 0, d.y or 0, d.s or 0, d.p or 0,
            pal64))
    end

    -- ----------------------------------------------------------------
    -- PART 2 - per-write log
    -- ----------------------------------------------------------------
    f:write("\n=== PART2 ===\n")
    f:write(string.format(
        "# %-5s %-3s %-5s %-5s %-5s %-5s %-3s  PC\n",
        "SEQ", "SL", "DOT", "V_BEF", "V_AFT", "NTADDR", "VAL"))

    for i, w in ipairs(write_log) do
        local ntstr = w.ntaddr and string.format("%04X", w.ntaddr) or "----"
        f:write(string.format(
            "  %5d  %3d  %4d  %04X  %04X  %s  %02X  %04X\n",
            w.seq, w.sl, 0,
            w.v_bef, w.v_aft, ntstr, w.val, w.pc))
    end

    f:close()
    print(string.format(
        "[mappy_f1_trace] Done: %d writes logged -> %s",
        #write_log, OUTFILE))
end

-- -----------------------------------------------------------------------
-- Main: advance TARGET_FRAME - 1 times to skip preceding frames, then
-- register an after-emulation callback to snapshot the trace (fires after
-- the PPU loop, while g_sl_trace still holds that frame's data), do one
-- recording advance, then write output.
-- -----------------------------------------------------------------------
for frame = 1, TARGET_FRAME - 1 do
    emu.frameadvance()
end

recording = true
emu.registerafter(capture_trace_callback)
emu.frameadvance()
recording = false
emu.registerafter(nil)

write_output()
emu.exit()

-- -----------------------------------------------------------------------
-- NT write log (PART2) and per-scanline NT write count
-- -----------------------------------------------------------------------
local recording = false
local write_log = {}      -- array of { seq, sl, v_bef, v_aft, ntaddr, val, pc }
local sl_nt_writes = {}   -- count of NT writes per scanline
for i = 0, 239 do sl_nt_writes[i] = 0 end

-- Loopy V/T state (tracked for PART2 v_bef/v_aft)
local v_reg    = 0
local t_reg    = 0
local fine_x   = 0
local w_latch  = 0
local ctrl_reg = 0

local function get_scanline()
    local ok, sl = pcall(function() return ppu.getcurrentline() end)
    if ok and type(sl) == "number" then return sl end
    return -1
end

local function record_write(reg_addr, val, v_bef, v_aft, ntaddr)
    if not recording then return end
    local regs = emu.getregisters()
    local sl   = get_scanline()
    if sl >= 0 and sl <= 239 then
        if ntaddr ~= nil and ntaddr >= 0x2000 and ntaddr <= 0x3EFF then
            sl_nt_writes[sl] = sl_nt_writes[sl] + 1
        end
    end
    local seq = #write_log + 1
    write_log[seq] = {
        seq    = seq,
        sl     = sl,
        v_bef  = v_bef,
        v_aft  = v_aft,
        ntaddr = ntaddr,
        val    = val,
        pc     = regs.pc or 0,
    }
end

-- PPU register write hooks
memory.registerwrite(0x2000, function(addr, val)
    ctrl_reg = val
    t_reg = bor(band(t_reg, 0xF3FF), lshift(band(val, 0x03), 10))
end)
memory.registerwrite(0x2001, function(addr, val) end)
memory.registerread(0x2002,  function()         w_latch = 0 end)
memory.registerwrite(0x2005, function(addr, val)
    if w_latch == 0 then
        t_reg   = bor(band(t_reg, 0xFFE0), rshift(val, 3))
        fine_x  = band(val, 7)
        w_latch = 1
    else
        t_reg   = bor(band(t_reg, 0x8C1F), bor(lshift(band(val, 7), 12), lshift(rshift(val, 3), 5)))
        w_latch = 0
    end
end)
memory.registerwrite(0x2006, function(addr, val)
    local v_bef = v_reg
    if w_latch == 0 then
        t_reg   = bor(band(t_reg, 0x00FF), lshift(band(val, 0x3F), 8))
        w_latch = 1
    else
        t_reg   = bor(band(t_reg, 0xFF00), val)
        v_reg   = t_reg
        w_latch = 0
        record_write(0x2006, val, v_bef, v_reg, nil)
    end
end)
memory.registerwrite(0x2007, function(addr, val)
    local v_bef    = v_reg
    local ppu_addr = band(v_reg, 0x3FFF)
    local ntaddr   = nil
    if ppu_addr >= 0x2000 and ppu_addr <= 0x3EFF then ntaddr = ppu_addr end
    v_reg = band(v_reg + (band(ctrl_reg, 0x04) ~= 0 and 32 or 1), 0x7FFF)
    record_write(0x2007, val, v_bef, v_reg, ntaddr)
end)

-- -----------------------------------------------------------------------
-- Output
-- -----------------------------------------------------------------------
local function write_output()
    local f = io.open(OUTFILE, "w")
    if not f then
        print("[mappy_f1_trace] ERROR: cannot open " .. OUTFILE)
        return
    end

    -- ----------------------------------------------------------------
    -- PART 1 - per-scanline summary using authoritative ppu.getscanlinedata
    -- ----------------------------------------------------------------
    f:write("=== PART1 ===\n")
    f:write(string.format(
        "# %-3s  %-5s %-5s %-5s %2s %1s  %2s  %2s  %3s %6s %4s  %3s  %-8s  %-4s %-2s %-2s %-2s %-2s %-2s  PAL\n",
        "SL", "V_S", "V_E", "T_E", "FX", "W",
        "CTL", "MSK", "SSX", "SS_Y", "SSNT", "NTw",
        "CYC", "PC", "A", "X", "Y", "S", "P"))

    for sl = 0, 239 do
        local d = ppu.getscanlinedata(sl)
        local pal64 = ""
        if d and d.palette then
            for i = 1, 32 do
                pal64 = pal64 .. string.format("%02X", d.palette[i] or 0)
            end
        else
            pal64 = string.rep("00", 32)
        end

        local v_s = d and d.v_start or 0
        local v_e = d and d.v       or 0
        local t_e = d and d.t       or 0
        local fx  = d and d.fine_x  or 0
        local w   = d and d.w       or 0
        local ctl = d and d.ppuctrl or 0
        local msk = d and d.ppumask or 0
        local cyc = d and d.cycles  or 0
        local pc  = d and d.pc      or 0
        local a   = d and d.a       or 0
        local x   = d and d.x       or 0
        local y   = d and d.y       or 0
        local s   = d and d.s       or 0
        local p   = d and d.p       or 0

        f:write(string.format(
            "  %3d  %04X  %04X  %04X  %d  %d  %02X  %02X  %3d %6d    %d  %3d  %8u  %04X %02X %02X %02X %02X %02X  %s\n",
            sl,
            band(v_s, 0x7FFF), band(v_e, 0x7FFF), band(t_e, 0x7FFF),
            fx, w,
            ctl, msk,
            0, 0, 0,
            sl_nt_writes[sl],
            cyc,
            pc, a, x, y, s, p,
            pal64))
    end

    -- ----------------------------------------------------------------
    -- PART 2 - per-write log
    -- ----------------------------------------------------------------
    f:write("\n=== PART2 ===\n")
    f:write(string.format(
        "# %-5s %-3s %-5s %-5s %-5s %-5s %-3s  PC\n",
        "SEQ", "SL", "DOT", "V_BEF", "V_AFT", "NTADDR", "VAL"))

    for i, w in ipairs(write_log) do
        local ntstr = w.ntaddr and string.format("%04X", w.ntaddr) or "----"
        f:write(string.format(
            "  %5d  %3d  %4d  %04X  %04X  %s  %02X  %04X\n",
            w.seq, w.sl, 0,
            w.v_bef, w.v_aft, ntstr, w.val, w.pc))
    end

    f:close()
    print(string.format(
        "[mappy_f1_trace] Done: %d writes logged -> %s",
        #write_log, OUTFILE))
end

-- -----------------------------------------------------------------------
-- Main: advance to TARGET_FRAME, record, write output, exit
-- -----------------------------------------------------------------------
for frame = 1, TARGET_FRAME do
    emu.frameadvance()
end

recording = true
emu.frameadvance()
recording = false

write_output()
emu.exit()