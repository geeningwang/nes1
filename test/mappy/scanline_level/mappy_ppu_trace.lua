-- mappy_ppu_trace.lua
-- Trace all $2000/$2005/$2006/$4014 writes near frame 270 to see how
-- Mappy sets up its scroll.  Exits after frame 280.

local TRACE_START = 265
local TRACE_END   = 275
local frame = 0

local w_latch = 0   -- track write latch for $2006
local t_reg   = 0   -- mirror t register
local last_2006_hi = nil

local log = {}

local function logline(s)
    log[#log + 1] = s
end

memory.registerwrite(0x2000, function(addr, val)
    if frame >= TRACE_START and frame <= TRACE_END then
        logline(string.format("F%03d  $2000 = %02X  (NMI:%d NT:%d)", frame, val,
            (val>>7)&1, val&3))
        -- $2000 resets w on some implementations; NES does NOT reset w here
        t_reg = (t_reg & 0xF3FF) | ((val & 0x03) << 10)
    end
end)

memory.registerwrite(0x2005, function(addr, val)
    if frame >= TRACE_START and frame <= TRACE_END then
        if w_latch == 0 then
            logline(string.format("F%03d  $2005[X] = %02X  coarse_x=%d fine_x=%d", frame, val, val>>3, val&7))
        else
            logline(string.format("F%03d  $2005[Y] = %02X  coarse_y=%d fine_y=%d", frame, val, val>>3, val&7))
        end
        w_latch = 1 - w_latch
    end
end)

memory.registerwrite(0x2006, function(addr, val)
    if frame >= TRACE_START and frame <= TRACE_END then
        if w_latch == 0 then
            last_2006_hi = val
            logline(string.format("F%03d  $2006[HI] = %02X  (t hi-byte)", frame, val))
        else
            local lo = val
            local hi = last_2006_hi or 0
            local addr16 = (hi << 8) | lo
            logline(string.format("F%03d  $2006[LO] = %02X  -> v=$%04X  coarseX=%d coarseY=%d NT=%d fineY=%d",
                frame, lo, addr16,
                addr16 & 0x1F,
                (addr16 >> 5) & 0x1F,
                (addr16 >> 10) & 3,
                (addr16 >> 12) & 7))
        end
        w_latch = 1 - w_latch
    end
end)

memory.registerwrite(0x2002, function() end)  -- reading $2002 resets w_latch
-- Actually registerwrite won't fire on reads; we need registerread:
memory.registerread(0x2002, function()
    if frame >= TRACE_START and frame <= TRACE_END then
        logline(string.format("F%03d  $2002 READ (resets w_latch)", frame))
    end
    w_latch = 0
end)

memory.registerwrite(0x4014, function(addr, val)
    if frame >= TRACE_START and frame <= TRACE_END then
        logline(string.format("F%03d  $4014 OAM DMA page=%02X", frame, val))
    end
end)

emu.registerbefore(function()
    frame = frame + 1
    if frame == TRACE_START then
        logline(string.format("=== Tracing frames %d-%d ===", TRACE_START, TRACE_END))
    end
    if frame > TRACE_END then
        -- Write log to file
        local f = io.open("C:\\Work\\nes1\\test\\mappy_out\\ppu_trace.txt", "w")
        if f then
            for _, line in ipairs(log) do f:write(line .. "\n") end
            f:close()
        end
        print("PPU trace written to ppu_trace.txt")
        emu.exit()
    end
end)

emu.speedmode("nothrottle")
print("PPU trace running, targeting frames " .. TRACE_START .. "-" .. TRACE_END)
