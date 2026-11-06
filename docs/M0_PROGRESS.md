# M0 progress and evidence — 2026-11-04

## Implemented

- C++20 core: typed errors/results, strict version-1 JSON config, bounded
  structured events and sanitized reports.
- Native Windows D3D12/OpenXR host: extension negotiation, runtime-required
  adapter/feature level, device/queue/fences, session lifecycle, LOCAL space,
  primary stereo, separate swapchains and predicted-time view location.
- Independent per-eye geometry: colored cubes at 1/3/8 meters, asymmetric
  projections, depth-buffered rasterization, GPU completion before release.
- Doctor/export-report, explicit missing prerequisites and inactive
  CrimsonDesertAdapter capabilities.
- Numeric OpenXR runtime version in reports; a later actual-runtime doctor
  query records 1.208.0. The earlier headset runs used the preceding binary.
- Revision/hash-pinned Khronos loader and JSON dependency, dependency notices,
  architecture/safety/research/manual acceptance docs and Release packaging.
- Windows Debug/Release CI definitions including WARP graphics checks.
- Packaged report collector gates host startup on doctor readiness, records
  source provenance, and preserves NOT_RUN for visual validation.
- Pinned static Cyberpunk VR/REFramework source audit and selected direct
  license inventory; no mod code/dependencies adopted. Sampled eye events now
  share frame index and relative predicted display timing, checked by native
  integration tests.
- GPU timestamp queries, bounded per-eye/stereo timing summaries and explicit
  retained/lifetime sample counts.
- Test-only OpenXR runtime exercising the compiled host with 25 normal/failure
  scenarios, including partial eye failures, session loss, timed exit and
  repeated image-wait timeouts. Local tests use per-process runtime overrides;
  a disposable-runner CI fixture restores its prior registration in finally.

## Observed locally (Windows x64)

MSVC 19.44.35229, Windows SDK 10.0.26100.0, Visual Studio 2022 Build Tools.
No Crimson Desert installation was used.

