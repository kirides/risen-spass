---
name: lua-typecheck
description: Run the same Lua checks as the GitHub Action (check_luadefs.py + LuaLS type check of mods/) right before a commit. Use immediately before every `git commit` that includes *.lua, types/risen.d.lua or anything under mods/.
---

# Lua type check before committing

CI (`.github/workflows/build.yml`, job `lua`) fails on any LuaLS warning, so check **just before committing**, not after every edit:

```powershell
.\scripts\check-lua.ps1
```

It runs `python tools/check_luadefs.py` and `lua-language-server --check . --configpath .luarc.json --checklevel Warning` in `mods/` (LuaLS 3.19.2 is pinned in CI; the script warns on a version mismatch) and prints `file:line:col [code] message`. Findings in git-ignored files (`mods/risen_extended`) are skipped because CI never sees them. Exit 0 = commit, 1 = fix first.

Typical fixes: annotate a table with `---@type table[]` / `---@class`, add the field to the class, `---@cast`, or fix `types/risen.d.lua` (hard rule 1). Do not silence a warning with `diagnostics.disable` in `.luarc.json` unless it is a real false positive.

The editor's inline Lua diagnostics (Lua 5.4 "deprecated", "undefined global bit") use a different config and are noise: only this script counts.
