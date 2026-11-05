# OpenDesertVR

Free, open-source experimental PCVR adaptation research for **Crimson Desert** (BlackSpace Engine, Windows DirectX 12). This is a community project, not affiliated with Pearl Abyss.

**Status: M0 — standalone OpenXR/D3D12 foundation. No in-game VR support has been implemented or verified.**

## Principles
- Open source, no paywall, no proprietary VR mod dependency, no telemetry.
- Do not redistribute Crimson Desert binaries/assets, circumvent DRM, or interfere with anti-cheat/network services.
- Never claim duplicated side-by-side images or depth reprojection provide genuine independent per-eye geometry.
- Separate evidence from hypotheses; game adapter remains inactive until safe, authorized experiments prove feasibility.
- No storefront compatibility claims without manual testing.

## Start with Codex
Read [Technical Specification](docs/OpenDesertVR_Technical_Specification.md) and [Codex Master Prompt](Codex_Master_Prompt.md). Execute **M0 only**. The required initial product is an independent D3D12/OpenXR stereo testhost, hardware diagnostics, deterministic tests, and Windows CI. It must build without a Crimson Desert installation.

## Development
Windows 11 x64; Visual Studio 2022 C++ workload; CMake; Windows SDK; Khronos OpenXR SDK. Full build commands and pinned dependency versions must be implemented and documented in M0. Any missing runtime or headset must be reported as *not tested*, not passed.

## Roadmap
- **G0/M0:** independent OpenXR/D3D12 stereo renderer, diagnostics, headless tests/CI
- **G1:** non-invasive game-build and DX12 feasibility research
- **G2:** demonstrate genuinely distinct per-eye views in game
- **G3:** head tracking, camera integration, comfort
- **G4:** stabilization and public alpha

License: MIT for **original project code** only, with separate third-party notices. Game and brand remain their owners' property.
