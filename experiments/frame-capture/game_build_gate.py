# SPDX-License-Identifier: MIT
"""Read-only build identity checks before any external diagnostic launch."""
import hashlib
import json
from pathlib import Path
import re


def field(text, name):
    values = re.findall(r'^\s*"' + re.escape(name) + r'"\s+"([^"\r\n]*)"\s*$', text, re.MULTILINE)
    if len(values) != 1:
        raise ValueError("Expected exactly one Steam manifest " + name)
    return values[0]


def validate_game_build(executable, manifest_path, catalog_path):
    executable = Path(executable).resolve()
    manifest_path = Path(manifest_path).resolve()
    if executable.name.casefold() != "crimsondesert.exe" or manifest_path.name != "appmanifest_3321460.acf":
        raise ValueError("Unexpected game executable or manifest name")
    manifest = manifest_path.read_text(encoding="utf-8-sig")
    if field(manifest, "appid") != "3321460" or field(manifest, "StateFlags") != "4":
        raise ValueError("Expected the installed, ready Steam app 3321460")
    install_dir = field(manifest, "installdir")
    if install_dir in {"", ".", ".."} or "/" in install_dir or "\\" in install_dir or ":" in install_dir:
        raise ValueError("Expected a single Steam installation directory name")
    expected = (manifest_path.parent / "common" / install_dir / "bin64" / "CrimsonDesert.exe").resolve()
    if str(executable).casefold() != str(expected).casefold():
        raise ValueError("Manifest does not describe the selected game installation")
    catalog = json.loads(Path(catalog_path).read_text(encoding="utf-8-sig"))
    if catalog.get("schema_version") != 1 or catalog.get("steam_app_id") != "3321460":
        raise ValueError("Unsupported reviewed-build catalog")
    builds = catalog.get("builds", [])
    pairs = [(b["executable_sha256"], b["steam_build_id"]) for b in builds]
    if not pairs or len(pairs) != len(set(pairs)):
        raise ValueError("Reviewed build identities must be present and unique")
    value = hashlib.sha256()
    with executable.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    executable_hash = value.hexdigest()
    build_id = field(manifest, "buildid")
    matching = [b for b in builds if b["executable_sha256"] == executable_hash and b["steam_build_id"] == build_id]
    if len(matching) != 1 or matching[0]["executable_bytes"] != executable.stat().st_size:
        raise ValueError("Game executable/Steam build combination has not been reviewed")
    return {"steam_app_id": "3321460", "steam_build_id": build_id, "executable_sha256": executable_hash}
