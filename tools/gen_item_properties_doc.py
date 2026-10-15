#!/usr/bin/env python3
"""Builds docs/ITEM_PROPERTIES.md from a spass.log written by mods/itemdump (every item template: all property sets, all properties with
their values, plus the RequiredSkills / ModifySkills arrays).

    scripts/test-run.ps1 -Mods itemdump -Seconds 90 -NoAutostart
    python tools/gen_item_properties_doc.py <test copy>/bin/spass/spass.log docs/TEMPLATES.md > docs/ITEM_PROPERTIES.md

"ITEM <template> <set> <prop>:<type>=<value>" and "ARR <template> required|modifiers <skill> <amount> [<mode>]" lines are aggregated per
property set and per template directory (the directory of a template comes from docs/TEMPLATES.md).
"""
import re
import sys
from collections import Counter, OrderedDict, defaultdict

log_path = sys.argv[1]
templates_md = sys.argv[2] if len(sys.argv) > 2 else None

# engine enum suffix (gESkill_<suffix>) -> the friendly name of risen.balance.skills()
FRIENDLY = {
    'Stat_STR': 'Strength', 'Stat_DEX': 'Dexterity', 'Stat_INT': 'Intelligence', 'Stat_HP': 'MaxHitPoints', 'Stat_MP': 'MaxManaPoints',
    'Stat_LP': 'LearnPoints', 'Stat_XP': 'Experience', 'Stat_LV': 'Level', 'Atrib_HP': 'HitPoints', 'Atrib_MP': 'ManaPoints',
    'Combat_Axe': 'Axe', 'Combat_Sword': 'Sword', 'Combat_Staff': 'Staff', 'Combat_Bow': 'Bow', 'Combat_CrossBow': 'CrossBow',
    'Prot_Edge': 'ProtEdge', 'Prot_Blunt': 'ProtBlunt', 'Prot_Point': 'ProtPoint', 'Prot_Fire': 'ProtFire', 'Prot_Ice': 'ProtIce',
    'Prot_Magic': 'ProtMagic', 'Magic_Missile': 'Missile', 'Magic_Frost': 'Frost', 'Magic_Fireball': 'Fireball', 'Magic_Circle': 'Circle',
    'Misc_Scribe': 'Scribe', 'Misc_Trophy': 'Trophy', 'Misc_Acrobat': 'Acrobat', 'Misc_Pickpocket': 'Pickpocket', 'Misc_Lockpick': 'Lockpick',
    'Misc_Sneak': 'Sneak', 'Misc_Mining': 'Mining', 'Misc_Smith': 'Smith', 'Misc_Alchemy': 'Alchemy',
}

item_re = re.compile(r'ITEM (\S+) (\S+) (\w+):(.+?)=(.*)$')
arr_re = re.compile(r'ARR (\S+) (required|modifiers) (?:(gESkill_\w+) (-?\d+)(?: (gESkillModifier_\w+))?|<(.*)>)$')

props = defaultdict(lambda: OrderedDict())      # set -> prop -> {'type', 'values': Counter, 'templates': set}
sets_of = defaultdict(set)                      # template -> sets
values = {}                                     # (template, set, prop) -> value
arrays = defaultdict(lambda: {'required': [], 'modifiers': []})
no_item = set()

for line in open(log_path, encoding='utf-8', errors='replace'):
    i = line.find(' ITEM ')
    if i >= 0:
        m = item_re.match(line[i + 1:].rstrip())
        if m:
            tpl, st, prop, typ, val = m.groups()
            p = props[st].setdefault(prop, {'type': typ, 'values': Counter(), 'templates': set()})
            p['values'][val] += 1
            p['templates'].add(tpl)
            sets_of[tpl].add(st)
            values[(tpl, st, prop)] = val
        continue
    i = line.find(' ARR ')
    if i >= 0:
        m = arr_re.match(line[i + 1:].rstrip())
        if not m:
            continue
        tpl, kind, skill, amount, mode, reason = m.groups()
        if reason:
            no_item.add(tpl)
        else:
            arrays[tpl][kind].append((FRIENDLY.get(skill[len('gESkill_'):], skill[len('gESkill_'):]), int(amount), (mode or '')[len('gESkillModifier_'):]))

