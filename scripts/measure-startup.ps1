<#
.SYNOPSIS
    Measures engine start-up variants in the test copy, one game at a time (mods/startupprobe): time to session.start(new), frame rate, wall time.

.DESCRIPTION
    Every variant is run -Repeat times, strictly sequentially (test-run.ps1 kills every Risen.exe). Variants that change files of the test copy
    (ConfigUser.xml, data\ini\ConfigLocal.xml) restore them afterwards. Results: docs/DEVELOPMENT.md "Startup time".

.PARAMETER Variant  baseline | noaudio | res640 | nowindow | maxfps | fps30 | lowgfx | all (comma separated in a -Command call)
#>
param(
    [string]$GameDir = 'B:\DevDrive\risen-test',
    [string[]]$Variant = @('baseline'),
    [int]$Repeat = 2,
    [int]$Seconds = 150
)
$ErrorActionPreference = 'Stop'
if ($GameDir -like 'B:\SteamLibrary*') { throw 'refusing to touch the real install' }
$userXml = Join-Path $GameDir 'localappdata\Local\Risen\ConfigUser.xml'
$localXml = Join-Path $GameDir 'data\ini\ConfigLocal.xml'
$log = Join-Path $GameDir 'bin\spass\spass.log'

$variants = [ordered]@{
    baseline = @{ Args = @() }
    noaudio  = @{ Args = @('-noaudio') }
    res640   = @{ Args = @('-res=640x360') }
    nowindow = @{ Args = @(); User = { param($x) [regex]::Replace($x, '(?s)\s*<Window\b.*?</Window>', '') } }
    maxfps   = @{ Args = @(); Local = { param($x) $x -replace '(?s)(<Engine>)', '$1<Timer MaxFPS="1000" MinFPS="2"/>' } }
    fps30    = @{ Args = @(); Local = { param($x) $x -replace '(?s)(<Engine>)', '$1<Timer MaxFPS="30"/>' } }   # control: proves that the Timer key is read
    fps30u   = @{ Args = @(); User = { param($x) $x -replace '(?s)(<Engine>)', '$1<Timer MaxFPS="30"/>' } }   # control: the same in ConfigUser.xml
    lowgfx   = @{ Args = @(); User = { param($x) $x -replace 'ViewDistance="3"', 'ViewDistance="1"' -replace 'Quality="High"', 'Quality="Low"' } }
}
$names = if ($Variant -contains 'all') { @($variants.Keys) } else { @($Variant | ForEach-Object { $_ -split ',' }) }

$origUser = [IO.File]::ReadAllText($userXml)
$origLocal = [IO.File]::ReadAllText($localXml)
$rows = @()
try {
    # repetitions are the outer loop: the variants alternate (A B A B), so slow drift of the machine does not favour one of them
    for ($i = 1; $i -le $Repeat; $i++) {
        foreach ($name in $names) {
            $v = $variants[$name]
            if ($null -eq $v) { throw "unknown variant $name" }
            [IO.File]::WriteAllText($userXml, $(if ($v.User) { & $v.User $origUser } else { $origUser }))
            [IO.File]::WriteAllText($localXml, $(if ($v.Local) { & $v.Local $origLocal } else { $origLocal }))
            $testArgs = @{ GameDir = $GameDir; Mods = @('startupprobe'); WaitForResult = $true; Scenario = 'startupprobe'; Seconds = $Seconds; Filter = 'NOTHING' }
            if ($v.Args.Count -gt 0) { $testArgs.ExtraArgs = $v.Args }
            & (Join-Path $PSScriptRoot 'test-run.ps1') @testArgs | Out-Null
            $run = $global:SpassRun
            $probe = if (Test-Path $log) { (Select-String -Path $log -Pattern '\[probe\]' | Select-Object -Last 1).Line } else { '' }
            $fps = if ($probe -match 'game_fps=([\d.]+)') { [double]$Matches[1] } else { $null }
            $row = [pscustomobject]@{ Variant = $name; Run = $i; Lua = $run.Timing.lua_started; FirstTick = $run.Timing.first_tick; SessionStart = $run.Timing.session_start; Result = $run.Timing.result; GameFps = $fps; Reason = $run.Reason }
            $rows += $row
            Write-Host ("{0,-9} #{1}: lua {2} | first tick {3} | session.start {4} | result {5} | {6} fps | {7}" -f $row.Variant, $i, $row.Lua, $row.FirstTick, $row.SessionStart, $row.Result, $row.GameFps, $row.Reason)
        }
    }
}
finally {
    Get-Process Risen -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($userXml, $origUser)
    [IO.File]::WriteAllText($localXml, $origLocal)
}
Write-Host ''
$rows | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
foreach ($g in ($rows | Group-Object Variant)) {
    $ss = ($g.Group | Where-Object { $null -ne $_.SessionStart } | Measure-Object SessionStart -Average).Average
    $res = ($g.Group | Where-Object { $null -ne $_.Result } | Measure-Object Result -Average).Average
    Write-Host ("{0,-9} mean session.start {1:N1} s, mean result {2:N1} s" -f $g.Name, $ss, $res)
}
