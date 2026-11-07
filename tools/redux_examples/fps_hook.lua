-- fps_hook.lua: a standalone PCSX-Redux Lua hook, no redux.py needed.
--
-- Counts calls of a once-per-frame function with an exec breakpoint and
-- counts vsyncs with an event listener; every WINDOW vsyncs it prints the
-- game's frame rate next to PCSX-Redux's own guest FPS (display-start changes
-- per second). Breakpoints need the debugger, which needs the interpreter:
--
--   tools/toolchain/pcsx-redux/bin/pcsx-redux -no-ui -debugger -interpreter -run \
--       -bios <BIOS> -iso "bin/Xenogears (USA) (Disc 1)/Xenogears (USA) (Disc 1).cue" \
--       -dofile work/agents/REDUX/examples/fps_hook.lua
--   (headless: SDL_JOYSTICK_HIDAPI=0 SDL_AUDIO_DRIVER=dummy in the environment)
--
-- or through the harness:  redux.py start --debugger --lua .../fps_hook.lua
--
-- Environment knobs: XG_FPS_ADDR (default 0x80044E9C = PutDispEnv in US main,
-- see config/us/symbols/main.txt), XG_FPS_WINDOW (vsyncs per report, default
-- 300 = 5 s NTSC), XG_FPS_QUIT_AFTER (quit after that many vsyncs; exit code 0).

local addr = tonumber(os.getenv('XG_FPS_ADDR') or '') or 0x80044E9C
local window = tonumber(os.getenv('XG_FPS_WINDOW') or '') or 300
local quitAfter = tonumber(os.getenv('XG_FPS_QUIT_AFTER') or '')
local refresh = 60 -- NTSC; 50 for PAL

local calls, vsyncs, windowCalls = 0, 0, 0

-- Returning true keeps the breakpoint; not pausing lets emulation continue.
local bp = PCSX.addBreakpoint(addr, 'Exec', 4, 'fps_hook', function(address, width, cause)
    calls = calls + 1
    return true
end)

local listener = PCSX.Events.createEventListener('GPU::Vsync', function()
    vsyncs = vsyncs + 1
    if vsyncs % window == 0 then
        local fps = (calls - windowCalls) * refresh / window
        print(string.format('[fps_hook] vsync %d: %.2f fps (%d calls of %08X), guest FPS %.2f, %d cycles', vsyncs,
                            fps, calls - windowCalls, addr, PCSX.GPU.getGuestFPS(), tonumber(PCSX.getCPUCycles())))
        windowCalls = calls
    end
    if quitAfter and vsyncs >= quitAfter then PCSX.quit(0) end
end)

-- Keep both alive: the listener and the breakpoint are removed when collected.
FPS_HOOK = { bp = bp, listener = listener }
print(string.format('[fps_hook] counting calls of %08X every %d vsyncs', addr, window))
