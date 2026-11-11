import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("extract", Path(__file__).resolve().parents[2] / "tools/extract_csv.py")
extract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extract)

class ExtractTests(unittest.TestCase):
    def test_sample_with_prompt_and_ansi(self):
        row = extract.parse_record("\x1b[0mfdc> sample,1,4,5000,15,0x0040,0x0000,0,0,2,100,0,200,0,300,0,400,0\r\n")
        self.assertEqual(len(row), 18)
        self.assertEqual(row[-2:], ["400", "0"])

    def test_partial_and_interleaved_rejected(self):
        self.assertIsNone(extract.parse_record("sample,1,2,3,4"))
        self.assertIsNone(extract.parse_record("sample,1,4,5000,15,0x0040,0x0000,0,0,2,100,0,200,0,300,0,400,0fdc>"))

    def test_no_range_is_blank_not_fake_zero(self):
        self.assertEqual(extract.parse_record("range,2,3,0,5,0,,,"), ["range", "2", "3", "0", "5", "0", "", "", ""])
        self.assertIsNone(extract.parse_record("range,2,3,0,5,0,0,0,0"))

    def test_range_and_bad_span(self):
        self.assertIsNotNone(extract.parse_record("range,3,2,15,4,1,100,300,200"))
        self.assertIsNone(extract.parse_record("range,3,2,15,4,1,100,300,300"))

if __name__ == "__main__":
    unittest.main()
