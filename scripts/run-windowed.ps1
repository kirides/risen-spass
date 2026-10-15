<#
.SYNOPSIS
    Development helper: launches Risen windowed, shrinks the window, waits, prints the mod-layer log and
    kills the game.

.DESCRIPTION
    Meant for a *test copy* of the game (see docs/DEVELOPMENT.md): the game rewrites its per-user config
    (%LOCALAPPDATA%\Risen\ConfigUser.xml) while running, so do not point this at an install whose
    settings you care about without a backup. The copy's data\ini\ConfigLocal.xml must contain
    FullScreen="false"; this script then resizes the (screen-sized) window to a small one.

    The wait ends at the first of: -Seconds elapsed, the game exited, -ResultFile appeared (risen.test.finish), or the
    watchdog fired (-StallSeconds without a new line in spass.log). The game is always killed on the way out (also on
    Ctrl-C or an error) unless -KeepRunning. The outcome is left in $global:SpassRun (Reason: result | exited | seconds |
    stall, Seconds, Pid).

.PARAMETER GameDir   Risen root directory (contains bin\Risen.exe).
.PARAMETER Seconds   Upper bound for the run.
.PARAMETER ResultFile  Path of spass\test-result.json: the run ends as soon as it exists.
.PARAMETER StallSeconds  Kill the game when spass.log did not grow for this long (0 = no watchdog).
.PARAMETER Width/Height  Size of the window the game is shrunk to.
#>
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [int]$Seconds = 90,
    [int]$Width = 1280,
    [int]$Height = 720,
    # top-left corner of the window (parallel instances get their own offset)
    [int]$WindowX = 100,
    [int]$WindowY = 100,
    [switch]$KeepRunning,
    [switch]$NoResize,
    # extra Risen.exe command line switches (e.g. '-noaudio', '-res=640x360')
    [string[]]$ExtraArgs = @(),
    [string]$ResultFile = '',
    [int]$StallSeconds = 0,
    # Save a PNG of the game window N seconds after this regex first appears in spass.log
    [string]$ScreenshotOn = '',
    [double]$ScreenshotDelay = 3,
    [string]$ScreenshotPath = (Join-Path $env:TEMP 'risen_shot.png')
)
$ErrorActionPreference = 'Stop'

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class RisenWin {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr h, int cmd);
    // WM_GETTEXT with a timeout: GetWindowText/Process.MainWindowTitle can block forever on a window whose game thread hangs
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr SendMessageTimeoutW(IntPtr h, uint msg, IntPtr wp, System.Text.StringBuilder lp, uint flags, uint timeoutMs, out IntPtr result);
    public static string TitleOf(IntPtr h) {
        var sb = new System.Text.StringBuilder(256); IntPtr r;
        if (SendMessageTimeoutW(h, 0x000D, (IntPtr)256, sb, 0x0002 /*SMTO_ABORTIFHUNG*/, 200, out r) == IntPtr.Zero) return "";
        return sb.ToString();
    }
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    public struct RECT { public int L, T, R, B; }
}
'@

$bin = Join-Path $GameDir 'bin'
$log = Join-Path $bin 'spass\spass.log'

# Risen calls SteamAPI_RestartAppIfNecessary(); if it ever returns true the Steam client relaunches the game from
# the REAL install path (without this test copy's proxy). Setting SteamAppId makes the Steam API treat the process
# as already started by Steam, which prevents that restart.
$env:SteamAppId = '40300'
$env:SteamGameId = '40300'
$started = Get-Date
$p = Start-Process -FilePath (Join-Path $bin 'Risen.exe') -ArgumentList (@('-nosplash') + $ExtraArgs) -WorkingDirectory $bin -PassThru
$global:SpassRun = [pscustomobject]@{ Reason = 'seconds'; Seconds = 0.0; Pid = $p.Id }

Add-Type -AssemblyName System.Drawing
$shotDue = $null
$shotDone = $false
$deadline = $started.AddSeconds($Seconds)
$resized = $false
$logLen = -1
$lastGrowth = Get-Date
$reason = 'seconds'
try {
    while ($true) {
        Start-Sleep -Milliseconds 100   # poll: the result file / exit is noticed within 0.1 s
        $p.Refresh()
        $now = Get-Date
        if ($p.HasExited) { $reason = 'exited'; break }
        if ($now -ge $deadline) { $reason = 'seconds'; break }
        if ($ResultFile -ne '' -and (Test-Path $ResultFile)) { $reason = 'result'; break }
        if ($StallSeconds -gt 0) {
            $len = if (Test-Path $log) { (Get-Item $log).Length } else { 0 }
            if ($len -ne $logLen) { $logLen = $len; $lastGrowth = $now }
            elseif (($now - $lastGrowth).TotalSeconds -ge $StallSeconds) { $reason = 'stall'; break }
        }
        if ($ScreenshotOn -ne '' -and -not $shotDone) {
            if ($null -eq $shotDue -and (Test-Path $log) -and (Select-String -Path $log -Pattern $ScreenshotOn -Quiet)) {
                $shotDue = $now.AddSeconds($ScreenshotDelay)
            }
            if ($null -ne $shotDue -and $now -ge $shotDue) {
                # PrintWindow(PW_RENDERFULLCONTENT) captures the DWM-composed window; CopyFromScreen shows black for D3D9.
                $rc = New-Object RisenWin+RECT
                [RisenWin]::GetWindowRect($p.MainWindowHandle, [ref]$rc) | Out-Null
                $bmp = New-Object System.Drawing.Bitmap ($rc.R - $rc.L), ($rc.B - $rc.T)
                $g = [System.Drawing.Graphics]::FromImage($bmp)
                $hdc = $g.GetHdc()
                [RisenWin]::PrintWindow($p.MainWindowHandle, $hdc, 2) | Out-Null
                $g.ReleaseHdc($hdc)
                $bmp.Save($ScreenshotPath, [System.Drawing.Imaging.ImageFormat]::Png)
                $g.Dispose(); $bmp.Dispose()
                $shotDone = $true
                Write-Host "screenshot saved: $ScreenshotPath"
            }
        }
        if (-not $resized -and -not $NoResize -and $p.MainWindowHandle -ne [IntPtr]::Zero -and [RisenWin]::TitleOf($p.MainWindowHandle) -like 'RISEN*') {
            # async variants: a game thread that hangs must never block this script (the watchdog has to be able to kill it)
            [RisenWin]::ShowWindowAsync($p.MainWindowHandle, 9) | Out-Null       # SW_RESTORE
            # SWP_NOZORDER | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS
            [RisenWin]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, $WindowX, $WindowY, $Width, $Height, 0x0004 -bor 0x0040 -bor 0x4000) | Out-Null
            $resized = $true
        }
    }
    $global:SpassRun.Reason = $reason
    $global:SpassRun.Seconds = ((Get-Date) - $started).TotalSeconds
    Write-Host ("game alive: {0}, window resized: {1}, ended by: {2} after {3:N0} s" -f (-not $p.HasExited), $resized, $reason, $global:SpassRun.Seconds)
    if (Test-Path $log) { Get-Content $log }
}
finally {
    # also on Ctrl-C or an error: never leave the game behind
    if (-not $KeepRunning -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; $p.WaitForExit(30000) | Out-Null }
}
