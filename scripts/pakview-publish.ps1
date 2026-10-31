<#
.SYNOPSIS  Publishes PakView as a Native AOT executable for Windows x64 (no .NET runtime needed on the target machine).
.DESCRIPTION
  Needs the .NET 10 SDK and the Visual Studio "Desktop development with C++" workload (the native linker).
  The ILCompiler's link step runs the MSVC environment script, which looks for vswhere.exe on PATH; the Visual Studio Installer
  folder is not on PATH in a plain shell ("vswhere.exe ... nicht gefunden"), so it is added here.
  Output: dist\pakview-win-x64\PakView.exe + the native Skia / HarfBuzz / ANGLE libraries next to it (symbols removed).
#>
param(
    [string]$Output = (Join-Path $PSScriptRoot '..\dist\pakview-win-x64'),
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
if (Test-Path $installer) { $env:PATH = "$installer;$env:PATH" }

$proj = Join-Path $PSScriptRoot '..\src\pakview\PakView\PakView.csproj'
dotnet publish $proj -c $Configuration -r win-x64 -o $Output --nologo -v q
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed ($LASTEXITCODE)" }
Get-ChildItem $Output -Filter *.pdb | Remove-Item
$exe = Get-Item (Join-Path $Output 'PakView.exe')
'{0}: {1:N1} MiB, folder {2:N1} MiB' -f $exe.FullName, ($exe.Length / 1MB), ((Get-ChildItem $Output | Measure-Object Length -Sum).Sum / 1MB)
