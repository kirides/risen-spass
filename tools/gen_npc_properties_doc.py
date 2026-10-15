#!/usr/bin/env python3
"""Builds docs/NPC_PROPERTIES.md from a spass.log written by mods/npcdump (every NPC and monster template: all reflected property sets and
properties with their shipped values, the non-reflected gCSkills_PS numbers, combat species fist items and starting inventories).

    scripts/test-run.ps1 -Mods npcdump -Seconds 150 -NoAutostart
    python tools/gen_npc_properties_doc.py <test copy>/bin/spass/spass.log docs/TEMPLATES.md > docs/NPC_PROPERTIES.md

Lines read: "NPC <template> <set> <prop>:<type>=<value>", "STATS <template> hp_max=.. mana_max=.. level=.. xp_reward=.. <Skill>=..",
"COMBAT <template> <species> <right fist> <left fist>", "LOOT <template> <item> <amount>", "LIMIT <id> <skill> <min> <max>".
"""
import re
import statistics
import sys
from collections import Counter, OrderedDict, defaultdict

log_path = sys.argv[1]
templates_md = sys.argv[2] if len(sys.argv) > 2 else None

props = defaultdict(OrderedDict)        # set -> prop -> {'type', 'values': Counter, 'templates': set}
sets_of = defaultdict(set)
stats = {}
combat = {}
loot = defaultdict(list)
limits = []

npc_re = re.compile(r'NPC (\S+) (\S+) (\w+):(.+?)=(.*)$')
for line in open(log_path, encoding='utf-8', errors='replace'):
    m = re.search(r' (NPC|STATS|COMBAT|LOOT|LIMIT) (.*)$', line.rstrip())
    if not m:
        continue
    kind, rest = m.groups()
    if kind == 'NPC':
        mm = npc_re.match('NPC ' + rest)
        if mm:
            tpl, st, prop, typ, val = mm.groups()
            p = props[st].setdefault(prop, {'type': typ, 'values': Counter(), 'templates': set()})
            p['values'][val] += 1
            p['templates'].add(tpl)
            sets_of[tpl].add(st)
    elif kind == 'STATS':
        parts = rest.split(' ')
        stats[parts[0]] = {k: int(v) for k, v in (x.split('=') for x in parts[1:])}
    elif kind == 'COMBAT':
        tpl, species, right, left = rest.split(' ')
        combat[tpl] = (species, right, left)
    elif kind == 'LOOT':
        tpl, item, amount = rest.split(' ')
        loot[tpl].append((item, int(amount)))
    elif kind == 'LIMIT':
        i, name, lo, hi = rest.split(' ')
        limits.append((int(i), name, int(lo), int(hi)))

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

VISUAL = {'eCRigidBody_PS', 'eCCollisionShape_PS', 'eCIlluminated_PS', 'eCAnimation_PS', 'eCBodyPart_PS', 'gCDynamicCollisionCircle_PS', 'gCNavigation_PS',
          'gCEffect_PS', 'gCMapInfo_PS'}
PURPOSE = {
    'gCNPC_PS': 'Identity and attitude: `Guild`, `Species`, `AttitudeLock`, `BraveryOverride`, `DamageCalculationType`, `Group`, voice and effect names, plus run-time state '
                '(current target, last attacker, guard status) that scripts write.',
    'gCCombatSystem_PS': 'Which combat species (move set, fist items) and fight AI the NPC uses (`CombatSpecies`, `ActiveFightAI`, `PassiveFightAI`).',
    'gCDamage_PS': 'The NPC\'s own damage (`DamageAmount`, `DamageMultiplier`, `DamageType`, `DamageScript`): only special monsters use it (Titan, Inferno); everybody else hits with weapons/fists.',
    'gCInventory_PS': 'The inventory (stacks of item templates, not reflected: `risen.npc.template_inventory/template_give/template_take`); `GeneratedPlunder`/`GeneratedTrade` flags.',
    'gCCharacterMovement_PS': 'Movement physics: speeds, acceleration, jump, climb, water depths.',
    'gCScriptRoutine_PS': 'The AI routine: current state, daily routine, waypoints, timestamps (run-time state mostly).',
    'gCParty_PS': 'Party membership (`PartyName`, `PartyMemberType`, `IsLeader`, `Waiting`).',
    'gCDialog_PS': 'Dialogue state of the NPC (infos are separate objects, see `risen.dialog`).',
    'gCInteraction_PS': 'Interaction with the world: focus, scripts for talk/loot/touch, slots.',
    'gCSkills_PS': '**Not reflected** (no properties): hit points, mana, level, attributes, protection, combat and misc skills. See "Skills".',
}


def summary(entry):
    typ, vals = entry['type'], entry['values']
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


