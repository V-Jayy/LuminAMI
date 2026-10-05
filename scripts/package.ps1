[CmdletBinding()]
param([string]$Version = '0.2.2')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be MAJOR.MINOR.PATCH.' }
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $root 'build.ps1') -Test
$actualVersion = & (Join-Path $root 'build\LuminAMI.exe') --version
if ($LASTEXITCODE -ne 0 -or $actualVersion -ne "LuminAMI $Version") {
    throw 'Package version does not match the executable.'
}
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$name = "LuminAMI-v$Version-windows-x64"
$staging = Join-Path $dist ($name + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $staging | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'build\LuminAMI.exe') -Destination $staging
foreach ($file in @('README.md', 'LICENSE', 'install.ps1', 'Export.cmd', 'Import.cmd', 'Restore.cmd', 'LuminAMI.cmd', 'InstallDrivers.cmd')) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $staging
}
foreach ($directory in @('scripts', 'docs', 'third_party', 'drivers')) {
    New-Item -ItemType Directory -Path (Join-Path $staging $directory) | Out-Null
}
foreach ($script in @('common.ps1', 'export.ps1', 'import.ps1', 'restore.ps1', 'menu.ps1', 'install-drivers.ps1', 'benchmark.ps1', 'benchmark-metrics.cs', 'render-benchmark.py')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination (Join-Path $staging 'scripts')
}
foreach ($doc in @('COMMANDS.md', 'DRIVERS.md', 'SETTINGS.md', 'COMPATIBILITY.md', 'BENCHMARK.md')) {
    Copy-Item -LiteralPath (Join-Path $root "docs\$doc") -Destination (Join-Path $staging 'docs')
}
Copy-Item -LiteralPath (Join-Path $root 'docs\benchmarks') -Destination (Join-Path $staging 'docs\benchmarks') -Recurse
Copy-Item -LiteralPath (Join-Path $root 'third_party\LICENSE-json.MIT') -Destination (Join-Path $staging 'third_party')
Copy-Item -LiteralPath (Join-Path $root 'drivers\README.md') -Destination (Join-Path $staging 'third_party\AMI-DRIVERS.md')
Copy-Item -LiteralPath (Join-Path $root 'drivers\README.md') -Destination (Join-Path $staging 'drivers\README.md')
$archive = Join-Path $dist "$name.zip"
if (Test-Path -LiteralPath $archive) { throw "Release ZIP already exists: $archive" }
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $archive
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Value "$hash  $name.zip" -Encoding ascii
Write-Output "Release: $archive"
Write-Output "SHA256: $hash"
