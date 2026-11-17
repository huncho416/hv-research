<#
.SYNOPSIS
  Provision a DEDICATED research VM for the hv-research experiments and record
  its security configuration into a config block for the certificate evidence.

.DESCRIPTION
  This does NOT bypass any protection. It (1) snapshots the current VBS/HVCI/
  Secure Boot/Hyper-V state via msinfo-equivalent queries, and (2) optionally
  flips test-signing so you can load the research driver. Flipping test-signing
  is a deliberate, logged configuration choice - it relaxes Secure Boot and must
  be recorded in the experiment config block.

.NOTES
  Run elevated, inside the research VM only. Reboot required after -EnableTestSigning.
#>
[CmdletBinding()]
param(
    [switch]$EnableTestSigning,
    [switch]$DisableTestSigning,
    [string]$OutFile = "$PSScriptRoot\..\captures\vm-config.txt"
)

function Get-VbsState {
    $g = Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard `
        -ClassName Win32_DeviceGuard -ErrorAction SilentlyContinue
    [pscustomobject]@{
        VbsStatus             = $g.VirtualizationBasedSecurityStatus  # 0 off,1 enabled-not-running,2 running
        SecurityServicesRunning = ($g.SecurityServicesRunning -join ',')  # 2 = HVCI
        RequiredSecurityProperties = ($g.RequiredSecurityProperties -join ',')
    }
}

$null = New-Item -ItemType Directory -Force -Path (Split-Path $OutFile)

$vbs = Get-VbsState
$secureBoot = try { Confirm-SecureBootUEFI } catch { 'unknown' }
$hyperv = (Get-CimInstance Win32_ComputerSystem).HypervisorPresent
$winver = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').DisplayVersion
$cpu = (Get-CimInstance Win32_Processor).Name

$block = @"
==== hv-research VM config block ($(Get-Date -Format o)) ====
CPU                         : $cpu
Windows display version     : $winver
Secure Boot enabled         : $secureBoot
HypervisorPresent           : $hyperv
VBS status (0/1/2)          : $($vbs.VbsStatus)
Security services running   : $($vbs.SecurityServicesRunning)   # 2 => HVCI/Memory Integrity
Required security props     : $($vbs.RequiredSecurityProperties)
Test signing requested now  : $EnableTestSigning
"@

$block | Tee-Object -FilePath $OutFile -Append

if ($EnableTestSigning) {
    Write-Warning 'Enabling test-signing. This relaxes Secure Boot driver enforcement. Record it as part of the experiment configuration.'
    bcdedit /set testsigning on | Out-Null
    Write-Host 'Reboot required for test-signing to take effect.'
}
if ($DisableTestSigning) {
    bcdedit /set testsigning off | Out-Null
    Write-Host 'Test-signing disabled. Reboot to restore.'
}