out = ['# NPC and monster properties', '',
       'Generated by `tools/gen_npc_properties_doc.py` from the engine\'s own reflection (`mods/npcdump`): every one of the '
       f'{len(templates)} NPC and monster templates (`NPC/...`, `Monster/...`, without `NPC/Test` and the head parts), all property sets and all properties with their shipped values, '
       'plus the hit points, level, attributes and protection values that the property mechanism cannot see (`gCSkills_PS`, read with `risen.npc.stats`). '
       'Read/write: `risen.npc.stats(name)`, `risen.npc.set_stats(name, {...})`, declaratively `risen.balance.apply { npcs = {...}, npc_groups = {...} }` '
       '([`LUA_API.md`](LUA_API.md) "Balancing NPCs"); any reflected property with `risen.template.patch`. Findings and offsets: [`RE.md`](RE.md) section 24.', '']

out += ['## Where the numbers live', '',
        '| What | Where | `risen.balance` setting |', '|---|---|---|',
        '| hit points (maximum / current), mana | `gCSkills_PS` skills `MaxHitPoints` (5), `HitPoints` (0), `MaxManaPoints` (6), `ManaPoints` (1) | `hp_max`, `hp`, `hp_mult`, `mana_max`, `mana`, `mana_mult` |',
        '| level | `gCSkills_PS` skill `Level` (2); only drives the experience reward | `level`, `level_add`, `level_mult` |',
        '| experience a kill gives | **derived**: the `GiveXP` script pays `10 * level`, at least 50 (`IsSmall` species Chicken/Nautilus: exactly `10 * level`) | `xp` (sets the level), `xp_mult` |',
        '| attributes | `gCSkills_PS` `Strength` (7), `Dexterity` (8), `Intelligence` (9), 0..200 | `skills = { Strength = 40 }`, `skills_mult`, `skills_add` |',
        '| protection | `gCSkills_PS` `ProtEdge`/`ProtBlunt`/`ProtPoint`/`ProtFire`/`ProtIce`/`ProtMagic` (10..15): a hit of damage type T is reduced by the victim\'s protection T | `skills = { ProtEdge = 60 }` |',
        '| combat skills | `gCSkills_PS` `Sword`, `Axe`, `Staff`, `Bow`, `CrossBow` (16..20), 0..10; `Circle`, `Fireball`, ... (21..24) | `skills` |',
        '| melee damage | **derived**: weapon `DamageMultiplier * (DamageAmount + attacker Strength)` minus the victim\'s protection; animals/monsters hit with their combat species\' *fist items* (`It_Fist_Monster` has `DamageAmount` 0, so a bite = Strength) | `damage_mult` (Strength), the fist/weapon items via the `items` section |',
        '| the NPC\'s own damage | `gCDamage_PS.DamageAmount/DamageType/DamageScript` (only `DoSpecialMonsterDamage`-style scripts: Titan, Inferno, ...) | `damage`, `damage_type`, `damage_mult` |',
        '| guild, species, attitude, bravery, damage calculation | `gCNPC_PS` `Guild`, `Species`, `AttitudeLock`, `BraveryOverride`, `DamageCalculationType` (`Immortal` = damage 0 per the decompiled scripts, untested in a fight) | `guild`, `species`, `attitude`, `bravery`, `damage_calc` |',
        '| fight style | `gCCombatSystem_PS.CombatSpecies` (+ the species object: `risen.npc.combat_species(name)`), `ActiveFightAI`, `PassiveFightAI` | `combat_species` |',
        '| inventory / loot | `gCInventory_PS` stacks (not reflected): `risen.npc.template_inventory/template_give/template_take` | `loot = { It_Apple = 3 }` |',
        '| anything else | any property of any set above | `properties = { gCNPC_PS = { Group = "x" } }` |', '']

out += ['## Skills (gCSkills_PS)', '',
        'Not reflected: 34 base values (`gESkill` 0..33) at `+0x20 + 4 * skill` and 34 modification values (worn items, effects) at `+0xa8 + 4 * skill`. The template stores base values only. '
        'Allowed range per skill (the engine clamps writes; `max <= 0` = unlimited; hit/mana points are bounded by their maximum):', '',
        '| id | Skill | min | max |', '|---|---|---|---|']
for i, name, lo, hi in limits:
    shown = {0: 'MaxHitPoints', 1: 'MaxManaPoints'}.get(i)
    out.append(f'| {i} | `{name}` | {lo} | {("= " + shown) if shown else ("unlimited" if hi <= 0 else hi)} |')
out.append('')

# ---- per group statistics ---------------------------------------------------------------------------------------------------------------
def med(xs):
    return int(statistics.median(xs)) if xs else 0


