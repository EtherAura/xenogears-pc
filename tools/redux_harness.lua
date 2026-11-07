-- redux_harness.lua: PCSX-Redux side of tools/redux.py.
--
-- Loaded with `pcsx-redux -dofile redux_harness.lua`. Adds web handlers under
-- /api/v1/lua/xg_<name>, each answering JSON. Every handler wants
-- ?token=<XG_REDUX_TOKEN>; without that environment variable the harness is
-- inert (every call answers 403), so loading it into an emulator whose web
-- server listens beyond loopback exposes nothing.
--
-- Handlers (query parameters in brackets):
--   xg_ping                         -> {ok, vsyncs}
--   xg_stats                        -> vsync count, CPU cycles, guest FPS, pc, breakpoints
--   xg_run      [vsyncs]            -> resume; pause again after that many vsyncs
--   xg_bp       [op=add|remove|clear|list, addr, name, pause=1, type, width]
--   xg_state    [op=save|load, path] -> PCSX save state (raw protobuf, not gzipped)
--   xg_pad      [op=press|release|seq|stop|state, port=1|2, buttons=A+B, path, pause=1]
--                                   -> controller input (see "pads" below)
--   xg_dofile   [path]              -> run a Lua file, return its results
--   xg_quit     [code]
--
-- Exec breakpoints only fire with the debugger on, which redux.py turns on
-- together with the interpreter (`start --debugger`).
--
-- Keep request lines under ~200 bytes: PCSX-Redux reads requests in 256-byte
-- chunks and parses the URL of each chunk separately, so a longer URL is cut.
-- That is why Lua code travels as a file (xg_dofile) and paths are relative to
-- the emulator's working directory (redux.py's run directory).

local token = os.getenv('XG_REDUX_TOKEN')

XG = XG or {}
XG.vsyncs = XG.vsyncs or 0
XG.bps = XG.bps or {}
XG.stopAtVsync = nil
XG.lastBreak = nil
XG.listeners = XG.listeners or {}
XG.pad = XG.pad or { held = { {}, {} }, seq = nil, seqEnd = nil }

-- Keep the listener referenced: it is removed when garbage-collected.
XG.listeners.vsync = PCSX.Events.createEventListener('GPU::Vsync', function()
    XG.vsyncs = XG.vsyncs + 1
    if XG.padTick then XG.padTick() end
    if XG.stopAtVsync and XG.vsyncs >= XG.stopAtVsync then
        XG.stopAtVsync = nil
        PCSX.pauseEmulator()
    end
end)
-- Any pause (breakpoint, API, the stop above) ends a pending xg_run.
XG.listeners.pause = PCSX.Events.createEventListener('ExecutionFlow::Pause', function()
    XG.stopAtVsync = nil
end)

-- ---------------------------------------------------------------- helpers --

