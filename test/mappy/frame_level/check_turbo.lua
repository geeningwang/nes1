-- check_turbo.lua: test if emu.speedmode("turbo") causes crash
local f = io.open("C:\\Work\\nes1\\test\\mappy_out\\turbo_test.txt", "w")
if f then f:write("before speedmode\n"); f:close() end
emu.speedmode("turbo")
local f2 = io.open("C:\\Work\\nes1\\test\\mappy_out\\turbo_test.txt", "a")
if f2 then f2:write("after speedmode\n"); f2:close() end
emu.registerbefore(function()
    local f3 = io.open("C:\\Work\\nes1\\test\\mappy_out\\turbo_test.txt", "a")
    if f3 then f3:write("frame callback\n"); f3:close() end
    emu.exit()
end)
