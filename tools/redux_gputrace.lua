-- GPU call trace for the PC port's GPU layer (tools/redux_gputrace.py drives it).
--
-- Exec breakpoints on the game's LIBGPU entry points (US main addresses)
-- record each call with the data it hands the GPU, read from RAM at the call:
-- the GP0 words of an ordering table, a LoadImage's pixels, the drawing and
-- display environments. VSync(0) is a frame marker: with shots, the display
-- area is saved at each (shot_<n>.bin, listed in shots.txt); the first and
-- every GT.every-th marker pause the emulator so the driver (redux_gputrace.py) can
-- fetch VRAM.
--
-- Trace lines:
--   V n                         frame marker n (VSync(0) entered)
--   O w...                      DrawOTag: the GP0 words of the whole list
--   E <drawenv> | w...          DrawOTagEnv
--   P w...                      DrawPrim
--   L x y w h w...              LoadImage (pixels as words)
--   M x y w h dx dy             MoveImage
--   C x y w h r g b             ClearImage
--   D cx cy cw ch ox oy tx ty tw th tpage dtd dfe isbg r g b   PutDrawEnv
--   S dx dy dw dh sx sy sw sh isinter isrgb24                 PutDispEnv
--   K mask                      SetDispMask
--   R mode                      ResetGraph

local ffi = require('ffi')

GT = GT or {}
GT.bps = GT.bps or {}

local ADDR = {
    DrawOTag = 0x80044BD0,
    DrawPrim = 0x80044B70,
    LoadImage = 0x80044894,
    MoveImage = 0x8004495C,
    ClearImage = 0x80044764,
    PutDrawEnv = 0x80044C44,
    PutDispEnv = 0x80044E9C,
    DrawOTagEnv = 0x80044D48,
    SetDispMask = 0x80044534,
    VSync = 0x8004B54C,
    ResetGraph = 0x80044110,
}

local function ptr(a)
    a = bit.band(a, 0x1FFFFFFF)
    if a >= 0x1F800000 and a < 0x1F800400 then
        return PCSX.getScratchPtr() + (a - 0x1F800000)
    end
    return PCSX.getMemPtr() + bit.band(a, 0x1FFFFF)
end
local function r32(a) return tonumber(ffi.cast('uint32_t*', ptr(a))[0]) end
local function r16s(a) return tonumber(ffi.cast('int16_t*', ptr(a))[0]) end
local function r16(a) return tonumber(ffi.cast('uint16_t*', ptr(a))[0]) end
local function r8(a) return tonumber(ffi.cast('uint8_t*', ptr(a))[0]) end
local function hex(v) return bit.tohex(v, 8) end

local function regs()
    local g = PCSX.getRegisters().GPR.n
    return tonumber(g.a0), tonumber(g.a1), tonumber(g.a2), tonumber(g.a3)
end

