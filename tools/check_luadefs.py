#!/usr/bin/env python3
"""Checks that types/risen.d.lua describes every Lua binding of the C++ sources (and nothing that does not exist).

    python tools/check_luadefs.py          # exit code 1 and a list of differences when out of sync

Sources scanned (src/game/*.cpp, src/game/npc_balance_lua.h, src/game/template_create_lua.h, src/game/spell_lua.h, src/lua/runtime.cpp):
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

USERTYPES = {'LuaEntity': 'Entity', 'LuaTemplate': 'Template', 'LuaQuest': 'Quest', 'LuaInfo': 'Info', 'ScriptContext': 'ScriptContext', 'AiContext': 'AiContext', 'GuiWindow': 'GuiWindow'}

for path in sorted(list((root / 'src').rglob('*.cpp')) + [root / 'src/game/npc_balance_lua.h', root / 'src/game/template_create_lua.h', root / 'src/game/spell_lua.h', root / 'src/game/recipe_lua.h']):   # the .h files hold the Lua chunks of NPC balancing, template creation and spells
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
    if 'new_usertype<GuiWindow>' in text:  # M16: the GuiWindow members are registered one by one with `ut.set_function("name", ...)`
        for key in re.findall(r'\n\s+ut\.set_function\("(\w+)"', text):
            members[f'GuiWindow.{key}'] = rel

# runtime.cpp: per-mod api (risen.on/off/emit) and risen.log.*
bound['risen.on'] = bound['risen.off'] = bound['risen.emit'] = 'src/lua/runtime.cpp'
bound['risen.config'] = 'src/game/lua_config.cpp'   # per-mod: api.set_function("config", ...)
bound['risen.on_unload'] = 'src/lua/runtime.cpp'    # per-mod (M15)
bound['risen.migrate'] = 'src/game/migrate_lua.h'   # per-mod (M15): `function api.migrate(` in the chunk of migrate_lua.h
bound['risen.retire'] = 'src/game/migrate_lua.h'    # per-mod (M15): `function api.retire(`
for fn in re.findall(r'^function M\.(\w+)\(', (root / 'src/game/migrate_lua.h').read_text(encoding='utf-8'), flags=re.M):
    bound[f'mods.{fn}'] = 'src/game/migrate_lua.h'     # the global chunk of risen.mods.retire / orphans / remove_orphans
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
# per-mod risen.spell.enable_overrides / create (the chunk in spell_lua.h that defines `function s.<name>(` on the mod's spell table)
for fn in re.findall(r'^function s\.(\w+)\(', (root / 'src/game/spell_lua.h').read_text(encoding='utf-8'), flags=re.M):
    bound[f'spell.{fn}'] = 'src/game/spell_lua.h'
# per-mod risen.recipe.learn_on_start / create (the chunk in recipe_lua.h that defines `function r.<name>(` on the mod's recipe table)
for fn in re.findall(r'^function r\.(\w+)\(', (root / 'src/game/recipe_lua.h').read_text(encoding='utf-8'), flags=re.M):
    bound[f'recipe.{fn}'] = 'src/game/recipe_lua.h'
# per-mod risen.npc.persistent / persistent_get / persistent_list (the chunk in persistent_npc_lua.h defines `function npc.<name>(` on the mod's npc table)
for fn in re.findall(r'^function npc\.(\w+)\(', (root / 'src/game/persistent_npc_lua.h').read_text(encoding='utf-8'), flags=re.M):
    bound[f'npc.{fn}'] = 'src/game/persistent_npc_lua.h'
# per-mod risen.cond.* / risen.spawn.* (the chunk in spawn_when_lua.h defines `function cond.<name>(` and `function spawn.<name>(`)
for ns in ('cond', 'spawn'):
    for fn in re.findall(rf'^function {ns}\.(\w+)\(', (root / 'src/game/spawn_when_lua.h').read_text(encoding='utf-8'), flags=re.M):
        bound[f'{ns}.{fn}'] = 'src/game/spawn_when_lua.h'
# the NPC routine API (routine_lua.h): shared `function world.<name>(` (risen.world) and per-mod `function world.<name>(` / `function npc.<name>(` (risen.world, risen.npc)
routine_src = (root / 'src/game/routine_lua.h').read_text(encoding='utf-8')
for ns_var, ns in (('world', 'world'), ('npc', 'npc')):
    for fn in re.findall(rf'^function {ns_var}\.(\w+)\(', routine_src, flags=re.M):
        bound[f'{ns}.{fn}'] = 'src/game/routine_lua.h'
# geometry (M13, geometry_lua.h): `function M.<name>(` is risen.math, `function world.<name>(` risen.world, `function R.<name>(` / the method names copied by the loop at the end of the
# random chunk are risen.random, `function Stream:<name>(` the methods of a random stream (the class RandomStream)
geo_src = (root / 'src/game/geometry_lua.h').read_text(encoding='utf-8')
for fn in re.findall(r'^function M\.(\w+)\(', geo_src, flags=re.M) + re.findall(r'^M\.(\w+)\s*=\s*(?:function|xyz)', geo_src, flags=re.M):
    bound[f'math.{fn}'] = 'src/game/geometry_lua.h'
for fn in re.findall(r'^function world\.(\w+)\(', geo_src, flags=re.M):
    bound[f'world.{fn}'] = 'src/game/geometry_lua.h'
for fn in re.findall(r'^function R\.(\w+)\(', geo_src, flags=re.M):
    bound[f'random.{fn}'] = 'src/game/geometry_lua.h'
random_names = re.search(r'for _, name in ipairs \{([^}]*)\} do\s*\n\s*local method = Stream', geo_src)
for fn in re.findall(r'"(\w+)"', random_names.group(1)) if random_names else []:
    bound[f'random.{fn}'] = 'src/game/geometry_lua.h'
for fn in re.findall(r'^function Stream:(\w+)\(', geo_src, flags=re.M):
    members[f'RandomStream.{fn}'] = 'src/game/geometry_lua.h'
# the AI bridge (M11): the per-mod risen.ai.start / stop / forget / persisted (the chunk in ai_lua.h defines `function ai.<name>(` on the mod's ai table)
for fn in re.findall(r'^function ai\.(\w+)\(', (root / 'src/game/ai_lua.h').read_text(encoding='utf-8'), flags=re.M):
    bound[f'ai.{fn}'] = 'src/game/ai_lua.h'
# per-mod risen.dialog.create (wrapped) / compile / describe (the chunk in dialog_cond_lua.h defines `function own.<name>(` on the mod's dialog table)
for fn in re.findall(r'^function own\.(\w+)\(', (root / 'src/game/dialog_cond_lua.h').read_text(encoding='utf-8'), flags=re.M):
    bound[f'dialog.{fn}'] = 'src/game/dialog_cond_lua.h'
# spec-less sol properties of usertypes use `"name", sol::property(` : include them
for path in (root / 'src/game/script_bridge.cpp', root / 'src/game/lua_entity.cpp'):
    text = path.read_text(encoding='utf-8')
    for key in re.findall(r'"(\w+)",\s*sol::property', text):
        members['ScriptContext.' + key if 'script_bridge' in path.name else 'Entity.' + key] = path.name
members.pop('Entity.__eq', None)
members.pop('GuiWindow.__eq', None)
members.pop('GuiWindow.__tostring', None)

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
for typ, fn in re.findall(r'^function (Entity|Template|Quest|Info|ScriptContext|AiContext|RandomStream|GuiWindow):(\w+)\(', defs, flags=re.M):
    described_members[f'{typ}.{fn}'] = True
for typ in ('ScriptContext', 'AiContext'):
    block = re.search(rf'---@class {typ}\n((?:---.*\n)*)', defs).group(1)
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
