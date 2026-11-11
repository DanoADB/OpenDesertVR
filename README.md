# OpenDesertVR

Free, open-source VR adaptation research for Crimson Desert (Windows).
Community project, not affiliated with Pearl Abyss.

**M0: standalone OpenXR/D3D12 stereo testhost implemented; core headset
acceptance passed on Meta Quest Pro (2026-11-05). No in-game VR support exists.**
VRcade remains a future roadmap. [G1's non-invasive baseline](docs/research/g1-baseline.md)
records a live-integration hold pending a permitted approach.

The testhost renders original red/green/blue cubes at 1/3/8 meters from
independent runtime eye poses and asymmetric projections. It runs without
Crimson Desert. Game adapter capabilities stay Unsupported/Unverified.

## Test from the Windows app

Extract the complete portable ZIP and double-click **OpenDesertVR.exe**.
Connect your usual PCVR/OpenXR setup, then choose **Check readiness** or **Start 60-second test**.
The default **Offline depth stereo - owned courtyard** mode reconstructs left/right
images from an original scene's matched color/depth and streams them to OpenXR.
It has a static source camera and no head tracking; thin black disocclusion gaps
may appear. Select **Original tracked cube calibration** for the earlier tracked test.
**Stop** requests an orderly exit. After the test, tick the visual checks you
personally completed, add optional notes, and select **Save observations**.
**Open reports** opens the current run folder. No terminal, Python or PowerShell
is needed. The app does not start a test automatically.

Each operation creates a fresh folder under `reports`; if the package folder is
not writable, reports go to `%LOCALAPPDATA%/OpenDesertVR/reports`.
Technical results and your observations are separate. Unchecked visual checks
stay NOT_RUN. The app clearly labels simulated runtimes and disables saving
headset observations for them. This tests owned content and never opens the game.
The new preview still needs its own headset acceptance. See the
[reconstruction contract and tests](docs/research/depth-stereo-preview.md).

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
The older Run-Headset-Test.cmd collector remains available for troubleshooting.

```powershell
./out/build/release/Release/odv-cli.exe doctor
./out/build/release/Release/odv-testhost.exe --config config/testhost.json --report out/headset-report.json
```

Doctor/export-report emit versioned sanitized JSON. Missing requirements
return exit 3. The host defaults to 60 seconds; Ctrl+C requests exit.
CPU and GPU eye-render timings are measured; runtime reprojection metrics remain NOT_RUN.
Reports label the test-only runtime explicitly and keep hardware validation separate.
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
