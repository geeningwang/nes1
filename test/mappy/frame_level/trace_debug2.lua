-- trace_debug2.lua: check why frame 5 callback never fires after recording frame 4
local OUTFILE = "C:\\Work\\nes1\\test\\mappy_out\\trace_debug2.txt"
local TARGET_FRAME = 4
local frame_count = 0

local function safe_log(msg)
    local f = io.open(OUTFILE, "a")
    if f then f:write(msg .. "\n"); f:close() end
end

local f0 = io.open(OUTFILE, "w")
if f0 then f0:write("script loaded\n"); f0:close() end

emu.speedmode("turbo")
safe_log("speedmode set")

-- Register a $2007 hook that processes many writes
local write_count = 0
memory.registerwrite(0x2007, function(addr, val)
    if frame_count == TARGET_FRAME then
        write_count = write_count + 1
        if write_count <= 5 or write_count % 100 == 0 then
            safe_log("write #" .. write_count .. " val=" .. string.format("%02X", val))
        end
    end
end)

emu.registerbefore(function()
    frame_count = frame_count + 1
    safe_log("frame=" .. frame_count .. " writes_so_far=" .. write_count)
    
    if frame_count == TARGET_FRAME + 1 then
        safe_log("DONE! Total writes in frame " .. TARGET_FRAME .. ": " .. write_count)
        emu.exit()
    end
    
    if frame_count > TARGET_FRAME + 2 then
        safe_log("STOPPING (exceeded target)")
        emu.exit()
    end
end)
safe_log("setup done")
