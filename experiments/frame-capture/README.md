# Single-frame game capture experiment

Status: **game launch/control attempt FAILED before a frame request; no game
GPU capture obtained.** The same launch API passed on the owned D3D12 fixture;
replay verified all 256 pixels. The game target connected, then disconnected
before READY_FOR_FRAME. A later game process had a different identity. This is
consistent with launch handoff, but the cause remains unverified. No retry,
late attachment, child/global hook or protection bypass was attempted.
See the [sanitized attempt record](../../docs/evidence/g1-frame-capture-attempt-2026-11-05.json).
The running-game guard also passed. Neither result proves game camera control
or stereo. The commands below document the attempted method, not a working
capture recipe for this Steam build.

An optional follow-up uses the
[reviewed isolated Steam development context](../../docs/research/g1-steam-capture-context.md).
Add -SteamDevelopmentContext to start-probe.ps1 to write the actual App ID only
inside the fresh ignored capture directory and use that as the working
directory. The ordinary Steam client must already be running. No game-directory
file, client setting or licensing check is modified. The default launch context
is unchanged; this option is a separate experiment with its own evidence.
The [follow-up result](../../docs/evidence/g1-steam-context-attempt-2026-11-05.json)
observed that the early-loaded game layer stayed connected and reached the
title menu. One frame request failed creating a readback buffer with
E_OUTOFMEMORY. No RDC was obtained; allocation size and underlying cause are
unverified. The helper's terminal report records its timeout at CAPTURE_REQUESTED.

## Reviewed method and scope

