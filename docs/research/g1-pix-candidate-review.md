# PIX candidate review after unsuccessful RenderDoc captures

Reviewed 2026-11-06. PIX is not installed at the
standard Program Files location here; no PIX installation, binary execution,
capture or game launch was performed for this review. A later static package
inspection below supplements the initial public-source review. The production adapter remains
inactive. RenderDoc's Low-texture failure is recorded separately in
[the updated-build experiment](g1-updated-build-2026-11-06.md).

## What a different capture implementation could test

Microsoft's [capture architecture explanation](https://devblogs.microsoft.com/pix/capturinggpuwork/)
describes a D3D12 wrapper that tracks command execution and resource state.
It defers some capture work until queue operations use it, while still needing
initial resource contents. This supports evaluating a different implementation;
it does not establish lower peak readback allocation for this game. The article
dates from 2020 and is architectural context, not a current compatibility test.

The [launch documentation](https://devblogs.microsoft.com/pix/taking-a-capture/)
describes instrumentation during process startup. Attaching for GPU capture
requires the matching capture DLL to have loaded before D3D12 initialization.
An ordinary already-running game is therefore not an eligible uninstrumented
target for that workflow. The documentation also describes following launcher
processes: do not assume that its defaults meet our single-target restriction.
Review the current launch controls before any experiment. Do not instrument
Steam, follow children, enable a global hook or transplant the RenderDoc setup
without validating PIX's own behavior.

## Distinguish timing, frame replay and crash diagnostics

The [PIX 2610.28 release notes](https://devblogs.microsoft.com/pix/pix-2610-28/)
place minimal GPU instrumentation under scripted **timing** captures, conditional
on application SDK support. Reduced memory during timing analysis is also not
a promise about replayable frame-capture allocation. Timing evidence may help
explain resource pressure but cannot satisfy the frame-replay or camera gate.

[DirectX Dump Files](https://devblogs.microsoft.com/directx/directx-dump-files-are-now-released/)
describe GPU crash/TDR diagnostics with OS, runtime and driver requirements.
Our readback OOM has not established a GPU timeout/reset. Crash dumps are not a
replacement for a successful frame capture. No driver, global D3DConfig,
Developer Mode or game runtime replacement is warranted by this evidence.

## Qualification before a game-facing experiment

1. Obtain a specific official tool version; audit its license, signatures,
   hashes, dependencies and local-only operation. Keep it out of runtime ZIPs.
2. Verify supported working-directory and single-process launch controls.
   If child instrumentation cannot be excluded, stop that loading method.
3. Use the existing owned ordinary-presenting fixture first. Require one
   completed capture, offline replay and its known cyan pixels, not tool exit
   status alone. Keep capture and timing results distinct.
4. Exercise cooperative timeout/disconnect cleanup and preserve raw local
   diagnostics before tool exit. Do not stack PIX with RenderDoc in one target.
5. Only after those checks, separately review reuse of the exact installed
   Steam build and isolated development context. Keep build guards and normal
   settings restoration. Request no game frame merely to see whether it works.

These are unmet qualification requirements, not reported passes or a claim that
PIX solves the observed allocation failure. A successful title-menu replay
would still leave world-pass identity, camera control and G2 unverified.

## Static package inspection

The [package evidence](../evidence/g1-pix-package-review-2026-11-06.json)
records the official 2610.28 x64 installer URL, observed SHA-256 and valid
Microsoft signature. The embedded MSI also has a valid Microsoft signature;
its bytes and the attached cabinet match the signed bundle's manifest hashes.
Cabinet data was extracted locally and the MSI database opened read-only.
No setup program, custom action or extracted PIX binary was executed.

The bundled SDK's PIX_LAUNCH_PROCESS_DESC supports InitialWorkingDirectory.
PIX_GPU_CAPTURE_OPTIONS and PIX_GPU_CAPTURE_DESC select a target PID. However,
the inspected PIX_APPLICATION_LAUNCH_FLAGS enum contains no child-instrumentation
opt-out. Microsoft's [child-process documentation](https://devblogs.microsoft.com/pix/pix-2303-02/#revamped-child-process-debugging)
describes automatically instrumenting children. A capture target selector is
not evidence that other processes are excluded from instrumentation. No
current supported exclusion mechanism was established, so the game launch
method is **not qualified**. This does not prove that no other supported
control exists; do not invent a flag or patch the tool to force one.

TakeGpuCapture can block while waiting for target readiness and accepts a
cancellation token. A normally exiting target can return a capture marked
PIX_GPU_CAPTURE_RESULT_FLAG_POSSIBLY_TRUNCATED. A future fixture harness must
check that flag and verify frame completeness and replayed pixels, rather than
treating a success HRESULT or file existence as full capture success.

The bundled terms cover Microsoft PIX and separate optional/plugin terms.
They include restrictions on tool redistribution and NVIDIA performance-counter
output disclosure. No tool payload or such counter output is adopted or
published. The license also describes features that exchange data; local-only
runtime behavior has not been established by static inspection. No privacy
settings, network rules or global runtime configuration were changed.

The next qualification step is to establish documented single-target and
local-only execution controls. Until then, neither game-facing launch nor an
owned runtime capture test has been run with PIX. The already available M0
portable package remains independent of this optional tool.
