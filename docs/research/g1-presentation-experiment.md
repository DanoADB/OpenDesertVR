# G1 external presentation baseline

Goal: measure presentation streams from the unmodified game, before proposing
any render/camera integration. This is ordinary external tracing, not a stereo
adapter. No game settings, save, binary or process memory is modified by these
scripts. Starting/playing the game may have its ordinary save/network effects;
the scripts neither launch nor control it.

## Tool and current observation

Use the standalone [official PresentMon 2.6.0 console release](https://github.com/GameTechDev/PresentMon/releases/tag/v2.6.0),
not its service, overlay or flash-injection functionality. Download
`PresentMon-2.6.0-x64.exe` from that release. Expected SHA256:

```text
b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af
```

The release API digest matched the downloaded file; Windows reported a valid
signature. The binary is in ignored out/tools locally and is not shipped with
OpenDesertVR. Consult its notices separately if redistributing it.

The first local trace probe failed with exit 6: Windows denied trace-session
creation. This process is neither elevated nor in Performance Log Users.
The game was not running and no game presentation was observed. This is a
Windows tracing privilege limitation, not evidence of game incompatibility.
The scripts do not auto-elevate, change group membership or stop other profilers.

The user subsequently ran the helper from Administrator PowerShell while the
game was at its title/start menu. Capture and analysis exited 0: 1,794 matching
rows in one DXGI stream, hardware-composed independent flip, median/p95/max
between presents 16.6869/18.4211/27.8759 ms. The game executable and helper-script
hashes are verified in [the actual evidence](../evidence/g1-title-presentation-2026-11-05.json).
Settings/resolution/frame generation were not recorded; this is not gameplay
or headset performance evidence. This CSV omits DisplayedTime, so undisplayed
row count is explicitly NOT_RUN/null in corrected analysis. The original raw
CSV remains local and is hash-bound to the preserved evidence.

## Run and record

Start the game normally through Steam. Use a stable title/menu scene first;
then separately record controlled gameplay and settings if needed. Keep these
workloads separate and label them in the human evidence record.

In a PowerShell with tracing access (an elevated PowerShell is one option), run
from the repository root, with Python 3 available:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/capture-present-baseline.ps1 -DurationSeconds 30
```

The default tool path is out/tools/presentmon-2.6.0/PresentMon-2.6.0-x64.exe;
use -PresentMonPath for another location. The helper verifies its hash and
requires exactly one CrimsonDesert process (or an explicit -TargetProcessId).
It uses a unique session, filters the process, disables input tracking, stops
after the requested interval and writes a fresh out/g1/capture-* directory.
No automatic game launch or settings change occurs.

Keep raw.csv and capture.log local: they can contain IDs/addresses. Share
summary.json and capture-info.json. The summary replaces swapchain addresses
with stream numbers, removes PIDs/paths and reports each stream separately.
NA, zero and malformed intervals are handled explicitly; missing columns/data
are never reported as measured zero. Failed capture/parse never claims success.

## Interpretation and next technical gate

The [pinned console documentation](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/README-ConsoleApplication.md)
defines the CSV presentation fields. DXGI can carry D3D11 or D3D12; a DXGI row
alone is insufficient evidence of the active renderer. Presentation timing also
does not establish access to engine views, extra world renders, a safe GPU
handoff or stereo. The script explicitly leaves those capabilities unverified.

Once actual data exists, inspect stream count, modes, positive interval
distributions and missing samples for each workload. Then design the smallest
reviewed experiment to distinguish the active render path and test camera/frame
ownership. Stop on a protection warning, game instability or a required bypass.
Do not disable protection or infer that texture copying produces true VR.
