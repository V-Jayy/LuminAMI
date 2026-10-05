Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-LuminAmiExecutable {
    param([string]$Executable)
    if ($Executable) {
        if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
            throw "Executable not found: $Executable"
        }
        return (Resolve-Path -LiteralPath $Executable).Path
    }
    $root = Split-Path -Parent $PSScriptRoot
    foreach ($candidate in @((Join-Path $root 'LuminAMI.exe'), (Join-Path $root 'build\LuminAMI.exe'))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw 'LuminAMI.exe was not found. Extract the release ZIP or run build.ps1 first.'
}

function Invoke-LuminAmi {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "LuminAMI failed (exit code $LASTEXITCODE). See the error above." }
}

function Get-LuminAmiRunId {
    return (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
}

function Invoke-LuminAmiJson {
    param([string]$Executable, [string[]]$Arguments)
    $output = & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "LuminAMI failed (exit code $LASTEXITCODE). See the error above." }
    return ($output -join "`n" | ConvertFrom-Json)
}

function Get-LuminAmiDriver {
    param(
        [string]$Executable,
        [string]$Driver,
        [switch]$InstallDrivers,
        [switch]$NonInteractive,
        [string]$Directory,
        [switch]$Choose
    )
    if ($Driver -and $InstallDrivers) { throw 'Use either -Driver or -InstallDrivers.' }
    if ($Driver) {
        $result = Invoke-LuminAmiJson $Executable @('use-driver', '--driver', $Driver)
        return $result.driver
    }
    if (-not $InstallDrivers -and -not $Choose) {
        $status = Invoke-LuminAmiJson $Executable @('driver-status')
        if ($status.configured) { return $status.driver }
        if ($status.PSObject.Properties.Name -contains 'error') { Write-Host $status.error }
    }
    if (-not $InstallDrivers) {
        if ($NonInteractive -and -not $Choose) {
            throw 'No verified AMI driver is configured. Pass -Driver PATH or -InstallDrivers, or run InstallDrivers.cmd -NonInteractive first.'
        }
        if (-not $NonInteractive) {
            if ([Console]::IsInputRedirected) { throw 'Input is redirected. Pass -Driver PATH or -InstallDrivers for headless use.' }
            Write-Host '1  Install provided drivers (offline)'
            Write-Host '2  Use a custom driver path'
            Write-Host '0  Cancel'
            $choice = Read-Host 'Driver choice [1]'
            if ($choice -eq '2') {
                $custom = (Read-Host 'AMI driver path').Trim().Trim('"')
                if (-not $custom) { throw 'Driver selection cancelled.' }
                return (Invoke-LuminAmiJson $Executable @('use-driver', '--driver', $custom)).driver
            }
            if ($choice -ne '' -and $choice -ne '1') { throw 'Driver selection cancelled.' }
        }
    }
    $arguments = @('install-drivers')
    if ($Directory) { $arguments += @('--directory', $Directory) }
    $result = Invoke-LuminAmiJson $Executable $arguments
    Write-Host "Provided drivers installed. Selected: $($result.driver)"
    return $result.driver
}
