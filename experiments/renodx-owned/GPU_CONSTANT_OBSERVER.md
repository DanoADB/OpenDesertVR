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

## Diagnostic revision

`devkit-readback-diagnostics-input-lock.json` pins a separate
`bc0ead3-gpu-constants-4` fork and `gpu-constant-diagnostics.patch`.
It keeps previous references unchanged. Summary rows expose
`odvGpuConstantSampleCount`, `odvGpuConstantAttemptCount` and
`odvGpuConstantAttemptsTruncated`. Select originating draws with nonzero
sample counts; a detailed reply on a later use of the same range need not
contain a ticket. Binding count alone is not a sample-selection criterion.

Detailed replies additionally expose at most six `odvGpuConstantAttempts`:
record status, source eligibility, heap type and allocation size. Native
resource metadata is queried only during the original valid recording
callback. Duplicates, capacity exhaustion and ineligible sources are distinct.
If capacity prevents source inspection, heap type/size remain null; do not
infer an unsupported heap from absent metadata. The attempted-candidate limit
can omit later eligible ranges and explicitly reports truncation. This
revision does not enable new source heap types or change resource states.

## Upload-input revision

`devkit-upload-input-lock.json` pins separate `bc0ead3-upload-input-5` and
`upload-input-observer.patch`. Explicit `ODVUploadInputs=1` registers up to six
finite active DEFAULT candidates of the configured `ODVConstantBytes` size.
Only a subsequent application copy fully covering a candidate range, within
five seconds of registration, can supply an input. Its source must be valid
UPLOAD memory. The observer preserves the app copy and inserts no source or
destination writes or transitions. It retains strong resource references.

At execution, a single exclusively owned list copies the matched UPLOAD span
on the actual application's queue before the upcoming app list. Its own
post-submission fence gates CPU readback; no app Present callback is needed.
This captures upload input, not the destination's later state or the exact
contents at the originally observed draw. GPU writers, partial updates,
different data paths and a missing subsequent upload can leave data unavailable.
Six unresolved copies share the original fixed copy capacity. Extra target and
source references can prolong existing allocations; this is not a process-wide
memory cap or a hard driver timeout.

Summary `odvUploadInputCount` locates originating candidate tickets. Detailed
`odvUploadInputs` report state, source metadata and private float32 values.
Do not infer a camera from a buffer size or from returned numbers. All
shader/text/dump exclusions remain. The Close callback also confirms retirement
after reset to an empty command list; before-reset alone never grants data.
Owned DEFAULT-buffer pixels, matching upload values and an unsubmitted control
must pass actual addon runtime qualification before any game use.

`devkit-upload-cleanup-input-lock.json` and `upload-input-cleanup.patch` retain
the **rejected v6 experiment**: device-finalization cleanup hung both owned
shutdown cases. It must not be used in the game. V5 acquired correct input
values but retained target references until addon unload. Neither result is
clean shutdown qualification for the subsequent game method.
The additional `--loop-dxil-upload-empty` control retires yellow's recorded
upload/draw and actually executes its empty replacement list. Yellow must
still return no bytes; reset/Close cannot turn an unexecuted copy into data.

`devkit-upload-swapchain-input-lock.json` and `upload-input-swapchain.patch`
pin v7: device identity is captured during valid registration; cleanup occurs
at non-resize primary-swapchain destruction, while the device is alive. COM
references are released outside observer and DeviceData locks. Completed owned
copies retire first; uncertain completion remains quarantined. Device closure
invalidates cached tickets. The normal shutdown log must contain no native
device reference-count warnings in owned qualification before game use.

V7 also matches equal-sized whole-buffer `CopyResource` uploads. The
`--loop-dxil-upload-whole` fixture uses actual DEFAULT destinations and whole
UPLOAD copies. Both whole and region paths preserve the original app copy;
only the candidate subrange is sampled. Partial writes are not assembled.

## Command provenance and finite-size selection

`devkit-command-provenance-input-lock.json` pins v8 and its combined patch.
An actual owned compute-then-indirect-graphics control showed v7 retaining
the compute shader and misreporting three correctly rendered graphics draws
as dispatches. Older game snapshots' command labels and chosen shader/root
domains therefore remain unverified. Raw historical outputs are preserved.

V8 tracks the latest D3D12 PSO bind point for unknown indirect callbacks.
It reports `IndirectGraphics`, `IndirectCompute` or `IndirectUnclassified`,
plus `odvApiCommand`, `odvClassificationKnown` and `odvBindingPhase`.
Classification establishes a shader domain, not a command-signature layout.
Bindings are explicitly before indirect arguments, which can change state;
no argument buffer is decoded. Unknown domains cannot produce samples.

Optional `ODVGraphicsOnly=1` retains known graphics work rather than copy and
compute prefixes. Copy-input matching still operates for existing candidates.
Optional `ODVConstantMaxBytes` (positive multiple of four, at most 4096)
selects finite active CBV ranges up to that size, taking precedence over the
older exact-size setting. Full/unknown ranges stay excluded; six-copy,
six-target, six-attempt-per-draw and 128-record bounds remain. Summary rows
report the filter and maximum. Neither option identifies a world camera.
