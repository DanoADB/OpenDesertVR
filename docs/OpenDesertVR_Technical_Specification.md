# OpenDesertVR — Technical Specification
**Version:** 0.1 / 2026-11-04  
**Target:** Windows x64; Crimson Desert; C++20; D3D12; OpenXR  
**Project phase:** feasibility / M0. Every statement about BlackSpace internals is unverified until recorded evidence is collected.

## 1. Product thesis
Deliver a community-owned, free and open-source VR adaptation for legally owned Windows PC copies of Crimson Desert. Deliver actual per-eye scene views, headset tracking, gamepad input, usable 3D/HUD presentation, and practical comfort features; do not charge for source/binaries. Begin with third-person seated/standing gamepad play. First-person cameras and motion-controlled gameplay are non-goals for initial release. Architecture may enable other games later, but initial validation must use Crimson Desert only.

## 2. Non-goals and constraints
Do not distribute game files, patches containing proprietary bytes, authentication bypasses, reverse-engineered proprietary code, anti-cheat avoidance, malicious injection, stealth mechanisms, telemetry, or performance promises. Do not depend on Luke Ross proprietary code. Do not mistake stereo SBS duplication, monoscopic projection, or depth reconstruction for two genuine world renders. MIT applies to original project source only; third-party components must be license-audited, pinned and attributed.

## 3. Compatibility
- Windows x64, DirectX 12, native OpenXR. SteamVR, Meta Quest PCVR, and other OpenXR runtimes are targets, not guaranteed compatibility.
- Steam, Epic, Xbox PC/Game Pass: all **untested** pending executable/presentation-path analysis. Do not assume identical integration permissions. Console is out of scope.
- Headset and game must not be required for CI and basic developer builds.
- Diagnose unsupported configurations clearly and fail closed without modifying game state.

## 4. Architecture
Separate portable VR and graphics core from a game adapter. Suggested modules:
- **odv_core**: typed Result/errors, versioned JSON config, CLI, bounded structured logs with sanitized export and no persistent identifiers.
- **odv_math**: Vec3/Quat/Mat4, transform composition, asymmetric per-eye projection, handedness/unit conventions, IPD/disparity tests.
- **odv_openxr**: loader/runtime discovery, extension negotiation, xr instance/system/session, graphics requirements, reference spaces, frame lifecycle, view location, session loss.
- **odv_d3d12**: GPU adapter selection matching OpenXR requirements, device/queue creation, command allocators, transitions, fences and swapchain image ownership.
- **odv_stereo**: independent synthetic left/right view matrices with near/mid/far diagnostic objects, predictable reference geometry and depth cues.
- **odv_game_crimson**: build fingerprint/capability interfaces, compatibility strategy and explicit unsupported adapter until validated.
- **odv_bridge (future)**: optional, separately consented integration with published safety constraints. Do not build actual graphics hooks or inject into game during M0.
- **odv_ui (future)**: HUD world depth, recenter, cutscene comfort, camera shake damping and user settings.

Prefer narrow abstractions:
`IGameAdapter::ProbeCapabilities() -> CapabilityReport`
`IXrSession::PollEvents() / BeginFrame() / LocateViews() / EndFrame()`
`IGraphicsBridge::GetCapabilities() / AcquireEyeTargets() / Submit()`.
Capabilities include build, graphics API, per-eye camera access, depth, motion vectors, HUD layers, frame ownership, adapter compatibility and error context. Return Unsupported/Unverified rather than speculate.

## 5. OpenXR/D3D12 critical correctness
Use Khronos official OpenXR SDK/headers with `XR_KHR_D3D12_enable` where supported. Query graphics requirements before D3D12 device selection; select compatible adapter. Implement OpenXR event-driven session states and `xrWaitFrame → xrBeginFrame → xrLocateViews → render both views → xrEndFrame` with correct swapchain acquire/wait/release and image layouts. Synchronize graphics submission using D3D12 fences. Handle runtime missing, HMD absent, extension missing, GPU mismatch, invalid state, device loss and session stopping cleanly. Respect expected coordinate systems, pose timestamps and asymmetrical projection; test left/right disparity against a deterministic baseline.

## 6. Stereo and camera proof
True stereo criterion: left and right eye images generated from geometrically distinct camera positions and projections, with expected near/far disparity, no shared copied view. Distinct images alone are insufficient unless independently grounded in geometry. Synthetic testhost proves OpenXR plumbing only; it does not prove the game supports additional render views. The G2 game test must demonstrate near/far geometrical differences and stable stereo under movement without rendering artifacts from screen-space effects, shadows, reflections, UI or temporal history.

## 7. Staged gates
**G0 / M0 — independent technical foundation.** Clean MSVC x64 CMake build; no-game D3D12/OpenXR stereo testhost; doctor and export-report CLI; unit/integration/failure-path tests; Windows Debug/Release CI; explicit manual headset test checklist. Mark actual headset validation pending if no connected hardware.

