# Manual M0 headset acceptance (PENDING)

This package tests synthetic geometry only. No game installation is needed.
M0 does not pass the roadmap's hardware exit gate until this checklist is
performed on a real headset and evidence is recorded.

## Build and deterministic verification

Windows x64, Visual Studio 2022 C++ tools, Windows SDK and CMake 3.24+:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
```

The first configure downloads immutable hash-checked dependencies. No game
files or headset are needed for builds/tests. The WARP test rasterizes
independent eyes and checks red/green/blue object pixel disparity. It is a
software GPU test, not headset evidence.

## Run the portable Release package

Extract out/packages/OpenDesertVR-M0-win64.zip into a writable directory.
Use your headset vendor's normal controls to select the active OpenXR runtime,
connect/wake the headset in PCVR mode, and enable its PC connection.
The host does not change your runtime registration.

```powershell
.\odv-cli.exe doctor
.\odv-cli.exe export-report --output doctor-report.json
.\odv-testhost.exe --config config/testhost.json --report headset-report.json
```

Default duration is 60 seconds; Ctrl+C requests an orderly session exit.
Doctor checks basic GPU/runtime/extensions/system and runtime adapter/device;
it does not create a presentation session. Exit 0 means these named checks
succeeded; 3 means a requirement is missing; 1/2 indicates execution/usage
failure. A missing headset is expected to fail readiness, not the build.
Runtime/driver failures can stall inside vendor calls; record that as a failure.

## Hardware checklist (all NOT_RUN until observed)

- [ ] Record package build-info commit, runtime version/family, non-unique HMD
      and GPU model, refresh rate and connection mode.
- [ ] Both eyes display red near (1 m), green mid (3 m), blue far (8 m) cubes.
      LOCAL space is initialized around eye height; look forward initially.
- [ ] Close each eye in turn: expected horizontal parallax changes by depth.
      No eye inversion, duplicated mono image, vertical mismatch or clipping.
- [ ] Translate/rotate the headset: world geometry remains anchored, correct
      motion direction, no unexpected scaling or head-locked world.
- [ ] Console reports both eye positions and 108 vertices per eye. Inspect
      headset-report.json for distinct poses and asymmetric FOV when provided.
- [ ] Record CPU frame median/p95/max and sample count. These include wait and
      submission/fence time; they are not GPU timestamps or display latency.
      GPU timestamp and runtime reprojection/drop metrics remain NOT_RUN.
- [ ] Test idle/visibility changes, runtime menu, session STOPPING and Ctrl+C.
      No crash or graphics validation error; confirm orderly destruction.
- [ ] Disconnect/reconnect or shut down runtime: host fails closed, then a
      fresh process can reconnect. Actual device/session-loss test is manual.
- [ ] Repeat with debug_layer=true after installing Windows Graphics Tools;
      collect local D3D12 debug messages. Do not claim clean validation untested.

Review raw console logs before sharing; vendor loader output can include paths.
Exported JSON strips persistent identifiers. Record perceived stereo/comfort
and observed shutdown separately; reports never auto-award visual validation.

## Failure interpretation

Missing runtime: install/select an OpenXR runtime.
Missing headset: connect, wake and enable PCVR.
Missing D3D12 extension: choose a compatible runtime.
Adapter mismatch: check vendor runtime/GPU routing; no fallback GPU is selected.
Allocation/device failure: check memory, drivers and local debug output.
Session loss: restart runtime and host. No game state is touched.

## Gate and next step

Keep G0/M0 hardware acceptance pending until observations above pass.
Then begin G1 with a non-invasive version/storefront/permissions and DX12
presentation evidence record. Do not add game hooks or claim game VR support.
