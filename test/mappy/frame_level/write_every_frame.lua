-- write_every_frame.lua: append count to file every single frame
local count = 0
-- Top level write first
local f0 = io.open("C:\\test_every.txt", "w")
if f0 then f0:write("script loaded\n"); f0:close() end

emu.registerbefore(function()
    count = count + 1
    local f = io.open("C:\\test_every.txt", "a")
    if f then
        f:write("frame=" .. count .. "\n")
        f:close()
    end
end)
