-- count_with_turbo.lua: count to 10 WITH emu.speedmode("turbo"), no emu.exit
emu.speedmode("turbo")
local count = 0
local done = false
local f0 = io.open("C:\\Work\\nes1\\test\\mappy_out\\count_test.txt", "w")
if f0 then f0:write("top level ok\n"); f0:close() end
emu.registerbefore(function()
    count = count + 1
    if count == 10 and not done then
        done = true
        local f = io.open("C:\\Work\\nes1\\test\\mappy_out\\count_test.txt", "a")
        if f then f:write("reached 10 frames\n"); f:close() end
    end
end)
