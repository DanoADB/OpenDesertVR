# Camera publication contract: source review

Reviewed 2026-11-06, public CrimsonDesertTelemetry revision
434b3b96646b410ab8396483e31c538e051f3a4e. The remote HEAD still matched that pin
at review. Five selected source/document hashes and URLs are retained in the
[review record](../evidence/g1-camera-publication-review-2026-11-06.json).
No new code was adopted or executed. This is not an exhaustive engine audit.

## What the selected sources establish

| Source | Observed behavior | Missing contract |
|---|---|---|
| EngineCameraReader.cs, Capture | Reads the chain and object counter, takes two byte copies, checks agreement, decodes and tracks buffer progress. Allows bounded retry. | Does not identify all engine writers, their update ordering, payload ownership or source lifetime guarantee. |
| SceneConstantsDecoder.cs, Decode | Assigns a buffer word the frame-number role and validates dimensions, sentinel, basis and matrices. | Numeric decoding does not establish when the engine publishes that word relative to geometry. |
| ENGINE_CAMERA_RESEARCH.md | Describes historical camera observations and explicitly limits the frame-atomicity claim of repeated reads. | Historical correlation is not a current-build synchronization contract. |
| BUILD-26361232-RECOVERY.md | Reports historical agreement between object and buffer frame words, progressing samples and recovered field locations. | A different older build and observed agreement do not prove update ordering or lifetime on our build. |
| render_capture.cpp, ReadScene/Record | Reads and validates scene data twice, records GPU buffer copies, and compares scene data again around recording; subsequent submission uses a fence. | GPU readback completion does not synchronize every engine CPU camera writer. Repeated scene agreement remains observation, not an engine publication protocol. |

The upstream profile selected for our hypotheses is build 26448710; our current
reviewed executable is build 26480954. We do not install that old profile or
transfer its compatibility claims. Our own association and rejected-field
observations remain the only relevant live evidence obtained here.

Observer-owned mutexes or a seqlock in an upstream telemetry transport can
protect the observer's published copy. They cannot retroactively synchronize
the original engine reads. Likewise, a D3D12 fence can prove completion of work
queued before it; that alone does not identify which CPU camera state the game
used for a particular world render. These are separate ownership questions.

## Consequence and next evidence

The [native batch prototype](g1-native-selected-read-method.md) establishes
bounded read mechanics and lower owned-fixture overhead. The adversarial owned
producer demonstrates why matching copies and numeric consistency are too weak
to establish a complete engine-frame snapshot without producer semantics.
Do not implement a game wrapper or repeat a game probe merely seeking equality.

Before that route advances, obtain evidence applicable to the exact executable
for token meaning/coverage, writer ordering, object/buffer replacement and lifetime,
and association with the rendered camera. Suitable leads could be a documented
engine callback/API, a reviewed current-build producer implementation or a
capture that identifies a camera constant's production and consumption. No
such interface is established by these five sources. Test the resulting actual
contract with an adversarial owned producer before a game-facing experiment.

This is a stop for the current acquisition route pending new engineering
evidence, not proof that BlackSpace VR is impossible. Reading camera data still
would not establish safe camera control, an extra world-render invocation,
temporal-history ownership or independent stereo. ADR 0003 continues to hold G2.
M0 and its existing portable artifact remain independent of this research;
headset tests remain deferred at the user's request.