**G1 — safe game integration research.** Record exact version/storefront/build fingerprint (without proprietary payload), inspect publicly permitted diagnostics, evaluate DX12 presentation, game camera approaches, permissions, loading mechanism and anti-cheat/network implications. No injection code until a technique has been reviewed for safety and terms. Outcome: evidence-supported integration plan or stop.

**G2 — real in-game two-eye rendering.** Demonstrate separate world view/projection for both eyes, calibrated stereo near/mid/far across normal gameplay, correct GPU submission, motion artifacts bounded and repeatable; otherwise stop or re-scope. A flat headset display is explicitly not G2.

**G3 — playable third-person VR.** Track head orientation and position, support recentering and gamepad; solve HUD/dialogue/cutscene/camera shake; document accessibility and configurable comfort options.

**G4 — alpha stabilization.** Safe opt-in installation and removal, clean failure path, performance/frame pacing profiles, compatible versions matrix, issues template and release packaging. Never modify protected binaries.

## 8. M0 deliverables
Repository: `CMakeLists.txt`, `CMakePresets.json`, `.editorconfig`, `.clang-format`, `.gitignore`, MIT `LICENSE`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `THIRD_PARTY_NOTICES.md`, GitHub Actions Windows CI.
Source: `src/core`, `src/math`, `src/openxr`, `src/d3d12`, `src/stereo`, `src/game/crimson_desert` (stub), `apps/odv-cli`, `apps/odv-testhost`, `tests/`.
CLI `odv-cli doctor`: report sanitized OS/GPU/OpenXR runtime/extensions/session readiness and missing requirements; do not require headset for basic checks. `odv-cli export-report`: versioned JSON with PASS/FAIL/NOT_RUN and no personal data.
Testhost: independent near/mid/far synthetic geometry, distinct stereo projections, D3D12/OpenXR render loop, debug overlays/logs including per-eye transforms, timings and state.
Docs: `docs/architecture.md`, `docs/feasibility.md`, `docs/evidence/README.md`, `docs/compatibility.md`, `docs/LEGAL_AND_SAFETY.md`, `docs/M0_ACCEPTANCE.md`, `docs/RESEARCH.md`, architecture decisions in `docs/decisions/`.

## 9. Test/acceptance matrix
Unit: transforms, handedness, quaternion normalization, asymmetric frusta, IPD sign, depth disparity, invalid config/version/schema.
Integration: OpenXR session state machine using mock runtime; mocked graphics resource failures; CLI structured output, log scrubbing.
Failure injection: missing runtime, missing headset, unavailable D3D12 extension, adapter mismatch, allocation failure, invalid session state, runtime shutdown, graphics device loss.
CI: Windows x64 MSVC Debug and Release, build and ctest, no game binaries, no headset; license/format checks when appropriate.
Manual: a real headset and OpenXR runtime must display correctly separated near/mid/far geometry; report runtime, GPU, HMD, measured frame pacing, correctness evidence, crash-free shutdown. Hardware tests never marked passed based solely on unit tests.

## 10. Performance quality targets (not guarantees)
Aim to produce stable frame timing appropriate to target headset refresh rather than optimizing only desktop FPS. Report frame time distribution, GPU/CPU timings, dropped/reprojected frames if supported. Avoid specifying minimum playable GPU until empirical profiling. Test motion, combat, cinematics, HUD and pause behaviors independently after G2.

## 11. Risk register and stop conditions
- **Critical:** BlackSpace may not permit independent eye renders without deep engine-level changes. G2 stop condition.
- **High:** temporal effects, motion vectors, camera control, UI and cutscenes may rely on one-camera assumptions.
- **High:** performance impact of extra world rendering may make target GPUs impractical.
- **High:** storefront build and executable protections may prevent safe integration.
- **Medium:** GPU/OpenXR runtime quirks and device mismatch.
- **Medium:** third-party open-source dependency incompatibility or upstream instability.
Each risk needs reproducible evidence and a mitigation experiment, not speculative hardcoded fixes.

## 12. Contribution, safety and distribution
Open GitHub issues, meaningful reproducible test templates, build/version matrix, signed/reproducible releases where feasible, code reviews and CI. Prefer permission-respecting methods and stop rather than circumventing DRM or anti-cheat. Explicitly disallow proprietary game assets and paid-mod source in contributions. Provide user-facing install/remove and safety instructions before G4. Contributions require author permission to license under project MIT terms.

## 13. Immediate next action
Implement **M0 only**, with a small working prototype, evidence and tests. A green CI build establishes compilation and deterministic mathematical correctness, **not** a working Crimson Desert VR mod. End M0 with a precise G1 research plan.
