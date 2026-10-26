$ErrorActionPreference = 'Stop'
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString())
try {
    $game = Join-Path $temporary 'steamapps\common\Test Game'
    $exe = Join-Path $game 'game\bin\win64\cs2.exe'
    New-Item -ItemType Directory -Force (Split-Path -Parent $exe) | Out-Null
    Set-Content -LiteralPath $exe -Value 'fixture file, not an executable'
    $hashBefore = (Get-FileHash -LiteralPath $exe).Hash
    $manifest = Join-Path $temporary 'steamapps\appmanifest_730.acf'
    Set-Content -LiteralPath $manifest -Value '"AppState" { "buildid" "123456" "LastOwner" "not-for-report" }'
    $output = Join-Path $temporary 'report.json'
    $script = Join-Path $PSScriptRoot '..\tools\Collect-CS2Info.ps1'
    & $script -GameDirectory $game -OutputPath $output
    $json = Get-Content -LiteralPath $output -Raw
    $report = $json | ConvertFrom-Json
    if ($report.steam_build_id -ne '123456' -or $report.files.Count -ne 4) { throw 'Incorrect report metadata' }
    if (-not $report.files[0].present -or $report.files[1].present) { throw 'Incorrect missing-module handling' }
    if ($report.files[0].sha256 -ne $hashBefore.ToLowerInvariant()) { throw 'Incorrect checksum' }
    if ((Get-FileHash -LiteralPath $exe).Hash -ne $hashBefore) { throw 'Game file was changed' }
    if ($json.Contains('not-for-report') -or $json.Contains('Test Game')) { throw 'Private metadata leaked' }
    $rejected = $false
    try { & $script -GameDirectory $game -OutputPath $exe } catch { $rejected = $true }
    if (-not $rejected -or (Get-FileHash -LiteralPath $exe).Hash -ne $hashBefore) { throw 'Must reject writing into game folder' }
    $rejected = $false
    try { & $script -GameDirectory (Join-Path $temporary 'missing') -OutputPath $output } catch { $rejected = $true }
    if (-not $rejected) { throw 'Must reject missing installation' }
    Write-Host 'Client information report tests passed.'
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}
