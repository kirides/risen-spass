-- Fixture of the scenario `http`: the mod may only reach example.org and *.example.net.
risen.on("http_test.probe", function(out)
    local r = {}
    out["hosts"] = r
    r.available, r.reason = risen.http.available()
    r.id, r.err = risen.http.get_text("http://127.0.0.1:" .. tostring(out.port) .. "/text", function() r.called = true end)
end)
