# SPDX-License-Identifier: MIT
# Selected pure filename-format routines are loaded from SHA-pinned CDMW source,
# MIT, Copyright (c) 2026 Ratrider. Full notice: docs/licenses/CDMW-MIT.txt.
"""Bounded technique-declaration contract inspection; no shader selection/execution."""
import __future__
import argparse
import ast
from collections import Counter
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET

from inspect_shader_entry import file_identity
from pamt_directory_probe import directory_markers, read_index
from pamt_metadata import Names, resolve_metadata
from resolve_shader_metadata import checked_location

REFERENCE_SHA256 = "507a2e252b45abd30205a770a8256b2906ddb333ce852026ac5ad1f5d676d990"
VERSIONS = {"cryptography": "50.0.2", "lz4": "4.4.5"}
MAX_ENTRY = 65536


def sha(data):
    return hashlib.sha256(data).hexdigest()


def bounded_file(path, maximum):
    if not path.is_file() or not 1 <= path.stat().st_size <= maximum:
        raise ValueError("input_file_size_outside_bounds")
    with path.open("rb") as stream:
        value = stream.read(maximum + 1)
    if not 1 <= len(value) <= maximum:
        raise ValueError("input_file_grew_outside_bounds")
    return value


def load_codec(reference):
    source = bounded_file(reference, 1024 * 1024)
    if sha(source) != REFERENCE_SHA256:
        raise ValueError("unreviewed_format_reference")
    versions = {name: importlib.metadata.version(name) for name in VERSIONS}
    if versions != VERSIONS:
        raise ValueError("unreviewed_codec_library_versions")
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
    import lz4.block
    constants = {"CHACHA20_HASH_INITVAL", "CHACHA20_IV_XOR", "CHACHA20_XOR_DELTAS"}
    functions = {"_rot32", "_add32", "_sub32", "hashlittle", "derive_chacha20_key_iv"}
    namespace = {"__builtins__": {"int": int, "str": str, "bytes": bytes, "len": len, "range": range},
                 "struct": struct, "Path": Path}
    selected = []
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            if node.targets[0].id in constants:
                namespace[node.targets[0].id] = ast.literal_eval(node.value)
        if isinstance(node, ast.FunctionDef) and node.name in functions:
            if node.decorator_list:
                raise ValueError("unexpected_format_decorator")
            selected.append(node)
    if {node.name for node in selected} != functions or not constants.issubset(namespace):
        raise ValueError("format_reference_contract_missing")
    # Reviewed pure math/name routines only; no upstream imports or top-level app code.
    code = compile(ast.Module(body=selected, type_ignores=[]), "pinned_format_math", "exec",
                   flags=__future__.annotations.compiler_flag, dont_inherit=True)
    exec(code, namespace)

    def transform(data, basename):
        key, nonce = namespace["derive_chacha20_key_iv"](basename)
        # The current library's 32-bit counter must not wrap for this bounded input.
        if struct.unpack_from("<I", nonce)[0] + (len(data) + 63) // 64 > 2 ** 32:
            raise ValueError("codec_counter_outside_supported_span")
        context = Cipher(algorithms.ChaCha20(key, nonce), mode=None).decryptor()
        return context.update(data) + context.finalize()

    def decode(data, basename, original, flags):
        if not 1 <= len(data) <= MAX_ENTRY or not 1 <= original <= MAX_ENTRY or (flags >> 4) & 15 != 3:
            raise ValueError("encoded_entry_outside_contract")
        value = transform(data, basename)
        if flags & 15 == 2:
            value = lz4.block.decompress(value, uncompressed_size=original)
        elif flags & 15 != 0 or len(data) != original:
            raise ValueError("unsupported_technique_compression")
        if len(value) != original:
            raise ValueError("decoded_size_mismatch")
        return value

    return decode, transform, namespace, versions, Cipher, algorithms, lz4.block


