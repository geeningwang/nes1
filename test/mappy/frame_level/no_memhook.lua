-- no_memhook.lua: frame counter only, no memory hooks
local OUTFILE = "C:\\Work\\nes1\\test\\mappy_out\\no_memhook.txt"
local TARGET_FRAME = 4
local frame_count = 0

local f0 = io.open(OUTFILE, "w")
if f0 then f0:write("script loaded\n"); f0:close() end

emu.speedmode("turbo")

emu.registerbefore(function()
    frame_count = frame_count + 1
    local f = io.open(OUTFILE, "a")
    if f then f:write("frame=" .. frame_count .. "\n"); f:close() end
    if frame_count == TARGET_FRAME + 1 then
        emu.exit()
    end
end)
