---@diagnostic disable: undefined-global
-- Crashes the game on purpose to prove the crash diagnostics (src/common/crash.*, docs/DEVELOPMENT.md "Crashes"):
--   .\scripts\test-run.ps1 -Mods crashprobe -Unsafe -WaitForResult -Seconds 150 -Scenario crashprobe -Param kind=av [-Param when=session.start]
-- kind: av (write to address 0x18, an access violation in the interpreter = inside the layer), exec (call address 0x10: a DEP fault with a bad
-- instruction pointer, like the ntdll+0x161914 sightings), exit (ExitProcess(3))
-- when: tick (the first tick, default), session.start (the new game started)
local kind = risen.test.param("kind", "av")
local when = risen.test.param("when", "tick")
local ffi = ffi   -- a global when the sandbox is off

local done = false
local function boom()
    if done then return end
    done = true
    risen.log.info("crashprobe: " .. kind .. " at " .. when)
    if kind == "av" then
        ffi.cast("int*", 0x18)[0] = 1
    elseif kind == "exec" then
        ffi.cast("void(*)()", 0x10)()
    elseif kind == "exit" then
        ffi.cdef("void ExitProcess(unsigned int code);")
        ffi.C.ExitProcess(3)
    end
end

if when == "session.start" then
    risen.on("session.start", boom)
else
    risen.on("tick", boom)
end
