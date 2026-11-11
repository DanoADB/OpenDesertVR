# Compatibility and evidence grades

| Configuration | Status |
|---|---|
| Windows x64 / MSVC standalone build | See M0_PROGRESS.md for actual local evidence |
| SteamVR OpenXR | UNVERIFIED |
| Meta Quest Pro / Meta PCVR OpenXR / RTX 4080 host | Synthetic stereo, tracking, comfortable motion, menu return and timed shutdown observed; see hardware evidence below |
| Other Meta Quest models/configurations | UNVERIFIED |
| Other OpenXR runtimes/headsets | UNVERIFIED |
| Crimson Desert Steam/Epic/Xbox PC | Unsupported adapter; all build compatibility UNVERIFIED |
| Console | Out of scope |

Requires XR_KHR_D3D12_enable, opaque primary stereo, LOCAL reference space,
a matching D3D12 adapter and a supported RGBA/BGRA format. The sample requests
single-sample eye images. No headset or runtime is needed for deterministic
core/math tests; the WARP graphics test uses Microsoft's software adapter.
CI evidence does not imply real headset or game validation.

On 2026-11-05, the user confirmed stereo depth, alternating-eye viewpoints,
world-anchored tracking and comfortable movement on Meta Quest Pro. The tested
host executable matches the clean 404d38a package. A recorded 60-second run
submitted 2,833 stereo frames, survived menu focus changes and exited 0 through
STOPPING/EXITING; the user confirmed correct menu return and clean finish.
The runtime predicted a roughly 72 Hz display period. These observations cover
this synthetic scene and configuration only, not game support, measured display
latency, dropped frames or every Meta runtime/driver version.
See [hardware reports](evidence/m0-quest-pro-reports-2026-11-05.json) and
[visual observations](evidence/m0-quest-pro-2026-11-05.json).

Quest Link disconnect/fresh-launch recovery also passed on this setup: the
old host stopped and exited 0; after reconnecting, a new host rendered 1,214
stereo frames and exited 0, with user-confirmed correct depth/tracking. This
does not validate arbitrary GPU removal or runtime crashes.
