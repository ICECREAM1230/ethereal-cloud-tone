#!/usr/bin/env python3
"""Behavioral checks for the portable, read-only Gowin project scanner."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCANNER = Path(__file__).with_name("find_gowin_project.py")


class ProjectScannerTests(unittest.TestCase):
    def test_inventory_and_secret_redaction(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            project_root = root / "中文 project"
            for folder in ("src", "impl/gwsynthesis", "impl/pnr", "sim"):
                (project_root / folder).mkdir(parents=True)
            (project_root / "src/a&b.v").write_text("module x; endmodule")
            (project_root / "impl/gwsynthesis/generated.v").write_text("module g; endmodule")
            (project_root / "impl/pnr/project.fs").write_bytes(b"sample")
            (project_root / "sim/tb.sv").write_text("module tb; endmodule")
            project = project_root / "fixture.gprj"
            project.write_text('''<?xml version="1"?>
<!DOCTYPE gowin-fpga-project>
<Project><Device pn="GW5AST-LV138PG484AC1/I0"/><FileList>
<!-- <File path="ghost.v" enable="1"/> -->
<File path="src/a&amp;b.v" type="file.verilog" enable="1"/>
<File path="C:\\old\\ghost.v" type="file.verilog" enable="0"/>
</FileList></Project>''')
            (project_root / "fixture.gprj.user").write_text('''<!DOCTYPE ProjectUserData>\n<Project>
<ResultFile ResultFilePath="impl/pnr/project.fs" ResultFileType="fs"/>
<Process ID="pnr" State="done"/></Project>''')
            (project_root / "impl/process_config.json").write_text('''{
"IncludePath": ["src", "路径 with space"],
"TopModule": "x", "topmodule": "x",
"SecretKey": "must-not-print", "Other": {"token": "nested-must-not-print"}
}''')

            args = [sys.executable, str(SCANNER), "--root", str(root),
                    "--project", str(project)]
            result = subprocess.run(args + ["--json"], capture_output=True, text=True, check=True)
            self.assertNotIn("must-not-print", result.stdout)
            self.assertNotIn("nested-must-not-print", result.stdout)
            record = json.loads(result.stdout)
            self.assertEqual(record["Device"], "GW5AST-LV138PG484AC1/I0")
            self.assertEqual(len(record["ProjectFiles"]), 2)
            self.assertTrue(record["ProjectFiles"][0]["Exists"])
            self.assertFalse(record["ProjectFiles"][1]["Exists"])
            self.assertGreater(record["FileCounts"]["synthesis"], 0)
            self.assertEqual(record["RegisteredResults"][0]["Exists"], True)
            self.assertEqual(record["ProcessConfigDetails"][0]["CaseVariantKeyGroups"], ["TopModule"])
            fields = record["ProcessConfigDetails"][0]["Fields"]
            self.assertEqual(next(f["Value"] for f in fields if f["Key"] == "SecretKey"), "<redacted>")
            self.assertEqual(next(f["Value"] for f in fields if f["Key"] == "Other"), "<redacted>")
            self.assertIn("路径 with space", next(f["Value"] for f in fields if f["Key"] == "IncludePath"))
            text_result = subprocess.run(args, capture_output=True, text=True, check=True)
            self.assertNotIn("must-not-print", text_result.stdout)

    def test_invalid_xml_and_entities_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp) / "bad.gprj"
            for contents in ("<Project><FileList>",
                             '<!DOCTYPE Project [<!ENTITY x SYSTEM "file:///etc/passwd">]><Project>&x;</Project>'):
                project.write_text(contents)
                result = subprocess.run([sys.executable, str(SCANNER), "--root", str(project), "--json"],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")

    def test_nested_projects_are_separate_but_manifest_references_remain(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            nested = root / "nested"
            (root / "impl").mkdir()
            (nested / "impl/pnr").mkdir(parents=True)
            (nested / "shared.v").write_text("module shared; endmodule")
            project = root / "parent.gprj"
            project.write_text('<Project><Device pn="PARENT"/><FileList>'
                               '<File path="nested/shared.v" enable="0"/></FileList></Project>')
            (nested / "child.gprj").write_text('<Project><Device pn="CHILD"/></Project>')
            (root / "impl/process_config.json").write_text('{"TopModule":"parent_top"}')
            (nested / "impl/process_config.json").write_text('{"TopModule":"child_top"}')
            (nested / "impl/pnr/child.fs").write_text("child image")
            result = subprocess.run([sys.executable, str(SCANNER), "--root", str(project), "--json"],
                                    capture_output=True, text=True, check=True)
            record = json.loads(result.stdout)
            self.assertEqual(record["ProcessConfigFiles"], ["impl/process_config.json"])
            self.assertEqual(record["NestedProjects"], ["nested/child.gprj"])
            self.assertEqual(record["FileCounts"]["project"], 1)
            self.assertEqual(record["RepresentativeOutputs"], [])
            self.assertTrue(record["ProjectFiles"][0]["Exists"])
            self.assertEqual(record["ProjectFiles"][0]["Enabled"], "0")

    def test_device_text_is_retained_without_inventing_a_part_number(self):
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp) / "device.gprj"
            for attributes, expected in (('name="" pn=""', 'gw5ast138c-007'),
                                         ('name="GW5AST-138C" pn="FULL-PART"', 'FULL-PART')):
                project.write_text(f'<Project><Device {attributes}> gw5ast138c-007 </Device></Project>')
                result = subprocess.run([sys.executable, str(SCANNER), "--root", str(project), "--json"],
                                        capture_output=True, text=True, check=True)
                record = json.loads(result.stdout)
                self.assertEqual(record["Device"], expected)
                self.assertEqual(record["DeviceIdentifier"], "gw5ast138c-007")
                self.assertEqual(record["DevicePartNumber"], "FULL-PART" if expected == "FULL-PART" else None)

    def test_outside_project_and_prefix_sibling_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "scope"
            sibling = Path(temp) / "scope-extra"
            root.mkdir()
            sibling.mkdir()
            outside = sibling / "outside.gprj"
            outside.write_text('<Project/>')
            result = subprocess.run([sys.executable, str(SCANNER), "--root", str(root),
                                     "--project", str(outside), "--json"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")

    def test_escaped_and_duplicate_nested_sensitive_keys_are_redacted(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "fixture.gprj").write_text('<Project/>')
            (root / "impl").mkdir()
            (root / "impl/process_config.json").write_text(r'''{
"to\u006ben":"escaped-marker",
"Other":{"same":{"password":"nested-marker"},"same":0},
"TopModule":[{"cre\u0064ential":"text-marker"}]
}''')
            args = [sys.executable, str(SCANNER), "--root", str(root)]
            for output in (subprocess.run(args + ["--json"], capture_output=True, text=True, check=True),
                           subprocess.run(args, capture_output=True, text=True, check=True)):
                for marker in ("escaped-marker", "nested-marker", "text-marker"):
                    self.assertNotIn(marker, output.stdout)
            fields = json.loads(subprocess.run(args + ["--json"], capture_output=True, text=True,
                                               check=True).stdout)["ProcessConfigDetails"][0]["Fields"]
            self.assertTrue(all(field["Value"] == "<redacted>" for field in fields))


if __name__ == "__main__":
    unittest.main()
