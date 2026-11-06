# PIX candidate review after unsuccessful RenderDoc captures

Reviewed 2026-11-06. This is source research only. PIX is not installed at the
standard Program Files location here; no PIX installation, capture or game
launch was performed for this review. The existing production adapter remains
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
