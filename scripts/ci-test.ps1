# SPDX-License-Identifier: MIT
# Fixture for disposable GitHub-hosted Windows workers only. The OpenXR loader
# ignores environment runtime overrides in their elevated service context.
param([Parameter(Mandatory=$true)][ValidateSet('windows-msvc-debug','windows-msvc-release')][string]$Preset)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'This registry fixture is restricted to GitHub Actions workers. Run ctest normally on a non-elevated local shell.' }
$configuration = if ($Preset -eq 'windows-msvc-debug') { 'Debug' } else { 'Release' }
$buildFolder = if ($configuration -eq 'Debug') { 'debug' } else { 'release' }
$manifest = (Resolve-Path -LiteralPath "out/build/$buildFolder/mock-runtime-$configuration.json").Path
$registryPath = 'HKLM:\SOFTWARE\Khronos\OpenXR\1'
$original = Get-ItemProperty -LiteralPath $registryPath -ErrorAction SilentlyContinue
$hadValue = $null -ne $original -and $null -ne $original.PSObject.Properties['ActiveRuntime']
$oldValue = if ($hadValue) { $original.ActiveRuntime } else { $null }
$suiteExit = 1
$missingExit = 1
try {
    if (!(Test-Path -LiteralPath $registryPath)) { New-Item -Path $registryPath -Force | Out-Null }
    New-ItemProperty -LiteralPath $registryPath -Name ActiveRuntime -PropertyType String -Value $manifest -Force | Out-Null
    ctest --preset $Preset --parallel 3 -E '^odv_cli_missing_runtime$' --output-log "out/ci-$Preset.log"
    $suiteExit = $LASTEXITCODE
    # Run actual missing-runtime diagnostics after all mock-runtime processes end.
    $absent = Join-Path $PWD 'out/ci-intentionally-absent-runtime.json'
    if (Test-Path -LiteralPath $absent) { throw 'Absent-runtime fixture unexpectedly exists' }
    Set-ItemProperty -LiteralPath $registryPath -Name ActiveRuntime -Value $absent
    ctest --preset $Preset -R '^odv_cli_missing_runtime$' --output-log "out/ci-$Preset-missing-runtime.log"
    $missingExit = $LASTEXITCODE
} finally {
    if ($hadValue) { Set-ItemProperty -LiteralPath $registryPath -Name ActiveRuntime -Value $oldValue }
    else { Remove-ItemProperty -LiteralPath $registryPath -Name ActiveRuntime -ErrorAction SilentlyContinue }
}
if ($suiteExit -ne 0) { exit $suiteExit }
exit $missingExit
