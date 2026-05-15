-- count_frames.lua: count 10 frames then write, NO emu.exit()
local count = 0
local done = false
emu.registerbefore(function()
    count = count + 1
    if count == 10 and not done then
        done = true
        local f = io.open("C:\\Work\\nes1\\test\\mappy_out\\count_test.txt", "w")
        if f then f:write("reached 10 frames\n"); f:close() end
    end
end)
