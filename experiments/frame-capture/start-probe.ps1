# SPDX-License-Identifier: MIT
param(
    [Parameter(Mandatory=$true)][string]$GameExecutable,
    [string]$RenderDocRoot,
    [ValidateRange(30, 300)][int]$TimeoutSeconds = 180,
    [switch]$SteamDevelopmentContext
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (Get-Process -Name CrimsonDesert -ErrorAction SilentlyContinue) {
    throw 'Close the game normally before starting a controlled fresh-launch capture.'
}
$gameFile = Get-Item -LiteralPath $GameExecutable
if ($gameFile.Name -ne 'CrimsonDesert.exe') { throw 'Unexpected executable name.' }
$gameHash = (Get-FileHash -LiteralPath $gameFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
if ($gameHash -ne '25f24e06a1f886e9310fd290f9462f66b480a5e7763d1fe156bfddaa8d1992b9') {
    throw 'Game build differs from the observed G1 baseline; fingerprint and review the new build first.'
}
if (!$RenderDocRoot) {
    $RenderDocRoot = Join-Path $repository 'out/tools/renderdoc-1.47/portable/RenderDoc_1.47_64'
}
$toolRoot = (Resolve-Path -LiteralPath $RenderDocRoot).Path
$pinned = @{
    'qrenderdoc.exe' = '787e3c60c4777ae46cd6c9847be49599ba5c083c554d102502b080c03d5c2f14'
    'renderdoc.dll' = 'a81e3f5fda89039886f34ed6d34b6718a399cb230204d43011d90687c4da4726'
}
foreach ($name in $pinned.Keys) {
    $toolFile = Join-Path $toolRoot $name
    if ((Get-FileHash -LiteralPath $toolFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pinned[$name]) {
        throw "Expected the reviewed RenderDoc 1.47 $name."
    }
    if ((Get-AuthenticodeSignature -LiteralPath $toolFile).Status -ne 'Valid') {
        throw "Tool signature verification failed: $name."
    }
}
$uiConfig = Join-Path $env:APPDATA 'qrenderdoc/UI.config'
if (!(Test-Path -LiteralPath $uiConfig) -or !(Get-Content -LiteralPath $uiConfig -Raw | ConvertFrom-Json).Analytics_TotalOptOut) {
    throw 'Configure the external tool privacy preferences before automated use; existing settings are not changed here.'
}
$case = Join-Path $repository ('out/g1/frame-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($case) | Out-Null
$script = Join-Path $PSScriptRoot 'capture_once.py'
$config = @{
    schema_version = 1
    mode = 'game_title'
    executable = $gameFile.FullName
    executable_sha256 = $gameHash
    script = $script
    timeout_seconds = $TimeoutSeconds
}
if ($SteamDevelopmentContext) {
    if (!(Get-Process -Name steam -ErrorAction SilentlyContinue)) { throw 'The ordinary Steam client must be running.' }
    # Vendor-documented development hint; no file is written into the game directory.
    [IO.File]::WriteAllText((Join-Path $case 'steam_appid.txt'), '3321460', [Text.UTF8Encoding]::new($false))
    $config.launch_context = 'steam_development_appid'
}
$configPath = Join-Path $case 'local-config.json'
[IO.File]::WriteAllText($configPath, (($config | ConvertTo-Json -Depth 4) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
$priorConfig = $env:ODV_FRAME_CAPTURE_CONFIG
try {
    $env:ODV_FRAME_CAPTURE_CONFIG = $configPath
    $process = Start-Process -FilePath (Join-Path $toolRoot 'qrenderdoc.exe') -ArgumentList @('--python',('"'+$script+'"')) -WindowStyle Hidden -PassThru
    $context = @{helper_process_id=$process.Id; helper_start_utc=$process.StartTime.ToUniversalTime().ToString('o'); privacy='LOCAL ONLY: do not publish process or path context.'}
    [IO.File]::WriteAllText((Join-Path $case 'helper-context.json'), (($context | ConvertTo-Json) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
} finally {
    $env:ODV_FRAME_CAPTURE_CONFIG = $priorConfig
}
Write-Output "Controlled capture helper started: $case"
Write-Output 'Wait for READY_FOR_FRAME, verify the title menu, then create capture-now.flag in that directory.'
Write-Output 'Keep raw captures, local configuration, contexts and errors local. No game support is inferred.'
