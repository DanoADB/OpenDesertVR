# Codex Master Implementation Prompt — OpenDesertVR

You are the principal C++ graphics/XR engineer implementing OpenDesertVR, a free, public, MIT-licensed (for original code) VR adaptation research effort for the Windows PC version of Crimson Desert. Read `docs/OpenDesertVR_Technical_Specification.md` **in full**; it is the authoritative product specification. Inspect existing repository first and preserve appropriate work.

## Assignment
Implement **M0 / G0 only**, not a purported finished VR mod. Deliver runnable C++20/DirectX 12/OpenXR standalone stereo testhost, deterministic math/graphics tests, diagnostics and Windows CI. This phase must work without Crimson Desert installed.

## Mandatory boundaries
- No paid/proprietary VR injector dependencies, game asset redistribution or telemetry.
- Do not copy R.E.A.L. VR implementation or bypass DRM, anti-cheat or protections.
- Do not modify game binaries, implement stealth persistence, or ship injection/hook code during M0.
- Do not invent BlackSpace offsets, SDKs, shader signatures, memory layouts, engine hooks or compatibility claims.
- Distinguish a real per-eye render from duplicate side-by-side/depth reprojection; label unknowns UNVERIFIED.
- Hardware/game-specific results must never be marked PASS unless actually tested.
- Use official Khronos OpenXR interfaces, D3D12 with correct adapter matching and synchronization.
- No unnecessary approval requests. When hardware/Windows cannot be used, implement everything possible, and document exact manual verification commands.

## Build deliverables
CMake/MSVC Windows x64 C++20 application with pinned audited deps; standard repo hygiene, MIT license/third-party notices, contributing/security docs; GitHub Actions Debug/Release builds and ctest. Provide reproducible presets.

## Source deliverables
1. `odv_core`: bounded structured logging, errors/Result, CLI and validated versioned JSON configuration.
2. `odv_math`: vectors, quaternions, matrices, eye poses and asymmetric frusta; test coordinate transforms, IPD and disparity.
3. `odv_openxr`: runtime discovery, extension negotiation, D3D12 graphics binding, system/session state, reference space, frame cycle and session-loss handling.
4. `odv_d3d12`: device/queue matching, fences, resource state management, swapchain image rendering and diagnostics.
5. `odv_stereo`: independent synthetic camera for each eye and near/mid/far depth calibration objects.
6. `odv_game_crimson`: interfaces, build capabilities and an explicit Unsupported/Unverified stub; **no live game hooking**.

## Programs
`odv-cli doctor`: display sanitized runtime/GPU/extension readiness and explicit missing requirements, without demanding a headset for simple checks.
`odv-cli export-report`: JSON PASS/FAIL/NOT_RUN with redacted identifiers.
`odv-testhost`: standalone stereo D3D12/OpenXR sample with session status and per-eye pose/geometry reporting. Provide non-XR unit/integration test paths where sensible.

## Tests and docs
Unit test matrix/projection/handedness/quaternion/IPD/config; simulate absent runtime/headset/extension, wrong graphics adapter, failed D3D12 allocations and session loss. Windows CI must not claim headset or game testing. Create `docs/architecture.md`, `docs/feasibility.md`, `docs/evidence/README.md`, `docs/compatibility.md`, `docs/LEGAL_AND_SAFETY.md`, `docs/M0_ACCEPTANCE.md`, `docs/RESEARCH.md`, and ADRs for consequential decisions.

## Execution plan
1. Describe concise implementation plan.
2. Make actual files and code in coherent commits. Prefer small independently testable interfaces and RAII resource ownership.
3. Test incrementally. Run `cmake --preset windows-msvc-debug`, `cmake --build --preset windows-msvc-debug`, `ctest --preset windows-msvc-debug` or matching actual preset names. If environment cannot run a command, state why; do not simulate test success.
4. Keep full frame-loop/swapchain synchronization correct, with orderly shutdown on OpenXR session loss.
5. At completion summarize changed files, build/test evidence, unverified conditions, remaining risks, and exact next G1 step.

## Exit criteria
Fresh Windows checkout builds without a Crimson Desert installation; CI passes deterministic tests; OpenXR stereo testhost is ready for *manual* hardware verification; diagnostics accurately identify missing runtime/headset/GPU support; game adapter stays inactive and honest. **Start implementing M0 now. Do not proceed to game integration until evidence gates are passed.**
