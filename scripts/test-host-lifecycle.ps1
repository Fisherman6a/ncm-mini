[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'artifacts\win11-integration\NCMMini.exe'
if (Get-Process -Name NCMMini -ErrorAction SilentlyContinue) { throw 'Close the existing NCMMini before lifecycle tests.' }
function Invoke-Host([string]$Arguments, [int]$Expected) {
    $p = Start-Process -FilePath $exe -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (-not $p.WaitForExit(15000)) { throw "Host timeout: $Arguments; no forced termination was used." }
    if ($p.ExitCode -ne $Expected) { throw "Expected exit $Expected, received $($p.ExitCode): $Arguments" }
}
Invoke-Host '--taskbar invalid' 2
Invoke-Host '--taskbar' 2
Invoke-Host '--duration invalid' 2
Invoke-Host '--taskbar-status' 1
Write-Output 'PASS invalid options and absent status do not launch a player'
$args = '--taskbar win11 --no-show-band --no-launch --keep-player --no-cover-download --no-lyrics --duration 8'
$hostProcess = Start-Process -FilePath $exe -ArgumentList $args -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Milliseconds 1200
    Invoke-Host '--taskbar-status' 3
    Write-Output 'PASS hidden modern instance reports waiting/hidden'
    Invoke-Host '--taskbar deskband --no-launch --keep-player' 0
    Start-Sleep -Milliseconds 1000
    Invoke-Host '--taskbar-status' 0
    Write-Output 'PASS duplicate launch requests display from existing mode, despite conflicting new mode'
    if (@(Get-Process -Name NCMMini).Count -ne 1) { throw 'Duplicate host detected.' }
    if (-not $hostProcess.WaitForExit(15000)) { throw 'Duration exit timed out; no forced termination was used.' }
    if ($hostProcess.ExitCode -ne 0) { throw "Host failed: $($hostProcess.ExitCode)" }
    Write-Output 'PASS duration exit with command worker sleeping'
} finally {
    if (-not $hostProcess.HasExited) { [void]$hostProcess.WaitForExit(15000) }
}
foreach ($iteration in 1..3) {
    Invoke-Host '--taskbar win11 --no-show-band --no-launch --keep-player --no-cover-download --no-lyrics --duration 1' 0
}
Invoke-Host '--taskbar-status' 1
Write-Output 'PASS repeated startup/shutdown leaves no host or controller'
