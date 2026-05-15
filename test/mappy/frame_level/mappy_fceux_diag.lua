-- mappy_fceux_diag.lua
-- Diagnostic: find working screenshot method in FCEUX 2.6.x
-- Run: fceux64.exe "Mappy (Japan).nes" -loadlua mappy_fceux_diag.lua

local LOGFILE = "C:\\Work\\nes1\\test\\mappy_out\\diag.txt"
local OUTDIR  = "C:\\Work\\nes1\\test\\mappy_out\\fceux_out\\"

local function log(msg)
    local f = io.open(LOGFILE, "a")
    if f then f:write(msg .. "\n"); f:close() end
    print(msg)
end

-- Clear log
local f = io.open(LOGFILE, "w"); if f then f:write("FCEUX Lua diagnostic\n"); f:close() end

-- Advance a few frames first so the screen has content
emu.speedmode("nothrottle")
for i = 1, 30 do emu.frameadvance() end
log("Frames advanced: 30")

-- Test 1: gui.gdscreenshot() — does it return userdata or string?
local ok, s = pcall(gui.gdscreenshot)
if ok then
    log("gui.gdscreenshot() type: " .. type(s))
    if type(s) == "userdata" then
        -- Try calling :png() directly on it
        local ok2, err2 = pcall(function() s:png(OUTDIR .. "diag_direct") end)
        log("direct :png() -> ok=" .. tostring(ok2) .. " err=" .. tostring(err2))
    elseif type(s) == "string" then
        log("gdscreenshot string len: " .. #s)
        -- Try gd.createfromgd
        if gd then
            local ok3, img = pcall(gd.createFromGdStr, s)
            log("gd.createfromgd() -> ok=" .. tostring(ok3) .. " type=" .. type(img))
            if ok3 and img then
                local ok4, err4 = pcall(function() img:png(OUTDIR .. "diag_fromgd") end)
                log("fromgd :png() -> ok=" .. tostring(ok4) .. " err=" .. tostring(err4))
            end
        else
            log("gd library NOT available")
        end
    end
else
    log("gui.gdscreenshot() FAILED: " .. tostring(s))
end

-- Test 2: emu.screenshot
if emu.screenshot then
    local ok5, err5 = pcall(emu.screenshot, OUTDIR .. "diag_emuscreenshot.png")
    log("emu.screenshot() -> ok=" .. tostring(ok5) .. " err=" .. tostring(err5))
else
    log("emu.screenshot not available")
end

log("Diagnostic complete")
emu.exit()
