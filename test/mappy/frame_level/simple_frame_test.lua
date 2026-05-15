-- simple_frame_test.lua: just count frames and write at frame 5
emu.speedmode("turbo")
local count = 0
local OUTFILE = "C:\\Work\\nes1\\test\\mappy_out\\frame_test.txt"
emu.registerbefore(function()
    count = count + 1
    if count == 5 then
        local ok, err = pcall(function()
            local f = io.open(OUTFILE, "w")
            if f then
                f:write("frame 5 reached\n")
                f:close()
            else
                -- Try writing to a simpler path
                local f2 = io.open("C:\\frame_test.txt", "w")
                if f2 then f2:write("fallback path\n"); f2:close() end
            end
        end)
        if not ok then
            local f3 = io.open("C:\\err.txt", "w")
            if f3 then f3:write(tostring(err)); f3:close() end
        end
        emu.exit()
    end
end)
