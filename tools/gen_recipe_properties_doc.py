#!/usr/bin/env python3
"""Builds docs/RECIPE_PROPERTIES.md from a spass.log written by mods/recipedump (every item template with a gCRecipe_PS: all properties of the recipe set and
the item sets, the ingredient and skill arrays, the labels).

    scripts/test-run.ps1 -Mods recipedump -Seconds 150 -NoAutostart
    python tools/gen_recipe_properties_doc.py <test copy>/bin/spass/spass.log > docs/RECIPE_PROPERTIES.md

"RECIPE <template> <set> <prop>:<type>=<value>", "RSETS <template> <sets>", "RING <template> <item> <count>", "RREQ <template> <skill> <amount>",
"RNAME <template> <label>" and "RITEM <item> <label>" lines are aggregated; the hand-written parts are the decompiled facts of docs/RE.md section 30.
"""
import re
import sys
from collections import Counter, OrderedDict, defaultdict

log_path = sys.argv[1]

recipe_re = re.compile(r'RECIPE (\S+) (\S+) (\w+):(.+?)=(.*)$')
rsets_re = re.compile(r'RSETS (\S+) (.*)$')
ring_re = re.compile(r'RING (\S+) (\S+) (\d+)$')
rreq_re = re.compile(r'RREQ (\S+) gESkill_(\w+) (-?\d+)$')
rname_re = re.compile(r'RNAME (\S+) (.*)$')
ritem_re = re.compile(r'RITEM (\S+) (.*)$')

FRIENDLY = {'Misc_Smith': 'Smith', 'Misc_Alchemy': 'Alchemy', 'Misc_Scribe': 'Scribe', 'Misc_Trophy': 'Trophy', 'Magic_Circle': 'Circle'}

props = defaultdict(OrderedDict)
values = {}
sets_of = {}
ingredients = defaultdict(list)
required = defaultdict(list)
label = {}
item_label = {}

for line in open(log_path, encoding='utf-8', errors='replace'):
    for tag, rx in (('RECIPE', recipe_re), ('RSETS', rsets_re), ('RING', ring_re), ('RREQ', rreq_re), ('RNAME', rname_re), ('RITEM', ritem_re)):
        i = line.find(' ' + tag + ' ')
        if i < 0:
            continue
        m = rx.match(line[i + 1:].rstrip())
        if not m:
            continue
        if tag == 'RECIPE':
            tpl, st, prop, typ, val = m.groups()
            p = props[st].setdefault(prop, {'type': typ, 'values': Counter(), 'templates': set()})
            p['values'][val] += 1
            p['templates'].add(tpl)
            values[(tpl, st, prop)] = val
        elif tag == 'RSETS':
            sets_of[m.group(1)] = m.group(2).split()
        elif tag == 'RING':
            ingredients[m.group(1)].append((m.group(2), int(m.group(3))))
        elif tag == 'RREQ':
            skill = m.group(2)
            required[m.group(1)].append((FRIENDLY.get(skill, skill), int(m.group(3))))
        elif tag == 'RNAME':
            label[m.group(1)] = m.group(2)
        else:
            item_label[m.group(1)] = m.group(2)
        break

templates = sorted(sets_of)
STATION_ORDER = ['Smith_Forge', 'Smith_Anvil', 'Smith_CoolWeapon', 'Smith_GrindStone', 'Goldsmith', 'Alchemy', 'WriteScroll', 'Cooking', 'Frying']
FRIENDLY_STATION = {'Smith_Forge': 'forge', 'Smith_Anvil': 'anvil', 'Smith_CoolWeapon': 'coolweapon', 'Smith_GrindStone': 'grindstone', 'Goldsmith': 'goldsmith',
                    'Alchemy': 'alchemy', 'WriteScroll': 'scroll', 'Cooking': 'cooking', 'Frying': 'frying'}
GAME_NUMBER = {'Alchemy': '0', 'Smith_Forge': '6', 'Smith_Anvil': '7', 'Smith_CoolWeapon': '8', 'Smith_GrindStone': '9'}   # as the CanInteract_* scripts compare them
MOB = {'Smith_Forge': 'Object_Interact_Forge', 'Smith_Anvil': 'Object_Interact_Anvil', 'Smith_CoolWeapon': 'Object_Interact_Cooling', 'Smith_GrindStone': 'Object_Interact_Animated_GrindStone',
       'Goldsmith': 'Object_Interact_Goldsmith', 'Alchemy': 'Object_Interact_Alchemy_Table'}


DRIVEN = {'Smith_Anvil', 'Alchemy'}   # stations whose real interaction was driven live (risen.interact.begin); the other mob names are guesses from the template names


def v(t, st, prop, default=''):
    x = values.get((t, st, prop))
    return default if x is None else x


def station_of(t):
    return v(t, 'gCRecipe_PS', 'Craft').replace('gERecipeCategory_', '')


