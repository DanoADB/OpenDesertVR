# Bounded game constant observation

This follows the first metadata observation on reviewed Steam build 26480954,
not a camera write or a stereo renderer. The first snapshot's compute-prefix
CBVs included finite active 2816-byte ranges. That size is a candidate to
observe, not an inferred camera layout or address.

Use the same exclusive two-file bootstrap staging, normal Steam launch,
reviewed executable fingerprint, process creation-time/image check, exact
pipe server PID, empty shader directories and exact-hash cleanup as
[the metadata method](g1-bounded-game-metadata-method.md). Close only an
observed title menu normally; do not interrupt unsaved gameplay. Never replace
the addon in a running process. Preserve the first case and its title results.

The separately pinned `bc0ead3-gpu-constants-3` observer is qualified on owned
geometry: finite 256-byte green and blue PS constants match authored values;
the recorded unsubmitted yellow list returns no values. Pixel controls remain
RGB/black, normal owned exit and input postflight pass. See
[owned qualification](../evidence/g1-owned-gpu-constants-2026-11-06.json).
The native driver additionally tests repeated command-list execution and
wrong-queue/thread rejection. Neither test proves game-camera semantics.

For a fresh game case set `ODVConstantBytes=2816`. Only exact-size finite,
active reflected UPLOAD CBVs qualify; source buffers are not written or
transitioned. Up to six unresolved GPU copies retain their source/readback
resources, each at most 4 KiB. This does not bound all upstream tool caches or
existing source allocation sizes. A before-execute event supplies no completed
data: a later same-thread/queue after-present fence must complete. Unknown
submission/lifetime stays unavailable and consumes capacity. No CPU read of
cached game resource pointers is allowed. Completed CPU samples are frozen
for their originating draw's first qualified execution only.

Retain separately labeled observations for title menu, a user-loaded static
3D scene, and a deliberate camera movement if available. At most three
snapshots per case, 128 records each, four summary pages and six detailed
draw/dispatch queries per snapshot; keep within the bridge request limit.
Never overwrite an existing observation. A timeout is not permission to
restart the game; revalidate the same process before retrying observation.
The prefix can omit world work. Missing samples, queue mismatch or a menu-only
result do not establish absence of a game camera.

Keep values/handles/logs private. Public evidence contains fingerprints,
counts, bounded sizes, completion state and interpretation. No shader text,
bytecode, game assets, replacements, camera overrides or headless VR claims.
First determine whether candidate values can be obtained with proven GPU
completion, then test matrix/camera behavior against actual scene changes.
Finish by normal exit and removal of only the two exact files created for
this case; preserve changed files and report exceptions.
