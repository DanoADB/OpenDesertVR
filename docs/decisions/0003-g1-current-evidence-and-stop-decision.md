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
The user subsequently requested proceeding without further headset tests;
leave those checks deferred and unverified until requested again. The
[new native-camera source lead](../research/crimson-telemetry-camera-lead.md)
and current-file guard observations supply a concrete offline research direction.
The [subsequent bounded read-only observation](../research/g1-readonly-association-method.md)
establishes a current-process global/object-first-pointer association only.
Camera field layout, semantics and control remain unverified; no old field
offset is thereby validated.
The [selected-field diagnostics](../research/g1-camera-field-hypothesis-method.md)
found numerically plausible individual byte copies, but every observed field
attempt rejected coherence. Counter-hypothesis bytes changed while the other
selected ranges matched in the two classified observations. Neither counter
meaning nor a coherent active-camera sample is established. Stop the existing
sequential acquisition method pending a reviewed design tested with an owned
producer; no check relaxation or retry-until-pass.
The [publication-contract review](../research/g1-camera-publication-review.md)
finds no engine writer-order/lifetime contract in the five selected pinned
sources. A native speedup alone does not qualify a new game probe. A documented
current-build producer/consumer relationship is the missing evidence for this
acquisition route; observer-owned transport synchronization does not supply it.
An [offline same-container reflection observation](../research/g1-shader-reflection-method.md)
qualifies the SDK query mechanism but reports no queried constant-buffer
declarations for that witness. It does not supply a camera-field mapping or
runtime binding. Further shader selection needs a concrete material/pass or
camera-consumer connection; no arbitrary shader scan or retry-until-match.
They do not change the hold on runtime adapter implementation or award G2.
Do not make another change merely to create work or consume the weekly usage
allowance. Other runtimes/headsets, compositor metrics and extended graphics
validation remain explicitly unverified until their prerequisites exist.

Do not begin the VRcade launcher or silently substitute theater mode for the
requested game-world stereo milestone. The roadmap permits redesign, but a
product-scope change needs an explicit decision grounded in the user's intent.

The [bounded technique-declaration check](../research/g1-technique-contract-method.md)
rejects the first declaration at XML inspection. The other fifteen and final
input postflight were not reached. It establishes no shader link and does not
justify another shader selection. The diagnostic records partial-read counts;
its result does not erase the initial failed attempt's missing accounting.

A subsequent specific pinned-source review justified an XML fragment envelope,
qualified on owned fixtures before observation. The [fragment observation](../evidence/g1-technique-fragment-contract-2026-11-06.json)
completes all sixteen declarations and final input checks, with zero literal
`.padxil` scalar references. This supersedes the incomplete attempt only for the
new parser contract; it does not rewrite earlier evidence or establish runtime
bindings. Opaque identifiers remain uninterpreted. The literal-reference branch
is complete without a shader link; G2 remains held.

The [selective GPU consumer review](../research/g1-gpu-consumer-binding-review.md)
examines four pinned public native capture files. They qualify neither a
current-build world-camera buffer identity nor its consumer: the selected
copies concern lighting/exposure, descriptor tables remain unresolved, and
CPU scene data remains unpaired. This does not rule out a separately justified
GPU route, but no game-facing camera readback bridge follows from these files.

A [file-only static seed inspection](../research/g1-static-function-extents-method.md)
qualifies three existing unique seeds against PE runtime-function metadata and
installed-tool linear decoding. It identifies bounded regions for further
engineering questions, not camera writers, complete logical functions or a
publication protocol. The rejected live acquisition route remains stopped;
no new process access, callback or game hook is qualified by these metadata.

The [pinned RenoDX inspection review](../research/g1-renodx-inspection-review.md)
supplies a distinct per-draw shader/resource metadata lead. Its reviewed export
does not establish vertex-only table coverage, submitted GPU work or per-draw
constant bytes; startup also has replacement capabilities. Qualify the actual
tool on owned fixtures with isolated configuration before considering a
current-game method. No addon or bridge was adopted or executed by the review.

The [owned ReShade descriptor-cache regression](../research/g1-owned-reshade-descriptor-method.md)
passes locally for Release under clean committed controller ca07e21. Its 1,024
allocation requests and orderly exit qualify that selected loading/cache step
only. Full devkit draw tracking, vertex-only table coverage, submitted draw
association and per-draw GPU constants remain unverified. No game instrument,
camera control or independent world render is qualified by this owned result.

The [owned Devkit transport qualification](../research/g1-owned-devkit-transport-method.md)
completes on clean source 07d12e8: matching owned-child pipe, empty-frame snapshot,
fresh bridge reconnect and cooperative shutdown. Zero captured draws are
consistent with the clear-only fixture and provide no draw-binding pass. The
next qualification needs actual owned geometry and descriptor/submission
controls. No game loading method, camera consumer or G2 result is established.

The original [owned draw fixture](../../experiments/draw-binding-owned/README.md)
now establishes independent WARP pixel ground truth in local Debug and Release
on clean source 4be273d. Three submitted root/table draws produce RGB, while a
fourth recorded but unsubmitted list leaves black. Submitting that same fourth
list in a paired control produces yellow. Every pixel is checked after fence
completion for two frames per case. [Evidence](../evidence/g1-owned-draw-pixels-2026-11-06.json)
does not yet qualify Devkit metadata: capture-tool execution is NOT_RUN for
this fixture. Presenting-mode binding comparison remains the next step; the
clear-only transport pass does not cover geometry. Production G2 stays held.

The [bounded geometry observation](../evidence/g1-owned-geometry-observer-2026-11-06.json)
on source cffb5d6 rejects this observer's descriptor-table association: both
table draws retain the earlier root draw's vertex and pixel buffers despite
independent GPU readback verifying green and blue. The recorded, unsubmitted
yellow control is also listed. This is an actual SM5.1 owned-case failure,
not a current-game DXIL finding or proof that game VR is impossible. Do not
use these unqualified table bindings to infer camera data.

The [DXIL comparison and correction](../evidence/g1-owned-dxil-cbv-observer-2026-11-06.json)
uses the same authored geometry compiled as SM6.6. The original observer reports
correct pixel CBVs but stale vertex-only table CBVs. A two-hunk, separately
pinned private observer patch exposes CBVs beyond pixel visibility and uses
the existing bounded enumeration for their table slots. All four owned
vertex/pixel resource pairs then match, including table rebinding; independent
GPU readback remains RGB/black. This removes that specific observer defect
from the camera research route. It does not prove game camera semantics,
constant-byte capture, resource lifetime, same-slot stage aliasing or GPU
execution markers. The next game method must address those explicit limits.

The user's priority is now a Crimson Desert VR showcase before further app
polish. Current-build headset acceptance remains necessary for the delivered
desktop app, but waiting for another cube test must not block G1 camera/render
research. The next game-facing milestone is independently rendered stereo
world views from a live game scene, followed by OpenXR presentation and head
tracking. No theater substitution or G2 claim is authorized by this decision.
