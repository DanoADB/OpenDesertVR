# One-index shader metadata resolution

The [directory-marker observation](g1-shader-directory-method.md) identified one
candidate index. This next method resolves its virtual folder path and classifies
only entry metadata in that exact root and its bounded descendants. It does not read PAZ payloads, extract or
decode shaders, access the game process or establish active bindings.

## Original bounded implementation

Optional pamt_metadata.py adapts selected metadata layout, parent-chain and flag
rules from pinned CDMW archive_format.py and models.py. Full MIT notice and
source fingerprints are retained. It does not import or execute upstream code.
Legacy absolute-parent and compact relative-parent encodings are distinct;
the presence of a strictly bounded extension table selects the compact format.
Unsupported or malformed metadata fails without a heuristic fallback.

Enforce the existing 16 MiB index limit, exact header/table bounds, at most
100,000 folder records, 500,000 file records and 100,000 selected entries.
Virtual names use strict UTF-8, at most 64 parent records and 4,096 characters.
Reject cycles, truncated/oversized distances, invalid parent references, control
characters, absolute/traversal paths and duplicate names. Reject duplicate
folder paths, overlapping nonempty folder ranges, out-of-range file spans,
invalid archive IDs/extensions and selected stored/original sizes outside
1..64 MiB. No checksum, PAZ range or complete-format validity is inferred.

Require exactly one resolved shadercache__/d3d12 directory. Select that root and
paths with its exact slash-delimited prefix; similarly named siblings do not
match. Only selected filenames are resolved, with the same total entry bound
across the subtree. Report direct versus descendant entry counts separately.
Classify padXIL/root-signature/other extensions, declared
compression/encryption flags, stored-size range and four fixed name-marker
counts. These metadata flags are not payload validation. Raw paths, records and
names stay in memory and are omitted from summary output; no asset list is
written. A declared shader entry does not prove that the game loads or binds it.

## Actual input gate and evidence

resolve_shader_metadata.py requires one explicit direct numeric-package/0.pamt
path under the supplied reviewed installation and its expected SHA-256 from
the prior observation. Reject symlinks and any different path shape or digest.
Reuse the manifest/executable build gate before reading. Compare a second
index hash and game/catalog identity after resolution. This detects observed
changes, not an atomic installation snapshot. Open evidence exclusively;
sanitized failures exit 3 without overwriting prior files or retrying.

Seven owned tests exercise both encodings, direct/subtree/sibling selection,
same basenames in distinct folders, valid flags/classes, path cycles,
truncation, UTF-8/traversal/depth, invalid entries/extensions, duplicates,
overlap and exclusive failure evidence. CI never accesses game files.

A positive observation would narrow the next question: is there a small,
identifiable candidate whose declared container/consumer can test camera-field
types or layout? It does not authorize a bulk asset scan, decoder import or a
new game-process/capture method. Any payload/reflection work needs its own
bounded design and owned qualification. Camera publication, active-frame
association, camera writes and independent world rendering remain unverified.

The [first exact-directory-only observation](../evidence/g1-shader-direct-directory-2026-11-06.json)
from d1beb56 resolved the root but reported zero direct entries. Its report is
preserved: it did not inspect descendants and does not show missing shader
resources. A separate metadata inspection of the same hash-pinned index found
one descendant owning the 84,678 records. The subsequent subtree selection is
explicit and owned-tested; it changes no payload/process access or runtime
acceptance gate and does not retroactively alter the first report.