local function emit(line)
    GT.buf[#GT.buf + 1] = line
end

local function flush()
    if GT.file and #GT.buf > 0 then
        GT.file:write(table.concat(GT.buf, '\n'), '\n')
        GT.file:flush()
    end
    GT.buf = {}
end

local function words(a, n, out)
    for i = 0, n - 1 do out[#out + 1] = hex(r32(a + 4 * i)) end
end

local function walk(a)
    local out = {}
    for _ = 1, 400000 do
        local tag = r32(a)
        words(a + 4, bit.rshift(tag, 24), out)
        local nxt = bit.band(tag, 0xFFFFFF)
        if bit.band(nxt, 0x800000) ~= 0 then break end
        a = nxt
    end
    return table.concat(out, ' ')
end

local function drawEnv(e)
    return string.format('%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d',
        r16s(e), r16s(e + 2), r16s(e + 4), r16s(e + 6), r16s(e + 8), r16s(e + 10),
        r16s(e + 12), r16s(e + 14), r16s(e + 16), r16s(e + 18), r16(e + 20),
        r8(e + 22), r8(e + 23), r8(e + 24), r8(e + 25), r8(e + 26), r8(e + 27))
end

local H = {}
function H.DrawOTag()
    local a0 = regs()
    emit('O ' .. walk(a0))
end
function H.DrawOTagEnv()
    local a0, a1 = regs()
    emit('E ' .. drawEnv(a1) .. ' | ' .. walk(a0))
end
function H.DrawPrim()
    local a0 = regs()
    local out = {}
    words(a0 + 4, bit.rshift(r32(a0), 24), out)
    emit('P ' .. table.concat(out, ' '))
end
function H.LoadImage()
    local a0, a1 = regs()
    local x, y, w, h = r16s(a0), r16s(a0 + 2), r16s(a0 + 4), r16s(a0 + 6)
    local cw = math.max(0, math.min(w, 1024))
    local ch = math.max(0, math.min(h, 512))
    local out = {}
    words(a1, math.floor((cw * ch + 1) / 2), out)
    emit(string.format('L %d %d %d %d %s', x, y, w, h, table.concat(out, ' ')))
end
function H.MoveImage()
    local a0, a1, a2 = regs()
    emit(string.format('M %d %d %d %d %d %d', r16s(a0), r16s(a0 + 2), r16s(a0 + 4), r16s(a0 + 6), a1, a2))
end
function H.ClearImage()
    local a0, a1, a2, a3 = regs()
    emit(string.format('C %d %d %d %d %d %d %d', r16s(a0), r16s(a0 + 2), r16s(a0 + 4), r16s(a0 + 6),
        bit.band(a1, 255), bit.band(a2, 255), bit.band(a3, 255)))
end
function H.PutDrawEnv()
    local a0 = regs()
    emit('D ' .. drawEnv(a0))
end
function H.PutDispEnv()
    local a0 = regs()
    emit(string.format('S %d %d %d %d %d %d %d %d %d %d', r16s(a0), r16s(a0 + 2), r16s(a0 + 4), r16s(a0 + 6),
        r16s(a0 + 8), r16s(a0 + 10), r16s(a0 + 12), r16s(a0 + 14), r8(a0 + 16), r8(a0 + 17)))
end
function H.SetDispMask()
    local a0 = regs()
    emit('K ' .. a0)
end
function H.ResetGraph()
    local a0 = regs()
    emit('R ' .. a0)
end
function H.VSync()
    local a0 = regs()
    if a0 ~= 0 then return end
    GT.marker = GT.marker + 1
    emit('V ' .. GT.marker)
    flush()
    if GT.shots then
        -- What the TV shows (the display area, raw 15- or 24-bit).
        local ss = PCSX.GPU.takeScreenShot()
        local name = string.format('shot_%05d.bin', GT.marker)
        local f = Support.File.open(GT.dir .. '/' .. name, 'TRUNCATE')
        f:writeMoveSlice(ss.data)
        f:close()
        GT.shots:write(string.format('%d %d %d %d\n', GT.marker, ss.width, ss.height, tonumber(ss.bpp) == 1 and 24 or 16))
        GT.shots:flush()
    end
    if GT.marker == 1 or GT.marker == GT.last or (GT.every > 0 and GT.marker % GT.every == 0) then
        GT.snap = GT.marker
        PCSX.pauseEmulator()
        -- Tell the driver through a file: polling the web server while the
        -- emulator runs can crash PCSX-Redux when a breakpoint pauses it.
        local f = io.open(GT.snapPath, 'w')
        f:write(tostring(GT.marker))
        f:close()
    end
end

function GT.start(path, every, shots, last)
    GT.stop()
    GT.dir = path:match('^(.*)/[^/]*$') or '.'
    if shots then
        GT.shots = io.open(GT.dir .. '/shots.txt', 'w')
    end
    GT.file = io.open(path, 'w')
    GT.snapPath = path .. '.snap'
    GT.buf = {}
    GT.marker = 0
    GT.snap = 0
    GT.every = every or 1
    GT.last = last or -1
    for name, addr in pairs(ADDR) do
        local fn = H[name]
        GT.bps[name] = PCSX.addBreakpoint(addr, 'Exec', 4, 'gt:' .. name, function()
            fn()
            return true
        end)
    end
    return true
end

function GT.stop()
    for name, bp in pairs(GT.bps) do
        bp:remove()
        GT.bps[name] = nil
    end
    if GT.file then
        flush()
        GT.file:close()
        GT.file = nil
    end
    if GT.shots then
        GT.shots:close()
        GT.shots = nil
    end
    return true
end