templates = sorted(sets_of)
group_of = {}
if templates_md:
    cur = None
    for line in open(templates_md, encoding='utf-8'):
        if line.startswith('## '):
            cur = line[3:].strip()
        elif cur and line.startswith('`'):
            for name in re.findall(r'`([^`]+)`', line):
                group_of[name] = cur
groups = defaultdict(list)
for t in templates:
    groups[group_of.get(t, '?')].append(t)

VISUAL = {'eCMesh_PS', 'eCRigidBody_PS', 'eCCollisionShape_PS', 'eCIlluminated_PS', 'eCStrip_PS', 'eCAnimation_PS', 'eCBodyPart_PS',
          'eCDecal_PS', 'eCPointLight_PS'}
PURPOSE = {
    'gCItem_PS': 'The item itself: gold value, category, use type, equip scripts and the two skill arrays (see "Skill arrays").',
    'gCDamage_PS': 'Weapon damage (`DamageAmount`, `DamageMultiplier`, `DamageBonus`, `DamageType`, `DamageScript`).',
    'gCCastInfo_PS': 'Runes, scrolls and cast items: mana cost, the cast item/ammo/fail templates.',
    'gCRecipe_PS': 'Recipes (alchemy, cooking, scrolls, trophies): result item, ingredients, required skills.',
    'gCProjectile2_PS': 'Projectiles (arrows, magic missiles, fireballs): speed, flight, hit effect.',
    'gCEffect_PS': 'Visual/sound effect hooks of the item.',
    'gCInteraction_PS': 'Interaction with the world: focus, scripts for touch/trigger, slots.',
    'gCMap_PS': 'Map items.', 'gCLetter_PS': 'Letters.', 'gCBook_PS': 'Books.', 'gCScriptRoutine_PS': 'AI routine of animated items.',
}


def summary(entry):
    typ, vals = entry['type'], entry['values']
    n = sum(vals.values())
    if re.fullmatch(r'(long|int|float|unsigned long|short|ulong)', typ):
        nums = []
        for v in vals:
            try:
                nums.append(float(re.sub(r'[^0-9eE+.\-]', '', v)))
            except ValueError:
                pass
        if nums:
            lo, hi = min(nums), max(nums)
            fmt = (lambda x: str(int(x))) if all(x == int(x) for x in nums) else (lambda x: f'{x:g}')
            return f'{fmt(lo)} .. {fmt(hi)} ({len(vals)} distinct)'
    if typ.startswith('bTPropertyContainer<enum') or typ == 'bool':
        top = vals.most_common(6)
        text = ', '.join(f'`{v}` x{c}' for v, c in top)
        return text + (f', ... ({len(vals)} distinct)' if len(vals) > 6 else '')
    top = [v for v, _ in vals.most_common(3)]
    shown = ', '.join('`' + (v if len(v) < 40 else v[:37] + '...') + '`' if v else '(empty)' for v in top)
    return shown + (f', ... ({len(vals)} distinct)' if len(vals) > 3 else '')


out = ['# Item properties', '',
       'Generated by `tools/gen_item_properties_doc.py` from the engine\'s own reflection (`mods/itemdump`): every one of the '
       f'{len(templates)} item templates (`Items/...`), all property sets and all properties with their shipped values. '
       'Read and write them with `risen.template.get(name):get_property/set_property` or `risen.template.patch`; the two skill arrays '
       '(`RequiredSkills`, `ModifySkills`) need `risen.item.requirements/modifiers` (see below and [`LUA_API.md`](LUA_API.md)). '
       'Declarative balancing: `risen.balance.apply { items = { It_1H_Sword = { value = 120, damage = 25, requires = { Strength = 5 } } } }`. '
       'Findings and offsets: [`RE.md`](RE.md) section 23.', '']

