-- trace_debug.lua: mini version of mappy_f4_trace with aggressive error catching
local OUTFILE = "C:\\Work\\nes1\\test\\mappy_out\\trace_debug.txt"
local TARGET_FRAME = 4
local frame_count = 0
local recording = false
local write_log = {}
local err_msgs = {}

local f0 = io.open(OUTFILE, "w")
if f0 then f0:write("script loaded\n"); f0:close() end

local function safe_log(msg)
    local f = io.open(OUTFILE, "a")
    if f then f:write(msg .. "\n"); f:close() end
end

emu.speedmode("turbo")

safe_log("after speedmode")

-- Register memory write hooks with error catching
local ok1, err1 = pcall(function()
    memory.registerwrite(0x2000, function(addr, val)
        if recording then
            -- just count
        end
    end)
end)
safe_log("2000 hook ok=" .. tostring(ok1) .. " " .. tostring(err1))

local ok2, err2 = pcall(function()
    memory.registerwrite(0x2007, function(addr, val)
        if recording then
            local seq = #write_log + 1
            write_log[seq] = { seq=seq, val=val }
        end
    end)
end)
safe_log("2007 hook ok=" .. tostring(ok2) .. " " .. tostring(err2))

emu.registerbefore(function()
    frame_count = frame_count + 1
    
    if frame_count == TARGET_FRAME then
        recording = true
        write_log = {}
        safe_log("RECORDING frame " .. frame_count)
    end
    
    if frame_count == TARGET_FRAME + 1 then
        recording = false
        safe_log("END recording, writes=" .. #write_log)
        emu.exit()
    end
end)

safe_log("setup complete")