def lab(item):
    name = item_label.get(item) or label.get(item)
    return f'`{item}`' + (f' ({name})' if name and name != item else '')


def summary(entry):
    typ, vals = entry['type'], entry['values']
    if re.fullmatch(r'(long|int|float|unsigned long|short|ulong)', typ):
        nums = []
        for val in vals:
            try:
                nums.append(float(re.sub(r'[^0-9eE+.\-]', '', val)))
            except ValueError:
                pass
        if nums:
            lo, hi = min(nums), max(nums)
            fmt = (lambda x: str(int(x))) if all(x == int(x) for x in nums) else (lambda x: f'{x:g}')
            return f'{fmt(lo)} .. {fmt(hi)} ({len(vals)} distinct)'
    if typ.startswith('bTPropertyContainer<enum') or typ == 'bool':
        top = vals.most_common(6)
        return ', '.join(f'`{x}` x{c}' for x, c in top) + (f', ... ({len(vals)} distinct)' if len(vals) > 6 else '')
    top = [x for x, _ in vals.most_common(3)]
    shown = ', '.join('`' + (x if len(x) < 40 else x[:37] + '...') + '`' if x else '(empty)' for x in top)
    return shown + (f', ... ({len(vals)} distinct)' if len(vals) > 3 else '')


by_station = defaultdict(list)
for t in templates:
    by_station[station_of(t)].append(t)

out = ['# Recipe properties', '',
       f'Generated by `tools/gen_recipe_properties_doc.py` from the engine\'s own reflection (`mods/recipedump`): the {len(templates)} item templates that carry a `gCRecipe_PS` (the recipes of all stations), '
       'their property sets and the ingredient/skill arrays as shipped. The hand-written sections are the decompiled facts of [`RE.md`](RE.md) section 30 (Script.dll `PSInventory::CanSynthesize/Synthesize`, '
       'Script_Game.dll station scripts). Edit recipes with `risen.balance.apply { recipes = { ... } }` / `new_recipes`, `risen.recipe.*` (see [`LUA_API.md`](LUA_API.md) "Recipes").', '']

out += ['## How crafting works', '',
        '* **A recipe is an item.** Any item template with a `gCRecipe_PS` property set (property set id `0x5a`) is a recipe: `Craft` (the station category, enum `gERecipeCategory`), `CraftIngredients` '
        '(`bTObjArray<gCCraftIngredient>`, each `ItemTemplate` + `ItemAmount`), `RequiredSkills` (`bTObjArray<gCSkillValue>`: the crafter\'s skills), `ResultItem` and `ResultAmount`.',
        '* **A character knows a recipe by carrying the recipe item.** There is no list of learned recipes and no recipe game variable: the stations list the stacks of the inventory whose template has a recipe of '
        'their category (`InvFilter_Craft_<Station>` sets the select limit of each stack to `CanSynthesize`, `HasRecipes(category)` is what the station\'s `CanInteract_Player_*` asks). Selling or dropping the item '
        'forgets the recipe, picking up or buying one learns it. Alchemy, cooking and scroll recipes are separate recipe items (`It_Recipe_*`, ...); **the smithing recipes are the intermediate blades themselves** '
        '(`It_1H_Steel_Raw` is the forge recipe whose ingredient is itself and whose result is `It_1H_Steel_Hot`, which is the anvil recipe for `It_1H_Steel_Shaped`, ...): carrying the blank is knowing how to work it.',
        '* **The check** is `PSInventory::CanSynthesize(recipe)`: 0 unless the crafter meets every `RequiredSkills` entry (`PSSkills::GetTotal >= Amount`), otherwise the smallest `amount in inventory / ItemAmount` over '
        'all ingredients (all stacks of the template count). **The craft** is `PSInventory::Synthesize(recipe, count)`: for every ingredient `DeleteItems(first stack, ItemAmount * count)`, then `CreateItems(ResultItem, count)`. '
        'It does **not** check anything itself and **never reads `ResultAmount`** (verified: 3 stays 1 per craft), so a recipe makes `count` result items per craft; every station calls it only for an amount the check allowed.',
        '* **The stations** (`Interact_Player_<Station>`, Script_Game.dll): the player uses the mob (`Object_Interact_*`, use type Anvil, Forge, ...): `CanInteract_Player_*` asks for the skill (Smith >= 1 / Alchemy >= 1), a tool '
        '(`It_SmithHammer` at the anvil) and a recipe item of the category; `PreInteract_Player_*` walks to the mob and opens the station\'s inventory page (`GUI2::OpenAnvil` ...: the inventory filtered by `InvFilter_Craft_*`); '
        'the click on a recipe is the script `InvEvent_OnClick_CraftSelect` (`SetSelection(stack, 1)`, closes the page; with a select limit above 1 it asks for the amount first); `Interact_Player_*` plays the animation and calls '
        '`PSInventory::Synthesize(recipe item, selected amount)`. The smith chain carries the half-finished blade in the left hand between the stations (`CanInteract_Player`: "CarryToAnvil"). `risen.interact.begin` starts it all.',
        '* **Result item and number of results**: the result is any item template (a new one made with `new_items` works the same); `ResultAmount` is data without effect.', '']

