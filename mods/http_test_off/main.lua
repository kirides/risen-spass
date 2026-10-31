-- Fixture of the scenario `http`: no `network` in mod.toml, so every request must be refused before it starts.
risen.on("http_test.probe", function(out)
    local r = {}
    out["off"] = r
    r.available, r.reason = risen.http.available()
    r.id, r.err = risen.http.get_text("http://127.0.0.1:" .. tostring(out.port) .. "/text", function() r.called = true end)
end)
