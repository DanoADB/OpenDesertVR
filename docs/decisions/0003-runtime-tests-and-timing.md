# ADR 0003: native runtime integration tests and bounded timing

Accepted for M0 hardening. Existing math/protocol mocks do not execute the
native host's OpenXR call sequence or cleanup after partial initialization.
Add a minimal test-only runtime using the official loader negotiation API.
CTest passes its manifest through a per-process environment override; it
never writes runtime registration. Runtime DLL/manifests are not installed.

Tests audit frame timestamps/order, distinct eye poses/swapchains, acquired/
waited/released images, partial eye/initialization failures, runtime loss,
exit requests and resource destruction. WARP rasterizes actual synthetic
eye geometry. These checks are headless software evidence, not headset
presentation, comfort, runtime conformance or game evidence.

D3D12 timestamps bracket per-eye render commands and resolve to COPY_DEST
readback memory; a fence completes before CPU access or reuse. Queue
frequency converts integer ticks to floating-point milliseconds. Per-eye
and summed stereo render durations exclude compositor/display work.
Nearest-rank percentiles use a bounded 4096-sample window and expose lifetime
counts. A zero duration can be a valid coarse-resolution observation.

Sources:
- https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing
- https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resolvequerydata

Optional debug-layer testing returns CTest skip code 77 when unavailable.
No validation result is invented. A repeated image timeout fails after five
seconds; an acquired but unwaited image is left to session destruction rather
than being released illegally.


Hosted runner finding: GitHub's Windows worker uses high integrity, where
the official loader rejects XR_RUNTIME_JSON. Runtime tests now launch through
a test-only helper: high-integrity parents create a restricted primary token
with administrator SID disabled, maximum privileges disabled and medium
integrity; ordinary parents launch normally. All test descendants inherit
the reduced token. Upstream loader protections and runtime registration are
unchanged. The helper is excluded from installed packages.

Process/token APIs:
- https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createrestrictedtoken
- https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasuserw
