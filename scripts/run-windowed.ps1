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
.PARAMETER AllowFocus  Let the game take the foreground as usual. By default the window is moved without activating it
                     (SW_SHOWNOACTIVATE, SWP_NOACTIVATE); the game itself stays in the background through config.toml
                     [debug] background (test-run.ps1 writes it). Either way the run reports when a window of the game became
                     the foreground window or the mouse cursor was confined ($global:SpassRun.TookFocus / ClippedCursor).
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
    # really resize the window to -Width/-Height after the start (buggy in the game: misaligned UI and mouse)
    [switch]$Resize,
    # extra Risen.exe command line switches (e.g. '-noaudio', '-res=640x360')
    [string[]]$ExtraArgs = @(),
    [string]$ResultFile = '',
    [int]$StallSeconds = 0,
    # Save a PNG of the game window N seconds after this regex first appears in spass.log
    [string]$ScreenshotOn = '',
    [double]$ScreenshotDelay = 3,
    [string]$ScreenshotPath = (Join-Path $env:TEMP 'risen_shot.png'),
    [switch]$AllowFocus,
    # the executable in bin to start: a renamed copy of Risen.exe (e.g. RisenTest.exe) escapes per-executable driver
    # profiles (AMD Adrenalin application profiles key on the file name)
    [string]$Exe = 'Risen.exe'
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
# own type (not added to RisenWin): a PowerShell session that already loaded an older RisenWin could not redefine it
if (-not ('RisenFocus' -as [type])) {
    Add-Type @'
using System; using System.Runtime.InteropServices;
public static class RisenFocus {
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool GetClipCursor(out RECT r);
    [DllImport("user32.dll")] static extern int GetSystemMetrics(int i);
    struct RECT { public int L, T, R, B; }
    // process id of the foreground window (0: none)
    public static uint ForegroundPid() { uint pid = 0; IntPtr h = GetForegroundWindow(); if (h != IntPtr.Zero) GetWindowThreadProcessId(h, out pid); return pid; }
    // the cursor is confined to less than the virtual screen (some program called ClipCursor)
    public static bool CursorClipped() {
        RECT r; if (!GetClipCursor(out r)) return false;
        int x = GetSystemMetrics(76), y = GetSystemMetrics(77), w = GetSystemMetrics(78), h = GetSystemMetrics(79);
        return r.L > x || r.T > y || r.R < x + w || r.B < y + h;
    }
}
'@
}

$bin = Join-Path $GameDir 'bin'
$log = Join-Path $bin 'spass\spass.log'

# Risen calls SteamAPI_RestartAppIfNecessary(); if it ever returns true the Steam client relaunches the game from
# the REAL install path (without this test copy's proxy). Setting SteamAppId makes the Steam API treat the process
# as already started by Steam, which prevents that restart.
$env:SteamAppId = '40300'
$env:SteamGameId = '40300'
$started = Get-Date
$p = Start-Process -FilePath (Join-Path $bin $Exe) -ArgumentList (@('-nosplash') + $ExtraArgs) -WorkingDirectory $bin -PassThru
$global:SpassRun = [pscustomobject]@{ Reason = 'seconds'; Seconds = 0.0; Pid = $p.Id; ExitCode = $null; TookFocus = $null; ClippedCursor = $null }
# a program that confined the cursor before the start is not the game's doing
$clippedBefore = [RisenFocus]::CursorClipped()

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
        # focus watch: seconds after the start at which a game window first was the foreground window / the cursor was confined
        if ($null -eq $global:SpassRun.TookFocus -and [RisenFocus]::ForegroundPid() -eq [uint32]$p.Id) { $global:SpassRun.TookFocus = [math]::Round(($now - $started).TotalSeconds, 1) }
        if ($null -eq $global:SpassRun.ClippedCursor -and -not $clippedBefore -and [RisenFocus]::CursorClipped()) { $global:SpassRun.ClippedCursor = [math]::Round(($now - $started).TotalSeconds, 1) }
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
            # SW_RESTORE activates the window (the posted request runs in the game's thread, with its right to take the foreground):
            # SW_SHOWNOACTIVATE unless -AllowFocus
            [RisenWin]::ShowWindowAsync($p.MainWindowHandle, $(if ($AllowFocus) { 9 } else { 4 })) | Out-Null
            # SWP_NOZORDER | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS (+ SWP_NOACTIVATE); the size is only changed with -Resize (the game starts at the size
            # from ConfigUser.xml, a runtime resize misaligns its UI and mouse), otherwise the window is just moved (SWP_NOSIZE)
            $flags = 0x0004 -bor 0x0040 -bor 0x4000
            if (-not $AllowFocus) { $flags = $flags -bor 0x0010 }
            if (-not $Resize) { $flags = $flags -bor 0x0001 }
            [RisenWin]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, $WindowX, $WindowY, $Width, $Height, $flags) | Out-Null
            $resized = $true
        }
    }
    $global:SpassRun.Reason = $reason
    $global:SpassRun.Seconds = ((Get-Date) - $started).TotalSeconds
    # the exit code of a game that ended by itself (0xC0000005 = access violation, 0xC0000409 = fast fail, ... : a crash); test-run.ps1 reports it
    if ($reason -eq 'exited') { try { $global:SpassRun.ExitCode = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$p.ExitCode), 0) } catch { } }
    Write-Host ("game alive: {0}, window resized: {1}, ended by: {2} after {3:N0} s" -f (-not $p.HasExited), $resized, $reason, $global:SpassRun.Seconds)
    $focusNote = @()
    if ($null -ne $global:SpassRun.TookFocus) { $focusNote += "a game window was the foreground window $($global:SpassRun.TookFocus) s after the start" }
    if ($null -ne $global:SpassRun.ClippedCursor) { $focusNote += "the mouse cursor was confined $($global:SpassRun.ClippedCursor) s after the start" }
    Write-Host ('focus: ' + $(if ($focusNote.Count -gt 0) { ($focusNote -join '; ') + $(if ($AllowFocus) { ' (-AllowFocus)' } else { ' (or the user clicked into it)' }) } else { 'the game stayed in the background, the cursor was never confined' }))
    if (Test-Path $log) { Get-Content $log }
}
finally {
    # also on Ctrl-C or an error: never leave the game behind
    if (-not $KeepRunning -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; $p.WaitForExit(30000) | Out-Null }
}
