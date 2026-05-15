-- ultra_minimal.lua: write to C:\test.txt at frame 3
local count = 0
emu.registerbefore(function()
    count = count + 1
    if count == 3 then
        local f = io.open("C:\\test_lua.txt", "w")
        if f then
            f:write("count=3\n")
            f:close()
        end
        -- also write error info regardless
        local f2 = io.open("C:\\test_lua2.txt", "w")
        if f2 then
            f2:write("count reached 3, f was: " .. tostring(f) .. "\n")
            f2:close()
        end
    end
end)