out += ['## Where the numbers live', '',
        '| What | Property | Notes |', '|---|---|---|',
        '| gold value | `gCItem_PS.GoldValue` (`value` in `risen.balance`) | plain `long`; used by trading and the inventory |',
        '| melee/ranged weapon damage | `gCDamage_PS.DamageAmount` (`damage`) | melee hit = `DamageMultiplier * (DamageAmount + attacker Strength)` (+50 while Berserker; `DoSwordDamage`, `DoAxeDamage`, `DoStaffDamage`, `DoFistDamage`); bows/crossbows carry their own `DamageAmount`, the arrows are `gCDamage_PS` items too (projectile path not decompiled) |',
        '| damage type | `gCDamage_PS.DamageType` | `gEDamageType_Edge/Blunt/Point/Fire/Ice/Magic` pick which protection skill reduces it |',
        '| equip conditions ("requirements") | `gCItem_PS.RequiredSkills` array (`requires`) | skill >= amount; only the **player** is checked (`CanEquipHasRequieredStats`), NPCs equip anything |',
        '| armor / shield / ring / amulet effects | `gCItem_PS.ModifySkills` array (`modifies`) | `Prot*` skills for protection, attributes (`Strength`, `Dexterity`, `MaxHitPoints`, ...) for rings; applied on equip, subtracted on unequip |',
        '| potion / food effects | `gCItem_PS.ModifySkills` array | `HitPoints`/`ManaPoints` = instant heal/mana, `MaxHitPoints`/`MaxManaPoints` = permanent maximum, `Strength`, `Experience`; applied when the item is used up |',
        '| use type / category | `gCItem_PS.UseType`, `Category`, `HoldType` | `UseType` decides the equip slot and which script handles it |',
        '| spell cost of runes/scrolls | `gCCastInfo_PS.ManaCost` (`mana_cost`) | plus `CastItem`, `CastAmmo`, `CastFail`, `ConsumeItem`; whole spells (projectiles, `spells` / `new_spells`): [`SPELL_PROPERTIES.md`](SPELL_PROPERTIES.md) |',
        '| recipe ingredients / skills | `gCRecipe_PS.CraftIngredients`, `RequiredSkills`, `ResultItem` | editable: `risen.balance` sections `recipes` / `new_recipes`, see [RECIPE_PROPERTIES.md](RECIPE_PROPERTIES.md) |',
        '', 'There is no durability and no weight in this game: no property for either exists on any item.', '']

# ---- property sets --------------------------------------------------------------------------------------------------------------------
out += ['## Property sets on items', '', '| Property set | Templates | Purpose |', '|---|---|---|']
for st in sorted(props, key=lambda s: (s in VISUAL, -len({t for t in templates if s in sets_of[t]}), s)):
    n = sum(1 for t in templates if st in sets_of[t])
    purpose = PURPOSE.get(st, 'visual / physics (not relevant for balancing)' if st in VISUAL else '')
    out.append(f'| `{st}` | {n} | {purpose} |')
out.append('')

for st in sorted(props, key=lambda s: (s in VISUAL, s)):
    if st in VISUAL:
        continue
    n = sum(1 for t in templates if st in sets_of[t])
    out += [f'### {st}', '', f'{n} templates. {PURPOSE.get(st, "")}'.strip(), '', '| Property | Type | Templates | Values seen |', '|---|---|---|---|']
    for name, entry in props[st].items():
        out.append(f'| `{name}` | `{entry["type"]}` | {len(entry["templates"])} | {summary(entry)} |')
    out.append('')

out += ['### Visual / physics sets', '',
        ', '.join(f'`{s}`' for s in sorted(props) if s in VISUAL) + '. Mesh, material, collision, lighting and trail settings; they have no influence on the numbers above. '
        'Properties: ' + '; '.join(f'`{s}`: ' + ', '.join(f'`{p}`' for p in props[s]) for s in sorted(props) if s in VISUAL), '']

