# M0 references and next G1 experiment

Latest live result, 2026-11-10: the [private native vertex-position probe](evidence/g1-game-clip-dxc22-2026-11-10.json)
records replacements in the actual Steam game: 4,635 identity, 3,414 left and
3,429 right calls. Native PSO cloning and original-signature forwarding now run
with unchanged original root/argument/count/constant buffers. All three shader
variants pass validation; the original mode is restored after each brief run.
The desktop scene continues normally. These are CPU recording counters with
desktop observation, not GPU pixel measurements or headset acceptance.

Two blockers were isolated and fixed: a replay-only query guard rejected single
forwarding inside active queries, and the older SDK validator rejected even the
untouched game shader. The pinned Microsoft.Direct3D.DXC1.9.2609.5 pair accepts
the original, an untouched assembly round trip, and all three modified variants.
The small clip-X displacement is not confidently visible in desktop captures;
geometry influence remains UNVERIFIED pending visible or measured displacement.
Independent game eye views, camera culling/history, head pose and OpenXR delivery
remain NOT_RUN. The next experiment must confirm visible displacement, then
establish complete eye views; this result does not complete G2.

Earlier integration observations, 2026-11-09: actual game GPU observations identified
a camera-like shadow input whose selected view fields respond to a controlled
camera turn. A later capture identified a screen-shaped standard-geometry
input, but its buffer state prevented a safe read. These are distinct captures;
resource identities do not carry across launches. See the
[shadow-camera evidence](evidence/g1-game-native-enhanced-2026-11-09.json) and
[world-input candidate](evidence/g1-game-native-scene-2026-11-09.json).

The [qualified observer14](evidence/g1-owned-native-indirect-2026-11-09.json)
captured both shader roles and actual native signature/root metadata in
[one game snapshot](evidence/g1-game-native-indirect-2026-11-09.json). The
shadow and world bindings share an exact 2,816-byte source range, with a
completed shadow GPU sample passing selected camera-layout checks. They use
different command-list recordings. The world signature has a 40-byte stride
and changes an index buffer and one root constant before DrawIndexed; its
root layout uses 22 DWORDs, 20 parameters and 13 static samplers. Private
replacement therefore needs an exact derived command signature and correct
post-indirect resets as well as proof of copy ordering and unchanged source
contents. An alias alone does not establish those conditions.

[Private binding](evidence/g1-owned-private-table-2026-11-09.json) and
[split-table rendering](evidence/g1-owned-private-split-2026-11-09.json) now
change and restore owned geometry using separately created roots/PSOs and
unchanged shaders and source buffers. Actual game layout compatibility, camera
influence, independently rendered eye views and game OpenXR submission remain
unverified. The next product gate is visible world-camera influence, followed
by distinct game eye renders and OpenXR delivery; additional cube tests are
not a prerequisite.

