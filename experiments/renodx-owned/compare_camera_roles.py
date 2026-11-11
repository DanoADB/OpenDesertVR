# SPDX-License-Identifier: MIT
"""Compare bounded private observer14 details; export no handles or game bytes.

An alias is only a candidate. This does not establish that GPU contents stayed
unchanged, that an indirect shader accessed the CBV, or that world pixels moved.
"""
import argparse
import json
from pathlib import Path


def compare(details):
    if not isinstance(details, list) or len(details) > 6:
        raise ValueError("expected at most six bounded draw details")
    roles = {0: [], 1: []}
    for draw in details:
        context = draw.get("odvNativeState", {}).get("recordingContext", {})
        role = context.get("role")
        if role not in roles or draw.get("submissionKnown") is not True:
            continue
        slot = 17 if role == 0 else 18
        ranges = [b for b in draw.get("constantBuffers", [])
                  if b.get("slot") == slot and b.get("space") == 35
                  and b.get("size") == 2816 and not b.get("fullRange")
                  and b.get("usedByActiveShader") is True]
        if len(ranges) != 1:
            continue
        bound = ranges[0]
        if (not bound.get("bufferHandle") or not context.get("recordingList")
                or type(context.get("recordingSerial")) is not int or context["recordingSerial"] <= 0
                or type(bound.get("offset")) is not int or bound["offset"] < 0):
            continue
        sample_ready = any(s.get("slot") == slot and s.get("space") == 35
                           and s.get("byteCount") == 2816 and s.get("state") == "READY"
                           for s in draw.get("odvGpuConstantSamples", []))
        roles[role].append((draw, context, bound, sample_ready))
    pairs = []
    for shadow, sc, sb, ready in roles[0]:
        for world, wc, wb, _ in roles[1]:
            same_range = all(sb.get(k) == wb.get(k) for k in ("bufferHandle", "offset", "size"))
            same_recording = all(sc.get(k) == wc.get(k) for k in ("recordingList", "recordingSerial"))
            before = (type(shadow.get("index")) is int and type(world.get("index")) is int
                      and shadow["index"] < world["index"])
            signature_safe = (wc.get("indirect") is False or
                              (wc.get("signatureMetadataKnown") is True
                               and wc.get("indirect") is True
                               and wc.get("rootMutationMask") == 0
                               and wc.get("signatureAction") in (0, 1)))
            pairs.append({"shadow_draw": shadow.get("index"), "world_draw": world.get("index"),
                          "same_range": same_range, "same_recording": same_recording,
                          "shadow_captured_before_world": before, "shadow_gpu_sample_ready": ready,
                          "world_signature_without_root_mutations": signature_safe,
                          "copy_forward_candidate": same_range and same_recording and before and ready and signature_safe,
                          "world_root_description": wc.get("rootSignature", {})})
    return {"status": "OBSERVED_ROLE_ALIAS_CANDIDATES" if pairs else "NO_COMPARABLE_ROLE_PAIR",
            "shadow_records": len(roles[0]), "world_records": len(roles[1]), "pairs": pairs,
            "intervening_source_writes": "UNVERIFIED", "cached_binding_validity_after_prior_indirects": "UNVERIFIED",
            "world_influence": "NOT_RUN",
            "stereo": "NOT_RUN",
            "meaning": "A candidate requires write-order validation before substituting a private camera copy. Binding and submission do not prove a nonzero indirect draw or shader access."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("details", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.details.stat().st_size > 1024 * 1024:
        raise ValueError("private details exceed bounded input size")
    result = compare(json.loads(args.details.read_text(encoding="utf-8")))
    with args.output.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(result, output, indent=2)
        output.write("\n")
    print(result["status"])
