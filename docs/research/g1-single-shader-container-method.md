# One shader-container structural observation

The [subtree metadata](g1-shader-metadata-method.md) provides exact local entry
identities but no active shader binding. This experiment asks whether one
bounded installed entry has structurally readable PASC/DXIL metadata, including
a reflection-bitcode header. It does not select or validate an active camera.

## Selection and owned qualification

A preparatory metadata-only analysis of the same hash-pinned index found 1,559
declared unencrypted/uncompressed .padxil entries. The explicit selection rule
is the largest such entry at most 1 MiB, with lexical full-path tie-break. The
result is 4,088 declared bytes. Its exact virtual-name SHA-256 is supplied to
the reader; it never picks a replacement if the selection fails. Names and
record locations are not exported. Selection is for format/reflection
availability, not semantic identity or representativeness of the other shaders.

The original optional reader uses selected PASC v8 wrapper rules informed by
pinned CDMW, with attribution and MIT notice retained. DXIL structural layouts
follow Microsoft's pinned [container source](shader-container-sources.json).
No upstream application/parser modules, decoder or dependencies are executed.

Owned synthetic containers test supported structure, duplicate/overlapping/
unaligned/out-of-bounds parts, wrapper lengths/version/digest agreement, and
program size/magic/bitcode spans. A separate owned fixture is compiled with the
installed Windows SDK DXC to Shader Model 6.0; its real container is inspected
and artificially wrapped for the reader test. It is not submitted to a GPU.
Owned files also test exact range counts, index/selector mismatches, encoded
flags, archive bounds, postflight changes and preserved/sanitized evidence.
The compiler fixture skips explicitly if SDK DXC is absent; it must not be
reported as a compiler pass in that environment. CI accesses owned files only.

## Read scope and rejection

Reuse the single explicit installed index path, expected index SHA-256 and
reviewed manifest/executable/catalog gates. Resolve metadata and require exactly
one .padxil whose complete virtual-path SHA-256 equals the explicit selector.
Require zero declared compression/encryption bits, equal stored/original sizes
and at most 1 MiB. No decoder, decompressor, decryption or alternate entry.

Open only its declared numeric sibling .paz file read-only. Reject symlinks,
non-files or a range outside the held file. Read exactly the selected range
twice; require equal bytes, held-file size/time/identity agreement and agreement
with the pathname's file identity. Recheck index hash and game/catalog identity
before container interpretation. The build gate authenticates neither the
archive as a whole nor this shader as an official distribution artifact. A
range SHA-256 records the bytes observed, not an a priori authenticity claim.
Observed stability is not an atomic installation snapshot.

PASC v8 size/offset and opaque digest agreement must match. Within DXBC, check
the bounded header/part table, unique tags, alignment, ranges and overlap.
Require DXIL and HASH; verify program and optional STAT bitcode-header spans,
magic, shader kind/model and agreement. No LLVM parsing, shader validation,
reflection-content interpretation, compiler invocation on game data or GPU
execution occurs. The HASH/wrapper comparison is opaque byte agreement, not
a cryptographic validation of shader code.

Keep the selected payload in reader memory only; write sanitized hashes,
lengths, stage/model and chunk-presence metadata. No raw shader, IR, asset list
or disassembly is retained or committed. Failures remain FAILED, exit 3, with
stage and sanitized rejection; exclusive outputs preserve earlier evidence.
No process access, game write, injection, capture or automatic retry occurs.

## Interpretation

A positive result is OBSERVED_SINGLE_SHADER_CONTAINER. It can qualify a future
bounded reflection reader; STAT presence does not prove its contents usable.
It does not map CPU candidate fields to GPU constants, establish producer
ordering/lifetime, identify an active world-camera shader or enable independent
world renders. Those G1/G2 requirements remain separate. Headset tests stay
deferred and the portable M0 build is unchanged.

## Actual structural result

The [single-entry observation](../evidence/g1-single-shader-container-2026-11-06.json)
ran committed source ae20066 once against reviewed Steam build 26480954 and
the prior exact index/entry-name fingerprints. Both reads of the selected
4,088-byte range agreed (8,176 archive bytes total). Index, held-file/path and
game/catalog rechecks passed, exit 0.

The entry is structurally PASC v8 containing 4,052 DXBC bytes, pixel shader
model 6.6, and seven chunks: SFI0, ISG1, OSG1, PSV0, STAT, HASH and DXIL.
The STAT bitcode-header check passed. No raw shader, IR or disassembly was
saved; no compiler or reflection API consumed game data, and no shader ran.
This establishes availability of a candidate reflection-bearing container,
not usable reflection content, an active camera shader or a CPU/GPU field map.

Next qualify an isolated bounded reflection reader against owned shaders, then
consider the same fingerprinted container under that separate scope. Retain
all input/range checks and expected payload/container hashes; do not choose a
different shader automatically to seek camera-looking fields. Camera control,
producer synchronization and independent game rendering remain unverified.
