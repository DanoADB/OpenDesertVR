# SPDX-License-Identifier: MIT
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments/shader-consumer-audit"))
from technique_contract import bounded_file, inspect_xml


class TechniqueContractTests(unittest.TestCase):
    def test_exact_literal_paths_unique_basenames_and_ambiguity_are_separate(self):
        paths = {"a/owned.padxil", "b/shared.padxil", "c/shared.padxil"}
        xml = b'<Technique Category="owned"><PipelineBinding Shader="a/owned.padxil"/><PipelineBinding Shader="owned.padxil"/><PipelineBinding Shader="shared.padxil"/></Technique>'
        result = inspect_xml(xml, paths)
        self.assertEqual(result["literal_shader_path_matches"], 1)
        self.assertEqual(result["ambiguous_basename_values"], 1)
        self.assertEqual(result["known_tag_counts"]["pipelinebinding"], 3)
        self.assertEqual(result["known_attribute_counts"]["category"], 1)
        self.assertNotIn("owned.padxil", str(result))
        unrelated = inspect_xml(b'<Technique PipelineBinding="opaque-id"><Shader>not-a-cache-key</Shader></Technique>', paths)
        self.assertEqual(unrelated["literal_shader_path_matches"], 0)

    def test_opaque_ids_compound_text_entities_encoding_and_bounds_do_not_become_links(self):
        paths = {"a/owned.padxil"}
        result = inspect_xml(b'<Technique Value="prefix a/owned.padxil suffix"/>', paths)
        self.assertEqual(result["scalar_values_containing_padxil"], 1)
        self.assertEqual(result["literal_shader_path_matches"], 0)
        for data in (b'<!DOCTYPE x [<!ENTITY y "z">]><x>&y;</x>', b'<!ENTITY x "y"><x/>',
                     b'<x>\xff</x>', b'<x>', b'x' * 65537, b'<x>' + b'<y/>' * 4096 + b'</x>'):
            with self.assertRaises((ValueError, ET.ParseError)):
                inspect_xml(data, paths)

    def test_file_bounds_apply_before_loading_unreviewed_references_or_plans(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "owned"
            path.write_bytes(b"data")
            self.assertEqual(bounded_file(path, 4), b"data")
            with self.assertRaises(ValueError):
                bounded_file(path, 3)


if __name__ == "__main__":
    unittest.main()
