# SPDX-License-Identifier: MIT
"""Bounded local snapshot of the explicitly selected external-tool log."""
import hashlib


def snapshot_log(source, destination, limit_bytes=16 * 1024 * 1024):
    if limit_bytes <= 0:
        raise ValueError("Log snapshot limit must be positive")
    if source.resolve() == destination.resolve():
        raise ValueError("Snapshot must be separate from the live log")
    try:
        stream = source.open("rb")
    except FileNotFoundError:
        return {"status": "UNAVAILABLE"}
    digest = hashlib.sha256()
    copied = 0
    with stream, destination.open("xb") as output:
        # Read at most the initial size, even if the tool is still appending.
        initial_size = source.stat().st_size
        remaining = min(initial_size, limit_bytes)
        while remaining:
            block = stream.read(min(1024 * 1024, remaining))
            if not block:
                break
            output.write(block)
            digest.update(block)
            copied += len(block)
            remaining -= len(block)
    return {"status": "SAVED", "snapshot_bytes": copied,
            "initial_source_bytes": initial_size, "truncated": copied < initial_size,
            "snapshot_sha256": digest.hexdigest(), "content": "LOCAL_ONLY"}
