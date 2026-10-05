[CmdletBinding()]
param(
    [string]$Workspace = (Join-Path (Get-Location) 'LuminAMI-backup'),
    [string]$Script,
    [string]$Driver,
    [switch]$Apply,
    [string]$Executable
)
. (Join-Path $PSScriptRoot 'common.ps1')
$exe = Get-LuminAmiExecutable $Executable
if (-not $Script) { $Script = Join-Path $Workspace 'BIOSSettings.txt' }
$capture = Join-Path $Workspace 'capture'
$original = Join-Path $Workspace 'OriginalSettings.txt'
foreach ($required in @($Script, $original, (Join-Path $capture 'capture.json'), (Join-Path $capture 'hii.bin'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Backup or settings file is missing: $required" }
}
if ($Apply) { Assert-LuminAmiDriver $Driver }
Invoke-LuminAmi $exe @('diff', '--before', $original, '--after', $Script)
$runId = Get-LuminAmiRunId
$plan = Join-Path $Workspace "plan-$runId.json"
Invoke-LuminAmi $exe @('import', '--capture', $capture, '--script', $Script, '--output', $plan)
if (-not $Apply) {
    Write-Host "Plan saved to $plan. No BIOS settings were written."
    Write-Host 'To apply it, rerun with -Driver <path> -Apply.'
    return
}
$journal = Join-Path $Workspace "import-$runId.json"
Invoke-LuminAmi $exe @(
    'import', '--driver', (Resolve-Path -LiteralPath $Driver).Path,
    '--capture', $capture, '--script', $Script, '--journal', $journal,
    '--report', (Join-Path $Workspace "import-$runId-result.json")
)
Write-Host "Import finished. Journal: $journal"
