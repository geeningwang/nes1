-- regbefore_test.lua: test emu.registerbefore callback
local OUTFILE = "C:\\Work\\nes1\\test\\mappy_out\\regbefore_out.txt"
local framecount = 0

emu.speedmode("turbo")

-- Write immediately at top level
local f = io.open(OUTFILE, "w")
if f then
    f:write("top_level_ok\n")
    f:write("framecount_at_load=" .. tostring(emu.framecount()) .. "\n")
    f:close()
end

emu.registerbefore(function()
    framecount = framecount + 1
    local f2 = io.open(OUTFILE, "a")
    if f2 then
        f2:write("before_frame=" .. framecount .. " emu.framecount=" .. tostring(emu.framecount()) .. "\n")
        f2:close()
    end
    if framecount >= 3 then
        emu.exit()
    end
end)
