# Lua definitions for risen-spass

`risen.d.lua` describes the whole Lua API (every function, class, event and option table) for
[lua-language-server](https://luals.github.io/) (LuaLS): completion, hover documentation, and warnings for wrong argument
types, misspelled functions, or values that can be `nil` (for example `risen.entity.player()` before a game is running).

## Setup

* **Installed game:** `scripts/install.ps1` copies this directory to `<Risen>\bin\spass\types` and a matching `.luarc.json` to
  `<Risen>\bin\spass\mods`. Open `<Risen>\bin\spass\mods` (or one mod) in VS Code with the *Lua* extension, or any editor with LuaLS.
* **This repository:** `mods/.luarc.json` already points at `../types`.
* **Your own setup:** add the directory to the library and declare the two globals:

```json
{
  "runtime.version": "LuaJIT",
  "workspace.library": ["path/to/spass/types"],
  "diagnostics.globals": ["risen", "mod"]
}
```

Check a mod from the command line (CI friendly):

```
lua-language-server --check mods/my_mod --configpath mods/.luarc.json --checklevel Warning
```

## Keeping it correct

`python tools/check_luadefs.py` compares the file with the bindings in `src/` and fails when a function or usertype method
exists on only one side. Run it after adding or renaming a binding, and describe the new function here
(`---@param`/`---@return`, a sentence on what it does).