[Observer15's game capture](evidence/g1-game-camera-layout-2026-11-09.json)
now preserves all 704 shadow-input words, including non-finite float bit
patterns. Selected historical camera fields pass numeric checks. The world
target is b18/space35 at root parameter5, descriptor offset18 within a finite
52-CBV range; no unbounded table ranges were observed. This resolves the
target table shape, but shader array shape, load bounds and downstream use
still require exact-shader consumption analysis before a private camera probe.
The world source remains ineligible for direct copying under the current
state proof. No camera override or independent game eye render has run.

[Observer16](evidence/g1-game-cbv-access-2026-11-09.json) encountered a
singleton 2,768-byte target declaration in the world VS and three PS variants.
A later declaration-format rejection prevents complete uniqueness and load-bound
qualification. The [selected-shader method](research/g1-static-cbv-access-method.md)
keeps this partial result explicit. A parser correction is qualified against
owned high-bit-space and unrelated dynamic-descriptor fixtures before another
game observation; it does not enable a camera override by itself.

The [corrected observer17 capture](evidence/g1-game-cbv-access17-2026-11-09.json)
passes static target-load bounds for the world VS: twelve loads, maximum byte
end2,720 within a singleton2,768-byte declaration. Three world PS variants still
have unsupported target-handle uses; ALL-stage root-CBV lifting remains
unqualified. The completed sample and this syntax observation do not prove
payload freshness or camera control.

A [private vertex-position probe](research/g1-clip-position-probe-method.md)
now passes [owned GPU qualification](evidence/g1-owned-clip-position-2026-11-09.json):
SDK assembly/validation, identity and restoration, unchanged original root,
signature and buffers, and depth-dependent4/2-pixel displacement. This is a
geometry-influence diagnostic, not complete game stereo. The next integration
step retains the actual native PSO description and changes only its VS; generic
pipeline subobjects omit some native state and cannot establish exact cloning.

The [native source clone](evidence/g1-owned-native-pso-clone-2026-11-10.json)
now passes local and hosted SM6.0/6.6 legacy/stream tests. The
[scoped probe integration](evidence/g1-owned-clip-probe-integration-2026-11-10.json)
also passes owned execution through the wrapper's actual PSO attachment and
shared indirect forwarding path, including subsequent draws without rebinding
the original PSO. Probe18 starts disabled, expires stale control input and
retains at most three private variants on each tagged original PSO. Game
geometry influence, independent eye views and OpenXR submission remain NOT_RUN.

The [owned indirect GPU test](evidence/g1-owned-warp-runtime-2026-11-09.json)
passes all 19 rendering/count/reset/preservation cases in hosted Debug and
Release with the debug layer enabled, using explicitly loaded test-only
Microsoft WARP1.0.22. The older hosted system runtime mutates an argument
byte; its failure is retained as diagnostic evidence. These results qualify
owned forwarding, not game integration. A prior green run that skipped GPU
execution is explicitly excluded from acceptance.

The earlier investigation and stop decisions follow as historical context.

Earlier advancement decision: [ADR 0003](decisions/0003-g1-current-evidence-and-stop-decision.md).
Hold G2/game-adapter implementation; stop repeating the failed capture method.
The new [native-camera telemetry lead](research/crimson-telemetry-camera-lead.md)
has a pinned historical exact-hash profile and actual current-file instruction
guard observations. It enables a targeted offline camera investigation without
another full-frame capture. Live camera validity and control remain unverified.
The [relationship follow-up](evidence/g1-camera-relationships-2026-11-06.json)
observes a writable RIP target and one ordered read-only function-pointer table
on the current file. Their association with a live camera and its field layout
remain separate unverified questions; no production profile is relocated.
The [bounded live follow-up](research/g1-readonly-association-method.md) then
observed stable global/object-first-pointer association on the reviewed current
game build. Active-camera semantics, field layout, pose and control remain
unverified. Further headset tests are deferred at the user's request.
The subsequent [selected-field hypothesis check](research/g1-camera-field-hypothesis-method.md)
was rejected because its sample changed within the coherence bracket. It did
not validate the layout. Current work must resolve acquisition coherence before
claiming a usable camera snapshot; no automatic retry or offset fallback occurs.
A separately scoped attribution check found stable global/source/table pointers
and changes only in the counter-hypothesis range; it also failed coherence.
See the method's recorded outcomes. That word's semantics and timing cause
remain unverified; matching non-counter fields do not establish frame atomicity.
The ADR identifies the missing capabilities and new evidence required to resume
game-facing work. M0 maintenance and actual headset verification remain useful.

Primary references:
- https://github.com/KhronosGroup/OpenXR-SDK/tree/781f2eab3698d653c804ecbd11e0aed47eaad1c6
- https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/release-1.1.43/src/tests/hello_xr/graphicsplugin_d3d12.cpp
- https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html
- https://learn.microsoft.com/en-us/windows/win32/direct3d12/hello-triangle-sample

The hello_xr D3D12 graphics-requirements, swapchain ownership and fence model
were consulted. Original M0 code uses a simpler CPU-transformed scene and
single-flight GPU queue. No third-party game mod code was adopted.
See GITHUB_REUSE_RESEARCH.md for earlier reconnaissance, not validation.

After hardware M0 acceptance, G1 first records exact legally owned game
version/storefront and executable hash (no proprietary payload), permissions,
network/anti-cheat implications, publicly available diagnostics and DX12
presentation evidence. Compare possible camera/frame-ownership approaches
using evidence only. Deliver an integration plan or stop decision before
implementing any hooks. No G1 experiment was performed during this M0 work.

After the 2026-11-05 Quest Pro core hardware acceptance, a
[G1 non-invasive baseline](research/g1-baseline.md) recorded the local Steam
build fingerprint and reviewed the published EULA/store disclosures.
Non-invasive technical research continues with an
[external presentation capture](research/g1-presentation-experiment.md).
Intrusive integration still needs review. A user-confirmed title-menu capture
observes one DXGI presentation stream; world-render API, camera control and
actual in-game stereo remain unverified. See the
[camera feasibility source review](research/crimson-camera-feasibility.md).
The subsequent [Direct3D12 event trace](evidence/g1-d3d12-execution-2026-11-05.json)
observes actual command-list execution in the game process. World-pass identity
and camera control are still separate unanswered questions.

The [controlled early-start frame-capture attempt](evidence/g1-frame-capture-attempt-2026-11-05.json)
validated the RenderDoc launch API on an owned D3D12 fixture, with exact replay
pixels. The game connection disconnected before a frame request; a different
game process subsequently appeared. No game capture was obtained and the cause
is unverified. This method stopped without child/global hooks, late attachment,
Steam configuration changes or protection bypass. A new loading approach needs
its own review before execution; the adapter remains inactive.

The later [isolated Steam development-context experiment](research/g1-steam-capture-context.md)
kept the early-loaded game target through D3D12 initialization and a verified
title-menu frame request. RenderDoc then reported E_OUTOFMEMORY while creating
a readback buffer; no capture file was obtained. This is loading/capture-failure
evidence, not camera control or G2. The later
[updated-build Low-texture attempt](research/g1-updated-build-2026-11-06.md)
also failed with E_OUTOFMEMORY and no RDC; original settings were restored.
Further game retries require review of a materially different memory strategy.
The [PIX candidate review](research/g1-pix-candidate-review.md) separates a
possible alternative frame-capture implementation from timing/crash diagnostics
and lists owned-fixture and launch-scope checks required before a game attempt.
The later [signed-package inspection](evidence/g1-pix-package-review-2026-11-06.json)
verified working-directory support but did not establish child-instrumentation
exclusion or local-only runtime behavior. PIX game launch remains unqualified;
no PIX tool has been installed or executed.
The earlier [diagnostic log](evidence/g1-capture-allocation-log-2026-11-05.json)
narrows the failure to initial-state READBACK heap creation and records a fatal
OOM handler path; see the [memory review](research/g1-capture-memory.md).
The [owned reconnect and aggregate replay checks](evidence/g1-owned-reconnect-2026-11-05.json)
passed independently. Game replay remains unverified.


## Pinned upstream stereo-source review

See [the static pipeline audit](research/stereo-pipeline-audit.md) for pinned
Cyberpunk VR and REFramework camera/render/submission paths, selected direct
dependency licenses, limits and adoption decision. No mod code or dependencies
were adopted; builds and game/hardware execution were NOT_RUN. This read-only
preparation does not advance the M0 hardware gate.


The later [Probe23 live control](evidence/g1-game-clip-probe23-2026-11-10.json)
records identity and both larger fixed shifts without errors, but neither desktop
observation nor the user confirms visible displacement. Geometry influence is
still UNVERIFIED. The [live Probe24 collapse control](evidence/g1-game-clip-probe24-2026-11-10.json)
also produces no noticeable visual change. Probe25 selects an alternate observed
material-pass vertex shader with a larger indirect capacity for the next
influence test. Actual GPU counts and visible contribution are still unknown.
These controls do not establish game stereo, head tracking or OpenXR delivery.


Probe25's rotating-camera control also showed no noticeable influence.
[Probe26 owned qualification](evidence/g1-owned-clip-probe26-2026-11-10.json)
adds fenced GPU-work queries with predication skip/pass controls before any
further shader selection. The private observer is built; live GPU-work results
are pending. This does not establish visible geometry control or game VR.


[Live Probe26 GPU evidence](evidence/g1-game-clip-probe26-2026-11-10.json)
now establishes nonzero occlusion samples in the selected identity pass and zero
in three collapse samples. This supports a real rasterization effect, while
visible scene influence remains unverified. The metadata snapshot truncates at
128 records before completing the frame; tracing later consumers is the next
step. Mode0 and normal rendering are restored; no game stereo is claimed.


[Probe28 live windows](evidence/g1-game-clip-probe28-2026-11-10.json) now reach
later graphics records after correcting idle-Present completion. They reveal
an explicit `vsTerrainMesh` candidate beyond the original128-record cutoff.
Probe29 is built and locally qualified for a reversible terrain-only test;
actual terrain influence, camera control and game stereo remain unverified.
