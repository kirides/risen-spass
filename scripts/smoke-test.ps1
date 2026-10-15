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
.PARAMETER Scenario  persistence, fetchquest, commands, branch, templates, balance or all.
#>
param(
    [string]$GameDir = 'B:\DevDrive\risen-test',
    [int]$Seconds = 200,
    # persistence | fetchquest | commands | branch | templates | balance | all
    [ValidateSet('persistence', 'fetchquest', 'commands', 'branch', 'templates', 'balance', 'all')][string]$Scenario = 'all'
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

if ($totalFailed -gt 0) { Write-Host "$totalFailed check(s) failed, see $log"; exit 1 }
Write-Host 'all scenarios passed'
