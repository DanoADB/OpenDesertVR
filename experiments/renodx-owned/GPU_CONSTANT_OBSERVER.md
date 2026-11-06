# Experimental GPU constant observer

The separate `devkit-readback-input-lock.json` reference combines the bounded
metadata patch with the original native constant-copy driver. The earlier
bounded reference and observer currently loaded in the game remain unchanged.
Apply `patches/gpu-constant-observer.patch` to the pinned upstream revision,
then copy the two original headers from their pinned source revision into
`src/addons/devkit/odv/`. Verify every locked file before building.

This adds no new RPC. Only detailed `get_draw` replies can include
`odvGpuConstantSamples`. Sampling is OFF by default. The explicit local INI
setting `[renodx-dev] ODVConstantBytes` accepts a multiple of four, 1..4096;
only active reflected finite CBV ranges of exactly that size are considered.
Only UPLOAD buffers with known fixed GENERIC_READ state qualify. No source
buffer writes or resource-state guesses are made. At most six unresolved
copies exist; the earlier 128-record snapshot cap remains.

Copies occur before the observed draw/dispatch. Each ticket belongs only to
that draw and stores its first qualified GPU execution sample. It is not a
per-frame history or proof of camera semantics. Every command-list execution
is tracked, even after the snapshot ends. A same-thread, same-native-queue
AFTER-present callback supplies a fence after the earlier actual execution.
Different submission queues/threads fail closed; cached handles never grant
permission to dereference game resources outside a valid recording callback.

Completed copies keep their allocations while their command recording may
execute again. A BEFORE-reset event merely marks reset pending. Subsequent
recording establishes retirement under conforming D3D12 use; destruction also
retires the recording. Retirement plus latest-execution completion permits
release; an unsubmitted retired copy can be discarded. Uncertain lifetimes
retain their references/capacity instead. The observer cannot repair invalid
application ordering, device failure or a stuck driver.

Samples expose state, byte count, slot/space and private float32 interpretation
(nonfinite values become null). No shader text or game assets are exported by
this method. Do not publish private game constant arrays. The native driver
tests do not qualify ReShade callback wiring: actual owned runtime comparison
against green/blue GPU controls and the unsubmitted yellow control is required
before any game buffer sampling. Game camera control and stereo remain NOT_RUN.
