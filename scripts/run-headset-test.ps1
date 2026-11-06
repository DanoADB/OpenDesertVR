# SPDX-License-Identifier: MIT
param(
    [string]$PackageDirectory = $PSScriptRoot,
    [string]$OutputDirectory,
    [ValidateRange(1, 3600)][int]$DurationSeconds = 60,
    [switch]$DebugLayer
)
$ErrorActionPreference = 'Stop'
$exitCode = 1
$summary = @{
    schema_version = 1
    technical_submission = 'NOT_RUN'
    headset_validation = 'NOT_RUN'
    game_validation = 'NOT_RUN'
    host_invoked = $false
    test_runtime = $false
}
function Write-JsonFile($Path, $Value) {
    [IO.File]::WriteAllText($Path, (($Value | ConvertTo-Json -Depth 16) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
}
try {
    $packageRoot = [IO.Path]::GetFullPath($PackageDirectory)
    if (!$OutputDirectory) {
        $OutputDirectory = Join-Path $packageRoot ('reports/run-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N'))
    }
    $reportRoot = [IO.Path]::GetFullPath($OutputDirectory)
    foreach ($name in @('doctor.json', 'host.json', 'test-config.json', 'run-summary.json')) {
        if (Test-Path -LiteralPath (Join-Path $reportRoot $name)) {
            throw 'Output already contains test evidence. Choose a fresh output directory.'
        }
    }
    [IO.Directory]::CreateDirectory($reportRoot) | Out-Null
    $doctor = Join-Path $packageRoot 'odv-cli.exe'
    $hostProgram = Join-Path $packageRoot 'odv-testhost.exe'
    if (!(Test-Path -LiteralPath $doctor) -or !(Test-Path -LiteralPath $hostProgram)) {
        throw 'Run from the extracted package, or pass -PackageDirectory containing both executables.'
    }
    $buildInfo = Join-Path $packageRoot 'build-info.json'
    if (Test-Path -LiteralPath $buildInfo) {
        $build = Get-Content -LiteralPath $buildInfo -Raw | ConvertFrom-Json
        if ($build.source_commit -match '^[0-9a-f]{40}$') {
            $summary.source_commit = $build.source_commit
            $summary.source_dirty = [bool]$build.source_dirty
        }
    }
    $doctorReport = Join-Path $reportRoot 'doctor.json'
    & $doctor export-report --output $doctorReport
    $doctorExit = $LASTEXITCODE
    $summary.doctor_exit = $doctorExit
    if (!(Test-Path -LiteralPath $doctorReport)) { throw 'Doctor did not write a report.' }
    $readiness = Get-Content -LiteralPath $doctorReport -Raw | ConvertFrom-Json
    $summary.test_runtime = [bool]$readiness.test_runtime
    if ($doctorExit -eq 0 -and $readiness.ready) {
        $config = @{ schema_version=1; near_z=0.05; far_z=100; duration_seconds=$DurationSeconds; debug_layer=[bool]$DebugLayer }
        $configFile = Join-Path $reportRoot 'test-config.json'
        Write-JsonFile $configFile $config
        $hostReport = Join-Path $reportRoot 'host.json'
        $summary.host_invoked = $true
        & $hostProgram --config $configFile --report $hostReport
        $exitCode = $LASTEXITCODE
        $summary.host_exit = $exitCode
        if (!(Test-Path -LiteralPath $hostReport)) { throw 'Host did not write a report.' }
        $result = Get-Content -LiteralPath $hostReport -Raw | ConvertFrom-Json
        $summary.test_runtime = [bool]$result.test_runtime
        $summary.technical_submission = if ($exitCode -eq 0 -and $result.ready) { 'PASS' } else { 'FAIL' }
        $summary.stereo_frames = $result.stereo_frames
    } else {
        $exitCode = if ($doctorExit -ne 0) { $doctorExit } else { 3 }
        [Console]::Error.WriteLine('Headset prerequisites are missing; inspect doctor.json. The host was not launched.')
    }
    if ($summary.test_runtime) { Write-Output 'TEST RUNTIME: software integration evidence only.' }
    Write-Output 'Visual stereo, tracking and comfort still require the manual M0 checklist.'
    Write-JsonFile (Join-Path $reportRoot 'run-summary.json') $summary
    Write-Output "Reports: $reportRoot"
} catch {
    [Console]::Error.WriteLine('Test runner failed. Check package paths and use a fresh writable output directory; existing reports are preserved.')
    $exitCode = 1
}
exit $exitCode