out += ['## Where the numbers live', '',
        '| What | Where | Notes |', '|---|---|---|',
        '| station | `gCRecipe_PS.Craft` (`station` in `recipes` / `new_recipes`) | enum `gERecipeCategory`: ' + ', '.join(f'`{FRIENDLY_STATION[s]}` = `gERecipeCategory_{s}`' for s in STATION_ORDER if s in by_station) +
        '; the scripts compare the numbers ' + ', '.join(f'{FRIENDLY_STATION[s]} {n}' for s, n in GAME_NUMBER.items()) + ' |',
        '| ingredients | `gCRecipe_PS.CraftIngredients` (`ingredients`, `counts`) | array of `{ ItemTemplate, ItemAmount }` at `+0x30` of the set, stride `0x18`; written through `gCRecipe_PS::SetCraftIngredients` |',
        '| required skills | `gCRecipe_PS.RequiredSkills` (`skills`) | same element type as `gCItem_PS.RequiredSkills` (`Smith`, `Alchemy`, `Scribe`, ...); array at `+0x40`; `gCRecipe_PS::SetRequiredSkills` |',
        '| result | `gCRecipe_PS.ResultItem` (`result`) | a template proxy at `+0x50`; `ResultAmount` (+0x58) is not used by the craft |',
        '| the recipe item | `gCItem_PS` (value, icon, category `Written` for recipe items), loc ids `FO_<template>` / `ITEMDESC_<template>` | like any item; `risen.template.create` clones it |',
        '| tool and skill of the station | scripts `CanInteract_Player_<Station>` (Smith >= 1 or Alchemy >= 1, `It_SmithHammer` at the anvil) | fixed in the scripts |', '']

out += ['## Stations', '',
        '| Station | `station` | Mob template | Recipes | Required skills seen |', '|---|---|---|---|---|']
for s in STATION_ORDER:
    ts = by_station.get(s, [])
    if not ts:
        continue
    skills = Counter()
    for t in ts:
        for sk, a in required.get(t, []):
            skills[f'{sk} {a}'] += 1
    out.append(f'| {s} | `{FRIENDLY_STATION[s]}` | {("`" + MOB[s] + "`" + (" (driven live)" if s in DRIVEN else " (by name, not driven)")) if s in MOB else "(not identified)"} | {len(ts)} | ' +
               (', '.join(f'{k} (x{c})' for k, c in sorted(skills.items())) if skills else '-') + ' |')
out.append('')

out += ['## All recipes by station', '',
        'Template of the recipe item, label, ingredients (the amount is per craft), result and the required skills. A recipe whose first ingredient is the recipe item itself is a smithing blank or a gem/food that is its own recipe.', '']
for s in STATION_ORDER:
    ts = by_station.get(s, [])
    if not ts:
        continue
    out += [f'### {s} ({len(ts)})', '', '| Recipe item | Label | Ingredients | Result | Skills |', '|---|---|---|---|---|']
    for t in ts:
        ing = ', '.join(f'{c}x {lab(i)}' for i, c in ingredients.get(t, []))
        res = v(t, 'gCRecipe_PS', 'ResultItem')
        sk = ', '.join(f'{k} {a}' for k, a in required.get(t, [])) or '-'
        out.append(f'| `{t}` | {label.get(t, "")} | {ing} | {lab(res)} | {sk} |')
    out.append('')

PURPOSE = {
    'gCRecipe_PS': 'The recipe: station, ingredients, required skills, result.',
    'gCItem_PS': 'The recipe item itself (gold value, icon, use type).',
}
out += ['## Property sets of the recipe items', '']
for st in ('gCRecipe_PS', 'gCItem_PS'):
    if st not in props:
        continue
    n = sum(1 for t in templates if st in sets_of[t])
    out += [f'### {st}', '', f'{n} templates. {PURPOSE[st]}', '', '| Property | Type | Templates | Values seen |', '|---|---|---|---|']
    for name, entry in props[st].items():
        out.append(f'| `{name}` | `{entry["type"]}` | {len(entry["templates"])} | {summary(entry)} |')
    out.append('')
common = Counter()
for t in templates:
    for st in sets_of[t]:
        common[st] += 1
out += ['Property sets of the recipe items (count of templates): ' + ', '.join(f'`{s}` {n}' for s, n in sorted(common.items())) + '.', '']

sys.stdout.reconfigure(encoding='utf-8')
print('\n'.join(out).replace('�', '?'))
