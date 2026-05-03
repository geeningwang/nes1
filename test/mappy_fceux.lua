-- mappy_fceux.lua
-- FCEUX automated screenshot capture for Mappy (Japan).nes
-- Mirrors the approach used by autotest_fceux.lua (PPU color test).
-- Advances NFRAMES from reset with no input, saving a BMP every INTERVAL frames.
--
-- Usage (command line):
--   fceux64.exe -lua mappy_fceux.lua "C:\Work\nes1\test\Mappy (Japan).nes"
--
-- Output: C:\Work\nes1\test\mappy_out\fceux_out\fceux_frame_NNNN.bmp  (256x240 BMP)
--
-- gui.gdscreenshot() returns 256x240 raw ARGB: 11-byte GD header then W*H*4 bytes.
-- Each pixel: A (byte 0), R (byte 1), G (byte 2), B (byte 3) — 1-indexed in Lua.
-- We write a standard 24bpp bottom-up BMP without needing the gd library (unavailable
-- in FCEUX 2.6.6).

local OUTDIR   = "C:\\Work\\nes1\\test\\mappy_out\\fceux_out\\"
local NFRAMES  = 300
local INTERVAL = 30
local W, H     = 256, 240

-- Create output directory (safe if it already exists)
os.execute('if not exist "' .. OUTDIR:sub(1,-2) .. '" mkdir "' .. OUTDIR:sub(1,-2) .. '"')

-- Write a 24bpp bottom-up BMP from a raw FCEUX GD screenshot string.
-- gd layout: 11 bytes header, then W*H pixels, each = A R G B (4 bytes, top-down).
local function saveGdAsBMP(fname, gd)
    local rowbytes = W * 3    -- 768, already 4-byte aligned (768 % 4 == 0)
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

    -- BITMAPFILEHEADER (14 bytes)
    f:write("BM"); w32(filesize); w32(0); w32(54)
    -- BITMAPINFOHEADER (40 bytes)
    w32(40); w32(W); w32(H); w16(1); w16(24)
    w32(0); w32(pixdata); w32(0); w32(0); w32(0); w32(0)

    -- Pixel data: BMP rows are bottom-up; GD rows are top-down.
    -- GD pixel offset for (x, gdrow): 12 + (gdrow*W + x)*4  (1-indexed)
    -- BMP row 0 = GD row H-1; BMP row H-1 = GD row 0.
    for bmprow = 0, H - 1 do
        local gdrow = H - 1 - bmprow
        local base  = 12 + gdrow * W * 4    -- offset of first pixel A on this GD row
        local rowbuf = {}
        for x = 0, W - 1 do
            local p = base + x * 4           -- A at p, R at p+1, G at p+2, B at p+3
            local r, g, b = gd:byte(p + 1, p + 3)   -- Lua :byte() is 1-indexed
            rowbuf[x + 1] = string.char(b, g, r)     -- BMP stores BGR
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
        print(string.format("[mappy_fceux] frame %4d  ok=%-5s -> %s",
                            frame, tostring(ok), fname))
    end
end

local n = math.floor(NFRAMES / INTERVAL)
print(string.format("[mappy_fceux] Done. %d BMPs saved to %s", n, OUTDIR))
emu.exit()
