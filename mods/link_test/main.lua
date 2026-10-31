---@diagnostic disable: need-check-nil, undefined-field, param-type-mismatch
-- Live scenario `link` (smoke-test.ps1, test-run.ps1 -LinkClient -Unsafe): the python client tools/linktest_client.py talks to this game over the live link's named pipe
-- and writes what it checked to <spass>\link-client-result.json; this mod provides the commands it calls and checks the result and the connect / disconnect events.
local T = risen.test
local connects, disconnects = 0, 0
risen.on("link.connected", function() connects = connects + 1 end)
risen.on("link.disconnected", function() disconnects = disconnects + 1 end)

local result_file = T.param("result_file", "")

T.plan { "new game started", "link available", "client result read", "client checks", "bad command names are refused", "connected event", "disconnected event" }

local function read_file(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end

T.run(function()
    T.eq("new game started", T.wait_event("session.start", 180, function(m) return m == "new" end), "new")
    T.expect("link available", risen.link.available() == true)
    local ok1, why1 = risen.link.on("Bad Name", function() end)
    local ok2 = risen.link.on("fine", "not a function")
    T.expect("bad command names are refused", ok1 == nil and type(why1) == "string" and ok2 == nil)

    T.allow_errors(true)  -- the client calls a command that raises a Lua error on purpose (logged as an error)
    local text
    T.wait_until(function()
        text = read_file(result_file)
        return text ~= nil and text:find("}", 1, true) ~= nil
    end, 150, "the client's result file")
    T.allow_errors(false)
    local results = risen.json.decode(text or "{}")
    T.expect("client result read", type(results) == "table" and next(results) ~= nil, text and #text)
    local failed = {}
    for name, r in pairs(results or {}) do
        if not r[1] then failed[#failed + 1] = name .. ": " .. tostring(r[2]) end
    end
    table.sort(failed)
    T.expect("client checks", #failed == 0, table.concat(failed, " | "))
    T.expect("connected event", connects == 1, tostring(connects))
    T.wait_until(function() return disconnects >= 1 end, 20, "link.disconnected")
    T.expect("disconnected event", disconnects == 1 and risen.link.connected() == false, tostring(disconnects))
    T.finish("link done")
end, "link")
