# ADR 0003: hold game-adapter implementation at the current G1 evidence gate

Date: 2026-11-06. Status: accepted for the currently observed configuration.

## Decision

Do not advance to G2 or implement a purported Crimson Desert VR adapter from
the current evidence. Keep CrimsonDesertAdapter inactive and capabilities
Unsupported/Unverified. Stop repeating the current RenderDoc game-capture
experiment. Further research must address a specific missing camera/render
capability or a materially different, qualified observation method.

This is a no-go for advancing the current implementation, not a determination
that BlackSpace can never support VR. A replayable GPU frame is one possible
research route, not a specification requirement that excludes other verified
camera/render interfaces. A working synthetic host does not supply that missing
game interface.

## Evidence reviewed

| Required capability | Current evidence | Decision implication |
|---|---|---|
| Independent synthetic eye rendering and XR lifecycle | [Quest Pro acceptance](../evidence/m0-quest-pro-reports-2026-11-05.json), WARP disparity and mock-runtime tests | M0 core acceptance stands for the tested build/configuration. |
| Current portable host and diagnostics | [fb1b574 package checks](../evidence/m0-package-launcher-2026-11-06.json) | Build available; no new hardware acceptance inferred from software checks. |
| Exact game identity | [Updated Steam baseline](../evidence/g1-baseline-2026-11-06.json), build 26480954 / UI 2.04.08 | Previous-build timing and source leads remain historical evidence. |
| Actual presentation and D3D12 execution | [Title-menu presentation](../evidence/g1-title-presentation-2026-11-05.json), [D3D12 events](../evidence/g1-d3d12-execution-2026-11-05.json) | Demonstrated on the older fingerprint; no camera or world-pass proof. |
| Replayable game-world frame or another verified observation interface | [Low-texture capture failure](../evidence/g1-low-texture-capture-2026-11-06.json), zero RDC files | No game frame replay or world-pass identification obtained. |
| Runtime 6DOF camera/view/projection control | [Pinned camera-source review](../research/crimson-camera-feasibility.md) | Static config editing, older speculative fields and heading observation do not establish this capability on our build. |
| Additional world-render scheduling and per-eye provenance | [Upstream stereo architecture audit](../research/stereo-pipeline-audit.md) | Other engines illustrate the required connection; no BlackSpace equivalent established. |
| Alternative capture method meeting current launch scope | [PIX package audit](../evidence/g1-pix-package-review-2026-11-06.json) | Working-directory support observed; child exclusion/local-only execution unresolved. No PIX execution performed. |

The current capture failure does not identify its exact allocation size or
root cause. Lower textures did not produce a frame. Capture-session video
settings and the game build also differed, so there is no isolated causal
texture-quality comparison. All recorded original settings were restored and
verified. More capture infrastructure alone does not answer the camera question.

## Conditions for resuming game-facing work

At least one new engineering lead must change the evidence:

- A documented or independently verified camera/render extension applicable
  to the exact installed build, with a reviewed loading method and a minimal
  reversible experiment connecting pose/projection changes to actual rendering.
- A materially different diagnostic method with a clear hypothesis, bounded
  process scope and local-only operation, first verified by capture/replay of
  the owned fixture. Successful title-menu replay alone still does not advance
  G2; it must enable a specific world-pass/camera investigation.
- A source-backed explanation of the failed capture allocation that supports
  a new mitigation. Retrying the same request with more logging or another
  speculative option is insufficient.

Any eventual G2 result must tie geometrically distinct eye poses/projections
to independently rendered world images and verify near/far parallax during
gameplay. Producer-queue synchronization and temporal effects remain separate
requirements. No guessed offsets, protection workaround, copied mono image or
depth reconstruction may replace that proof.

## Work that remains useful now

Keep the tested M0 package available and maintain its actual evidence grades.
Perform a new packaged launcher/headset check when the Quest Pro is available.
Do not make another change merely to create work or consume the weekly usage
allowance. Other runtimes/headsets, compositor metrics and extended graphics
validation remain explicitly unverified until their prerequisites exist.

Do not begin the VRcade launcher or silently substitute theater mode for the
requested game-world stereo milestone. The roadmap permits redesign, but a
product-scope change needs an explicit decision grounded in the user's intent.
