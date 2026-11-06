# Selective GPU readback: camera consumer association review

Reviewed 2026-11-06 at CrimsonDesertTelemetry revision
434b3b96646b410ab8396483e31c538e051f3a4e. The
[four-file record](../evidence/g1-gpu-consumer-binding-review-2026-11-06.json)
retains source fingerprints, locations and limitations. Each local reference
matches its pinned Git object after checkout line-ending normalization; both
raw and Git-object hashes are recorded, and the reference worktree is clean. No upstream
code was adopted or executed, and no game or headset was accessed.

## Question and result

Could this source supply the missing camera-to-render relationship through a
selective GPU copy, avoiding the failed CPU snapshot route and full-frame
capture allocation? The reviewed paths do not establish that relationship.
This is a narrower conclusion than ruling out selective GPU readback in general.

| Source path | What the selected code establishes | Camera evidence still missing |
|---|---|---|
| render_capture.cpp:217-243, 387-492 | CPU scene reads surround copies of lighting/ambient resources. | The copied resource is not identified as the world camera constant buffer; CPU agreement does not pair it with a draw. |
| spatial_probe.cpp:315-325, 746-753, 1017-1041 | A transaction is scoped to the selected AdaptExposure dispatch; its report explicitly leaves CPU scene/cache unpaired. | A world-geometry draw and its camera-consuming pipeline are not identified. |
| spatial_readback.cpp:148-229 | Root arguments are tracked, signatures invalidate prior arguments, and explicit GI/exposure resources are copied. Address-range checks concern those resources. | A camera buffer identity or a shader field-to-camera relationship is not established. |
| spatial_readback.cpp:169-176, 323-350 | Descriptor-table handles are retained without resource resolution; matching list generation/submission precedes fence signaling. | The table-to-resource relationship remains unresolved. Completion of these copies does not establish camera semantics. |

The source contains useful distinctions between identity, binding and timing.
Its same-submission fence is relevant to completion of the recorded GPU copy;
it does not make unrelated CPU scene bytes part of that GPU transaction.
Likewise, a descriptor-table handle cannot be treated as a resource address.
No upstream historical compatibility claim transfers to our current build.

## Next experiment prerequisites

A different route could use a concrete current-build world-render consumer or
documented callback/API that identifies the pipeline/draw and the exact camera
resource, offset and span. Indirect descriptors need their own demonstrated
table/register/resource association. Resource lifetime, binding changes and
submission ordering must apply to the actual selected consumer. An owned
adversarial fixture should qualify that contract before a game-facing design.
This can be distinct from proving atomicity of the previous CPU-read method;
that method's stop does not rule out a separately justified GPU approach.

Reading a camera buffer would still leave control, extra eye-view scheduling
and temporal-history ownership unverified. No camera-buffer readback bridge,
game hook, alternate shader selection or repeat game probe is justified by
these four files alone. The [publication review](g1-camera-publication-review.md),
[completed literal-reference check](g1-technique-contract-method.md) and ADR 0003
therefore retain their limits. M0's tested code and available package are
unchanged, and headset testing remains deferred.
