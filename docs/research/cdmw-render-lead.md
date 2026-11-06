# CDMW graphics-consumer lead

Selected public-source review, 2026-11-06. Pinned Ratty123/CDMW-Full revision
0dfc3b18f8fcf6fdfe087060dd0f0f4cdb25ca6b. Nine file hashes, exact URLs and the
MIT root-license identity are retained in the [inventory](cdmw-render-source-inventory.json).
No code/dependencies were adopted or executed; this is not full redistribution
clearance. No game archive or shader payload was extracted or retained.

That describes the initial review. Subsequent original bounded readers adapt
selected metadata layout/name-chain/flag rules, with full attribution and
license retained: see [the resolver method](g1-shader-metadata-method.md).
No upstream application, full archive parser or payload decoder is executed.

## Useful direction and limits

The repository's consolidated research reference describes static shader-cache
inspection, container structure and resource bindings on an older game build.
Its EyeCover investigation distinguishes inspected shader consumers from a
verified active runtime permutation, and leaves GPU resource identification
open. This supplies a possible route to studying camera-data consumers, not an
engine camera publication contract or evidence of a second world render.

The current installation has no loose shadercache__/d3d12 directory at its
root. That path check does not prove the resources absent from its archives.
Any further current-build resource study needs a separately bounded read-only
method and exact input identity; do not transfer older static findings into a
runtime capability claim.

## Capture tools are not an established memory mitigation

capture_crimson_renderdoc_frame.py constructs launch or injection commands.
Its defaults include child hooking and a soft-memory limit. The hotkey runner
also refers to temporary Steam app-ID and RenderDoc configuration changes.
Those defaults do not match our scoped loading method and were not applied.
The API-trigger source generator contains placeholder routines; generating an
injector command does not demonstrate a working frame-capture implementation.

None of these selected routines establishes a remedy for our initial-state
D3D12 READBACK allocation failure. Our [pinned RenderDoc source review](g1-capture-memory.md)
already found no D3D12 soft-limit use in the inspected implementation. A command
option alone is not evidence that it bounds that allocation. Do not retry the
failed game capture on this basis or execute these upstream scripts.

## Next qualification

Review the public shader-preview/container and consumer-study implementation
before deciding whether a small, original read-only current-build analysis is
worthwhile. It must answer a specific camera-constant production/consumption
question, not merely enumerate shaders or recreate a mesh viewport. No loading,
hooks, resource patching or new game observation follows from this review.
The [publication-contract gap](g1-camera-publication-review.md), G2 hold and
inactive production adapter remain unchanged.
