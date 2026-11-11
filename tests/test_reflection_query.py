# SPDX-License-Identifier: MIT
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments/shader-consumer-audit"))
import reflection_query as query
from test_shader_container import fixture


def valid():
    return {"schema_version": 1, "status": "OBSERVED_REFLECTION_METADATA", "constant_buffers": 1,
            "bound_resources": 1, "variables": 1, "query_matches": [{"query_offset": 32, "buffer_ordinal": 0,
                "declaration_offset": 32, "declaration_bytes": 4, "exact_start": True, "type": "uint",
                "class": 0, "rows": 1, "columns": 1, "elements": 0, "members": 0, "compiler_used": True}]}


class QueryTests(unittest.TestCase):
    def tools(self, root):
        helper, library = root / "odv_shader_reflection.exe", root / "dxcompiler.dll"
        helper.write_bytes(b"owned helper not executed")
        library.write_bytes(b"owned SDK stand-in not loaded")
        return helper, query.digest(helper, 1000), library, query.digest(library, 1000)

    def test_fixed_export_schema_and_typed_span_validation(self):
        result = valid()
        result["extra_raw_name"] = "ownedPrivateName"
        result["scope"] = "ownedPrivateName"
        self.assertNotIn("ownedPrivateName", json.dumps(query.validate(result)))
        for field, value in (("type", "rawName"), ("query_offset", 999), ("declaration_bytes", 0),
                             ("exact_start", False), ("compiler_used", 1), ("buffer_ordinal", 64)):
            data = valid()
            data["query_matches"][0][field] = value
            with self.subTest(field=field), self.assertRaises(query.ReflectionError):
                query.validate(data)
        for key, value in (("schema_version", True), ("variables", -1), ("constant_buffers", 65)):
            data = valid()
            data[key] = value
            with self.assertRaises(query.ReflectionError):
                query.validate(data)

    def test_tool_fingerprints_reject_before_any_child_runs(self):
        with tempfile.TemporaryDirectory() as folder:
            helper, _, library, library_hash = self.tools(Path(folder))
            with patch.object(query.subprocess, "run") as child, self.assertRaises(query.ReflectionError):
                query.checked_tools(helper, "0" * 64, library, library_hash)
            child.assert_not_called()

    @unittest.skipUnless(os.name == "nt", "Windows helper protocol")
    def test_timeout_failed_exit_oversized_and_malformed_output_do_not_retry(self):
        with tempfile.TemporaryDirectory() as folder:
            tools = self.tools(Path(folder))
            failure = subprocess.TimeoutExpired(["owned"], 10)
            with patch.object(query.subprocess, "run", side_effect=failure) as child:
                with self.assertRaisesRegex(query.ReflectionError, "timeout"):
                    query.reflect(fixture()[36:], *tools)
                child.assert_called_once()
            for code, output in ((3, b"{}"), (0, b"bad-json"), (0, b"x" * (128 * 1024 + 1)), (0, b"{}")):
                with patch.object(query.subprocess, "run", return_value=SimpleNamespace(returncode=code, stdout=output)) as child:
                    with self.assertRaises(query.ReflectionError):
                        query.reflect(fixture()[36:], *tools)
                    child.assert_called_once()

    @unittest.skipUnless(os.name == "nt", "Windows helper protocol")
    def test_missing_metadata_and_postflight_tool_changes_cannot_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            tools = self.tools(Path(folder))
            with patch.object(query.subprocess, "run") as child:
                with self.assertRaisesRegex(query.ReflectionError, "metadata_missing"):
                    query.reflect(fixture(False)[36:], *tools)
                child.assert_not_called()
            helper, helper_hash, library, library_hash = tools
            with patch.object(query.subprocess, "run", return_value=SimpleNamespace(returncode=0, stdout=json.dumps(valid()).encode())), \
                    patch.object(query, "digest", side_effect=[helper_hash, library_hash, "0" * 64]):
                with self.assertRaisesRegex(query.ReflectionError, "tools_changed"):
                    query.reflect(fixture()[36:], *tools)


if __name__ == "__main__":
    unittest.main()
