-- Minimal test of emu.registerafter
local function my_callback()
    local f = io.open("C:/Work/nes1/test/mappy_out/test_callback.txt", "a")
    if f then f:write("CALLBACK FIRED!\n") f:close() end
end

local f0 = io.open("C:/Work/nes1/test/mappy_out/test_start.txt", "w")
if f0 then f0:write("start\n") f0:close() end

emu.frameadvance()

local f1 = io.open("C:/Work/nes1/test/mappy_out/test_after_advance1.txt", "w")
if f1 then f1:write("after advance 1\n") f1:close() end

emu.registerafter(my_callback)
emu.frameadvance()

local f2 = io.open("C:/Work/nes1/test/mappy_out/test_after_advance2.txt", "w")
if f2 then f2:write("after advance 2\n") f2:close() end

emu.registerafter(nil)
emu.exit()
