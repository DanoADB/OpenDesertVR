# Optional owned D3D12 capture lab

This is a separate research fixture, not a game adapter or an installed M0
component. It verifies an external GPU capture/replay workflow before attempting
to inspect game render passes. It does not launch or attach to Crimson Desert.

## Requirements and boundaries

Windows x64, MSVC/CMake and the external RenderDoc 1.47 portable tool. Obtain
the [official release](https://github.com/baldurk/renderdoc/releases/tag/v1.47)
from the linked renderdoc.org download. The local archive's observed SHA256 is
b51712e58d40066b34ca30c7869d15f72e4b5c001b2d92232f6783a636d5a3c1.
The archive digest is a local fingerprint, not an independently published vendor
checksum. Windows Authenticode validated renderdoccmd.exe, renderdoc.dll and
qrenderdoc.exe as signed by Baldur Scott Karlsson.

The external renderdoc_app.h is supplied by the portable tool; it is not copied
into this repository or fetched by the main build. No tool binaries/captures
are shipped with the runtime package. No global hook, Vulkan layer registration,
service, driver or child-process hook is enabled for this lab.

## Build and capture

From the repository root in PowerShell (substitute your external tool path):

```powershell
$renderdocRoot = (Resolve-Path 'out/tools/renderdoc-1.47/portable/RenderDoc_1.47_64').Path
cmake -S experiments/capture-lab -B out/build/capture-lab -G 'Visual Studio 17 2022' -A x64 "-DRENDERDOC_INCLUDE_DIR=$renderdocRoot"
cmake --build out/build/capture-lab --config Release
$case = Join-Path $PWD ('out/capture-lab/run-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $case | Out-Null
$fixture = (Resolve-Path 'out/build/capture-lab/Release/odv_capture_fixture.exe').Path
& "$renderdocRoot/renderdoccmd.exe" capture --wait-for-exit $fixture "$case/owned-clear"
```

The fixture requires the externally loaded capture API, creates an owned
16x16 RGBA8 texture and explicitly captures a cyan clear. Endpoint channels
avoid fractional UNORM rounding differences. GPU completion is fence-checked
before ending capture. No window, headset or game is needed. Running the
fixture without the capture layer fails rather than fabricating a capture.

## Replay and verify

The portable tool embeds its Python bindings in qrenderdoc; the system Python
cannot simply import those bindings. Use its documented --python entrypoint:

```powershell
@{fixture='owned_d3d12_clear'; capture="$case/owned-clear_capture.rdc"; output="$case/replay-summary.json"} | ConvertTo-Json | Set-Content "$case/analysis-config.json" -Encoding UTF8
$env:ODV_CAPTURE_LAB_CONFIG = "$case/analysis-config.json"
$analysis = (Resolve-Path 'experiments/capture-lab/analyse_owned.py').Path
Start-Process "$renderdocRoot/qrenderdoc.exe" -ArgumentList @('--python',('"'+$analysis+'"')) -WindowStyle Hidden -Wait
Get-Content "$case/replay-summary.json"
```

The local new RenderDoc UI profile was created with Analytics_TotalOptOut=true
before automated use; no existing user profile was overwritten. Operators
retain control of their tool's telemetry preferences. The script's SystemExit
skips the main UI after replay. The app's exit code alone does not report Python
script success: require a fresh PASS JSON, matching capture digest, one clear
and one 16x16 RGBA8 target with all 256 pixels exactly [0,255,255,255].

The [recorded local validation](../../docs/evidence/g1-owned-capture-lab-2026-11-05.json)
passes those checks. This validates tooling on owned synthetic content only.
It does not establish that a protected game can be captured, that a capture
identifies camera matrices, or that independent game-world stereo is possible.
