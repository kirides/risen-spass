#!/usr/bin/env python3
"""Checks that types/risen.d.lua describes every Lua binding of the C++ sources (and nothing that does not exist).

    python tools/check_luadefs.py          # exit code 1 and a list of differences when out of sync

Sources scanned (src/game/*.cpp, src/game/npc_balance_lua.h, src/game/template_create_lua.h, src/lua/runtime.cpp):
  * `<table>.set_function("fn", ...)` where the table is published with `risen["ns"] = <table>;`
  * Lua helper chunks:  `function risen.ns.fn(`
  * usertype members:   `new_usertype<LuaX>("Type", ..., "member", ...)` and `ut["member"] = ...`
Definitions scanned: `function xxxApi.fn(` inside classes linked to `risen.<ns>` through `---@field ns RisenXApi`,
and `function Type:method(` / `---@field name` for usertypes.
"""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
defs = (root / 'types' / 'risen.d.lua').read_text(encoding='utf-8')

# ---- what the C++ sources bind ---------------------------------------------------------------------------------------
bound = {}      # "ns.fn" -> source
members = {}    # "Type.member" -> source

USERTYPES = {'LuaEntity': 'Entity', 'LuaTemplate': 'Template', 'LuaQuest': 'Quest', 'LuaInfo': 'Info', 'ScriptContext': 'ScriptContext'}

for path in sorted(list((root / 'src').rglob('*.cpp')) + [root / 'src/game/npc_balance_lua.h', root / 'src/game/template_create_lua.h']):   # the .h files hold the Lua chunks of NPC balancing and template creation
    text = path.read_text(encoding='utf-8', errors='replace')
    rel = path.relative_to(root).as_posix()
    # table variables published in `risen`
    published = dict(re.findall(r'risen\["(\w+)"\]\s*=\s*(\w+);', text))
    var_to_ns = {v: ns for ns, v in published.items()}
    # aliases: sol::table quest = risen["quest"];
    for var, ns in re.findall(r'sol::table\s+(\w+)\s*=\s*risen\["(\w+)"\];', text):
        var_to_ns[var] = ns
    for var, fn in re.findall(r'\b(\w+)\.set_function\("(\w+)"', text):
        ns = var_to_ns.get(var)
        if ns:
            bound[f'{ns}.{fn}'] = rel
    # Lua chunks
    for ns, fn in re.findall(r'^function risen\.(\w+)\.(\w+)\(', text, flags=re.M):
        bound[f'{ns}.{fn}'] = rel
    # usertypes
    # (the argument list is found by balancing parentheses: the lambdas inside contain `);`)
    for m in re.finditer(r'new_usertype<(\w+)>\(', text):
        cpp = m.group(1)
        if cpp not in USERTYPES:
            continue
        depth, i, in_str = 1, m.end(), False
        while i < len(text) and depth:
            c = text[i]
            if in_str:
                if c == '\\':
                    i += 1
                elif c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
            i += 1
        body = text[m.end():i]
        for key in re.findall(r'^\s*"(\w+)",', body, flags=re.M):
            if key != USERTYPES[cpp] and key != cpp:
                members[f'{USERTYPES[cpp]}.{key}'] = rel
    for key in re.findall(r'\n\s+ut\["(\w+)"\]\s*=', text):
        members[f'Entity.{key}'] = rel

# runtime.cpp: per-mod api (risen.on/off/emit) and risen.log.*
bound['risen.on'] = bound['risen.off'] = bound['risen.emit'] = 'src/lua/runtime.cpp'
for lvl in ('trace', 'debug', 'info', 'warn', 'error'):
    bound[f'log.{lvl}'] = 'src/lua/runtime.cpp'
# per-mod risen.save (lua_save.cpp registers on the mod's api table)
for fn in re.findall(r'save\.set_function\("(\w+)"', (root / 'src/game/lua_save.cpp').read_text(encoding='utf-8')):
    bound[f'save.{fn}'] = 'src/game/lua_save.cpp'
# per-mod risen.assets (write/remove/clear/mount on the mod's own table `own`) and the Lua helpers of lua_assets.cpp (`function A.<name>(`)
assets_src = (root / 'src/game/lua_assets.cpp').read_text(encoding='utf-8')
for fn in re.findall(r'own\.set_function\("(\w+)"', assets_src) + re.findall(r'^function A\.(\w+)\(', assets_src, flags=re.M):
    bound[f'assets.{fn}'] = 'src/game/lua_assets.cpp'
