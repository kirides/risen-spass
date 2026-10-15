<#
.SYNOPSIS
    End-to-end smoke test: launches the isolated test copy, plays two scenarios and asserts on the lines the mod layer writes
    to spass.log:
      persistence - new game -> runtime content -> quick save -> change -> quick load -> new game (mods/persistdemo)
      fetchquest  - the example quest played through by mods/fetchquest_driver

.DESCRIPTION
    Needs a built sentry.dll and the test copy (see CLAUDE.md); takes ~3 minutes per scenario. Exit code 0 = every assertion held.

.PARAMETER GameDir   Test copy of Risen (never the real install).
.PARAMETER Seconds   How long the game runs per scenario (fetchquest gets 60 s more).
.PARAMETER Scenario  persistence, fetchquest, commands, branch, templates, balance, npcs or all.
#>
param(
    [string]$GameDir = 'B:\DevDrive\risen-test',
    [int]$Seconds = 200,
    # persistence | fetchquest | commands | branch | templates | balance | npcs | all
    [ValidateSet('persistence', 'fetchquest', 'commands', 'branch', 'templates', 'balance', 'npcs', 'all')][string]$Scenario = 'all'
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$log = Join-Path $GameDir 'bin\spass\spass.log'

$totalFailed = 0
function Run-Checks($title, $text, $checks) {
    Write-Host "== $title"
    $failed = 0
    foreach ($c in $checks) {
        if ($text -match $c[1]) { Write-Host ("  ok    " + $c[0]) }
        else { Write-Host ("  FAIL  " + $c[0]) -ForegroundColor Red; $failed++ }
    }
    Write-Host ("{0} of {1} checks passed" -f ($checks.Count - $failed), $checks.Count)
    return $failed
}

# Each check: description, regex that must match the log.
$checks = @(
    @('mod layer started',                     'game ready: starting Lua runtime'),
    @('all engine symbols resolved',           'symbols: \d+ resolved in this pass, 0 waiting'),
    @('session.start(new) fired',              'eventlog session\.start\(new\)'),
    @('world.changed fired',                   'eventlog world\.changed\(World\)'),
    @('runtime content created',               'persistdemo \[created\]: entity=Entity\(SPASS_Pers.*quest=true.*info=true.*runs=1 marker=from-first-session'),
    @('quest.status_changed fired',            'eventlog quest\.status_changed\(Q_SPASS_Pers, Running, Open\)'),
    @('savegame written',                      'save: saved ''QuickSave'''),
    @('save.after reported success',           'eventlog save\.after\(QuickSave, true\)'),
    @('mod data was changed before the load',  'persistdemo \[before load\].*marker=changed-after-save'),
    @('load.before fired before load.after',   '(?s)eventlog load\.before\(QuickSave\).*eventlog load\.after\(QuickSave, true\)'),
    @('mod data rolled back by the load',      'persistdemo \[load\.after QuickSave ok=true\].*marker=from-first-session'),
    @('quest state survived the load',         'persistdemo \[load\.after QuickSave ok=true\].*quest=true status=gEQuestStatus_Running log_entries=1'),
    @('info "given" flag survived the load',   'persistdemo \[load\.after QuickSave ok=true\].*given=true'),
    @('old entity handle is gone after load',  'persistdemo \[load\.after QuickSave ok=true\]: entity=nil'),
    @('session.start(load) fired',             'eventlog session\.start\(load\)'),
    @('new game removes runtime quests',       'persistdemo \[session\.start\(new\) before ensure\]: entity=nil quest=false status=nil.*runs=nil'),
    @('no Lua errors',                         '^(?!(?s).*\[error\])')
)

if ($Scenario -in 'persistence', 'all') {
    & (Join-Path $here 'test-run.ps1') -GameDir $GameDir -Mods persistdemo, eventlog -Seconds $Seconds -Filter 'NOTHING' | Out-Null
    if (-not (Test-Path $log)) { throw "no log at $log" }
    $totalFailed += Run-Checks 'persistence and events' (Get-Content $log -Raw) $checks
}

if ($Scenario -in 'fetchquest', 'all') {
    & (Join-Path $here 'test-run.ps1') -GameDir $GameDir -Mods fetchquest, fetchquest_driver -Seconds ($Seconds + 60) -Filter 'NOTHING' | Out-Null
    $fetch = @(
        @('Marta was spawned',                      'driver: Marta exists, quest status = gEQuestStatus_Open'),
        @('the work option started the quest',      'driver: quest running, log entries = 1'),
        @('Marta is spawned again after a load',    'driver: after load: Marta respawned=true same_spot=true'),
        @('quest and dialogue options survive',     'driver: after load: .*quest=gEQuestStatus_Running infos=true/true'),
        @('hand-in with too few apples is refused', 'driver: not enough apples: status=gEQuestStatus_Running .*apples=1'),
        @('hand-in with 3 apples completes it',     'driver: DONE status=gEQuestStatus_Success apples=0 machetes=1'),
        @('no Lua errors',                          '^(?!(?s).*\[error\])')
    )
    $totalFailed += Run-Checks 'fetch quest example' (Get-Content $log -Raw) $fetch
}

if ($Scenario -in 'commands', 'all') {
    & (Join-Path $here 'test-run.ps1') -GameDir $GameDir -Mods dialogcmds -Seconds ($Seconds - 20) -Filter 'NOTHING' | Out-Null
    $cmds = @(
        @('invalid command lines are refused',     'cmds: invalid specs refused: true'),
        @('unknown command names the problem',     'unknown command ''NoSuchCommand'''),
        @('unknown parameter names the problem',   'command GiveXP has no parameter ''Count'''),
        @('command parameters read back',          'cmds: command 2 Amount=2 Item=It_Apple'),
        @('Give delivered the items',              'cmds: after: apples=2 event=true'),
        @('no Lua errors',                         '^(?!(?s).*\[error\])')
    )
    $totalFailed += Run-Checks 'dialogue commands' (Get-Content $log -Raw) $cmds
}

if ($Scenario -in 'branch', 'all') {
    & (Join-Path $here 'test-run.ps1') -GameDir $GameDir -Mods dialogbranch -Seconds ($Seconds + 60) -Filter 'NOTHING' | Out-Null
    $branch = @(
        @('only the parent is offered at the top',  '(?m)branch: top-level options: PANKRATZX2_00004,SPASS_Br_Town\r?$'),
        @('the children replace the menu',          'branch: the children are offered: SPASS_Br_Back,SPASS_Br_Harbor,SPASS_Br_Monastery'),
        @('a child answers and the menu stays',     'branch: after the child: SPASS_Br_Back,SPASS_Br_Harbor,SPASS_Br_Monastery running=true'),
        @('Back returns to the top level',          'branch: back at the top level: PANKRATZX2_00004,SPASS_Br_Town'),
        @('no Lua errors',                          '^(?!(?s).*\[error\])')
    )
    $totalFailed += Run-Checks 'branching dialogue' (Get-Content $log -Raw) $branch
}

if ($Scenario -in 'templates', 'all') {
    & (Join-Path $here 'test-run.ps1') -GameDir $GameDir -Mods templatepatch, templatepatch_driver -Seconds ($Seconds + 40) -Filter 'NOTHING' | Out-Null
    $tpl = @(
        @('templates resolve from the first tick on',   'tpdriver available@mods\.loaded false(?s).*tpdriver available@first tick true'),
        @('a new spawn uses the edited template',       'templatepatch: verify spawn: value=777'),
        @('an inventory stack follows the template',    'templatepatch: verify inventory stack \d+: value=777'),
        @('existing instances were patched',            'templatepatch: verify instances: n=\d+ values=777(,777)*'),
        @('an NPC template property applies to spawns', 'templatepatch: verify npc: guild=4'),
        @('patch with instances=true updates the world','tpdriver after patch n=\d+ count=\d+ values=\{999/a1x\d+\} template_value=999'),
        @('the targeted NPC patch keeps hp and group',  'tpdriver npc after targeted patch \[1 guild=gEGuild_Pir hp=7/180 stacks=1 grp=TPG\]'),
        @('the full patch_instances resets them',       'after full patch_instances.*\[1 guild=gEGuild_Pir hp=180/180 stacks=1 grp=\]'),
        @('a full patch also resets item amounts',      'tpdriver gold amount before patch_entity 25.*(?s).*tpdriver gold amount after patch_entity 1'),
        @('saved values win over an edited template',   'tpdriver after load \(template 555\) n=\d+ count=\d+ values=\{999/a1x\d+\} template_value=555'),
        @('patching after the load updates them',       'tpdriver after patch n=\d+ count=\d+ values=\{555/a1x\d+\} template_value=555'),
        @('a new game world does not follow the edit',  'tpdriver second new game .* values=\{(?!555)\d+/a1x\d+\} template_value=555'),
        @('no Lua errors',                              '^(?!(?s).*\[error\])')
    )
    $totalFailed += Run-Checks 'template editing' (Get-Content $log -Raw) $tpl
}

if ($Scenario -in 'balance', 'all') {
    & (Join-Path $here 'test-run.ps1') -GameDir $GameDir -Mods balance_items, balance_driver -Seconds ($Seconds + 40) -Filter 'NOTHING' | Out-Null
    $bal = @(
        @('the spec is applied on the first tick',        'balance: first tick: applied 7 of 7 entries'),
        @('session.start writes the live entities',       'balance: session\.start \(new\): applied 7 of 7 entries, \d+ live entities written'),
        @('the template carries the new numbers',         'balance_items: template sword: requires Strength=5 value 120 damage 25'),
        @('the armor modifiers read back by name',        'balance_items: template armor: modifies ProtBlunt\+25,ProtEdge\+35,ProtFire\+10,ProtIce\+10,ProtMagic\+10,ProtPoint\+20'),
        @('a rune: mana cost property and circle requirement', 'balance_items: template rune: mana 60 requires Circle=3'),
        @('a new spawn uses the new numbers',             'balance_items: spawn sword: requires Strength=5 value 120 damage 25'),
        @('an inventory stack follows the template',      'balance_items: inventory armor: requires Strength=5 modifies ProtBlunt\+25,ProtEdge\+35.* value 1500'),
        @('every world berry was rewritten',              'balance_items: world berries: n=(\d+) patched=\1\b'),
        @('the engine check passes with enough Strength', 'balance_items: can_use sword \(needs 5\): true'),
        @('the engine check fails for 999 Strength',      'balance_items: can_use two-hander \(needs 999\): false'),
        @('raising the requirement fails the check',      'balance_items: can_use sword after requires Strength=500: false'),
        @('lowering the requirement passes the check',    'balance_items: can_use sword after requires Strength=1: true'),
        @('wearing armor applies its modifiers',          'balance_items: equip: equip_all=1 ProtEdge after 35 delta 35'),
        @('a lowered requirement lets the engine equip it', 'balance_items: equip: after lowering the two-hander requirement equip_all=2'),
        @('an entity-level edit leaves the template alone', 'balance_items: entity edit: true entity Dexterity=3 template Strength=5'),
        @('using a potion applies its modifiers',         'balance_items: potion: hp 10 -> 50 max 80 potions left 0'),
        @('a bad skill name is refused with the list',    "balance_items: bad spec: true items\.It_1H_Sword: requires: unknown skill 'Strenght' \(valid: Strength, Dexterity"),
        @('an unknown template and section are refused',  "balance_items: bad spec 2: true items\.It_Nope: unknown template .*unknown section 'spells'"),
        @('a rejected spec writes nothing',               'balance_items: sword value after the rejected specs 120'),
        @('apply with instances rewrites the world',      'baldriver after apply \S+ n=\d+ instances=\{77\|Strength\+7\}x\d+ template=\{77\|Strength\+7\}'),
        @('saved values win over an edited template',     'baldriver after load .*instances=\{77\|Strength\+7\}x\d+ template=\{1\|\}'),
        @('a new game world keeps the shipped values',    'baldriver second new game .*instances=\{(?!77)[^}]*\}x\d+ template=\{1\|\}'),
        @('no Lua errors',                                '^(?!(?s).*\[error\])')
    )
    $totalFailed += Run-Checks 'item balancing' (Get-Content $log -Raw) $bal
}

if ($Scenario -in 'npcs', 'all') {
    & (Join-Path $here 'test-run.ps1') -GameDir $GameDir -Mods balance_npcs, balance_npcs_driver -Seconds ($Seconds + 100) -Filter 'NOTHING' | Out-Null
    $npc = @(
        @('the spec is applied on the first tick',          'balance: first tick: applied 7 of 7 entries'),
        @('session.start writes the live NPCs',             'balance: session\.start \(new\): applied 7 of 7 entries, \d+ live entities written'),
        @('absolute settings reach the template',           'balance_npcs: template Wolf hp=500/500 level=20 str=90 prot_edge=60 xp=200 guild=None bravery=Brave'),
        @('relative settings are computed from the shipped values', 'balance_npcs: template Boar hp=360/360 level=8 str=30 prot_edge=24 xp=80 guild=None bravery=None \(shipped hp 180 level 6\)'),
        @('a pattern group scales hit points and damage',   'balance_npcs: template Stingrat hp=180/180 level=4 str=40 .*\(shipped str 20\)'),
        @('a template group path works',                    'balance_npcs: template Ghul hp=390/390 level=14 .*\(shipped level 13\)'),
        @('guild, attributes and loot of a human',          'balance_npcs: template Peasant_1_01 hp=300/300 level=5 str=50 prot_edge=20 xp=50 guild=Cit'),
        @('the template inventory carries the loot',        'balance_npcs: template loot Peasant_1_01: It_Applex3'),
        @('any property by name',                           'balance_npcs: template Gnome role: balanced by balance_npcs'),
        @('item groups scale the shipped gold value',       'balance_npcs: potion value 15 shipped 30 expected 15'),
        @('a new spawn has the template numbers',           'balance_npcs: spawn Wolf hp=500/500 level=20 str=90 prot_edge=60 xp=200 guild=None bravery=Brave'),
        @('...also through the entity API',                 'balance_npcs: spawn wolf \(entity API\) hp 500 / 500 level 20 str 90 90'),
        @('the engine pays 10 * level for a kill',          'balance_npcs: GiveXP wolf: 1 experience 0 -> 200 200'),
        @('spawns carry the loot',                          'balance_npcs: spawn peasant guild 4 apples 3 hp 300'),
        @('every world wolf was rewritten',                 'balance_npcs: world Wolf n=(\d+) \{500/20/90\}x\1\b'),
        @('every world boar was rewritten',                 'balance_npcs: world Boar n=(\d+) \{360/8/30\}x\1\b'),
        @('every world stingrat was rewritten',             'balance_npcs: world Stingrat n=(\d+) \{180/4/40\}x\1\b'),
        @('applying again keeps a damaged NPC damaged',     'balance_npcs: damaged wolf after second apply hp 7 / 500 full wolf 500 / 500'),
        @('applying again does not compound (template)',    'balance_npcs: after second apply Boar template hp 360 level 8'),
        @('applying again does not compound (world)',       'balance_npcs: after second apply world Boar n=(\d+) \{360/8/30\}x\1\b'),
        @('set_stats on an entity touches only it',         'balance_npcs: entity set_stats: 1 nil boar hp 77 / 77 level 3 prot_fire 5 template Boar hp 360'),
        @('conflicting settings are refused',               'bad spec conflict: true npcs\.Wolf: hp_max and hp_mult exclude each other'),
        @('unknown settings are refused',                   "bad spec unknown key: true npcs\.Wolf: unknown setting 'health' \(valid: ai_group, attitude"),
        @('unknown skills are refused with the list',       "bad spec unknown skill: true npcs\.Wolf: skills: unknown skill 'Strenght' \(valid: HitPoints"),
        @('out-of-range values are refused',                'bad spec range: true npcs\.Wolf: Strength: 999 is outside the allowed range 0\.\.200'),
        @('non-NPC templates are refused',                  'bad spec not an NPC: true npcs\.It_1H_Sword: not an NPC template'),
        @('unknown enum names are refused',                 "bad spec unknown guild: true npcs\.Wolf: guild: unknown value 'Pirates' \(valid: None, Don"),
        @('an empty group is refused',                      'bad spec empty group: true npc_groups\[1\]: matches no NPC template'),
        @('rejected specs write nothing',                   'balance_npcs: wolf template hp after the rejected specs 500 \(was 500\)'),
        @('combat: the shipped wolf bites for Strength - protection', 'balance_npcs: fight shipped: first bite 10 \(wolf strength 30 - victim ProtEdge 20 = 10\)'),
        @('combat: the balanced wolf bites harder',         'balance_npcs: fight balanced: first bite 70 \(wolf strength 90 - victim ProtEdge 20 = 70\)'),
        @('the edit survives spawn init and AI ticks',      'balance_npcs: later spawn wolf hp 500 / 500 level 20 str 90 state ZS_'),
        @('driver: set_stats writes the live NPCs',         'npcdriver after set_stats \(one NPC damaged to 3\) \S+ n=(\d+) instances=\{3/(\d+)\|(\d+)\|(\d+)\}x1,\{\2/\2\|\3\|\4\}x\d+ template=\{\2\|\3\|\4\}'),
        @('driver: saved values win over a reverted template', 'npcdriver after load \(saved values vs the reverted template\) \S+ n=\d+ instances=\{3/(\d+)\|[^}]*\}x1,\{\1/\1\|[^}]*\}x\d+ template=\{(?!\1\|)\d+\|'),
        @('driver: a new game world keeps the shipped values', 'npcdriver second new game .*instances=\{(\d+)/\1\|\d+\|\d+\}x\d+ template=\{(?!\1\|)\d+\|'),
        @('driver: propagation fixes the new world',        'npcdriver propagated into the new world (\d+) \S+ n=\1 instances=\{(\d+)/\2\|\d+\|\d+\}x\1 template=\{\2\|'),
        @('no Lua errors',                                  '^(?!(?s).*\[error\])')
    )
    $totalFailed += Run-Checks 'NPC balancing' (Get-Content $log -Raw) $npc
}

if ($totalFailed -gt 0) { Write-Host "$totalFailed check(s) failed, see $log"; exit 1 }
Write-Host 'all scenarios passed'
