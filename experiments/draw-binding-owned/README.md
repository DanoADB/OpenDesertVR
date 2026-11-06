# Owned draw ground truth

This optional original SDK-only project establishes software-GPU pixel ground
truth before Devkit draw metadata is compared. It has no install target, M0
runtime linkage, game access, OpenXR, capture callbacks or external source.

Build with `cmake -S experiments/draw-binding-owned -B out/build/draw-binding-owned -A x64`,
then `cmake --build out/build/draw-binding-owned --config Release` and
`ctest --test-dir out/build/draw-binding-owned -C Release --output-on-failure`.
Debug is also supported. Requires Windows x64, the Windows SDK, Python and WARP.
No automatic hardware fallback is allowed. CI runs both configurations.

The authored SM 5.1 shaders use vertex b2/space3 and pixel b5/space7. Eight
distinct upload buffers remain alive and persistently mapped, with no writes
after initialization. Sizes are 256 through 2048 bytes. Root CBVs produce the
red draw. Single-entry descriptor tables, with vertex-only/pixel-only visibility,
produce green then blue using distinct heap slots. Descriptors are never
overwritten while referenced. All vertex offsets are zero in this baseline;
it does not independently validate changing vertex constants or camera values.

Four procedural triangles cover four 16-pixel columns of a 64 by 32 target.
List A contains red, green and blue. List B contains yellow. List C copies the
target to a readback buffer. Baseline submits A,C, leaving B recorded but
unsubmitted. Its expected columns are RGB/black. A paired positive execution
control submits A,B,C and expects RGB/yellow. The same B recording path is used
in both. Every RGBA byte in all 2048 pixels is compared after a queue fence,
for two frames per case; each frame resets the same list/allocator objects
only after completion. Fence wait is bounded to five seconds.

The harness gives each run fresh retained directories, imposes a 20-second
child timeout per case, checks exact typed results, fingerprints the executable
before/after, and verifies identical owned shader SHA256/CRC32 identities in
both controls. Capture DLLs beside the executable are rejected. This is not an
attestation of every module Windows may load. Native resource pointers are
retained only in `ground-truth.private.json`; shader blobs are original owned
assets. None are installed into the M0 package. Do not publish private handles.

An explicit `--loop` mode runs the same offscreen pixel check while presenting
a separate black 64 by 32 swapchain through a hidden window. It stops on a
`stop.flag` in the output directory's parent or after 60 seconds, writes
`ready.flag` after the first verified/presented frame, and keeps all resource
identities alive until exit. Unlike baseline mode, it permits a local graphics
proxy so a separately qualified controller can stage its pinned observer.
Its result labels capture-tool execution as EXTERNAL_OBSERVER_POSSIBLE, never
as verified by the fixture itself. No loop mode is installed into M0.

`--loop-dxil` uses the same control geometry with fixed local files
`owned.dxil.vs.cso` and `owned.dxil.ps.cso` beside the executable instead of
the built-in FXC compilation. They must be generated from this fixture's
original HLSL by a separately fingerprinted compiler and staged/hash-checked
by the qualification controller. Reads are bounded to 64 KiB per shader.
This isolates SM6.6/DXIL binding behavior from the older SM5.1 parser; it is
not an arbitrary shader or game-asset test. Default CI controls remain SM5.1.

The pixel baseline does **not** execute Devkit, qualify its CBV resource identities,
verify its submission metadata, or observe per-draw GPU constant bytes. No
headset/game test is run. Compare the exact authored shader pair and known bindings through the
already qualified owned Devkit transport. Known pixel buffers must validate
the native/ReShade handle convention before interpreting other missing/stale
bindings. A listed draw alone cannot establish GPU submission or completion.
