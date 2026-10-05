param([Parameter(Mandatory = $true)][string]$Executable, [Parameter(Mandatory = $true)][string]$Fixtures)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $Fixtures 'GitHub installer tests'
New-Item -ItemType Directory -Path $work | Out-Null
$checks = 0
function Check([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { throw "Installer test failed: $Message" }
}
$payload = Join-Path $work 'payload'
New-Item -ItemType Directory -Path $payload | Out-Null
Copy-Item -LiteralPath $Executable -Destination (Join-Path $payload 'LuminAMI.exe')
Set-Content -LiteralPath (Join-Path $payload 'README.md') -Value 'new release'
$script:testZip = Join-Path $work 'LuminAMI-v0.2.2-windows-x64.zip'
Compress-Archive -Path (Join-Path $payload '*') -DestinationPath $script:testZip
$script:testHash = Join-Path $work 'SHA256SUMS.txt'
$goodHash = (Get-FileHash -LiteralPath $script:testZip).Hash
Set-Content -LiteralPath $script:testHash -Value "$goodHash  LuminAMI-v0.2.2-windows-x64.zip"
# Mock only GitHub I/O; run the actual checksum, extraction, update, and driver setup.
function Invoke-RestMethod {
    param($Uri, $Headers)
    return [pscustomobject]@{ tag_name = 'v0.2.2'; assets = @(
        [pscustomobject]@{name='LuminAMI-v0.2.2-windows-x64.zip'; browser_download_url='https://github.com/V-Jayy/LuminAMI/releases/download/v0.2.2/LuminAMI-v0.2.2-windows-x64.zip'},
        [pscustomobject]@{name='SHA256SUMS.txt'; browser_download_url='https://github.com/V-Jayy/LuminAMI/releases/download/v0.2.2/SHA256SUMS.txt'}
    ) }
}
$downloads = [pscustomobject]@{ Zip = $script:testZip; Hash = $script:testHash }
${function:Invoke-WebRequest} = {
    param([switch]$UseBasicParsing, $Uri, $OutFile)
    $source = if ($Uri.EndsWith('.zip')) { $downloads.Zip } else { $downloads.Hash }
    Copy-Item -LiteralPath $source -Destination $OutFile
}.GetNewClosure()
$previousConfig = $env:LUMINAMI_CONFIG_DIR
$previousPath = $env:PATH
$env:LUMINAMI_CONFIG_DIR = Join-Path $work 'driver-state'
try {
    $install = Join-Path $work 'installed app'
    & (Join-Path $root 'install.ps1') -Directory $install -NoPath | Out-Null
    Check ((& (Join-Path $install 'LuminAMI.exe') --version) -eq 'LuminAMI 0.2.2') 'Verified release installs into a spaced path'
    $status = & (Join-Path $install 'LuminAMI.exe') driver-status | ConvertFrom-Json
    Check ($status.configured) 'Clean install sets up embedded drivers without loading them'
    Check ($env:PATH -eq $previousPath) 'NoPath leaves process PATH unchanged'
    $custom = Join-Path $work 'custom.sys'
    Copy-Item -LiteralPath (Join-Path $env:LUMINAMI_CONFIG_DIR 'drivers\amifldrv64.sys') -Destination $custom
    & (Join-Path $install 'LuminAMI.exe') use-driver --driver $custom | Out-Null
    $configHash = (Get-FileHash -LiteralPath (Join-Path $env:LUMINAMI_CONFIG_DIR 'driver.json')).Hash
    Set-Content -LiteralPath (Join-Path $install 'README.md') -Value 'old release'
    & (Join-Path $root 'install.ps1') -Directory $install -NoPath | Out-Null
    Check ((Get-Content -LiteralPath (Join-Path $install 'README.md') -Raw).Trim() -eq 'new release') 'Update replaces managed release files'
    Check ((Get-FileHash -LiteralPath (Join-Path $env:LUMINAMI_CONFIG_DIR 'driver.json')).Hash -eq $configHash) 'Update preserves the saved custom driver'
    & {
        # Native background PowerShell may inherit a module path without this function.
        function Get-FileHash { throw 'Get-FileHash is unavailable in this process.' }
        & (Join-Path $root 'install.ps1') -Directory $install -NoPath | Out-Null
    }
    Check ((Get-Content -LiteralPath (Join-Path $install 'README.md') -Raw).Trim() -eq 'new release') 'Checksum verification works without Get-FileHash'
    Set-Content -LiteralPath $script:testHash -Value (('0' * 64) + '  LuminAMI-v0.2.2-windows-x64.zip')
    $rejected = $false
    try { & (Join-Path $root 'install.ps1') -Directory $install -NoPath | Out-Null }
    catch { $rejected = $_.Exception.Message -match 'checksum did not match' }
    Check $rejected 'Checksum mismatch is rejected before publication'
    Check ((Get-Content -LiteralPath (Join-Path $install 'README.md') -Raw).Trim() -eq 'new release') 'Rejected download preserves the installed files'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $script:testZip = Join-Path $work 'traversal.zip'
    $downloads.Zip = $script:testZip
    $zip = [IO.Compression.ZipFile]::Open($script:testZip, [IO.Compression.ZipArchiveMode]::Create)
    try { $null = $zip.CreateEntry('../escaped.txt') } finally { $zip.Dispose() }
    $badZipHash = (Get-FileHash -LiteralPath $script:testZip).Hash
    Set-Content -LiteralPath $script:testHash -Value "$badZipHash  LuminAMI-v0.2.2-windows-x64.zip"
    $rejected = $false
    try { & (Join-Path $root 'install.ps1') -Directory $install -NoPath | Out-Null }
    catch { $rejected = $_.Exception.Message -match 'invalid path' }
    Check $rejected 'Archive traversal is rejected before extraction'
    Check (-not (Test-Path -LiteralPath (Join-Path $work 'escaped.txt'))) 'Invalid archive publishes no escaped file'
    $global:LASTEXITCODE = 0
    Write-Output "PASS: $checks GitHub installer checks; network mocked, no driver loaded."
} finally { $env:LUMINAMI_CONFIG_DIR = $previousConfig }
