# M0 references and next G1 experiment

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
evidence, not camera control or G2. The current next experiment records original
graphics settings, temporarily reduces capture memory requirements, and reconnects
to the same early-loaded layer. The [owned reconnect and aggregate replay checks](evidence/g1-owned-reconnect-2026-11-05.json)
passed independently. Game replay remains unverified.


## Pinned upstream stereo-source review

See [the static pipeline audit](research/stereo-pipeline-audit.md) for pinned
Cyberpunk VR and REFramework camera/render/submission paths, selected direct
dependency licenses, limits and adoption decision. No mod code or dependencies
were adopted; builds and game/hardware execution were NOT_RUN. This read-only
preparation does not advance the M0 hardware gate.
