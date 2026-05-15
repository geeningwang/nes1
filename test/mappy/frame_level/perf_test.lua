-- perf_test.lua: measure time for emu.getregisters + ppu.getcurrentline calls
local OUTFILE = "C:\\Work\\nes1\\test\\mappy_out\\perf_test.txt"
local TARGET_FRAME = 4
local frame_count = 0
local recording = false
local call_count = 0

local f0 = io.open(OUTFILE, "w")
if f0 then f0:write("start\n"); f0:close() end

local function safe_log(msg)
    local f = io.open(OUTFILE, "a")
    if f then f:write(msg .. "\n"); f:close() end
end

emu.speedmode("turbo")

memory.registerwrite(0x2007, function(addr, val)
    if not recording then return end
    call_count = call_count + 1
    local ok1, r1 = pcall(emu.getregisters)
    local ok2, r2 = pcall(ppu.getcurrentline)
    if not ok1 then safe_log("getregisters failed: " .. tostring(r1)) end
    if not ok2 then safe_log("getcurrentline failed: " .. tostring(r2)) end
    if call_count <= 3 or call_count % 100 == 0 then
        safe_log("call_count=" .. call_count .. " ok1=" .. tostring(ok1) .. " ok2=" .. tostring(ok2))
    end
end)

emu.registerbefore(function()
    frame_count = frame_count + 1
    safe_log("before frame=" .. frame_count .. " count=" .. call_count)
    if frame_count == TARGET_FRAME then
        recording = true
    end
    if frame_count == TARGET_FRAME + 1 then
        safe_log("DONE! Total calls: " .. call_count)
        emu.exit()
    end
end)
