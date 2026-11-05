# Prepared single-frame game capture experiment

Status: **preparation only; game GPU capture NOT_RUN.** The external RenderDoc
API entrypoints/options were checked against the installed tool, Python and
PowerShell syntax passed, and the launcher correctly rejected an already-running
game. The new API launch/control path still requires an actual test. Separately,
the [owned capture lab](../capture-lab/README.md) already captured/replayed D3D12
and checked every expected pixel. Neither result proves game capture or VR.

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
capture or stereo capability. An untested mode remains unverified.