local function jsonEncode(v)
    local t = type(v)
    if t == 'nil' then
        return 'null'
    elseif t == 'boolean' then
        return tostring(v)
    elseif t == 'cdata' then
        return jsonEncode(tonumber(v))
    elseif t == 'number' then
        if v ~= v or v == math.huge or v == -math.huge then return 'null' end
        if math.floor(v) == v and math.abs(v) < 2 ^ 53 then return string.format('%d', v) end
        return string.format('%.17g', v)
    elseif t == 'string' then
        return '"' .. v:gsub('[%c"\\]', function(c)
            if c == '"' then return '\\"' end
            if c == '\\' then return '\\\\' end
            return string.format('\\u%04x', c:byte())
        end) .. '"'
    elseif t == 'table' then
        local n = #v
        if n > 0 then
            local parts = {}
            for i = 1, n do parts[i] = jsonEncode(v[i]) end
            return '[' .. table.concat(parts, ',') .. ']'
        end
        local keys = {}
        for k in pairs(v) do keys[#keys + 1] = tostring(k) end
        table.sort(keys)
        local parts = {}
        for _, k in ipairs(keys) do
            local val = v[k]
            if val == nil then val = v[tonumber(k)] end
            parts[#parts + 1] = jsonEncode(k) .. ':' .. jsonEncode(val)
        end
        return '{' .. table.concat(parts, ',') .. '}'
    end
    return jsonEncode(tostring(v))
end
XG.jsonEncode = jsonEncode

local function urlDecode(s)
    s = s:gsub('+', ' ')
    return (s:gsub('%%(%x%x)', function(h) return string.char(tonumber(h, 16)) end))
end

local function parseQuery(q)
    local out = {}
    for pair in (q or ''):gmatch('[^&]+') do
        local k, v = pair:match('^([^=]*)=?(.*)$')
        out[urlDecode(k)] = urlDecode(v)
    end
    return out
end

local function reply(status, body)
    return string.format('HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n',
                         status, #body) .. body
end

local function need(q, key)
    local v = q[key]
    if v == nil or v == '' then error('missing parameter: ' .. key) end
    return v
end

local function cycles() return tonumber(PCSX.getCPUCycles()) end

-- ------------------------------------------------------------ breakpoints --

local function bpInfo(b)
    return { addr = string.format('0x%08X', b.addr), hits = b.hits, lastVsync = b.lastVsync, pause = b.pause,
             type = b.type }
end

local function bpRemove(name)
    local b = XG.bps[name]
    if b then
        b.bp:remove()
        XG.bps[name] = nil
    end
end

-- Count every hit; with pause=true also stop the emulator on it.
function XG.addBreakpoint(addr, name, pause, bptype, width)
    name = name or string.format('%08X', addr)
    bpRemove(name)
    local b = { addr = addr, hits = 0, lastVsync = nil, pause = pause, type = bptype or 'Exec' }
    b.bp = PCSX.addBreakpoint(addr, b.type, width or 4, 'xg:' .. name, function(address, w, cause)
        b.hits = b.hits + 1
        b.lastVsync = XG.vsyncs
        if b.pause then
            XG.lastBreak = { name = name, addr = string.format('0x%08X', address), vsync = XG.vsyncs,
                             cycles = cycles() }
            PCSX.pauseEmulator()
        end
        return true
    end)
    XG.bps[name] = b
    return b
end

-- ------------------------------------------------------------------- pads --
-- Controller input through PCSX-Redux's pad overrides (active-low: an
-- override holds a button down, clearing it lets go), on top of whatever
-- the host input says (nothing, headless). Two kinds:
--   holds: press/release, kept until released;
--   one sequence: steps of {buttons, vsyncs}, advanced on every vsync, so
--     each step is held for exactly that many vsyncs (games read the pad
--     once a frame). With pause=1 the emulator pauses when it ends.
-- Buttons: PCSX.CONSTS.PAD.BUTTON names (SELECT START UP RIGHT DOWN LEFT L2
-- R2 L1 R1 TRIANGLE CIRCLE CROSS SQUARE) or X O TRI SQ, joined with '+'.

local BUTTON = PCSX.CONSTS.PAD.BUTTON
local ALIAS = { X = 'CROSS', O = 'CIRCLE', TRI = 'TRIANGLE', SQ = 'SQUARE' }

local function padOf(port)
    local slot = PCSX.SIO0.slots[port]
    if not slot then error('no pad port ' .. tostring(port)) end
    return slot.pads[1]
end

local function parseButtons(text)
    local out = {}
    if text == nil or text == '' or text == '_' then return out end
    for name in text:gmatch('[^+,%s]+') do
        name = ALIAS[name:upper()] or name:upper()
        if BUTTON[name] == nil then error('unknown button: ' .. name) end
        out[#out + 1] = name
    end
    return out
end

-- A button stays down while a hold or the running sequence wants it.
local function padApply(port)
    local pad = padOf(port)
    local seq = XG.pad.seq
    for name, bit in pairs(BUTTON) do
        local down = XG.pad.held[port][name]
        if seq and seq.port == port and seq.down and seq.down[name] then down = true end
        if down then pad.setOverride(bit) else pad.clearOverride(bit) end
    end
end

local function seqEnter(seq, i)
    seq.i = i
    seq.left = seq.steps[i].vsyncs
    seq.down = {}
    for _, name in ipairs(seq.steps[i].buttons) do seq.down[name] = true end
    padApply(seq.port)
end

function XG.padTick()
    local seq = XG.pad.seq
    if not seq then return end
    seq.left = seq.left - 1
    if seq.left > 0 then return end
    if seq.i < #seq.steps then
        seqEnter(seq, seq.i + 1)
        return
    end
    XG.pad.seq = nil
    XG.pad.seqEnd = XG.vsyncs
    padApply(seq.port)
    if seq.pause then PCSX.pauseEmulator() end
end

local function padState(port)
    local pad = padOf(port)
    local held, down = {}, {}
    for name in pairs(XG.pad.held[port]) do held[#held + 1] = name end
    for name, bit in pairs(BUTTON) do
        if pad.getButton(bit) then down[#down + 1] = name end
    end
    table.sort(held)
    table.sort(down)
    local seq = XG.pad.seq
    return { port = port, held = held, down = down, vsyncs = XG.vsyncs, seqEnd = XG.pad.seqEnd,
             seq = seq and { port = seq.port, step = seq.i, steps = #seq.steps, left = seq.left } or nil }
end

-- --------------------------------------------------------------- handlers --

local H = {}

function H.ping(q) return { ok = true, vsyncs = XG.vsyncs } end

function H.stats(q)
    local bps = {}
    for name, b in pairs(XG.bps) do bps[name] = bpInfo(b) end
    local regs = PCSX.getRegisters()
    return {
        vsyncs = XG.vsyncs,
        cycles = cycles(),
        guestFPS = PCSX.GPU.getGuestFPS(),
        pc = string.format('0x%08X', regs.pc),
        ra = string.format('0x%08X', regs.GPR.n.ra),
        stopAtVsync = XG.stopAtVsync,
        lastBreak = XG.lastBreak,
        bps = bps,
    }
end

function H.run(q)
    local n = tonumber(q.vsyncs or '')
    if n then XG.stopAtVsync = XG.vsyncs + n else XG.stopAtVsync = nil end
    XG.lastBreak = nil
    PCSX.resumeEmulator()
    return { vsyncs = XG.vsyncs, stopAtVsync = XG.stopAtVsync }
end

function H.bp(q)
    local op = q.op or 'list'
    if op == 'add' then
        local addr = tonumber(need(q, 'addr'))
        if not addr then error('bad address: ' .. q.addr) end
        local b = XG.addBreakpoint(addr, q.name ~= '' and q.name or nil, q.pause == '1', q.type,
                                   tonumber(q.width or ''))
        return bpInfo(b)
    elseif op == 'remove' then
        bpRemove(need(q, 'name'))
    elseif op == 'clear' then
        for name in pairs(XG.bps) do bpRemove(name) end
    elseif op ~= 'list' then
        error('unknown op: ' .. op)
    end
    local bps = {}
    for name, b in pairs(XG.bps) do bps[name] = bpInfo(b) end
    return { bps = bps }
end

function H.state(q)
    local op, path = need(q, 'op'), need(q, 'path')
    if op == 'save' then
        local slice = PCSX.createSaveState()
        local size = slice.size
        local f = Support.File.open(path, 'TRUNCATE')
        if f:failed() then error('cannot write ' .. path) end
        f:writeMoveSlice(slice)
        f:close()
        return { path = path, bytes = size, vsyncs = XG.vsyncs }
    elseif op == 'load' then
        local f = Support.File.open(path)
        if f:failed() then error('cannot read ' .. path) end
        PCSX.loadSaveState(f)
        f:close()
        return { path = path, vsyncs = XG.vsyncs }
    end
    error('unknown op: ' .. op)
end

function H.pad(q)
    local op = q.op or 'state'
    local port = tonumber(q.port or '1') or 1
    if port ~= 1 and port ~= 2 then error('port must be 1 or 2') end
    if op == 'press' then
        for _, name in ipairs(parseButtons(need(q, 'buttons'))) do XG.pad.held[port][name] = true end
        padApply(port)
    elseif op == 'release' then
        if q.buttons == nil or q.buttons == '' or q.buttons == 'all' then
            XG.pad.held[port] = {}
        else
            for _, name in ipairs(parseButtons(q.buttons)) do XG.pad.held[port][name] = nil end
        end
        padApply(port)
    elseif op == 'seq' then
        -- One step per line: "<buttons or _> <vsyncs>" (a file: URLs are short).
        local f = io.open(need(q, 'path'), 'r')
        if not f then error('cannot read ' .. q.path) end
        local steps = {}
        for line in f:lines() do
            local b, n = line:match('^%s*(%S+)%s+(%d+)%s*$')
            if b then
                n = tonumber(n)
                if n < 1 then error('a step lasts at least one vsync') end
                steps[#steps + 1] = { buttons = parseButtons(b), vsyncs = n }
            elseif line:match('%S') then
                error('bad step: ' .. line)
            end
        end
        f:close()
        if #steps == 0 then error('empty sequence') end
        local old = XG.pad.seq
        XG.pad.seq = { port = port, steps = steps, pause = q.pause == '1' }
        if old and old.port ~= port then padApply(old.port) end
        XG.pad.seqEnd = nil
        seqEnter(XG.pad.seq, 1)
    elseif op == 'stop' then
        local old = XG.pad.seq
        XG.pad.seq = nil
        if old then padApply(old.port) end
    elseif op ~= 'state' then
        error('unknown op: ' .. op)
    end
    return padState(port)
end

function H.dofile(q)
    local fn, err = loadfile(need(q, 'path'))
    if not fn then error(err) end
    local res = { pcall(fn) }
    if not res[1] then error(res[2]) end
    local out = {}
    for i = 2, #res do out[#out + 1] = res[i] end
    return { results = out }
end

function H.quit(q)
    local code = tonumber(q.code or '') or 0
    PCSX.nextTick(function() PCSX.quit(code) end)
    return { quitting = code }
end

PCSX.WebServer = PCSX.WebServer or {}
PCSX.WebServer.Handlers = PCSX.WebServer.Handlers or {}
for name, fn in pairs(H) do
    PCSX.WebServer.Handlers['xg_' .. name] = function(request)
        local q = parseQuery(request.urlData.query)
        if not token or token == '' or q.token ~= token then
            return reply('403 Forbidden', '{"error":"bad or missing token"}')
        end
        local ok, res = pcall(fn, q, request)
        if not ok then return reply('500 Internal Server Error', jsonEncode({ error = tostring(res) })) end
        return reply('200 OK', jsonEncode(res))
    end
end

print('redux_harness.lua loaded' .. (token and '' or ' (inert: XG_REDUX_TOKEN not set)'))