out += ['### Shipped values by template group', '', '| Group | Templates | hp_max (min / median / max) | level | Strength | ProtEdge | xp reward |', '|---|---|---|---|---|---|---|']
for g in sorted(groups):
    rows = [stats[t] for t in groups[g] if t in stats]
    if not rows:
        continue

    def rng(key):
        xs = [r[key] for r in rows]
        return f'{min(xs)} / {med(xs)} / {max(xs)}'
    out.append(f'| {g} | {len(rows)} | {rng("hp_max")} | {rng("level")} | {rng("Strength")} | {rng("ProtEdge")} | {rng("xp_reward")} |')
out.append('')

out += ['### Monsters and notable NPCs', '', 'Base values as shipped (`Monster/*` and a few representative humans). Fist items are the item templates the combat species hits with '
        '(their `gCDamage_PS.DamageAmount` is added to Strength).', '',
        '| Template | hp_max | level | xp | Str | Dex | Int | ProtEdge | Sword/Axe | Guild | Species | Right fist / left fist |', '|---|---|---|---|---|---|---|---|---|---|---|---|']
reps = [t for t in templates if group_of.get(t, '').startswith('Monster/') and t in stats and not group_of[t].endswith('Heads')]
reps += [t for t in ('Peasant_1_01', 'Townsguard_01', 'Inq_Guard_01', 'Inq_Novice_Fighter_01', 'Pirate_S_01', 'Pirate_L_01', 'Don_Henchman_S_01', 'Don_Henchman_L_01')
         if t in stats]
vals = {}
for line in open(log_path, encoding='utf-8', errors='replace'):
    m = re.search(r' NPC (\S+) gCNPC_PS (Guild|Species):[^=]*=(.*)$', line.rstrip())
    if m:
        vals[(m.group(1), m.group(2))] = m.group(3).split('_', 1)[-1]
for t in reps:
    s = stats[t]
    species, right, left = combat.get(t, ('-', '-', '-'))
    out.append(f'| `{t}` | {s["hp_max"]} | {s["level"]} | {s["xp_reward"]} | {s["Strength"]} | {s["Dexterity"]} | {s["Intelligence"]} | {s["ProtEdge"]} | '
               f'{s["Sword"]}/{s["Axe"]} | {vals.get((t, "Guild"), "")} | {vals.get((t, "Species"), "")} | `{right}` / `{left}` ({species}) |')
out.append('')

fists = Counter((v[1], v[2]) for v in combat.values())
out += ['### Combat species and fist items', '', '| Right fist | Left fist | NPC templates |', '|---|---|---|']
for (r, l), n in fists.most_common():
    out.append(f'| `{r}` | `{l}` | {n} |')
out.append('')

inv = [(t, loot[t]) for t in templates if loot.get(t)]
out += ['### Templates that carry items', '', f'{len(inv)} of {len(templates)} templates start with items in their inventory (most NPCs get theirs from scripts and the level data): '
        + '; '.join(f'`{t}`: ' + ', '.join(f'{i} x{n}' for i, n in items) for t, items in inv[:40]) + (' ...' if len(inv) > 40 else ''), '']

# ---- reflected property sets ---------------------------------------------------------------------------------------------------------------
out += ['## Reflected property sets', '', '| Property set | Templates | Purpose |', '|---|---|---|']
for st in sorted(props, key=lambda s: (s in VISUAL, -len({t for t in templates if s in sets_of[t]}), s)):
    n = sum(1 for t in templates if st in sets_of[t])
    purpose = PURPOSE.get(st, 'visual / physics / navigation (not relevant for balancing)' if st in VISUAL else '')
    out.append(f'| `{st}` | {n} | {purpose} |')
out += ['| `gCSkills_PS` | all | ' + PURPOSE['gCSkills_PS'] + ' |', '']

for st in sorted(props, key=lambda s: (s in VISUAL, s)):
    if st in VISUAL:
        continue
    n = sum(1 for t in templates if st in sets_of[t])
    out += [f'### {st}', '', f'{n} templates. {PURPOSE.get(st, "")}'.strip(), '', '| Property | Type | Templates | Values seen |', '|---|---|---|---|']
    for name, entry in props[st].items():
        out.append(f'| `{name}` | `{entry["type"]}` | {len(entry["templates"])} | {summary(entry)} |')
    out.append('')

out += ['### Visual / physics / navigation sets', '',
        ', '.join(f'`{s}`' for s in sorted(props) if s in VISUAL) + '. They have no influence on the numbers above. '
        'Properties: ' + '; '.join(f'`{s}`: ' + ', '.join(f'`{p}`' for p in props[s]) for s in sorted(props) if s in VISUAL), '']

sys.stdout.reconfigure(encoding='utf-8')
print('\n'.join(out).replace('�', '?'))
