-- find_crash.lua: find exactly when L gets set to NULL
local OUTFILE = "C:\\Work\\nes1\\test\\mappy_out\\find_crash.txt"
local frame_count = 0

local f0 = io.open(OUTFILE, "w")
if f0 then f0:write("script loaded\n"); f0:close() end

local function safe_log(msg)
    local f = io.open(OUTFILE, "a")
    if f then f:write(msg .. "\n"); f:close() end
end

emu.speedmode("turbo")

-- Register a $2007 hook using pcall to catch errors
memory.registerwrite(0x2007, function(addr, val)
    -- intentionally minimal - just validate emu.getregisters works
    local ok, r = pcall(emu.getregisters)
    if not ok then
        safe_log("ERROR in $2007 hook: " .. tostring(r))
    end
end)

emu.registerbefore(function()
    frame_count = frame_count + 1
    safe_log("BEFORE frame=" .. frame_count)
    if frame_count >= 6 then
        emu.exit()
    end
end)

emu.registerafter(function()
    safe_log("AFTER frame=" .. frame_count)
end)
