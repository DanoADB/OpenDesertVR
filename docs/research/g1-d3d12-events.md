# G1 Direct3D12 execution-event experiment

Question: does the running game submit D3D12 command lists? The title-menu
PresentMon trace identifies DXGI presentation, while Windows reports both
D3D11 and D3D12 modules loaded. Neither observation settles the execution API.

The next test uses the installed Windows Microsoft-Windows-Direct3D12 provider,
GUID 5d8087dd-3a9b-4f56-90df-49196cdc4f11. The local manifest identifies
ExecuteCommandList start events 53/139 (version 0), using the Devices keyword
0x2. The script derives these event identities from the installed schema rather
than assuming that every Windows version uses the same event IDs.

## Reviewed method

Use Windows logman to start a unique volatile ETW session, record Devices events
for 10 seconds into a circular file capped at 64 MB, then stop that session in
finally. No service, driver, saved collector, group change or automatic elevation
is installed. The game is started normally by the user; no injection, camera
write, binary edit or protection bypass occurs.

Get-WinEvent reads the completed ETL. Only provider GUID, event ID/version and
matching game PID are used to count events. PID/addresses/event properties and
messages are excluded from the exported summary. The raw trace stays local.
Game executable/script hashes bind the evidence to its source. An absent
execution event remains UNVERIFIED rather than a claim of D3D11 compatibility.

The method follows Microsoft's [logman trace options](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/logman-create-trace)
and [direct-session example](https://learn.microsoft.com/en-us/windows/win32/winsock/control-of-winsock-tracing)
for ETW session control. It uses the graphics provider above, not the network
provider in that control example. Local provider schema and script preconditions
were checked before use. Actual collection is now recorded below; syntax checks
alone never establish a successful trace.

## Operator step

Leave the unmodified game at its title/menu. In Administrator PowerShell, from
any directory, run the repository's script by its full path:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:/Users/danod/.codex/.chatgpt-projects/g-p-69dfa3a24f448191a2dba1dd630f668f/OpenDesertVR/scripts/capture-d3d12-events.ps1" -DurationSeconds 10
```

Other checkouts should substitute their own script path. Default output is a
fresh out/g1/d3d12-* directory. Let the command finish so its finally block stops
the trace; stop only its generated OpenDesertVR-D3D12 session if manual cleanup
is required. Never stop another profiler's session. Share summary.json and
capture-info.json, keeping raw.etl/local-context.json/capture.log local.

## Interpretation

Matched execution events establish D3D12 command-list execution in that process
and sampled workload. They do not identify which lists render the world or
prove camera control, two-eye rendering, temporal history isolation or XR texture
handoff. Counts may be incomplete due to the bounded circular trace and are
not frame timings. G2 remains gated on actual independent world views.

## Observed title-menu result — 2026-11-05

The user completed the trace in Administrator PowerShell and confirmed the
title/start menu workload. Start/stop returned 0 and the session was stopped.
The game executable and script hashes match the pinned build/source. The
3,932,160-byte ETL hash and all matching-process counts were independently
rechecked. The source at capture was bba8504; the provider-format fix was already
included. See [the structured evidence](../evidence/g1-d3d12-execution-2026-11-05.json).

| Event ID/version | Local manifest meaning | Observed count |
|---|---|---:|
| 139/0 | ExecuteCommandList start | 6,623 |
| 140/0 | ExecuteCommandList stop | 6,623 |
| 52/0 | CommandList info | 8,429 |

This establishes D3D12 execution in the game process for that sample. It does
not assign lists to world/HUD passes, determine camera layout or prove stereo.
The original raw trace remains local. Later helper metadata records the event
schema, while a local-only PID/start-time context supports offline reanalysis
after the game exits. Do not publish local-context.json with the safe summaries.
