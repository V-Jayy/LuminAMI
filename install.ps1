[CmdletBinding()]
param(
    [string]$Version,
    [string]$Directory = (Join-Path $env:LOCALAPPDATA 'Programs\LuminAMI'),
    [switch]$NoPath,
    [int]$WaitForProcess = 0,
    [string]$LogPath
)
$ErrorActionPreference = 'Stop'
if ($LogPath) { Start-Transcript -LiteralPath $LogPath -Append | Out-Null }
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('LuminAMI-install-' + [guid]::NewGuid().ToString('N'))
try {
    if ($WaitForProcess -gt 0) { Wait-Process -Id $WaitForProcess -ErrorAction SilentlyContinue }
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $Directory = [IO.Path]::GetFullPath($Directory)
    if ($Directory.TrimEnd('\') -eq [IO.Path]::GetPathRoot($Directory).TrimEnd('\')) {
        throw 'Choose an installation folder, not a drive root.'
    }
    $endpoint = 'https://api.github.com/repos/V-Jayy/LuminAMI/releases/'
    if ($Version) {
        if ($Version -notmatch '^v?\d+\.\d+\.\d+$') { throw 'Use a version such as 0.2.0.' }
        $endpoint += 'tags/v' + $Version.TrimStart('v')
    } else { $endpoint += 'latest' }
    $release = Invoke-RestMethod -Uri $endpoint -Headers @{ 'User-Agent' = 'LuminAMI-installer' }
    if ($release.tag_name -notmatch '^v(\d+\.\d+\.\d+)$') { throw 'Unexpected release version.' }
    $expectedVersion = $Matches[1]
    $name = "LuminAMI-v$expectedVersion-windows-x64.zip"
    $archiveAsset = @($release.assets | Where-Object name -eq $name)
    $hashAsset = @($release.assets | Where-Object name -eq 'SHA256SUMS.txt')
    if ($archiveAsset.Count -ne 1 -or $hashAsset.Count -ne 1) { throw 'Release assets are missing.' }
    foreach ($asset in @($archiveAsset[0], $hashAsset[0])) {
        if ($asset.browser_download_url -notlike 'https://github.com/V-Jayy/LuminAMI/releases/download/*') {
            throw 'Unexpected release download URL.'
        }
    }
    New-Item -ItemType Directory -Path $temporary | Out-Null
    $zipPath = Join-Path $temporary $name
    $hashPath = Join-Path $temporary 'SHA256SUMS.txt'
    Invoke-WebRequest -UseBasicParsing -Uri $archiveAsset[0].browser_download_url -OutFile $zipPath
    Invoke-WebRequest -UseBasicParsing -Uri $hashAsset[0].browser_download_url -OutFile $hashPath
    $pattern = '^([a-fA-F0-9]{64})\s+\*?' + [regex]::Escape($name) + '$'
    $lines = @(Get-Content -LiteralPath $hashPath | Where-Object { $_ -match $pattern })
    if ($lines.Count -ne 1) { throw 'Release checksum is missing or ambiguous.' }
    $null = $lines[0] -match $pattern
    $expectedHash = $Matches[1]
    $stream = [IO.File]::OpenRead($zipPath)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try { $actualHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '') }
    finally { $stream.Dispose(); $sha256.Dispose() }
    if ($actualHash -ne $expectedHash) {
        throw 'Release checksum did not match. Nothing was installed.'
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $unpacked = Join-Path $temporary 'unpacked'
    $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $identities = @{}
        foreach ($entry in $zip.Entries) {
            $relative = $entry.FullName.Replace('/', '\')
            if ([IO.Path]::IsPathRooted($relative) -or $relative.Contains(':') -or
                @($relative.Split('\') | Where-Object { $_ -eq '..' -or $_ -eq '.' }).Count -gt 0) {
                throw 'Release archive contains an invalid path.'
            }
            if ($identities.ContainsKey($relative)) { throw 'Release archive contains duplicate paths.' }
            $identities[$relative] = $true
        }
    } finally { $zip.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $unpacked)
    $executable = Join-Path $unpacked 'LuminAMI.exe'
    $actualVersion = & $executable --version
    if ($LASTEXITCODE -ne 0 -or $actualVersion -ne "LuminAMI $expectedVersion") { throw 'Release executable version mismatch.' }
    New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    # Save only the files being replaced, so a failed copy can restore the previous install.
    $backup = Join-Path $temporary 'previous'
    $published = @()
    try {
        foreach ($file in Get-ChildItem -LiteralPath $unpacked -File -Recurse) {
            $relative = $file.FullName.Substring($unpacked.Length + 1)
            $destination = Join-Path $Directory $relative
            $previous = Join-Path $backup $relative
            if (Test-Path -LiteralPath $destination) {
                New-Item -ItemType Directory -Path (Split-Path -Parent $previous) -Force | Out-Null
                Copy-Item -LiteralPath $destination -Destination $previous
            }
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            $published += $relative
            Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        }
    } catch {
        foreach ($relative in $published) {
            $destination = Join-Path $Directory $relative
            $previous = Join-Path $backup $relative
            if (Test-Path -LiteralPath $previous) { Copy-Item -LiteralPath $previous -Destination $destination -Force }
            elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination }
        }
        throw
    }
    $installed = Join-Path $Directory 'LuminAMI.exe'
    $stateDirectory = if ($env:LUMINAMI_CONFIG_DIR) { $env:LUMINAMI_CONFIG_DIR } else { Join-Path $env:LOCALAPPDATA 'LuminAMI' }
    if (-not (Test-Path -LiteralPath (Join-Path $stateDirectory 'driver.json'))) {
        & $installed install-drivers
        if ($LASTEXITCODE -ne 0) { throw 'Installed, but driver file setup failed. Run LuminAMI install-drivers.' }
    }
    if (-not $NoPath) {
        $userPath = [string][Environment]::GetEnvironmentVariable('Path', 'User')
        if (@($userPath -split ';' | Where-Object { $_.TrimEnd('\') -eq $Directory.TrimEnd('\') }).Count -eq 0) {
            [Environment]::SetEnvironmentVariable('Path', ($userPath.TrimEnd(';') + ';' + $Directory).TrimStart(';'), 'User')
        }
        if (@($env:Path -split ';' | Where-Object { $_.TrimEnd('\') -eq $Directory.TrimEnd('\') }).Count -eq 0) {
            $env:Path = $Directory + ';' + $env:Path
        }
    }
    Write-Output "Installed LuminAMI $expectedVersion to $Directory"
    Write-Output 'Run LuminAMI -h. Open a new terminal if PATH has not refreshed.'
} finally {
    if (Test-Path -LiteralPath $temporary) {
        $resolvedTemporary = [IO.Path]::GetFullPath($temporary)
        $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if ($resolvedTemporary.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $resolvedTemporary) -like 'LuminAMI-install-*') {
            Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force
        }
    }
    if ($LogPath) { Stop-Transcript | Out-Null }
}
