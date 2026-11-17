<#
.SYNOPSIS
  Load the driver, run the E0-E4 protocol, and collect evidence into captures/.

.DESCRIPTION
  Orchestrates hvctl.exe for a full run and writes a timestamped transcript.
  Run twice: once with VBS/HVCI ON (Config A) and once OFF (Config B), then
  compare. The driver (hvresearch.sys) and hvctl.exe must already be built.

  TargetVa should be an executable kernel code address you have chosen for E2/E3
  on the research VM (e.g. obtained from a kernel debugger: x nt!NtCreateFile).
  Do NOT point this at third-party protected software - the target is Windows.
#>
[CmdletBinding()]
param(
    [string]$Sys     = "$PSScriptRoot\..\driver\x64\Release\hvresearch.sys",
    [string]$Hvctl   = "$PSScriptRoot\..\tools\hvctl\x64\Release\hvctl.exe",
    [Parameter(Mandatory)] [string]$TargetVa,   # hex, e.g. fffff80312345678
    [string]$ConfigLabel = 'A',
    [string]$OutDir  = "$PSScriptRoot\..\captures"
)

$ErrorActionPreference = 'Stop'
$null = New-Item -ItemType Directory -Force -Path $OutDir
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $OutDir "run-$ConfigLabel-$stamp.log"

function Log($m) { $m | Tee-Object -FilePath $log -Append }

Log "== hv-research run (config $ConfigLabel) $stamp =="

# Create/start the service for the research driver.
sc.exe create hvresearch type= kernel binPath= $Sys | Out-Null
sc.exe start  hvresearch | Out-Null
Start-Sleep -Milliseconds 300

try {
    Log "--- E0 probe ---";              Log (& $Hvctl probe)
    Log "--- E1 virtualize ---";         Log (& $Hvctl on)
    Log "--- stats after bring-up ---";  Log (& $Hvctl stats)
    Log "--- E2 read kernel ---";        Log (& $Hvctl read $TargetVa 32)
    Log "--- E3 write exec page ---";    Log (& $Hvctl write $TargetVa 90)
    Log "--- E4 npt hook demo ---";      Log (& $Hvctl hook $TargetVa)
    Log "--- stats after experiments ---"; Log (& $Hvctl stats)
    Log "--- devirtualize ---";          Log (& $Hvctl off)
}
finally {
    sc.exe stop   hvresearch | Out-Null
    sc.exe delete hvresearch | Out-Null
}

Log "== done; transcript at $log =="
Write-Host "Saved: $log"