def inspect_xml(data, shader_paths):
    if not 1 <= len(data) <= MAX_ENTRY:
        raise ValueError("xml_size_outside_bounds")
    text = data.decode("utf-8-sig", errors="strict")
    if "<!DOCTYPE" in text.upper() or "<!ENTITY" in text.upper():
        raise ValueError("xml_declarations_rejected")
    root = ET.fromstring(text)
    known = {"category", "technique", "pipelinebinding"}
    tags, attributes = Counter(), Counter()
    by_basename = {}
    for path in shader_paths:
        by_basename.setdefault(path.rsplit("/", 1)[-1], set()).add(path)
    exact, ambiguous, potential, nodes = set(), 0, 0, 0
    for node in root.iter():
        nodes += 1
        if nodes > 4096 or len(node.attrib) > 64:
            raise ValueError("xml_structure_outside_bounds")
        tag = node.tag.rsplit("}", 1)[-1].casefold()
        if tag in known:
            tags[tag] += 1
        for name in node.attrib:
            name = name.rsplit("}", 1)[-1].casefold()
            if name in known:
                attributes[name] += 1
        for value in [*node.attrib.values(), node.text or ""]:
            value = value.strip().replace("\\", "/").casefold()
            if ".padxil" not in value:
                continue
            potential += 1
            matches = {value} if value in shader_paths else by_basename.get(value, set())
            if len(matches) == 1:
                exact.update(matches)
            elif len(matches) > 1:
                ambiguous += 1
    return {"xml_nodes": nodes, "known_tag_counts": dict(tags), "known_attribute_counts": dict(attributes),
            "scalar_values_containing_padxil": potential, "literal_shader_path_matches": len(exact),
            "ambiguous_basename_values": ambiguous,
            "matched_shader_name_sha256": sorted(sha(value.encode("utf-8")) for value in exact)}


def self_test(codec):
    decode, transform, namespace, versions, Cipher, algorithms, block = codec
    stream = Cipher(algorithms.ChaCha20(bytes(32), bytes(16)), mode=None).decryptor().update(bytes(64))
    if stream[:16].hex() != "76b8e0ada0f13d90405d6ae55386bd28":
        raise ValueError("owned_stream_vector_failed")
    if namespace["hashlittle"](b"", 0) != 0xdeadbeef:
        raise ValueError("owned_hash_vector_failed")
    if namespace["derive_chacha20_key_iv"]("x/OWNED.TECHNIQUE") != namespace["derive_chacha20_key_iv"]("owned.technique"):
        raise ValueError("owned_basename_case_contract_failed")
    plain = b'<Technique Category="Owned"><PipelineBinding Shader="owned.padxil"/></Technique>'
    for compressed in (False, True):
        source = block.compress(plain, store_size=False) if compressed else plain
        encoded = transform(source, "owned.technique")
        if decode(encoded, "owned.technique", len(plain), 0x32 if compressed else 0x30) != plain:
            raise ValueError("owned_codec_roundtrip_failed")
    result = inspect_xml(plain, {"owned/owned.padxil"})
    if result["literal_shader_path_matches"] != 1:
        raise ValueError("owned_literal_reference_failed")
    return {"status": "PASS", "scope": "Owned stream/hash/name/codec/XML fixtures only", "versions": versions}


