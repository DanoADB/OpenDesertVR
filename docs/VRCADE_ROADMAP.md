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

### Active priority: Crimson Desert VR showcase (2026-11-06)

User direction supersedes the earlier delivery note that made further testing
of the cube app the next priority. Core M0 headset acceptance already exists
for the tested Quest Pro configuration. New M0 artifacts retain their own
honest hardware status, but repeating cube acceptance is not a prerequisite
for continuing game integration research.

The next user-facing milestone is the actual Crimson Desert world rendered
from distinct left/right viewpoints in the headset. Existing third-party mods
support feasibility; they do not supply this project's missing camera and
render integration. No completion date is established yet.

Work in this order:

1. Identify and validate the current game's world camera/render inputs in an
   actual 3D scene. The corrected observer now reaches direct and indirect
   graphics commands and has acquired one 768-byte upload input at the title
   menu. This is a shader-input observation, not an identified camera. Compare
   static-world and camera-movement data, then test the most credible camera
   candidate. Resolve capture lifetime problems only as needed to perform this
   experiment reliably; do not expand the observer into a general toolkit.
2. Demonstrate a reversible camera/view change affecting actual world
   geometry, then establish how to render distinct eye views with correct
   submission and temporal history. Camera discovery alone is not stereo.
3. Feed those independent game views into the existing OpenXR path and
   provide a simple launchable showcase build for Quest Pro testing. Initially
   target third-person gamepad use in a controlled scene; document limitations.

Until that demonstration, defer cube-scene embellishments, launcher expansion,
cosmetic UI work, broad settings work and unrelated infrastructure. Tests and
instrumentation should answer a concrete camera/render question or protect
the integration being changed. Polish, broader gameplay coverage and comfort
tuning follow the showcase. A flat game display does not satisfy this goal.

**Priority reaffirmed, 2026-11-06:** optimize for the first visible game-world
stereo demonstration. Existing Crimson Desert VR mods support feasibility;
our next deliverable must establish our own camera control and eye rendering.
There is no additional cube acceptance gate. Limit the first showcase to one
controlled scene and gamepad input, then improve gameplay coverage and polish.
See [the first live graphics-input result](evidence/g1-game-graphics-input-2026-11-06.json).

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

**Delivery update, 2026-11-06:** clean 112cdad adds a native Windows headset
testing app with readiness, stereo Start/Stop, reports and visual observations.
The portable package launches without scripts. Hosted Debug/Release validation
passes 35 core native checks, 94 Python tests and five optional native checks
per configuration. Current-build hardware acceptance is NOT_RUN; previous
Quest Pro results apply to 404d38a. Device testing remains available when the
user is ready; the active engineering priority above is game integration.
This app is an M0 test interface, not the VRcade launcher.
See [package evidence](evidence/m0-desktop-package-2026-11-06.json).

The original foundation snapshot follows:

Standalone OpenXR/D3D12 stereo testhost, diagnostics, configuration, inactive
game adapter, pinned dependencies, packaging and Windows CI are implemented.
Local MSVC Debug/Release builds each report 33 tests passed, zero failed and
one skipped (optional D3D12 debug validation unavailable locally). Hosted
Windows CI passes all 34 checks in both configurations, including debug validation.
WARP raster/readback verifies separate-eye pixel disparity. The active Meta
runtime is discoverable; no headset system was available. On 2026-11-05, core headset acceptance passed on Meta Quest Pro: user-confirmed
stereo/tracking/comfort/menu return, recorded clean timed shutdown, and tested
Quest Link disconnect followed by successful fresh-process rendering after
reconnection. Extended graphics validation and compositor metrics remain
unverified. No game VR integration exists.
See `docs/M0_PROGRESS.md` and `docs/M0_ACCEPTANCE.md`.
