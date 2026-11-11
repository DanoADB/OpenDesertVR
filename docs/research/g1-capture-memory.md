# G1 D3D12 capture memory failure review

The isolated-context title-menu attempt loaded the external capture layer and
requested one frame, but its overlay reported E_OUTOFMEMORY while creating a
readback buffer. No RDC was saved. The exact failing resource/allocation is
unknown; see the [attempt record](../evidence/g1-steam-context-attempt-2026-11-05.json).

The subsequent [local diagnostic-log review](../evidence/g1-capture-allocation-log-2026-11-05.json)
narrows the failed call to CreateHeap for initial-state READBACK heap index 4.
The first failure is in the texture initial-content path, followed by repeated
texture and buffer errors within the same single frame request. The log records
one fatal OOM-handler event and discards the frame. It does not report the
failing resource dimensions or requested bytes. Raw log data remains local;
the evidence contains only aggregate counts, call-site interpretation and its
snapshot fingerprint.

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
The actual log records the HRESULT handler's fatal OOM path at the heap-creation
call. A fresh game process is therefore required for the next comparison.
No error state, API return or game/protection check will be patched or forced.

The documented
[soft memory option](https://github.com/baldurk/renderdoc/blob/v1.47/renderdoc/api/app/renderdoc_app.h#L222)
applies only where an API can defer allocations and explicitly does not
guarantee a memory limit. Do not promise that setting it fixes this D3D12
readback allocation, or introduce unreviewed tool/vendor-extension overrides.

An actual CaptureOptions instance in the reviewed 1.47 binary reports
refAllResources=false, captureAllCmdLists=false, verifyBufferAccess=false and
softMemoryLimit=0. saveAllInitials is not exposed by this version's Python
CaptureOptions; no nonexistent switch is set. The prior game script constructed
these defaults and did not modify those fields. New capture reports record
the supported values explicitly. This audits scope settings, not the live
game's resource sizes, and does not establish a memory mitigation.

## Controlled comparison and current gate

Additional pinned v1.47 source inspection found that PrepareInitialContents
iterates potentially dirty resources before InsertInitialContentsChunks filters
unreferenced initial contents. The historical local log reports up to 4,163
potentially dirty resources in that one request. This is not the number of
world-pass resources actually used or an allocation-size measurement.
See [resource_manager.h](https://github.com/baldurk/renderdoc/blob/b0e1370309f7d7bf986a73830446ca39d9b8c1fd/renderdoc/core/resource_manager.h#L1448).
The base persistency predicate returns false and no D3D12 override was found.
Searching all 73 D3D12 C++/header files at that revision found no softMemoryLimit
reference. This narrows the source-level reasoning: refAllResources=false does
not prevent the initial preparation path, and a soft limit is not an established
D3D12 mitigation. It still does not identify the actual failed allocation.

Record the original game resolution, texture/graphics preset, ray-tracing and
upscaling/frame-generation settings from the actual UI before modifying any.
Temporarily lower memory requirements through ordinary graphics controls,
record the exact changed values and use a fresh early-start context. This is
a mitigation hypothesis, not a verified fix. Request one title-menu frame and
retain the actual outcome without automatic retries or changing protections.
Restore the original settings and verify restoration after the experiment.
On 2026-11-06 normal-session mouse input worked. The
[updated-build experiment](g1-updated-build-2026-11-06.md) verified Low textures
in a fresh title-menu process, but one capture still failed creating a readback
buffer with E_OUTOFMEMORY and saved no RDC. The capture session also displayed
different upscaling/frame-generation/Reflex values without agent changes.
Original settings, including Ultra textures, were restored and verified in a
normal session. No manual menu-input gate remains. A repeated blind retry is
not justified; review a materially different capture-memory strategy first.

The latest run did not retain a matching raw log, so its exact allocation branch
is unknown. The helper now uses the supported
[SetDebugLogFile API](https://github.com/baldurk/renderdoc/blob/v1.47/renderdoc/api/replay/renderdoc_replay.h#L2236)
to select a local per-case log shared by analysis and captured programs before
launch, then retains a bounded snapshot before helper exit. Diagnostic contents
remain local; only size, hash and truncation metadata enter the report. An
embedded-Python prelaunch failure check verified retention, not target logging.

Even a successful title-menu capture would not prove world rendering, camera
control or stereo. The next evidence gate remains a locally replayable frame
followed by verified world-pass/camera analysis, then independent game-world
views with near/far parallax for G2.