def verify_record(index, digest, entry):
    data, actual = read_index(index)
    if actual != digest:
        raise ValueError("index_changed")
    directory_markers(data)
    archive_ids = {struct.unpack_from("<I", data, 12 + i * 12)[0]
                   for i in range(struct.unpack_from("<I", data, 4)[0])}
    if entry[1] not in archive_ids:
        raise ValueError("entry_archive_not_declared")
    cursor = 12 + struct.unpack_from("<I", data, 4)[0] * 12
    blocks, counts = [], []
    for stride in (1, 1, 16, 20):
        if cursor + 4 > len(data):
            raise ValueError("metadata_count_truncated")
        count = struct.unpack_from("<I", data, cursor)[0]
        cursor += 4
        if cursor + count * stride > len(data):
            raise ValueError("metadata_table_truncated")
        blocks.append(data[cursor:cursor + count * stride])
        counts.append(count)
        cursor += count * stride
    if cursor + 4 > len(data):
        raise ValueError("metadata_extension_truncated")
    length = struct.unpack_from("<I", data, cursor)[0]
    if length > 8192 or cursor + 4 + length != len(data):
        raise ValueError("metadata_extension_bounds")
    extensions = data[cursor + 4:].split(b"\0")
    names, folders = Names(blocks[1], True), Names(blocks[0], True)
    matches = []
    for ordinal, (name, offset, stored, original, archive, flags) in enumerate(struct.iter_unpack("<IIIIHH", blocks[3])):
        if [archive, offset, stored, original, flags] != entry[1:]:
            continue
        if flags >> 8 >= len(extensions) or extensions[flags >> 8] != b".technique":
            raise ValueError("unexpected_entry_extension")
        owners = [folders.resolve(path) for _, path, start, count in struct.iter_unpack("<IIII", blocks[2])
                  if start <= ordinal < start + count <= counts[3]]
        if len(owners) != 1:
            raise ValueError("ambiguous_metadata_owner")
        matches.append(owners[0] + "/" + names.resolve(name) + ".technique")
    if matches != [entry[0]]:
        raise ValueError("metadata_record_not_unique_and_exact")


