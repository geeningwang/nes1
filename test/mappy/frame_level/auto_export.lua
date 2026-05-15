-- Auto-run Mappy for 270 frames, then exit.
-- Frame/scanline trace export happens automatically in fceu.cpp (C-side).
-- This script just needs to advance emulation far enough.

local NFRAMES = 270

emu.speedmode("nothrottle")

for frame = 1, NFRAMES do
    emu.frameadvance()
    if frame % 30 == 0 then
        print(string.format("[auto_export] frame %4d / %d", frame, NFRAMES))
    end
end

print(string.format("[auto_export] Done. %d frames.", NFRAMES))
emu.exit()
