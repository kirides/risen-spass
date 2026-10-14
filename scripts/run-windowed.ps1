<#
.SYNOPSIS
    Development helper: launches Risen windowed, shrinks the window, waits, prints the mod-layer log and
    kills the game.

.DESCRIPTION
    Meant for a *test copy* of the game (see docs/DEVELOPMENT.md): the game rewrites its per-user config
    (%LOCALAPPDATA%\Risen\ConfigUser.xml) while running, so do not point this at an install whose
    settings you care about without a backup. The copy's data\ini\ConfigLocal.xml must contain
    FullScreen="false"; this script then resizes the (screen-sized) window to a small one.

.PARAMETER GameDir   Risen root directory (contains bin\Risen.exe).
.PARAMETER Seconds   How long to let the game run before collecting the log.
.PARAMETER Width/Height  Size of the window the game is shrunk to.
#>
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [int]$Seconds = 90,
    [int]$Width = 1280,
    [int]$Height = 720,
    [switch]$KeepRunning,
    [switch]$NoResize,
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
$p = Start-Process -FilePath (Join-Path $bin 'Risen.exe') -ArgumentList '-nosplash' -WorkingDirectory $bin -PassThru

Add-Type -AssemblyName System.Drawing
$shotDue = $null
$shotDone = $false
$deadline = (Get-Date).AddSeconds($Seconds)
$resized = $false
while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
    Start-Sleep -Milliseconds 500
    $p.Refresh()
    if ($ScreenshotOn -ne '' -and -not $shotDone) {
        if ($null -eq $shotDue -and (Test-Path $log) -and (Select-String -Path $log -Pattern $ScreenshotOn -Quiet)) {
            $shotDue = (Get-Date).AddSeconds($ScreenshotDelay)
        }
        if ($null -ne $shotDue -and (Get-Date) -ge $shotDue) {
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
    if (-not $resized -and -not $NoResize -and $p.MainWindowHandle -ne [IntPtr]::Zero -and $p.MainWindowTitle -like 'RISEN*') {
        [RisenWin]::ShowWindow($p.MainWindowHandle, 9) | Out-Null            # SW_RESTORE
        # SWP_NOZORDER | SWP_SHOWWINDOW
        [RisenWin]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, 100, 100, $Width, $Height, 0x0004 -bor 0x0040) | Out-Null
        $resized = $true
    }
}
Write-Host ("game alive: {0}, window resized: {1}" -f (-not $p.HasExited), $resized)
if (Test-Path $log) { Get-Content $log }
if (-not $KeepRunning -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force; $p.WaitForExit(30000) | Out-Null }
