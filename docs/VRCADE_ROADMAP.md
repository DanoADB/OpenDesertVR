# VRcade — North-star roadmap

**Decision:** VRcade is the long-term open-source launcher/platform; OpenDesertVR is the first technical test bed, not the final product. **Status:** roadmap only, no functional launcher or completed VR mod.

## Mission
One free, local-first launcher for compatible PC games that selects honest, evidence-backed headset experiences. No mandatory cloud account, telemetry, paywall, game-binary redistribution, DRM circumvention or anti-cheat evasion.

## Modes and promises
- **Theater**: game plays on a 2D virtual display. Broad compatibility target, not universal: capture, protected content, overlays and runtime limitations may prevent some titles.
- **Immersive 3D**: supported native 3D sources or optional clearly labeled depth-based reconstruction. Do not mislabel reconstructed stereo as independent game rendering.
- **Full VR**: independently rendered left/right game world views, 6DOF pose, usable camera/HUD/controller behavior. Only verified per-game engine adapters qualify.

## Architectural boundaries to preserve immediately
1. Core libraries (`odv_math`, `odv_openxr`, `odv_d3d12`, diagnostics, synthetic XR testhost) must have **no Crimson Desert imports** and no Steam-only assumptions.
2. Implement game-specific research/integration behind `IGameAdapter`, starting with `CrimsonDesertAdapter` in an explicitly unsupported state until proven.
3. Keep capability reports versioned, with `Supported/Unsupported/Unverified` for theater, native stereo, reconstructed depth, head tracking, motion control, and game build compatibility. Only add production schemas when an actual consumer exists.
4. Respect legally acquired copies and platform protections. Fail closed. No stealth hooks or DRM/anti-cheat bypass.
5. Design OpenXR handling independent of storefront; Steam is only the first discovery integration for future VRcade.
6. Keep dependencies and notices auditable; permissive core license when possible.
7. Treat headless CI, hardware test, and actual game VR test as distinct evidence grades.

## Roadmap and gates

### A. OpenDesertVR M0: independent XR substrate — NOW
C++20/CMake, headset runtime discovery, D3D12 adapter/queue/fence/swapchain, synthetic **real** per-eye geometry, math tests, health diagnostics, Windows CI. Exit only after actual headset test verifies pose, correct stereo and orderly shutdown. **No launcher yet**.

### B. Crimson Desert G1–G2: integration proof
Investigate safe permitted game loading/presentation and camera approaches across PC storefront builds. Demonstrate independently rendered world views and correct reprojection/motion under gameplay. This is a **hard go/no-go**; if impossible without disallowed techniques, stop or redesign rather than ship fake VR.

### C. OpenDesertVR G3–G4: playable game adapter
Third-person gamepad, 6DOF, HUD/dialogue/cutscene comfort, settings, build-version compatibility, uninstall/rollback and documented alpha. Make game adapter independent of OpenXR host internals.

### D. VRcade MVP: launcher + theater baseline
Start separate `VRcade` repository after M0 is stable and G1 direction is understood. Version/reuse core libs rather than copying them. Desktop launcher with Steam game discovery and manually added local executables; no external credentials or scraping required. OpenXR theater display with screen size/distance/curvature, controller/keyboard pass-through, recenter, safe capture and runtime selection, accessible plain desktop fallback. Present profile compatibility and source attribution. Xbox PC, Epic and GOG can follow.

### E. VRcade adapters: community ecosystem
Versioned capability manifest and opt-in adapter installs. Supported modes selected per-title with realistic defaults; compatibility reports and regression checks. Reuse documented open-source adapters where licenses permit; NEVER silently modify games. Third-party compatibility must be earned through actual tests.

### F. VRcade alpha and sustainability
Documentation, local-first privacy, portable profiles, safe error recovery, contributions, licenses, release packaging, reproducible signed binaries if feasible. Keep modes labeled honestly.

## Next technical decisions
- Implement and verify M0 before building game discovery or a VR library UI.
- Don't add a universal DX12 injection framework based on unverified claims.
- Keep first game adapter confined to Crimson Desert but export only *validated* general patterns.
- Record architecture decisions in `docs/decisions` whenever changing core abstractions.

## Current implementation snapshot (2026-11-04)
CMake presets, Windows CI configuration, stereo math and headless test source have been committed. They have **not** yet been independently verified by a successful build. OpenXR/D3D12 testhost and actual game integration remain unimplemented. See `docs/M0_PROGRESS.md`.
