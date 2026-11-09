# M0 references and next G1 experiment

Current integration state, 2026-11-09: actual game GPU observations identified
a camera-like shadow input whose selected view fields respond to a controlled
camera turn. A later capture identified a screen-shaped standard-geometry
input, but its buffer state prevented a safe read. These are distinct captures;
resource identities do not carry across launches. See the
[shadow-camera evidence](evidence/g1-game-native-enhanced-2026-11-09.json) and
[world-input candidate](evidence/g1-game-native-scene-2026-11-09.json).

The [qualified observer14](evidence/g1-owned-native-indirect-2026-11-09.json)
can capture both shader roles and actual native signature/root metadata within
one recording. The next live capture must compare exact source ranges,
recording identities and ordering. An alias alone does not prove unchanged
contents or valid cached bindings after prior indirect commands. Establish
those conditions before substituting a private camera copy.

[Private binding](evidence/g1-owned-private-table-2026-11-09.json) and
[split-table rendering](evidence/g1-owned-private-split-2026-11-09.json) now
change and restore owned geometry using separately created roots/PSOs and
unchanged shaders and source buffers. Actual game layout compatibility, camera
influence, independently rendered eye views and game OpenXR submission remain
unverified. The next product gate is visible world-camera influence, followed
by distinct game eye renders and OpenXR delivery; additional cube tests are
not a prerequisite.

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
