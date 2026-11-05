# GitHub Adjacent Projects — Research & Adoption Plan
**Research date:** 2026-11-04  
**Project:** OpenDesertVR  
**Status:** Source and project claims surveyed; no upstream component integrated or tested yet.

Current follow-up: [pinned stereo-source audit](research/stereo-pipeline-audit.md)
and [M0 implementation evidence](M0_PROGRESS.md). The survey below is the
initial reconnaissance snapshot, not current build or hardware status.

## Executive decision
Use Khronos OpenXR hello_xr and Microsoft DirectX 12 samples as the **M0 reference baseline**; examine the MIT-licensed Cyberpunk2077-VR-OpenSource and REFramework projects for architectural approaches, not proof of independently rendered game stereo. Defer Crimson Desert modding, archive handling, and runtime integrations to **G1 after M0**. Never inherit license assumptions from a repository's README without checking each copied source file, submodule and dependency.

## Ranked candidates

### A1. KhronosGroup/OpenXR-SDK-Source
- Repo: https://github.com/KhronosGroup/OpenXR-SDK-Source
- License verified: Apache-2.0 (upstream LICENSE; dependency notices still required).
- Use: official `src/tests/hello_xr` sample; runtime/session state, graphics binding, swapchain management and asymmetric per-eye projection.
- Action: model independent `odv-testhost` on the D3D12 sample where applicable; pin upstream tag/commit. Preserve Apache notices where copying.
- Acceptance: OpenXR runtime lifecycle, D3D12 graphics requirements and correct per-eye frame loop compile and pass diagnostics in MSVC.

### A2. microsoft/DirectX-Graphics-Samples
- Repo: https://github.com/microsoft/DirectX-Graphics-Samples
- License verified: MIT.
- Use: D3D12 adapter/device creation, command queues, barriers, fences, resource lifetime, crash/device removal.
- Action: identify smallest reusable patterns for testhost; avoid shipping entire sample framework.
- Acceptance: deterministic and well-instrumented GPU setup; clear device-lost and adapter-mismatch behavior.

### A3. pinducoding/Cyberpunk2077-VR-OpenSource
- Repo: https://github.com/pinducoding/Cyberpunk2077-VR-OpenSource
- License verified: MIT.
- README claims OpenXR, D3D12 hook, backbuffer-to-swapchain copy, camera tracking **needing testing**. Claims not independently validated.
- Use: design comparison for runtime-to-game boundaries, thread safety, state recovery and coordinate conversion.
- Critical warning: mono backbuffer capture/texture copy is not by itself two genuine eye renders.
- Action: review specific source files, upstream build status, dependencies; write an audit before code adoption.

### A4. praydog/REFramework
- Repo: https://github.com/praydog/REFramework
- License verified at root: MIT; transitive sources may differ.
- Use: architectural lessons from an established game engine-specific VR integration, session handling, camera features and comfort.
- Action: map interface patterns, not RE Engine offsets or binaries. Review subcomponent license headers.

### B1. tkhquang/CrimsonDesertTools
- Repo: https://github.com/tkhquang/CrimsonDesertTools
- License verified: MIT.
- Use: game-specific modular plugin structure, versioning, diagnostics. Only study and evaluate after safe G1 research authorization.
- Action: assess whether its approach is relevant to per-eye camera/view/projection access. Do not conclude this from transmog features alone.

### B2. FitzDegenhub/UltimateCameraMod
- Repo: https://github.com/FitzDegenhub/UltimateCameraMod
- README: camera settings, FoV, centered HUD, steadycam, many camera states; claims Steam/Epic/Xbox PC compatibility.
- License: **unverified** in repository fetch. Treat source as reference-only; obtain explicit license/permission before copying.
- Critical distinction: mod edits archive camera configuration; it does not establish access to runtime view/projection matrices, multiple rendering passes, or a legally/technically supported route into stereo.

### B3. faisalkindi/CrimsonDesert-UltimateModsManager
- Repo: https://github.com/faisalkindi/CrimsonDesert-UltimateModsManager
- Use: assess mod packaging, conflict detection, uninstallation, compatibility after integration exists.
- License and implementation not audited: reference-only until verified.

### B4. KiefBC/desert-modding
- Repo: https://github.com/KiefBC/desert-modding
- Use: observe game native plugin packaging and version drift; no confirmed camera/stereo capability. License not audited.

### C1. effcol/Stereopticon
- Repo: https://github.com/effcol/Stereopticon
- License verified: LGPL-2.1-or-later, with differently licensed shaders and upstream components.
- Use: architecture and output-format ideas; not a drop-in MIT code library. Full OpenXR direct frame submission described as in progress.
- Action: reference only initially; consult licensing requirements before linking or distributing modified components.

### C2. SpreadTheBirb/ResidentEvil5VR
- Repo: https://github.com/SpreadTheBirb/ResidentEvil5VR
- Use: interesting per-eye projection, HUD, OpenXR architecture from another engine.
- Root LICENSE retrieval failed: treat as reference-only until actual license and dependencies are checked.

### C3. sathishssj3/Stereix-Engine
- Repo: https://github.com/sathishssj3/Stereix-Engine
- Important: its root LICENSE was explicitly **proprietary**, despite its public source. **Do not copy/reuse**. Public repository does not equal open-source permission.
- Its README claims conversion of mono output to stereo; this is also not evidence of independent geometry per eye.

## Suggested architecture alignment
- Keep `odv_openxr` based on Khronos APIs and correct D3D12 graphics binding.
- Keep `odv_d3d12` narrowly scoped to testhost infrastructure, use Microsoft examples for synchronization/device-loss handling.
- `odv_math` owns documented coordinate system/frustum conversions with deterministic tests.
- `odv_stereo` must render two distinct geometric world views, not copy a mono frame twice.
- `odv_game_crimson` remains Unsupported/Unverified in M0; G1 must demonstrate lawful game integration path without altering protected content or evading anti-cheat.
- Abstract frame rendering from game integration to support later experiments without rewriting OpenXR.
- Hold project at G2 if separate in-game camera transforms/render passes cannot be demonstrated.

## Priority actions for Codex
1. Inspect Khronos `hello_xr` D3D12 implementation and list the minimal compile units/API calls needed for M0.
2. Inspect Microsoft's smallest D3D12 sample for adapter selection, queue/fence/resource barriers and device loss.
3. Audit Cyberpunk project code paths: where do views originate? Where are eye camera matrices set? Does it render the game scene from both eye poses or copy one backbuffer? Record evidence with source paths/line refs.
4. Audit REFramework at an architecture level; document any engine-specific coupling.
5. Produce a dependency manifest including source URL, pinned revision, license, copied files, and notices.
6. Integrate only sources whose licenses allow intended redistribution and where doing so reduces risk compared with clean-room implementation.
7. Deliver M0 without importing native Crimson Desert hooks.

## Attribution template
| Component | Repository | Pinned revision | SPDX license | Files used | Notices copied | Local modifications |
|---|---|---|---|---|---|---|
| (fill before importing) | | | | | | |

## Verification limits
This reconnaissance checked repository README descriptions and selected root LICENSE files. It did **not** compile candidate projects, audit all dependencies, establish current operational status, prove game compatibility, or prove real stereoscopic world rendering. Research must not be confused with tested engineering capability.
