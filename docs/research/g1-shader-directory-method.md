# Bounded shader-directory candidate locator

The [CDMW lead](cdmw-render-lead.md) places compiled shader entries in archives,
not loose installed files. Before considering payload/reflection work, locate
the metadata that could contain the relevant subtree. This experiment answers
only whether a bounded archive-index directory block contains either of two
fixed markers. It does not resolve full paths or identify shader entries.

The original optional pamt_directory_probe.py uses the selected PAMT header
layout described by pinned CDMW archive_format.py. Its root MIT notice is
retained. No upstream module, parser, decoder or dependency is executed.
The shader-preview source inspects container metadata; the consumer-study tool
uses Windows SDK DXC but addresses vertex-record consumers, not engine CPU
publication. Neither is a ready-made current-camera proof.

## Read scope and bounds

Require the existing reviewed game-build gate before reading index contents:
exact manifest/install path, build ID and executable fingerprint. Enumerate only
direct four-digit package directories in that root, each with 0.pamt. Reject
symlinks and missing/non-file indices. No registry or drive-wide discovery.

At most 64 indices, 16 MiB per index, 128 MiB per pass and two passes. One extra
sentinel byte may be read to reject an input that grows beyond a bound. Bounds
apply to actual accepted read lengths as well as initial sizes. Parse only the leading
archive-ID table and sized directory block (at most 8 MiB); reject truncation,
duplicate/invalid archive IDs or out-of-bounds spans. Count shadercache__ and
d3d12 only within that block; bytes elsewhere cannot satisfy the marker check.
These substrings can be in separate records, so agreement is not a resolved
shader-directory path. No full-format/CRC validity claim is made.

Hash each complete index, recheck the package set and compare a second hash
pass, then recheck game identity/catalog. This detects observed changes, not an atomic
installation snapshot. No PAZ payload, process memory, shader, extraction,
decompression, decryption or game write is involved. Export hashes, lengths
and marker counts only; no raw index bytes, asset names or private paths.
Create evidence exclusively and refuse to overwrite it; failures remain
sanitized FAILED, exit 3, with no automatic retry or fallback.

Owned metadata tests cover markers outside the directory span, valid counts,
truncated headers/tables/blocks, oversized counts/spans/files/aggregate input,
duplicate identifiers and preserved/sanitized failure evidence. CI uses those
fixtures only. A positive actual result would justify reviewing a narrow path/
entry resolver, not running CDMW, decoding assets, asserting current shader
bindings or advancing the game adapter/G2. Headset testing is unrelated.

## Actual marker observation

Committed source 3e7c797 ran once against the reviewed Steam build 26480954.
The [sanitized record](../evidence/g1-shader-directory-markers-2026-11-06.json)
reports 34 indices, 47,417,639 input bytes per pass, and successful hash/game
identity rechecks, exit 0. Exactly one index had both markers in its directory
block. No raw metadata was retained; no shader data or process memory was accessed.
The counts do not resolve whether those markers form one full directory path.
Next qualify a bounded path/entry resolver for this single candidate rather
than adopting or executing the upstream application's archive scan/extractor.
