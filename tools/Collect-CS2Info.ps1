# Read-only client identification. Does not attach to CS2 or change game files.
[CmdletBinding()]
param(
    [string]$GameDirectory,
    [string]$OutputPath = (Join-Path $PSScriptRoot 'CS2-client-info.json')
)
$ErrorActionPreference = 'Stop'

if (-not $GameDirectory) {
    $steamRoots = @()
    foreach ($key in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')) {
        $registry = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if ($registry.SteamPath) { $steamRoots += $registry.SteamPath }
        if ($registry.InstallPath) { $steamRoots += $registry.InstallPath }
    }
    $libraries = @($steamRoots)
    foreach ($root in $steamRoots) {
        $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $vdf) {
            foreach ($match in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $libraries += $match.Groups[1].Value.Replace('\\', '\')
            }
        }
    }
    foreach ($library in ($libraries | Select-Object -Unique)) {
        $manifestPath = Join-Path $library 'steamapps\appmanifest_730.acf'
        if (-not (Test-Path -LiteralPath $manifestPath)) { continue }
        $manifest = Get-Content -LiteralPath $manifestPath -Raw
        $install = [regex]::Match($manifest, '"installdir"\s+"([^"]+)"')
        if (-not $install.Success) { continue }
        $candidate = Join-Path (Join-Path $library 'steamapps\common') $install.Groups[1].Value
        if (Test-Path -LiteralPath (Join-Path $candidate 'game\bin\win64\cs2.exe')) {
            $GameDirectory = $candidate
            break
        }
    }
}
if (-not $GameDirectory -or -not (Test-Path -LiteralPath (Join-Path $GameDirectory 'game\bin\win64\cs2.exe'))) {
    throw 'CS2 was not found. Run with -GameDirectory "D:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive" (use your installation folder).'
}
$GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path
$relativeFiles = @(
    'game/bin/win64/cs2.exe',
    'game/bin/win64/engine2.dll',
    'game/bin/win64/schemasystem.dll',
    'game/csgo/bin/win64/client.dll'
)
$outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
# Keep the report outside the installation, including when the caller supplies a path.
$gamePrefix = $GameDirectory.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if ($outputFullPath.StartsWith($gamePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Choose an output path outside the game installation.'
}
$files = foreach ($relative in $relativeFiles) {
    $path = Join-Path $GameDirectory $relative
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $file = Get-Item -LiteralPath $path
        [ordered]@{
            file = $relative
            present = $true
            bytes = $file.Length
            file_version = $file.VersionInfo.FileVersion
            product_version = $file.VersionInfo.ProductVersion
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    } else {
        [ordered]@{ file = $relative; present = $false }
    }
}
$steamApps = Split-Path -Parent (Split-Path -Parent $GameDirectory)
$appManifest = Join-Path $steamApps 'appmanifest_730.acf'
$buildId = $null
if (Test-Path -LiteralPath $appManifest) {
    $buildMatch = [regex]::Match((Get-Content -LiteralPath $appManifest -Raw), '"buildid"\s+"([0-9]+)"')
    if ($buildMatch.Success) { $buildId = $buildMatch.Groups[1].Value }
}
$report = [ordered]@{
    report_format = 1
    collected_utc = [DateTime]::UtcNow.ToString('o')
    steam_app_id = 730
    steam_build_id = $buildId
    os_version = [Environment]::OSVersion.Version.ToString()
    os_is_64_bit = [Environment]::Is64BitOperatingSystem
    files = @($files)
    note = 'Client identification only. No integration or compatibility claim. No game memory or account data read.'
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $outputFullPath -Encoding UTF8
Write-Host "Saved: $outputFullPath"
Write-Host 'The report contains module versions and checksums, not account details. Nothing is uploaded automatically.'
