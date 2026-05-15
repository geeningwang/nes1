-- mappy_fceux_dense.lua
-- Like mappy_fceux.lua but saves EVERY frame (INTERVAL=1) for frames 1-270,
-- so we can find the first frame where nes1 and FCEUX diverge.
--
-- Usage:
--   fceux64.exe -lua mappy_fceux_dense.lua "C:\Work\nes1\test\Mappy (Japan).nes"
--
-- Output: C:\Work\nes1\test\mappy_out\fceux_dense_out\fceux_frame_NNNN.bmp

local OUTDIR   = "C:\\Work\\nes1\\test\\mappy_out\\fceux_dense_out\\"
local NFRAMES  = 270
local INTERVAL = 1
local W, H     = 256, 240

os.execute('if not exist "' .. OUTDIR:sub(1,-2) .. '" mkdir "' .. OUTDIR:sub(1,-2) .. '"')

local function saveGdAsBMP(fname, gd)
    local rowbytes = W * 3
    local pixdata  = rowbytes * H
    local filesize = 54 + pixdata

    local f = io.open(fname, "wb")
    if not f then return false end

    local function w32(v)
        v = v % 0x100000000
        f:write(string.char(v % 256, math.floor(v / 256) % 256,
                            math.floor(v / 65536) % 256, math.floor(v / 16777216) % 256))
    end
    local function w16(v)
        f:write(string.char(v % 256, math.floor(v / 256) % 256))
    end

    f:write("BM"); w32(filesize); w32(0); w32(54)
    w32(40); w32(W); w32(H); w16(1); w16(24)
    w32(0); w32(pixdata); w32(0); w32(0); w32(0); w32(0)

    for bmprow = 0, H - 1 do
        local gdrow = H - 1 - bmprow
        local base  = 12 + gdrow * W * 4
        local rowbuf = {}
        for x = 0, W - 1 do
            local p = base + x * 4
            local r, g, b = gd:byte(p + 1, p + 3)
            rowbuf[x + 1] = string.char(b, g, r)
        end
        f:write(table.concat(rowbuf))
    end

    f:close()
    return true
end

emu.speedmode("nothrottle")

print(string.format("Running %d frames, saving every %d...", NFRAMES, INTERVAL))

for frame = 1, NFRAMES do
    emu.frameadvance()

    if frame % INTERVAL == 0 then
        local fname = OUTDIR .. string.format("fceux_frame_%04d.bmp", frame)
        local ok = saveGdAsBMP(fname, gui.gdscreenshot(true))
        if frame % 30 == 0 then
            print(string.format("[dense] frame %4d  ok=%-5s", frame, tostring(ok)))
        end
    end
end

print(string.format("[dense] Done. %d BMPs saved to %s", NFRAMES, OUTDIR))
emu.exit()
