# ADR 0011: Separate stereo image delivery from game camera integration

Status: accepted, 2026-11-10.

The next showcase needs two game eye images delivered to OpenXR. Camera research
can advance independently of the receiver, and the engine-independent core must
remain testable without a game or headset.

Use a local, same-adapter D3D12 image-pair publisher/receiver with two retained slots
and explicit GPU fences. Add opt-in stream modes to the existing CLI and desktop
app. Preserve the original synthetic test by default. Missing streams must fail
without substitution. The transport labels source origin but cannot validate it.

This permits exact owned-content GPU tests and mock OpenXR teardown tests while
game interaction is unavailable. It avoids a desktop capture or depth reconstruction
being mistaken for native game stereo. It does not resolve camera control, render
coverage, temporal-history separation, source/runtime FOV matching or head tracking.

See [implementation and ownership contract](../research/stereo-image-stream.md).
