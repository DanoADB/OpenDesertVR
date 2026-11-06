# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / "experiments/renodx-owned/bridge_client.py"
spec = importlib.util.spec_from_file_location("owned_bridge", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DevkitBridgeTests(unittest.TestCase):
    def test_protocol_identity_duplicates_errors_and_oversized_lines_cannot_pass(self):
        self.assertEqual(module.decode_response(b'{"jsonrpc":"2.0","id":1,"result":{}}\n', 1), {})
        for value in (b'{"jsonrpc":"2.0","id":true,"result":{}}\n',
                      b'{"jsonrpc":"2.0","id":2,"result":{}}\n',
                      b'{"jsonrpc":"2.0","id":1,"result":{"x":1,"x":2}}\n',
                      b'{"jsonrpc":"2.0","id":1,"error":{},"result":{}}\n',
                      b'{"jsonrpc":"2.0","id":1,"result":NaN}\n', b'x' * (module.MAX_LINE + 1)):
            with self.assertRaises(ValueError):
                module.decode_response(value, 1)

    def test_observer_never_requests_mutation_dump_or_shader_text(self):
        for name, arguments in (("devkit_load_live_shaders", {}), ("devkit_dump_shader", {}),
                                ("devkit_set_resource_clone", {}),
                                ("devkit_get_shader", {"deviceIndex": 0, "shaderHash": "0x12345678", "view": "all"})):
            with self.assertRaises(ValueError):
                module.tool_arguments(name, arguments)
        self.assertEqual(module.tool_arguments("devkit_get_shader", {"deviceIndex": 0, "shaderHash": "0x12345678"})["view"], "summary")
        client = module.Bridge.__new__(module.Bridge)
        client.calls = 0
        with self.assertRaises(ValueError):
            client.request("tools/call", {"name": "devkit_dump_shader", "arguments": {}})

    def test_device_and_page_bounds_reject_implicit_or_boolean_indices(self):
        for name, arguments in (("devkit_get_draw", {"drawIndex": 0}),
                                ("devkit_get_draw", {"deviceIndex": True, "drawIndex": 0}),
                                ("devkit_list_draws", {"deviceIndex": 0, "limit": 33}),
                                ("devkit_status", {"other": 1})):
            with self.assertRaises(ValueError):
                module.tool_arguments(name, arguments)
        self.assertEqual(module.tool_arguments("devkit_status", {}), {})


if __name__ == "__main__":
    unittest.main()
