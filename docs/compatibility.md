# Compatibility and evidence grades

| Configuration | Status |
|---|---|
| Windows x64 / MSVC standalone build | See M0_PROGRESS.md for actual local evidence |
| SteamVR OpenXR | UNVERIFIED |
| Meta Quest PCVR OpenXR | UNVERIFIED (runtime discovery alone is insufficient) |
| Other OpenXR runtimes/headsets | UNVERIFIED |
| Crimson Desert Steam/Epic/Xbox PC | Unsupported adapter; all build compatibility UNVERIFIED |
| Console | Out of scope |

Requires XR_KHR_D3D12_enable, opaque primary stereo, LOCAL reference space,
a matching D3D12 adapter and a supported RGBA/BGRA format. The sample requests
single-sample eye images. No headset or runtime is needed for deterministic
core/math tests; the WARP graphics test uses Microsoft's software adapter.
CI evidence does not imply real headset or game validation.
