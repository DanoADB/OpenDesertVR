# OpenDesertVR

Free, open-source VR adaptation research for Crimson Desert (Windows).
Community project, not affiliated with Pearl Abyss.

**M0: standalone OpenXR/D3D12 stereo testhost implemented. Headset verification
is pending. No in-game VR support exists.** VRcade remains a future roadmap.

The testhost renders original red/green/blue cubes at 1/3/8 meters from
independent runtime eye poses and asymmetric projections. It runs without
Crimson Desert. Game adapter capabilities stay Unsupported/Unverified.

## Build and test

Windows x64, Visual Studio 2022 C++ tools/Windows SDK, CMake 3.24+:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
```

Dependencies download on configure from immutable revisions with SHA256 checks.
The ZIP includes the OpenXR loader, redistributable MSVC DLLs, config and
license notices. Install/activate your vendor OpenXR runtime separately.
See [manual headset checklist](docs/M0_ACCEPTANCE.md) and
[actual evidence](docs/M0_PROGRESS.md).

```powershell
./out/build/release/Release/odv-cli.exe doctor
./out/build/release/Release/odv-testhost.exe --config config/testhost.json --report out/headset-report.json
```

Doctor/export-report emit versioned sanitized JSON. Missing requirements
return exit 3. The host defaults to 60 seconds; Ctrl+C requests exit.
CPU frame timings are measured; GPU/reprojection metrics remain NOT_RUN.
Core/math tests can also use the portable-debug Ninja preset without Windows;
the XR host and graphics test are Windows-only.

## Boundaries

No game hooks, injection, protected binary edits, game files, telemetry,
paid injector dependency or DRM/anti-cheat circumvention. Synthetic stereo
does not prove the game can render additional views. Headless CI, software
GPU tests, actual headset tests and actual game tests are separate evidence.

Read [technical specification](docs/OpenDesertVR_Technical_Specification.md),
[build prompt](Codex_Master_Prompt.md), [VRcade roadmap](docs/VRCADE_ROADMAP.md),
[architecture](docs/architecture.md) and [G1 research plan](docs/RESEARCH.md).

New implementation is MIT ([LICENSE-MIT](LICENSE-MIT)); the initial repository
retains [CC0](LICENSE). Third-party licenses are preserved in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
