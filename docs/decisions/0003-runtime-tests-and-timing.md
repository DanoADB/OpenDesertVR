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


Hosted runner finding: GitHub's elevated Windows service ignores
XR_RUNTIME_JSON. Use scripts/ci-test.ps1 only on the disposable GitHub worker:
save ActiveRuntime, register the built test-runtime manifest for the native
suite, then point it to an absent manifest for the missing-runtime check.
Restore the prior value in finally. The fixture rejects ordinary local
invocation and is not installed in the package. The missing-runtime phase
runs after all native test processes have exited, avoiding registration races.

Local tests run from a non-elevated shell with per-process environment
overrides. The application and packaged collector do not write runtime
registration. The upstream loader is unmodified. Hosted tests retain the
same actual native host and official loader as local tests.
