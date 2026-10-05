[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Driver,
    [string]$Workspace = (Join-Path (Get-Location) 'LuminAMI-backup'),
    [string]$Executable
)
. (Join-Path $PSScriptRoot 'common.ps1')
$exe = Get-LuminAmiExecutable $Executable
Assert-LuminAmiDriver $Driver
$capture = Join-Path $Workspace 'capture'
foreach ($required in @((Join-Path $capture 'capture.json'), (Join-Path $capture 'hii.bin'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Backup file is missing: $required" }
}
$runId = Get-LuminAmiRunId
$journal = Join-Path $Workspace "restore-$runId.json"
Invoke-LuminAmi $exe @(
    'restore', '--driver', (Resolve-Path -LiteralPath $Driver).Path,
    '--capture', $capture, '--journal', $journal,
    '--report', (Join-Path $Workspace "restore-$runId-result.json")
)
Write-Host "Restore finished. Journal: $journal"
