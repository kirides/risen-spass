<#
.SYNOPSIS
  Live check of risen.vegetation (docs/RE.md section 64): the plants around the start before, after erasing and after scattering new ones
  (bin\spass\screenshots\veg_*.png).
#>
& "$PSScriptRoot\test-run.ps1" -Mods vegprobe -Instance 2 -WaitForResult -Seconds 200 -Filter 'vegprobe|\[error\]'
