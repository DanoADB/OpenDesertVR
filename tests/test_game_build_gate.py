# SPDX-License-Identifier: MIT
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments" / "frame-capture"))
from game_build_gate import validate_game_build


class GameBuildGateTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.steamapps = Path(self.temporary.name) / "steamapps"
        self.executable = self.steamapps / "common" / "Owned Test" / "bin64" / "CrimsonDesert.exe"
        self.executable.parent.mkdir(parents=True)
        self.payload = b"Owned test bytes only, never executable or game content."
        self.executable.write_bytes(self.payload)
        self.manifest = self.steamapps / "appmanifest_3321460.acf"
        self.manifest.write_text('"AppState"\n{\n"appid" "3321460"\n"buildid" "123"\n'
                                 '"StateFlags" "4"\n"installdir" "Owned Test"\n}\n', encoding="utf-8")
        self.catalog = self.steamapps / "reviewed.json"
        self.reviewed = {"schema_version": 1, "steam_app_id": "3321460", "builds": [{
            "steam_build_id": "123", "executable_sha256": hashlib.sha256(self.payload).hexdigest(),
            "executable_bytes": len(self.payload)}]}
        self.write_catalog()

    def write_catalog(self):
        self.catalog.write_text(json.dumps(self.reviewed), encoding="utf-8")

    def validate(self):
        return validate_game_build(self.executable, self.manifest, self.catalog)

    def test_reviewed_executable_and_content_build(self):
        self.assertEqual(self.validate()["steam_build_id"], "123")

    def test_content_update_with_unchanged_executable_is_rejected(self):
        self.manifest.write_text(self.manifest.read_text().replace('"buildid" "123"', '"buildid" "124"'))
        with self.assertRaises(ValueError):
            self.validate()

    def test_executable_update_with_unchanged_manifest_is_rejected(self):
        self.executable.write_bytes(self.payload + b"changed")
        with self.assertRaises(ValueError):
            self.validate()

    def test_installation_pending_update_is_rejected(self):
        self.manifest.write_text(self.manifest.read_text().replace('"StateFlags" "4"', '"StateFlags" "128"'))
        with self.assertRaises(ValueError):
            self.validate()

    def test_manifest_for_another_installation_is_rejected(self):
        self.manifest.write_text(self.manifest.read_text().replace('"installdir" "Owned Test"', '"installdir" "Other"'))
        with self.assertRaises(ValueError):
            self.validate()

    def test_ambiguous_build_identity_is_rejected(self):
        with self.manifest.open("a", encoding="utf-8") as stream:
            stream.write('"buildid" "124"\n')
        with self.assertRaises(ValueError):
            self.validate()

    def test_duplicate_reviewed_identity_is_rejected(self):
        self.reviewed["builds"].append(dict(self.reviewed["builds"][0]))
        self.write_catalog()
        with self.assertRaises(ValueError):
            self.validate()


if __name__ == "__main__":
    unittest.main()
