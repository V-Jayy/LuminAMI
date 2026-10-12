[CmdletBinding()]
param([switch]$SkipBuild)
# Stages dist\LuminAMI-portable (LuminAMI.exe + Export.bat) and zips it. The folder needs no install:
# Export.bat keeps the drivers, the driver choice and every export next to itself.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $root 'build.ps1') -Test }
$staging = Join-Path $root 'dist\LuminAMI-portable'
New-Item -ItemType Directory -Path $staging -Force | Out-Null
# Only these two files are replaced; exports already made inside the folder are left alone.
$files = @((Join-Path $root 'build\LuminAMI.exe'), (Join-Path $root 'portable\Export.bat'))
Copy-Item -LiteralPath $files -Destination $staging -Force
$archive = Join-Path $root 'dist\LuminAMI-portable.zip'
Compress-Archive -LiteralPath ($files | ForEach-Object { Join-Path $staging (Split-Path -Leaf $_) }) -DestinationPath $archive -Force
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "Portable folder: $staging"
Write-Output "Portable ZIP: $archive"
Write-Output "SHA256: $hash"
