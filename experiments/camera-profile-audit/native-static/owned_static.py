# SPDX-License-Identifier: MIT
"""Qualify installed MSVC bounded decoding on an original owned executable."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audit_profile import Image
from function_extents import containing_function, disassemble, observe, runtime_functions


def main():
    executable, tool = map(lambda value: Path(value).resolve(strict=True), sys.argv[1:])
    image = Image(executable.read_bytes())
    tool_hash = hashlib.sha256(tool.read_bytes()).hexdigest()
    result = subprocess.run([str(tool), "/NOLOGO", "/EXPORTS", str(executable)],
                            capture_output=True, timeout=10)
    if result.returncode or len(result.stdout) > 65536:
        raise ValueError("owned_export_query_failed")
    matches = re.findall(rb"^\s*\d+\s+[0-9A-F]+\s+([0-9A-F]+)\s+OwnedPublish(?: = OwnedPublish)?\s*$",
                         result.stdout, re.MULTILINE)
    if len(matches) != 1:
        raise ValueError("owned_export_not_unique")
    seed = int(matches[0], 16)
    record = containing_function(runtime_functions(image), seed)
    summary = observe(image, [("owned_export", seed)])
    if not summary["seeds"][0]["seed_at_function_entry"]:
        raise ValueError("owned_export_is_not_function_entry")
    decoded = disassemble(tool, tool_hash, executable, image, record)
    if decoded["instruction_lines"] < 4 or not decoded["mnemonic_counts"].get("call"):
        raise ValueError("owned_calling_function_not_decoded")
    try:
        disassemble(tool, "0" * 64, executable, image, record)
    except ValueError:
        pass
    else:
        raise ValueError("changed_tool_not_rejected")
    print("PASS: owned PE extent, export entry, bounded installed-tool decoding and changed-tool rejection")


if __name__ == "__main__":
    main()