| Check | Evidence |
|---|---|
| Debug configure/build | PASS |
| Release configure/build | PASS |
| CTest Debug | 33 PASS, 0 FAIL, 1 SKIPPED (debug layer unavailable) |
| CTest Release | 33 PASS, 0 FAIL, 1 SKIPPED (debug layer unavailable) |
| Real D3D12 WARP raster/readback | PASS; near/mid/far pixel disparities 20.1375 / 7 / 4.95688 |
| Actual loader with absent runtime override | PASS: categorized FAIL readiness, exit 3, sanitized JSON |
| Installed runtime discovery | PASS: Meta runtime exposes XR_KHR_D3D12_enable |
| Testhost without available headset | Expected FAIL readiness: XR_ERROR_FORM_FACTOR_UNAVAILABLE (-35), zero submitted frames |
| Extracted Release package smoke | PASS: both programs run; missing headset returns exit 3 |
| Real headset visual stereo/tracking | User-reported PASS on Meta Quest Pro (2026-11-05); host binary matches packaged 404d38a build |
| Real headset submission and shutdown | First run: 536 stereo frames, host exit 0, STOPPING then IDLE; second: 747 stereo frames, exit summary missing |
| Real headset menu return and timed shutdown | PASS: user-confirmed depth/tracking restored and clean finish; 60-second run, 2,833 stereo frames, focus transitions, STOPPING/EXITING and host exit 0 |
| Quest Link disconnect and fresh-launch recovery | PASS tested path: 449 stereo frames, STOPPING/IDLE, timed exit 0; reconnect run 1,214 stereo frames and exit 0, user confirmed correct viewing |
| Arbitrary GPU removal/runtime crash | NOT_RUN |
| Game integration/stereo | NOT_RUN; adapter inactive |
| Hosted GitHub Actions execution | PASS: Debug and Release each 34/34, including D3D12 debug validation; [run 41965528980](https://github.com/DanoADB/OpenDesertVR/actions/runs/41965528980) |

Deterministic tests cover asymmetric FOV edges, invalid FOV/pose/quaternion,
IPD and handedness, clipping depth, geometric disparity, version/schema/config
validation, event bounds/redaction, lifecycle transitions, frame ordering,
invalid tracking, skipped frames and rendering failure cleanup. Protocol
mocks cover prerequisite/session failures. Native tests exercise invalid
adapter LUID and allocation/device HRESULT classification. These injected
errors are not proof of actual headset recovery or physical device removal.

## Verification commands

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
```

Current hardening evidence: [m0-hardening-2026-11-04.json](evidence/m0-hardening-2026-11-04.json).

Initial build/package evidence (before hardening): [m0-local-2026-11-04.json](evidence/m0-local-2026-11-04.json).

Build artifacts stay in ignored out/. The package includes build-info.json
with source commit and dirty flag. See M0_ACCEPTANCE.md for exact hardware
steps and interpretation. Basic GPU discovery runs even without a headset.

## Remaining gates and limits

Meta Quest Pro visual depth, alternating-eye viewpoint, world-anchored tracking
and comfortable motion were reported passing by the user on 2026-11-05.
See [the observation record](evidence/m0-quest-pro-2026-11-05.json).
Actual reports verify build provenance, CPU/GPU timings and one successful
host exit following STOPPING. See [hardware reports](evidence/m0-quest-pro-reports-2026-11-05.json).
The user also confirmed menu return and clean timed finish during a recorded
60-second run (2,833 stereo frames). CPU frame median/p95/max were
13.8455/14.1924/29.4084 ms, including runtime waits and non-rendering frames.
The predicted display period is approximately 72 Hz; no measured display cadence
or compositor drop/reprojection claim is made. Quest Link disconnect/recovery
passed: the host stopped its session and exited 0; after reconnecting, a fresh
launch rendered 1,214 stereo frames, exited 0 and passed user visual confirmation.
**Core M0 hardware acceptance passes for this tested configuration.** Hardware
graphics debug validation, arbitrary GPU removal/runtime crash and compositor
metrics remain unverified. No production game integration exists. G1 recorded
presentation and D3D12 execution evidence. The subsequent
[early-start capture attempt](evidence/g1-frame-capture-attempt-2026-11-05.json)
lost its game connection before requesting a frame. The owned capture/replay
fixture passed; game world-pass and camera control remain unverified. The
first loading method stopped. A later isolated Steam development context kept
the early-loaded target through D3D12 readiness and a title-menu frame request,
but the capture failed allocating an initial-state READBACK heap with
E_OUTOFMEMORY. No game frame was saved. The
[memory review](research/g1-capture-memory.md) led to a
[fresh-process Low-texture attempt](evidence/g1-low-texture-capture-2026-11-06.json)
on updated Steam build 26480954. It also failed with a readback-buffer
E_OUTOFMEMORY and produced no RDC. Capture-session video settings differed
without agent changes, limiting comparison. All recorded original settings,
including Ultra textures, were restored and verified before normal close.
The helper now retains a bounded per-case diagnostic-log snapshot; five
owned-file tests and an actual embedded-Python prelaunch rejection passed.
Review a different capture-memory strategy before another game attempt.
CPU and GPU eye/stereo timing windows are bounded and report sample counts.
Runtime dropped/reprojected frame counters and display-latency measurements
remain NOT_RUN. D3D12 debug validation is SKIPPED here because Graphics Tools
is unavailable; Windows capability installation requires elevation.
Single-flight CPU fence waits favor correctness over throughput; performance
and comfort require measurement on hardware. No automatic session reconnection.
Vendor calls may block during runtime/driver faults; no hard watchdog guarantee.

After successful hardware acceptance, G1 starts with permitted, non-invasive
game version/storefront/hash, permissions/protections and DX12 presentation
evidence. Produce an evidence-backed plan or stop decision before hooks.
