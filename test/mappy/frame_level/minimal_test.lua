-- minimal_test.lua: bare minimum to test if script runs at all
local f = io.open("C:\\Work\\nes1\\test\\mappy_out\\minimal.txt", "w")
if f then
    f:write("Script loaded at startup\n")
    f:close()
end