RenderDoc 1.47 is an external diagnostic tool, not a runtime dependency.
Its [capture documentation](https://github.com/baldurk/renderdoc/blob/v1.47/docs/window/capture_attach.rst)
requires loading its capture layer before the graphics API initializes. Its
[supported replay/control API](https://github.com/baldurk/renderdoc/blob/v1.47/renderdoc/api/replay/renderdoc_replay.h)
provides ExecuteAndInject, local target control and one-frame capture requests.
This probe launches a fresh process through that supported early-start path.
It never calls late InjectIntoProcess, enables global/child hooks, changes Steam
launch options, copies DLLs into the game folder or alters protection settings.
No approval from the game publisher is inferred from this technical review.

The launcher accepts only the exact executable fingerprint already recorded in
the G1 baseline and the reviewed signed RenderDoc binaries. It checks existing
tool privacy preferences without changing them. It refuses an existing game
process rather than interrupting gameplay. If normal Steam handoff, permissions,
tool incompatibility or protection prevents capture, retain that result and
stop the attempted method; do not force a relaunch or bypass the cause.

## Execution

Close the game normally first. From the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./experiments/frame-capture/start-probe.ps1 -GameExecutable "D:/path/to/Crimson Desert/bin64/CrimsonDesert.exe"
```

Substitute the actual executable path. No elevation is requested. The background
helper exposes its phase in out/g1/frame-*/capture-info.json. After READY_FOR_FRAME,
observe that the game is at the intended title menu, then create capture-now.flag
in that case directory. It requests one frame through the local control API,
without a keyboard hotkey or screenshot action. A fresh frame.rdc copy is kept
locally and its hash is checked against the completed original capture.

Native tool calls are not a hard watchdog; the timeout bounds the polling loop.
Do not infer success from qrenderdoc's process exit code. Require CAPTURED,
readable D3D12 capture metadata and verified hashes. Close the experiment's game
normally afterward; the tool's capture layer lives only in that process. Handle
any Windows security/permission dialog manually. The agent does not grant
network access or change Windows security settings.

Raw captures contain proprietary game content and may contain machine metadata.
Keep frame.rdc, other .rdc files, local-config.json, local/helper contexts and
local-error.txt local. Only sanitized capture-info.json is suitable for a public
evidence review. No proprietary capture payload is committed or uploaded.

## Evidence gate

A successful file proves a diagnostic frame was captured, not camera control.
Next inspect render passes and bound resources locally, keeping game data local.
Camera pose/projection identity must be established with observed changes, and
independent world renders must prove near/far parallax before G2. The production
game adapter stays inactive and Unsupported/Unverified throughout this experiment.

The script's owned_launch mode is restricted to odv_capture_fixture.exe and is
reserved for validating the launcher API independently; it cannot award game
capture or stereo capability. Owned launch/replay passed on 2026-11-05; game
target-control frame triggering remains unverified.

## Owned target-control regression checks

The separate odv_present_fixture is an ordinary owned D3D12 application with
a small cyan window, two swapchain buffers and a known 16x16 cyan texture.
It contains no RenderDoc API calls, game code or capture hooks. Each frame
clears both targets, presents and waits for GPU completion before reusing its
allocator. It exits cooperatively on stop.flag, window close or after 60 seconds.

Build the capture lab as described in its README, then run from the repository
root with the reviewed portable RenderDoc 1.47 already available:

```powershell
./experiments/frame-capture/test-owned-control.ps1 -Scenario capture
./experiments/frame-capture/test-owned-control.ps1 -Scenario disconnect
./experiments/frame-capture/test-owned-control.ps1 -Scenario timeout
```

These checks use owned_control mode and the same launch, identity, readiness,
trigger, notification, snapshot and hash-verification path as the game probe.
Capture requires successful replay of all 256 known pixels. Disconnect closes
the fixture after readiness without requesting a frame; timeout leaves it
running without a request until the polling deadline. Both require a FAILED
report at READY_FOR_FRAME, no RDC, the expected local failure reason and no
game capability claim. All scenarios verify the fixture's actual exit code 0.
No target is force-terminated or automatically relaunched after a timeout.

All three [local checks passed](../../docs/evidence/g1-owned-control-2026-11-05.json).
Failure reports now retain failure_stage to distinguish launch, readiness,
capture request and saving failures. CI compiles the ordinary fixture and
checks script syntax; it does not execute RenderDoc or award capture evidence.
These owned checks do not resolve the game's process handoff.

## Reconnect to an already early-loaded layer

After a failed request, -ResumeFromCase followed by the original case path
creates a fresh output case and reconnects to that layer. It does not launch
the game or call ExecuteAndInject/InjectIntoProcess. The launcher checks the
same original live process, exact executable hash and reviewed loaded DLL;
the Python helper also checks the prior failed report, executable, target PID
and sibling-case provenance. An absent, changed or busy target fails closed.
Do not use this option as a way to inject into an uninstrumented running game.

The resumed layer still writes raw captures under its original local prefix.
New reports/snapshots use the fresh case, and previously existing capture paths
cannot satisfy a new request. The prior failure record remains unchanged.
One owned timeout/reconnect/capture/replay check passed on the same live process
and verified all 256 pixels before normal exit 0; see
[its evidence](../../docs/evidence/g1-owned-reconnect-2026-11-05.json).
The game's lower-memory retry remains NOT_RUN pending graphics-settings review.

## Aggregate offline frame structure

summarize_frame.py runs through qrenderdoc --python with
ODV_FRAME_SUMMARY_CONFIG pointing to a local JSON containing capture, its
expected capture_sha256, a fresh output path and this script's absolute script
path. It opens only a fingerprint-matched D3D12 capture, initializes replay,
then counts action flags, texture shapes and buffer sizes. It never exports
resource names/IDs, shaders, texture pixels, buffer payloads or addresses.
Raw captures and summary-local-error.txt remain local. Require a fresh REPLAYED
report with the expected hash; tool exit code alone is insufficient.

The owned fixture produced two clears, one present, zero draw calls, a 16x16
target and a 320x180 target. This matches the ordinary fixture's structure;
independent pixel replay is separate. No game frame has yet been replayed.
REPLAYED does not assert image fidelity, world-pass identity or camera control.
