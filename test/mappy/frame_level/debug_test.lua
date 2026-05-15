-- debug_test.lua: test using emu.frameadvance() style
local OUTFILE = "C:\\Work\\nes1\\test\\mappy_out\\fceux_debug.txt"

emu.speedmode("turbo")

-- Advance 3 frames
emu.frameadvance()
emu.frameadvance()
emu.frameadvance()

-- Test APIs
local ok1, sl = pcall(ppu.getcurrentline)
local ok2, loopy = pcall(ppu.getloopy)
local ok3, regs = pcall(emu.getregisters)

local f = io.open(OUTFILE, "w")
if f then
    f:write("frame=3\n")
    f:write("ppu.getcurrentline ok=" .. tostring(ok1) .. " val=" .. tostring(sl) .. "\n")
    f:write("ppu.getloopy ok=" .. tostring(ok2) .. "\n")
    if ok2 and type(loopy) == "table" then
        f:write("  v=" .. tostring(loopy.v) .. " t=" .. tostring(loopy.t) .. " finex=" .. tostring(loopy.finex) .. " w=" .. tostring(loopy.w) .. "\n")
    end
    f:write("emu.getregisters ok=" .. tostring(ok3) .. "\n")
    if ok3 and type(regs) == "table" then
        f:write("  pc=" .. tostring(regs.pc) .. " a=" .. tostring(regs.a) .. "\n")
    end
    f:close()
end
