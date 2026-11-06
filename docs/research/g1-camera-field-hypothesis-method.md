# Bounded candidate-field hypothesis observation

Reviewed 2026-11-06 after a successful
[pointer association observation](../evidence/g1-live-association-2026-11-06.json).
The next question is numeric consistency and progression of selected candidate
fields, not camera writes or stereo. No headset is needed.

## Explicit hypothesis

The pinned upstream historical direct layout supplies a source-pointer member
and selected buffer locations. Their semantic labels remain hypotheses on the
current executable, even though its global/object/table association passed.
Do not install a relocated profile or claim these object fields were validated
by the preceding experiment. No additional offsets are guessed or scanned.

The source-pointer member comes only from the exact hash-pinned profile.
Selected ranges cover the buffer counter, screen dimensions/reciprocals,
position, forward vector, world/relative view matrices, projection, near-related
value and a numeric layout sentinel. The raw far-related field, lighting data,
player objects and the rest of the buffer are not read.

## Acquisition and rejection

Reuse the existing build/path/profile/file-hash/module-size/normal-access gates.
Derive the global/table from the current file afresh. Require their live range
properties and the object-first-pointer match before following the published
source-pointer hypothesis. Read only committed readable, non-executable ranges;
each explicit read is at most 128 bytes. Normal access denial stops the probe.

Three samples bracket two copies of the selected fields with global, object,
source and buffer-counter checks. Any mismatch, unreadable range, replacement,
short read or invalid numeric data stops without retry/fallback or a new offset
search. Each attempt reads at most 540 explicit payload bytes; three total
1,620 bytes. OS module metadata queries are separate. A one-second cooperative
sampling deadline does not claim a hard watchdog around Windows API calls.

Check finite values, resolution reciprocals, unit/orthogonal basis and its
handedness, view translation versus position/basis, world/relative rotation
agreement, affine matrix structure, and the historical symmetric positive-Z
perspective shape. Reject orthographic/asymmetric or otherwise unsupported
layouts. Require plausible unsigned buffer-counter progression between samples,
including wrap handling. Export counts/check status only; no raw counter,
position, matrix, address, PID, path or game payload is retained in the report.

## Interpretation and code provenance

A passing result is OBSERVED_NUMERIC_CONSISTENCY for that sample and hypothesis.
The camera-field capability remains UNVERIFIED. These checks do not prove
frame-atomic acquisition, active primary-camera semantics, coordinate units,
world origin, visibility, simulation/render timing or safe camera modification.
No game pose is converted to OpenXR and no data is submitted to a headset.
Controlled camera-only movement and visible render correlation remain separate
requirements before relying on these fields.

Selected validator rules are adapted from the pinned MIT upstream decoder and
reader, with attribution and full license in THIRD_PARTY_NOTICES.md and
docs/licenses/CrimsonDesertTelemetry-MIT.txt. No upstream program, ASI or
dependency is executed or adopted. Owned byte fixtures test valid geometry,
reflection/non-finite/translation/projection failures, coherent reads, torn
samples, source replacement, stopped/backwards counters and unsigned wrap.
The Windows owned-child fixture also verifies the small byte-read API.

## Actual first attempt and acquisition gate

The [first game attempt](../evidence/g1-camera-field-attempt-2026-11-06.json)
ran committed source 66a3b1d on the same original responsive process and reviewed
current build. It exited 3 at SAMPLING because the coherence guard rejected a
changed sample. No numeric-layout result was awarded. The report does not
distinguish which global/source/table/counter/payload check changed; no raw data
was retained, so that detail cannot be reconstructed. The scene remains unknown.

The method stopped without retry or offset fallback, and the game remained
responsive. An external-read race is a possible explanation, not an observed
root cause or evidence that the candidate layout is wrong. Do not relax matrix
or counter validation merely to obtain a pass.

Subsequent reporting now distinguishes fixed boolean coherence checks and the
names of changed hypothetical ranges, without values or locations. Owned
fixtures verify those labels for counter changes and source replacement. No
additional game observation was performed for that reporting change. The next
experiment needs a justified acquisition strategy or specifically scoped
coherence diagnosis before attempting semantic validation.
