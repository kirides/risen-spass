---@diagnostic disable: need-check-nil, param-type-mismatch, missing-parameter, assign-type-mismatch
-- Live scenario `http` (scripts/smoke-test.ps1, test-run.ps1 -HttpServer): risen.http and risen.json against tools/httptest_server.py (loopback), the `network`
-- manifest key (http_test: true, http_test_off: absent, http_test_hosts: a host list), the player's switches and the Network buttons of the mod manager.
local T = risen.test
local port = T.param("http_port", "")
local base = "http://127.0.0.1:" .. port

T.plan {
    "new game started", "server port given",
    "available", "callback comes later", "get_text", "get_json", "post_json", "headers and method", "http error status", "invalid json", "empty answer",
    "redirect", "timeout", "connection refused", "bad arguments", "cancel", "cancel: no callback", "json round trip", "json null and errors",
    "mod without network", "host list", "mod switch", "global switch", "manager buttons", "manager switch", "manager global switch",
}

-- Starts a request through `start(callback)` and waits for the callback: returns result, err, info (or nil, "refused: reason" when it was not started).
local function await(start, seconds)
    local got
    local id, why = start(function(a, b, c) got = { a, b, c } end)
    if not id then
        return nil, "refused: " .. tostring(why)
    end
    if got ~= nil then
        return nil, "callback ran synchronously"
    end
    T.wait_until(function() return got ~= nil end, seconds or 30, "http callback")
    return got[1], got[2], got[3]
end

