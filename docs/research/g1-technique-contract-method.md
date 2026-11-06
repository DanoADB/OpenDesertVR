# Bounded technique declaration contract inspection

The pinned CDMW source describes Category, Technique and PipelineBinding in
`.technique` declarations, but supplies no verified identifier mapping to the
current shader cache. Inspect the sixteen metadata-identified declarations for
literal full-path or unique-basename references to known `.padxil` entries.
Opaque identifiers and stem/hash similarities remain uninterpreted. This is
an offline asset-format question, not a runtime binding or camera-layout test.

## Inputs and implementation

Require the reviewed game build/catalog, an explicit SHA-pinned local metadata
plan, the exact current shader index, and the pinned CDMW archive_format.py
fingerprint in technique_contract.py. Validate each planned record against its
index's name chain, folder ownership, archive ID, range, sizes and flags. Require
exactly sixteen unique declaration paths, at most 64 KiB stored/decoded per
entry and at most 128 KiB decoded overall. Read each numeric sibling PAZ range
twice, comparing bytes and file identity; recheck indices, plan and game build.
These stability checks do not authenticate an asset or prove mounted ownership.

The optional recipe selects only `_rot32`, `_add32`, `_sub32`, `hashlittle` and
`derive_chacha20_key_iv`, plus three literal constants, from the exact reviewed
MIT source. It executes those pure format routines without upstream imports or
application top-level code. AST selection is not a sandbox for arbitrary input;
the immutable source fingerprint is essential. Full attribution is retained in
THIRD_PARTY_NOTICES.md and docs/licenses/CDMW-MIT.txt.

Use the already installed cryptography 50.0.2 and lz4 4.4.5 only; reject other
versions. Support the identified format's encryption type 3 and compression
types 0/2. Reject counter wrap, size disagreement and unsupported flags. The
[cryptography API](https://cryptography.io/en/50.0.2/hazmat/primitives/symmetric-encryption/)
and [LZ4 block API](https://python-lz4.readthedocs.io/en/stable/lz4.block.html)
define the selected codec interfaces; require exact decoded size in addition
to the LZ4 output bound. No dependency installation, game process access,
configuration change, shader execution or game-file write is performed.

Decode only in memory, require strict UTF-8 XML, reject DTD/entity declarations,
and cap input at 64 KiB, nodes at 4,096 and attributes at 64 per node. Compare
individual scalar values only, normalizing case and slash direction. Export
counts, input/payload fingerprints and hashes of matched shader names. Do not
export keys, raw names, XML or payload. The local metadata plan is ignored by
Git. The public report reserves a fresh file exclusively and records sanitized
failure instead of retrying or choosing another shader.

## Qualification and limits

Local owned qualification checks a known ChaCha20 stream prefix, empty hash
vector, basename/case behavior, compressed and uncompressed round trips, and a
literal XML reference. These are codec/format checks, not game-data evidence.
Three standard-library tests cover literal equality, ambiguous basenames,
opaque and compound values, malformed XML/encoding/entities and input bounds.
CI runs those XML tests without codecs or the external source reference.

A positive literal reference still requires a material/pass/runtime connection
before selecting another game shader. A negative result addresses only literal
references in these sixteen declarations; it does not exclude opaque keys,
other formats, runtime generation or another interface. G1 camera publication,
field semantics and runtime association remain unverified; G2 stays held.
Headset testing remains deferred at the user's request.

## Current-build rejection, 2026-11-06

The [diagnostic evidence](../evidence/g1-technique-contract-rejection-2026-11-06.json)
records an unsuccessful attempt using source 8cdd340. It reached XML inspection
of the first declaration after two 1,738-byte archive reads. The decoded size
matched its declaration and carried a UTF-8 BOM, but the XML contract rejected
it. The other fifteen declarations and final input postflight were not reached.
There is no literal-reference result, and successful decoding alone does not
validate the document. Do not treat this as evidence that references are absent.

The initial 78e172e attempt also failed but omitted rejection-stage and partial
read accounting. The diagnostic run followed the reporting fix; both attempts
remain distinct. Stop this asset probe pending a specific format review and
owned qualification of any parser change, rather than loosening checks or
retrying until it parses. G2 and the production adapter remain held.