# per-mod voice functions (lua_voice.cpp: `own.set_function` on the mod's assets table (import_sound) and its voice table (check/set/load_dir))
voice_src = (root / 'src/game/lua_voice.cpp').read_text(encoding='utf-8')
for fn in re.findall(r'own\.set_function\("(\w+)"', voice_src):
    bound[('assets.' if fn == 'import_sound' else 'voice.') + fn] = 'src/game/lua_voice.cpp'
for fn in re.findall(r'loc\.set_function\("(\w+)"', (root / 'src/game/loc_files.cpp').read_text(encoding='utf-8')):
    bound[f'loc.{fn}'] = 'src/game/loc_files.cpp'
# risen.trace.* (runtime.cpp) and the per-mod risen.test.wait_event (a chunk in lua_test.cpp: `function t.<name>(`)
for fn in ('enable', 'disable', 'enabled', 'channels'):
    bound[f'trace.{fn}'] = 'src/lua/runtime.cpp'
for fn in re.findall(r'^function t\.(\w+)\(', (root / 'src/game/lua_test.cpp').read_text(encoding='utf-8'), flags=re.M):
    bound[f'test.{fn}'] = 'src/game/lua_test.cpp'
# per-mod risen.balance.install (a Lua chunk in lua_balance.cpp that defines `function b.<name>(` on the mod's balance table)
for fn in re.findall(r'^function b\.(\w+)\(', (root / 'src/game/lua_balance.cpp').read_text(encoding='utf-8'), flags=re.M):
    bound[f'balance.{fn}'] = 'src/game/lua_balance.cpp'
# spec-less sol properties of usertypes use `"name", sol::property(` : include them
for path in (root / 'src/game/script_bridge.cpp', root / 'src/game/lua_entity.cpp'):
    text = path.read_text(encoding='utf-8')
    for key in re.findall(r'"(\w+)",\s*sol::property', text):
        members['ScriptContext.' + key if 'script_bridge' in path.name else 'Entity.' + key] = path.name
members.pop('Entity.__eq', None)

# ---- what the definitions describe -----------------------------------------------------------------------------------
ns_class = dict(re.findall(r'---@field (\w+) (Risen\w+Api)\n', defs))
class_var = {}
for m in re.finditer(r'---@class (Risen\w+Api)\n(?:---.*\n)*?local (\w+) = \{\}', defs):
    class_var[m.group(1)] = m.group(2)
var_ns = {class_var[c]: ns for ns, c in ns_class.items() if c in class_var}
# the top-level `risen_` table
var_ns['risen_'] = 'risen'
described = {}
for var, fn in re.findall(r'^function (\w+)\.(\w+)\(', defs, flags=re.M):
    if var in var_ns:
        described[f'{var_ns[var]}.{fn}'] = True
described_members = {}
for typ, fn in re.findall(r'^function (Entity|Template|Quest|Info|ScriptContext):(\w+)\(', defs, flags=re.M):
    described_members[f'{typ}.{fn}'] = True
for typ in ('ScriptContext',):
    block = re.search(r'---@class ScriptContext\n((?:---.*\n)*)', defs).group(1)
    for f in re.findall(r'---@field (\w+)', block):
        described_members[f'{typ}.{f}'] = True
block = re.search(r'---@class Entity\n((?:---.*\n)*)', defs).group(1)
for f in re.findall(r'---@field (\w+)', block):
    described_members[f'Entity.{f}'] = True

problems = []
for k, src in sorted(bound.items()):
    if k not in described:
        problems.append(f'binding not described in types/risen.d.lua: risen.{k}  ({src})')
for k, src in sorted(members.items()):
    if k not in described_members:
        problems.append(f'usertype member not described: {k}  ({src})')
for k in sorted(described):
    if k not in bound:
        problems.append(f'described but not bound: risen.{k}')
for k in sorted(described_members):
    if k not in members:
        problems.append(f'described but not bound: {k}')

if problems:
    print('\n'.join(problems))
    print(f'\n{len(problems)} difference(s)')
    sys.exit(1)
print(f'ok: {len(bound)} functions and {len(members)} usertype members are described')
