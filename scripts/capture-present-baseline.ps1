# SPDX-License-Identifier: MIT
param(
    [string]$PresentMonPath,
    [string]$OutputDirectory,
    [ValidateRange(1, 300)][int]$DurationSeconds = 30,
    [ValidateRange(0, 2147483647)][int]$TargetProcessId = 0
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
if (!$PresentMonPath) {
    $PresentMonPath = Join-Path $repository 'out/tools/presentmon-2.6.0/PresentMon-2.6.0-x64.exe'
}
$toolPath = (Resolve-Path -LiteralPath $PresentMonPath).Path
$toolHash = (Get-FileHash -LiteralPath $toolPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($toolHash -ne 'b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af') {
    throw 'Expected the pinned official PresentMon 2.6.0 console executable; SHA256 mismatch.'
}
if ($TargetProcessId) {
    $target = Get-Process -Id $TargetProcessId
    if ($target.ProcessName -ne 'CrimsonDesert') { throw 'Target is not CrimsonDesert.exe.' }
} else {
    $targets = @(Get-Process -Name CrimsonDesert -ErrorAction SilentlyContinue)
    if ($targets.Count -ne 1) { throw 'Start the unmodified game normally; exactly one game process is required, or specify -TargetProcessId.' }
    $target = $targets[0]
}
$python = (Get-Command python.exe -ErrorAction Stop).Source
$gameFile = Get-Item -LiteralPath $target.Path
$gameHash = (Get-FileHash -LiteralPath $gameFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$summaryScript = Join-Path $PSScriptRoot 'summarize-presentmon.py'
$captureId = [Guid]::NewGuid().ToString('N')
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repository ('out/g1/capture-' + $captureId) }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) { throw 'Use a new output directory to preserve previous evidence.' }
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$csv = Join-Path $outputRoot 'raw.csv'
$log = Join-Path $outputRoot 'capture.log'
$summary = Join-Path $outputRoot 'summary.json'
$metadata = [ordered]@{
    schema_version = 1
    tool = 'PresentMon console'
    tool_version = '2.6.0'
    tool_sha256 = $toolHash
    requested_duration_seconds = $DurationSeconds
    target = 'CrimsonDesert.exe'
    game_executable_sha256 = $gameHash
    game_executable_size_bytes = $gameFile.Length
    game_file_version = $gameFile.VersionInfo.FileVersion
    capture_script_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
    summary_script_sha256 = (Get-FileHash -LiteralPath $summaryScript -Algorithm SHA256).Hash.ToLowerInvariant()
    capture_exit = $null
    summary_exit = $null
    status = 'STARTED'
}
$metadataPath = Join-Path $outputRoot 'capture-info.json'
try {
    # Unique session: do not stop another profiler, auto-elevate, inject, or track input.
    & $toolPath --process_id $target.Id --output_file $csv --session_name ('OpenDesertVR-' + $captureId) --timed $DurationSeconds --terminate_after_timed --no_console_stats --no_track_input *> $log
    $metadata.capture_exit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'PresentMon capture failed. Inspect capture.log; no presentation result is inferred.' }
    & $python $summaryScript $csv --process-id $target.Id --output $summary
    $metadata.summary_exit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'CSV summary failed; do not treat this capture as validated.' }
    $metadata.status = 'CAPTURED'
} catch {
    $metadata.status = 'FAILED'
    throw
} finally {
    [IO.File]::WriteAllText($metadataPath, (($metadata | ConvertTo-Json -Depth 4) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
}
Write-Output "Capture complete: $summary"
Write-Output 'Keep raw.csv and capture.log local; share summary.json and capture-info.json for review.'
