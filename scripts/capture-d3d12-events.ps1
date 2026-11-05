# SPDX-License-Identifier: MIT
param(
    [string]$OutputDirectory,
    [ValidateRange(1, 30)][int]$DurationSeconds = 10,
    [ValidateRange(0, 2147483647)][int]$TargetProcessId = 0
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
if ($TargetProcessId) {
    $target = Get-Process -Id $TargetProcessId
    if ($target.ProcessName -ne 'CrimsonDesert') { throw 'Target is not CrimsonDesert.exe.' }
} else {
    $targets = @(Get-Process -Name CrimsonDesert -ErrorAction SilentlyContinue)
    if ($targets.Count -ne 1) { throw 'Start the unmodified game normally; exactly one game process is required.' }
    $target = $targets[0]
}
$provider = Get-WinEvent -ListProvider 'Microsoft-Windows-Direct3D12'
$providerId = [Guid]'5d8087dd-3a9b-4f56-90df-49196cdc4f11'
if ($provider.Id -ne $providerId) { throw 'Unexpected Direct3D12 provider identity.' }
$executionEvents = @($provider.Events | Where-Object { $_.Task.Name -eq 'ExecuteCommandList' -and $_.Opcode.Name -eq 'win:Start' })
if (!$executionEvents.Count) { throw 'Installed event schema lacks ExecuteCommandList start events.' }
foreach ($entry in $executionEvents) {
    if (!($entry.Keywords | Where-Object { $_.Name -eq 'Devices' -and $_.Value -eq 2 })) {
        throw 'Installed Direct3D12 execution event keyword schema is unsupported.'
    }
}
$gameFile = Get-Item -LiteralPath $target.Path
$captureId = [Guid]::NewGuid().ToString('N')
$sessionName = 'OpenDesertVR-D3D12-' + $captureId
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repository ('out/g1/d3d12-' + $captureId) }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) { throw 'Use a new output directory to preserve prior evidence.' }
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$etl = Join-Path $outputRoot 'raw.etl'
$log = Join-Path $outputRoot 'capture.log'
$infoPath = Join-Path $outputRoot 'capture-info.json'
$info = [ordered]@{
    schema_version = 1
    tool = 'Windows logman and Get-WinEvent'
    provider = 'Microsoft-Windows-Direct3D12'
    provider_guid = $providerId.ToString()
    keyword_mask = '0x2 (Devices)'
    circular_file_limit_mb = 64
    requested_duration_seconds = $DurationSeconds
    target = 'CrimsonDesert.exe'
    game_executable_sha256 = (Get-FileHash -LiteralPath $gameFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    game_executable_size_bytes = $gameFile.Length
    game_file_version = $gameFile.VersionInfo.FileVersion
    script_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
    status = 'STARTED'
    start_exit = $null
    stop_exit = $null
    trace_session_stopped = $false
}
$started = $false
try {
    # Direct volatile ETW session, no saved collector, service, injection or group change.
    & logman.exe start $sessionName -p $providerId.ToString() '0x2' 5 -o $etl -f bincirc -max 64 -bs 64 -nb 16 64 -ct perf -ets *> $log
    $info.start_exit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Could not start Direct3D12 trace; inspect capture.log and tracing privileges.' }
    $started = $true
    try {
        Start-Sleep -Seconds $DurationSeconds
    } finally {
        & logman.exe stop $sessionName -ets *>> $log
        $info.stop_exit = $LASTEXITCODE
        $info.trace_session_stopped = $LASTEXITCODE -eq 0
        if ($LASTEXITCODE -ne 0) { throw 'Could not verify trace stop; inspect capture.log.' }
    }
    $counts = @{}
    try {
        Get-WinEvent -Path $etl -Oldest | ForEach-Object {
            if ($_.ProviderId -eq $providerId -and $_.ProcessId -eq $target.Id) {
                $key = [string]$_.Id + ':' + [string]$_.Version
                if (!$counts.ContainsKey($key)) { $counts[$key] = 0 }
                $counts[$key]++
            }
            # Do not read/export event properties, addresses, messages, paths or PID.
        }
    } catch {
        if ($_.FullyQualifiedErrorId -notlike 'NoMatchingEventsFound*') { throw }
    }
    $executionCount = 0
    foreach ($entry in $executionEvents) {
        $key = [string]$entry.Id + ':' + [string]$entry.Version
        if ($counts.ContainsKey($key)) { $executionCount += $counts[$key] }
    }
    $events = @($counts.Keys | Sort-Object | ForEach-Object {
        $parts = $_.Split(':')
        @{event_id=[int]$parts[0]; version=[int]$parts[1]; count=$counts[$_]}
    })
    $summary = [ordered]@{
        schema_version = 1
        evidence_kind = 'external_d3d12_etw_event_counts'
        application = 'CrimsonDesert.exe'
        provider_guid = $providerId.ToString()
        input_sha256 = (Get-FileHash -LiteralPath $etl -Algorithm SHA256).Hash.ToLowerInvariant()
        events = $events
        execute_command_list_start_events = $executionCount
        d3d12_execution = if ($executionCount -gt 0) { 'OBSERVED' } else { 'UNVERIFIED' }
        world_render_pass_identity = 'UNVERIFIED'
        camera_control = 'UNVERIFIED'
        native_stereo = 'UNVERIFIED'
        limitations = @(
            'Counts cover retained events in a bounded circular trace, not complete frame/GPU timing.'
            'Observed D3D12 command-list execution does not identify world passes, camera matrices or stereo.'
            'No matching execution events is inconclusive; it does not prove a D3D11 renderer.'
        )
    }
    [IO.File]::WriteAllText((Join-Path $outputRoot 'summary.json'), (($summary | ConvertTo-Json -Depth 8) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
    $info.status = 'CAPTURED'
} catch {
    $info.status = 'FAILED'
    throw
} finally {
    if ($started -and !$info.trace_session_stopped) {
        & logman.exe stop $sessionName -ets *>> $log
        $info.stop_exit = $LASTEXITCODE
        $info.trace_session_stopped = $LASTEXITCODE -eq 0
    }
    [IO.File]::WriteAllText($infoPath, (($info | ConvertTo-Json -Depth 5) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
}
Write-Output "Capture complete: $outputRoot"
Write-Output 'Keep raw.etl and capture.log local; share summary.json and capture-info.json.'