def run(args, codec):
    from game_build_gate import validate_game_build
    root = args.game_root.resolve(strict=True)
    catalog = Path(__file__).resolve().parents[1] / "frame-capture/reviewed-builds.json"
    catalog_hash = sha(catalog.read_bytes())
    identity = validate_game_build(root / "bin64/CrimsonDesert.exe", args.steam_manifest, catalog)
    plan_data = bounded_file(args.metadata_plan, 128 * 1024)
    if sha(plan_data) != args.plan_sha256:
        raise ValueError("metadata_plan_changed_or_oversized")
    records = json.loads(plan_data)["records"]
    if not isinstance(records, list) or len(records) != 16:
        raise ValueError("technique_catalog_count_outside_bounds")
    for record in records:
        entry = record["entry"]
        if (len(entry) != 6 or type(entry[0]) is not str or any(type(value) is not int for value in entry[1:]) or
                not 1 <= entry[3] <= MAX_ENTRY or not 1 <= entry[4] <= MAX_ENTRY):
            raise ValueError("invalid_metadata_record_types_or_sizes")
    if sum(record["entry"][4] for record in records) > 128 * 1024:
        raise ValueError("technique_catalog_outside_bounds")
    if len({record["entry"][0] for record in records}) != 16:
        raise ValueError("duplicate_technique_records")
    checked_location(argparse.Namespace(game_root=root, index=args.shader_index, index_sha256=args.shader_index_sha256))
    shader_data, shader_hash = read_index(args.shader_index)
    if shader_hash != args.shader_index_sha256:
        raise ValueError("shader_index_changed")
    shader_paths = {item[0].casefold() for item in resolve_metadata(shader_data)["_entries"] if item[0].casefold().endswith(".padxil")}
    observations = []
    for record in records:
        index = Path(record["index_path"])
        checked_location(argparse.Namespace(game_root=root, index=index, index_sha256=record["index_sha256"]))
        entry = record["entry"]
        if len(entry) != 6 or type(entry[0]) is not str or any(type(value) is not int for value in entry[1:]):
            raise ValueError("invalid_metadata_record_types")
        if sha(entry[0].encode()) != record["entry_name_sha256"]:
            raise ValueError("metadata_name_fingerprint_changed")
        verify_record(index, record["index_sha256"], entry)
        path, archive, offset, stored, original, flags = entry
        if not path.endswith(".technique") or not 0 <= archive <= 65535 or not 1 <= stored <= MAX_ENTRY or not 1 <= original <= MAX_ENTRY:
            raise ValueError("entry_outside_scope")
        archive_path = index.with_name(f"{archive}.paz")
        if archive_path.is_symlink() or not archive_path.is_file():
            raise ValueError("unexpected_archive_path")
        with archive_path.open("rb") as stream:
            before = os.fstat(stream.fileno())
            if offset < 0 or offset + stored > before.st_size:
                raise ValueError("archive_range_outside_bounds")
            stream.seek(offset)
            raw = stream.read(stored)
            stream.seek(offset)
            again = stream.read(stored)
            if len(raw) != stored or raw != again or file_identity(before) != file_identity(os.fstat(stream.fileno())) or file_identity(before) != file_identity(archive_path.stat()):
                raise ValueError("archive_changed_or_short_read")
        decoded = codec[0](raw, path.rsplit("/", 1)[-1], original, flags)
        observations.append({"entry_name_sha256": sha(path.encode()), "index_sha256": record["index_sha256"],
                             "stored_bytes": stored, "decoded_bytes": len(decoded), "stored_sha256": sha(raw),
                             "decoded_sha256": sha(decoded), **inspect_xml(decoded, shader_paths)})
    for record in records:
        if read_index(Path(record["index_path"]))[1] != record["index_sha256"]:
            raise ValueError("index_changed_postflight")
    if (read_index(args.shader_index)[1] != shader_hash or sha(bounded_file(args.metadata_plan, 128 * 1024)) != args.plan_sha256 or
            sha(catalog.read_bytes()) != catalog_hash or
            validate_game_build(root / "bin64/CrimsonDesert.exe", args.steam_manifest, catalog) != identity):
        raise ValueError("inputs_changed_postflight")
    return {**identity, "status": "OBSERVED_TECHNIQUE_LITERAL_REFERENCE_CONTRACT", "entries": len(observations),
            "archive_bytes_read": 2 * sum(item["stored_bytes"] for item in observations), "observations": observations,
            "versions": codec[3], "reference_sha256": REFERENCE_SHA256,
            "plan_sha256": args.plan_sha256, "shader_index_sha256": shader_hash, "reviewed_catalog_sha256": catalog_hash,
            "scope": "Sixteen identified technique declarations only. Selected reviewed pure format routines and installed codecs, then bounded UTF-8 XML inspection. Literal full-path/unique-basename equality only, no hash/stem inference. Public report omits names, keys, XML and payload; private metadata plan remains local. No shader scan/execution, game process access, writes or retry. Input stability and valid decoding do not authenticate assets or establish runtime bindings."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--format-reference", type=Path, required=True)
    parser.add_argument("--self-test", action="store_true")
    for name in ("game-root", "steam-manifest", "metadata-plan", "shader-index", "output"):
        parser.add_argument("--" + name, type=Path)
    for name in ("plan-sha256", "shader-index-sha256"):
        parser.add_argument("--" + name)
    args = parser.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "frame-capture"))
    codec = load_codec(args.format_reference)
    qualified = self_test(codec)
    if args.self_test:
        print(json.dumps(qualified, indent=2))
        return 0
    if any(getattr(args, name) is None for name in
           ("game_root", "steam_manifest", "metadata_plan", "shader_index", "output", "plan_sha256", "shader_index_sha256")):
        parser.error("All explicit input identities and a fresh output are required")
    with args.output.open("x", encoding="utf-8") as output:
        report = {"schema_version": 1, "status": "FAILED", "evidence_kind": "bounded_technique_literal_reference_contract",
                  "owned_qualification": qualified, "source_sha256": sha(Path(__file__).read_bytes()),
                  "camera_field_layout": "UNVERIFIED", "native_stereo": "UNVERIFIED"}
        code = 3
        try:
            report.update(run(args, codec))
            code = 0
        except Exception:
            report["failure_reason"] = "input_codec_or_contract_rejected"
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")
    print(json.dumps(report, indent=2))
    return code


if __name__ == "__main__":
    sys.exit(main())
