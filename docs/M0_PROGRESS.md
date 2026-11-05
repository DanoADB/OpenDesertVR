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
- Revision/hash-pinned Khronos loader and JSON dependency, dependency notices,
  architecture/safety/research/manual acceptance docs and Release packaging.
- Windows Debug/Release CI definitions including WARP graphics checks.

## Observed locally (Windows x64)

MSVC 19.44.35229, Windows SDK 10.0.26100.0, Visual Studio 2022 Build Tools.
No Crimson Desert installation was used.

| Check | Evidence |
|---|---|
| Debug configure/build | PASS |
| Release configure/build | PASS |
| CTest Debug | PASS: 6/6 |
| CTest Release | PASS: 6/6 |
| Real D3D12 WARP raster/readback | PASS; near/mid/far pixel disparities 20.1375 / 7 / 4.95688 |
| Actual loader with absent runtime override | PASS: categorized FAIL readiness, exit 3, sanitized JSON |
| Installed runtime discovery | PASS: Meta runtime exposes XR_KHR_D3D12_enable |
| Testhost without available headset | Expected FAIL readiness: XR_ERROR_FORM_FACTOR_UNAVAILABLE (-35), zero submitted frames |
| Real headset visual stereo/tracking | NOT_RUN |
| Real headset stop/disconnect/device loss | NOT_RUN |
| Game integration/stereo | NOT_RUN; adapter inactive |
| Hosted GitHub Actions execution | NOT_RUN locally; workflow configured |

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

Build artifacts stay in ignored out/. The package includes build-info.json
with source commit and dirty flag. See M0_ACCEPTANCE.md for exact hardware
steps and interpretation. Basic GPU discovery runs even without a headset.

## Remaining gates and limits

M0 implementation is ready for manual hardware testing; the roadmap's M0
exit gate is still PENDING. No G1 integration work has begun.
CPU frame timing is bounded and reported, but GPU timestamps, runtime dropped/
reprojected frame counters and display-latency measurements remain NOT_RUN.
Single-flight CPU fence waits favor correctness over throughput; performance
and comfort require measurement on hardware. No automatic session reconnection.
Vendor calls may block during runtime/driver faults; no hard watchdog guarantee.

After successful hardware acceptance, G1 starts with permitted, non-invasive
game version/storefront/hash, permissions/protections and DX12 presentation
evidence. Produce an evidence-backed plan or stop decision before hooks.
