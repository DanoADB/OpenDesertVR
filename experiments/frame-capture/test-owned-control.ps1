# SPDX-License-Identifier: MIT
param([ValidateSet('capture','disconnect','timeout')][string]$Scenario = 'capture')
$ErrorActionPreference = 'Stop'
$repository = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$fixture = Join-Path $repository 'out/build/capture-lab/Release/odv_present_fixture.exe'
if (Get-Process -Name odv_present_fixture -ErrorAction SilentlyContinue) {
    throw 'An owned fixture is already running; wait for its normal exit before testing.'
}
$toolRoot = Join-Path $repository 'out/tools/renderdoc-1.47/portable/RenderDoc_1.47_64'
$pins = @{
    'qrenderdoc.exe'='787e3c60c4777ae46cd6c9847be49599ba5c083c554d102502b080c03d5c2f14'
    'renderdoc.dll'='a81e3f5fda89039886f34ed6d34b6718a399cb230204d43011d90687c4da4726'
}
foreach ($name in $pins.Keys) {
    $path = Join-Path $toolRoot $name
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pins[$name] -or
        (Get-AuthenticodeSignature -LiteralPath $path).Status -ne 'Valid') {
        throw "Reviewed external tool identity check failed: $name"
    }
}
$uiConfig = Join-Path $env:APPDATA 'qrenderdoc/UI.config'
if (!(Test-Path -LiteralPath $uiConfig) -or !(Get-Content -LiteralPath $uiConfig -Raw | ConvertFrom-Json).Analytics_TotalOptOut) {
    throw 'Configure external tool privacy preferences manually before use; existing settings are not changed.'
}
$case = Join-Path $repository ('out/capture-lab/control-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($case) | Out-Null
$script = Join-Path $PSScriptRoot 'capture_once.py'
$configPath = Join-Path $case 'local-config.json'
$config = @{mode='owned_control'; executable=$fixture; executable_sha256=(Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash.ToLowerInvariant(); script=$script; timeout_seconds=40}
if ($Scenario -eq 'timeout') { $config.timeout_seconds = 3 }
$config | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding UTF8
$priorCapture = $env:ODV_FRAME_CAPTURE_CONFIG
$priorReplay = $env:ODV_CAPTURE_LAB_CONFIG
$target = $null
try {
    $env:ODV_FRAME_CAPTURE_CONFIG = $configPath
    $helper = Start-Process -FilePath (Join-Path $toolRoot 'qrenderdoc.exe') -ArgumentList @('--python',('"'+$script+'"')) -WindowStyle Hidden -PassThru
    @{helper_process_id=$helper.Id; privacy='LOCAL ONLY'} | ConvertTo-Json | Set-Content (Join-Path $case 'helper-context.json') -Encoding UTF8
    $deadline = (Get-Date).AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 100
        $report = if (Test-Path (Join-Path $case 'capture-info.json')) { Get-Content (Join-Path $case 'capture-info.json') -Raw | ConvertFrom-Json }
        if ($report.status -eq 'FAILED') { throw 'Owned fixture failed before readiness.' }
    } until ($report.status -eq 'READY_FOR_FRAME' -or (Get-Date) -gt $deadline -or $helper.HasExited)
    if ($report.status -ne 'READY_FOR_FRAME') { throw 'No owned D3D12 readiness observed; inspect the live helper before retrying.' }
    $context = Get-Content (Join-Path $case 'local-context.json') -Raw | ConvertFrom-Json
    $target = Get-Process -Id $context.target_process_id -ErrorAction Stop
    if ($target.Path -ne $fixture) { throw 'Unexpected owned target executable.' }
    # Retain the process handle so normal exit and exit code can be verified.
    $null = $target.Handle
    if ($Scenario -eq 'capture') {
        New-Item -ItemType File -Path (Join-Path $case 'capture-now.flag') | Out-Null
    } elseif ($Scenario -eq 'disconnect') {
        New-Item -ItemType File -Path (Join-Path $case 'stop.flag') | Out-Null
    }
    if (!$helper.WaitForExit(20000)) { throw 'Owned helper is still live; inspect the same process before retrying.' }
    $report = Get-Content (Join-Path $case 'capture-info.json') -Raw | ConvertFrom-Json
    if ($Scenario -eq 'capture') {
        if ($report.status -ne 'OWNED_CONTROL_CAPTURED' -or $report.game_gpu_capture -ne 'NOT_RUN') { throw 'Expected owned-only capture success.' }
        $analysisConfig = Join-Path $case 'analysis-config.json'
        @{fixture='owned_d3d12_clear';capture=(Join-Path $case 'frame.rdc');output=(Join-Path $case 'replay-summary.json')} | ConvertTo-Json | Set-Content -LiteralPath $analysisConfig -Encoding UTF8
        $env:ODV_CAPTURE_LAB_CONFIG = $analysisConfig
        $analysis = Join-Path $repository 'experiments/capture-lab/analyse_owned.py'
        $replay = Start-Process -FilePath (Join-Path $toolRoot 'qrenderdoc.exe') -ArgumentList @('--python',('"'+$analysis+'"')) -WindowStyle Hidden -PassThru
        if (!$replay.WaitForExit(20000)) { throw 'Owned replay remains live; inspect it before retrying.' }
        $pixels = Get-Content (Join-Path $case 'replay-summary.json') -Raw | ConvertFrom-Json
        if ($pixels.status -ne 'PASS' -or $pixels.capture_sha256 -ne $report.capture_sha256 -or $pixels.verified_rgba8_targets -ne 1) { throw 'Owned replay verification failed.' }
    } else {
        if ($report.status -ne 'FAILED' -or $report.failure_stage -ne 'READY_FOR_FRAME' -or
            $report.game_gpu_capture -ne 'NOT_RUN' -or @(Get-ChildItem -LiteralPath $case -Filter '*.rdc').Count) {
            throw 'Expected a pre-capture failure without an RDC or game-capability claim.'
        }
        $errorText = Get-Content (Join-Path $case 'local-error.txt') -Raw
        $expected = if ($Scenario -eq 'timeout') { 'Capture deadline expired' } else { 'Capture target disconnected' }
        if (!$errorText.Contains($expected)) { throw 'Failure reason does not match the requested scenario.' }
    }
    New-Item -ItemType File -Path (Join-Path $case 'stop.flag') -Force | Out-Null
    if (!$target.WaitForExit(10000) -or $target.ExitCode -ne 0) { throw 'Owned fixture did not exit cleanly.' }
    @{schema_version=1; scenario=$Scenario; status='PASS'; fixture_exit_code=$target.ExitCode; game_capture='NOT_RUN'} |
        ConvertTo-Json | Set-Content (Join-Path $case 'test-summary.json') -Encoding UTF8
    Write-Output "Owned $Scenario check PASS: $case"
} finally {
    # Cooperative cleanup only: no forced termination, registry or security changes.
    New-Item -ItemType File -Path (Join-Path $case 'stop.flag') -Force | Out-Null
    $env:ODV_FRAME_CAPTURE_CONFIG = $priorCapture
    $env:ODV_CAPTURE_LAB_CONFIG = $priorReplay
}