T.run(function()
    T.section("new game")
    T.eq("new game started", T.wait_event("session.start", 180, function(m) return m == "new" end), "new")
    T.expect("server port given", port ~= "", "start the scenario with test-run.ps1 -HttpServer")
    if port == "" then return end
    T.sleep(30)

    T.section("basics")
    local ok, why = risen.http.available()
    T.expect("available", ok == true, tostring(why))

    local flag
    local id = risen.http.get_text(base .. "/text", function() flag = true end)
    T.expect("callback comes later", type(id) == "number" and flag == nil, tostring(id))
    T.wait_until(function() return flag end, 20, "first callback")

    local text, err, info = await(function(cb) return risen.http.get_text(base .. "/text", cb) end)
    T.expect("get_text", text == "hello" and err == nil and info.status == 200 and info.headers["content-type"] == "text/plain" and info.url == base .. "/text",
        ("%s %s %s"):format(tostring(text), tostring(err), tostring(info and info.status)))

    local data
    data, err, info = await(function(cb) return risen.http.get_json(base .. "/json", cb) end)
    T.expect("get_json", type(data) == "table" and data.name == "risen" and #data.list == 3 and data.list[3] == 3 and data.nested.ok == true and data.nothing == nil and err == nil,
        tostring(err))

    local echo
    echo, err = await(function(cb) return risen.http.post_json(base .. "/echo", { a = 1, b = { 1, 2 }, s = "x\"y" }, cb) end)
    local sent = echo and risen.json.decode(echo.body)
    T.expect("post_json", err == nil and echo and echo.method == "POST" and echo.headers["content-type"] == "application/json" and sent and sent.a == 1 and sent.b[2] == 2 and sent.s == "x\"y",
        tostring(err))

    echo, err = await(function(cb)
        return risen.http.request({ url = base .. "/echo", method = "put", as = "json", body = "raw!", headers = { Authorization = "Bearer abc", ["X-Num"] = 7 }, timeout = 10 }, cb)
    end)
    T.expect("headers and method", err == nil and echo and echo.method == "PUT" and echo.body == "raw!" and echo.headers.authorization == "Bearer abc" and echo.headers["x-num"] == "7"
        and (echo.headers["user-agent"] or ""):find("risen-spass/", 1, true) ~= nil and (echo.headers["user-agent"] or ""):find("http_test", 1, true) ~= nil,
        tostring(err) .. " " .. tostring(echo and echo.headers["user-agent"]))

    T.section("failures")
    text, err, info = await(function(cb) return risen.http.get_text(base .. "/404", cb) end)
    T.expect("http error status", text == nil and err == "HTTP 404" and info.status == 404 and info.body == "not here", tostring(err))

    data, err, info = await(function(cb) return risen.http.get_json(base .. "/badjson", cb) end)
    T.expect("invalid json", data == nil and tostring(err):find("invalid JSON", 1, true) == 1 and info.body == "{not json", tostring(err))

    data, err = await(function(cb) return risen.http.get_json(base .. "/empty", cb) end)
    T.expect("empty answer", data == true and err == nil, tostring(data) .. " " .. tostring(err))

    text, err, info = await(function(cb) return risen.http.get_text(base .. "/redirect", cb) end)
    T.expect("redirect", text == "hello" and info.redirects == 1 and info.url == base .. "/text", tostring(err))

    text, err = await(function(cb) return risen.http.get_text(base .. "/slow", { timeout = 1 }, cb) end, 30)
    T.expect("timeout", text == nil and err == "timed out", tostring(err))

    text, err = await(function(cb) return risen.http.get_text("http://127.0.0.1:1/", { timeout = 2 }, cb) end)
    T.expect("connection refused", text == nil and err ~= nil and err ~= "", tostring(err))

    local bad = {
        { risen.http.get_text("ftp://example.org/", function() end) },
        { risen.http.get_text(base .. "/text") },
        { risen.http.get_text(42, function() end) },
        { risen.http.get_text(base .. "/text", { headers = "no" }, function() end) },
        { risen.http.get_text(base .. "/text", { headers = { ["Bad Name"] = "x" } }, function() end) },
        { risen.http.request({ url = base .. "/text", method = "TRACE" }, function() end) },
        { risen.http.post_json(base .. "/echo", { f = function() end }, function() end) },
        { risen.http.request("not a table", function() end) },
    }
    local all_refused = true
    for i, r in ipairs(bad) do
        if r[1] ~= nil or type(r[2]) ~= "string" then
            all_refused = false
            T.expect("bad call " .. i .. " is refused with a reason", false, tostring(r[1]) .. " " .. tostring(r[2]))
        end
    end
    T.expect("bad arguments", all_refused)

    local called = false
    local quick = risen.http.get_text(base .. "/text", function() called = true end)
    T.expect("cancel", risen.http.cancel(quick) == true and risen.http.cancel(quick) == false and risen.http.cancel(99999) == false)
    T.sleep(120)  -- the answer arrived long ago: a cancelled request never calls back
    T.expect("cancel: no callback", called == false)

    T.section("json")
    local doc = { name = "x", list = { 1, 2.5, "three", true }, empty = {}, nested = { a = { b = { c = 1 } } } }
    local encoded = risen.json.encode(doc)
    local back = risen.json.decode(encoded)
    T.expect("json round trip", back.name == "x" and back.list[2] == 2.5 and back.list[3] == "three" and back.list[4] == true and #back.empty == 0 and back.nested.a.b.c == 1
        and risen.json.encode({ 1, 2 }) == "[1,2]" and risen.json.encode({ k = 1 }) == '{"k":1}' and risen.json.encode("a\nb") == '"a\\nb"', encoded)
    local withnull = risen.json.decode('{"a":null,"l":[1,null,3]}')
    local cyc = {}
    cyc.self = cyc
    local e1, e2, e3 = select(2, risen.json.decode("{oops")), select(2, risen.json.encode(cyc)), select(2, risen.json.encode(function() end))
    T.expect("json null and errors", withnull.a == nil and #withnull.l == 3 and withnull.l[2] == risen.json.null and withnull.l[3] == 3 and risen.json.encode({ risen.json.null }) == "[null]"
        and type(e1) == "string" and type(e2) == "string" and type(e3) == "string", tostring(e1) .. " | " .. tostring(e2) .. " | " .. tostring(e3))

    T.section("permissions")
    local out = { port = port }
    risen.emit("http_test.probe", out)
    local off, hostmod = out.off or {}, out.hosts or {}
    T.expect("mod without network", off.available == false and tostring(off.reason):find("mod.toml", 1, true) ~= nil and off.id == nil and tostring(off.err):find("mod.toml", 1, true) ~= nil
        and off.called == nil, tostring(off.reason))
    local plain = risen.mods.get("http_test_off")
    local hosts = risen.mods.get("http_test_hosts")
    local own = risen.mods.get("http_test")
    T.expect("host list", hosts and hosts.network.declared and #hosts.network.hosts == 2 and hosts.network.hosts[2] == "*.example.net" and own.network.declared and #own.network.hosts == 0
        and plain and not plain.network.declared and hostmod.available == true and hostmod.id == nil and tostring(hostmod.err):find("host list", 1, true) ~= nil and hostmod.called == nil,
        tostring(hostmod.err))

    T.section("the player's switches")
    local fine = risen.mods.set_network(false, "http_test")
    local ok2, why2 = risen.http.available()
    local rid, rwhy = risen.http.get_text(base .. "/text", function() end)
    T.expect("mod switch", fine == true and ok2 == false and tostring(why2):find("this mod", 1, true) ~= nil and rid == nil and rwhy == why2 and risen.mods.get("http_test").network.allowed == false
        and risen.mods.network().mods.http_test == false, tostring(why2))
    risen.mods.set_network(true, "http_test")
    text = await(function(cb) return risen.http.get_text(base .. "/text", cb) end)
    local after_mod = text == "hello"
    risen.mods.set_network(false)
    local ok3, why3 = risen.http.available()
    local gid = risen.http.get_text(base .. "/text", function() end)
    local net = risen.mods.network()
    T.expect("global switch", after_mod and ok3 == false and tostring(why3):find("switched off", 1, true) ~= nil and gid == nil and net.enabled == false, tostring(why3))
    risen.mods.set_network(true)

    T.section("mod manager")
    local st = {}
    risen.emit("modmanager.state", st)
    local mm = st.model
    if not mm then
        T.expect("manager buttons", false, "no modmanager model")
        T.expect("manager switch", false, "no modmanager model")
        T.expect("manager global switch", false, "no modmanager model")
    else
        mm:refresh()
        mm:select("http_test")
        local b = mm:snapshot().buttons
        mm:select("http_test_off")
        local b_off = mm:snapshot().buttons
        T.expect("manager buttons", b.network.enabled and b.network.label == "Network access: allowed" and not b_off.network.enabled and b.netall.label == "Network: on",
            tostring(b.network.label) .. " / " .. tostring(b_off.network.label) .. " / " .. tostring(b.netall.label))
        mm:select("http_test")
        mm:press("network")
        local off_snap = mm:snapshot()
        local off_ok = off_snap.buttons.network.label == "Network access: OFF" and risen.mods.get("http_test").network.allowed == false
        mm:press("network")
        T.expect("manager switch", off_ok and mm:snapshot().buttons.network.label == "Network access: allowed" and risen.mods.get("http_test").network.allowed == true,
            tostring(off_snap.buttons.network.label))
        mm:press("netall")
        local all_off = mm:snapshot().buttons.netall.label == "Network: OFF" and risen.mods.network().enabled == false and risen.http.available() == false
        mm:press("netall")
        T.expect("manager global switch", all_off and mm:snapshot().buttons.netall.label == "Network: on" and risen.mods.network().enabled == true and risen.http.available() == true)
    end

    T.finish("http done")
end, "http")