# ---- skill arrays ------------------------------------------------------------------------------------------------------------------------
out += ['## Skill arrays', '',
        '`gCItem_PS.RequiredSkills` is a list of `{ Skill, Amount }` (`gCSkillValue`), `gCItem_PS.ModifySkills` a list of `{ Skill, Amount, Modifier }` '
        '(`gCModifySkill`). The property text format shows them only as `Array[n]`: use `risen.item.requirements(name)` / `risen.item.set_requirements`, '
        '`risen.item.modifiers` / `risen.item.set_modifiers` (friendly names `Strength`, `ProtEdge`, ... from `risen.balance.skills()`) or the raw '
        '`risen.item.skill_array`. Skills are listed by their friendly names (engine enum `gESkill_Stat_STR` = `Strength`, `Prot_Edge` = `ProtEdge`, `Atrib_HP` = `HitPoints` (current), `Stat_HP` = `MaxHitPoints`, ...). Modifier modes: `AddValue` (all shipped items), `SetToMax`, `SetToValue`, `AddPercentOfMax`.', '']

for kind, title in (('required', 'Requirements (RequiredSkills) by directory'), ('modifiers', 'Modifiers (ModifySkills) by directory')):
    out += [f'### {title}', '', '| Directory | Items with entries | Skill: amount range (items) |', '|---|---|---|']
    for g in sorted(groups):
        stat = defaultdict(list)
        holders = 0
        for t in groups[g]:
            es = arrays[t][kind]
            if es:
                holders += 1
            for skill, amount, mode in es:
                stat[skill + (f' [{mode}]' if mode and mode != 'AddValue' else '')].append(amount)
        if not holders:
            continue
        cells = '; '.join(f'`{s}` {min(a)}..{max(a)} ({len(a)})' if min(a) != max(a) else f'`{s}` {a[0]} ({len(a)})' for s, a in sorted(stat.items()))
        out.append(f'| {g} | {holders} of {len(groups[g])} | {cells} |')
    out.append('')

# ---- examples -----------------------------------------------------------------------------------------------------------------------------
def describe(t):
    parts = []
    for st, prop, label in (('gCItem_PS', 'GoldValue', 'GoldValue'), ('gCDamage_PS', 'DamageAmount', 'DamageAmount'),
                            ('gCDamage_PS', 'DamageType', 'DamageType'), ('gCCastInfo_PS', 'ManaCost', 'ManaCost'),
                            ('gCItem_PS', 'UseType', 'UseType'), ('gCItem_PS', 'Category', 'Category')):
        v = values.get((t, st, prop))
        if v is not None:
            parts.append(f'{label}={v.split("_")[-1] if "_" in v and label in ("UseType", "Category", "DamageType") else v}')
    req = ', '.join(f'{s} {a}' for s, a, _ in arrays[t]['required'])
    mod = ', '.join(f'{s} {a:+d}' + (f' ({m})' if m and m != 'AddValue' else '') for s, a, m in arrays[t]['modifiers'])
    if req:
        parts.append(f'requires: {req}')
    if mod:
        parts.append(f'modifies: {mod}')
    return '; '.join(parts)


out += ['## Representative items', '', 'One or two templates per kind, as shipped:', '', '| Template | Kind | Numbers |', '|---|---|---|']
for t, kind in (('It_1H_Sword', 'one-handed sword'), ('It_2H_TwoHandedSword', 'two-handed sword'), ('It_Axe_Waraxe', 'axe'), ('It_Staff_Combat', 'staff'),
                ('It_Bow_Long', 'bow'), ('It_Arrow', 'arrow'), ('It_Armor_Guard', 'armor'), ('It_Helmet_Guard', 'helmet'),
                ('It_Shield_Guard', 'shield'), ('It_Ri_STR_Small', 'ring'), ('It_Ri_Prot_Missile', 'ring (protection)'), ('It_Am_STR', 'amulet'),
                ('It_Po_Health_01', 'health potion'), ('It_Po_HP_Max', 'permanent potion'), ('It_Po_STR_Max', 'permanent potion'), ('It_Apple', 'food'),
                ('It_Stew', 'food'), ('It_Ru_Inferno', 'rune'), ('It_Scr_Inferno', 'scroll'), ('It_Gold', 'currency')):
    if t in sets_of:
        out.append(f'| `{t}` | {kind} | {describe(t)} |')
out.append('')

if no_item:
    out += ['## Templates without gCItem_PS', '', ', '.join(f'`{t}`' for t in sorted(no_item)) + '.', '']

sys.stdout.reconfigure(encoding='utf-8')
print('\n'.join(out).replace('�', '?'))
