# G1 D3D12 capture memory failure review

The isolated-context title-menu attempt loaded the external capture layer and
requested one frame, but its overlay reported E_OUTOFMEMORY while creating a
readback buffer. No RDC was saved. The exact failing resource/allocation is
unknown; see the [attempt record](../evidence/g1-steam-context-attempt-2026-11-05.json).

## Pinned external source review

RenderDoc 1.47
[d3d12_initstate.cpp](https://github.com/baldurk/renderdoc/blob/v1.47/renderdoc/driver/d3d12/d3d12_initstate.cpp#L293)
uses the observed error text in multiple initial-content paths, including
buffers, texture copies and acceleration-structure serialization. The overlay
alone cannot select one branch or identify the resource size/type.

Its
[CreateInitialStateBuffer](https://github.com/baldurk/renderdoc/blob/v1.47/renderdoc/driver/d3d12/d3d12_device.cpp#L1661)
allocates READBACK heaps in chunks of at least 128 MiB, using larger dedicated
heaps for larger requested buffers, and places the requested resource in the
heap. Failure can occur creating the heap or placing the resource. This is
tool implementation context, not evidence of the game's actual failing size
or whether global commit, an allocation limit, fragmentation or compatibility
caused the failure. Available system memory after the failure is not proof of
available memory at its exact failing allocation.

The
[error handling](https://github.com/baldurk/renderdoc/blob/v1.47/renderdoc/driver/d3d12/d3d12_device.h#L1011)
distinguishes capture failure from fatal device state. The device's HRESULT
handling can also retain an out-of-memory fatal error. Our successful owned
timeout/reconnect test does not prove recovery from this game's memory error.
Prefer a fresh controlled game launch after changing graphics settings to
avoid attributing retained capture-layer failure state to the new settings.
No error state, API return or game/protection check will be patched or forced.

The documented
[soft memory option](https://github.com/baldurk/renderdoc/blob/v1.47/renderdoc/api/app/renderdoc_app.h#L222)
applies only where an API can defer allocations and explicitly does not
guarantee a memory limit. Do not promise that setting it fixes this D3D12
readback allocation, or introduce unreviewed tool/vendor-extension overrides.

## Next controlled comparison

Record the original game resolution, texture/graphics preset, ray-tracing and
upscaling/frame-generation settings from the actual UI before modifying any.
Temporarily lower memory requirements through ordinary graphics controls,
record the exact changed values and use a fresh early-start context. This is
a mitigation hypothesis, not a verified fix. Request one title-menu frame and
retain the actual outcome without automatic retries or changing protections.
Restore the original settings and verify restoration after the experiment.
The game has not responded to automated menu input; the user has been asked
to open its graphics settings manually. No graphics setting has been changed.

Even a successful title-menu capture would not prove world rendering, camera
control or stereo. The next evidence gate remains a locally replayable frame
followed by verified world-pass/camera analysis, then independent game-world
views with near/far parallax for G2.
