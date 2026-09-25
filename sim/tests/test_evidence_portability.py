"""Retained evidence names no machine-specific directory.

Every raw file, table and manifest under sim/fixtures/ is published as-is, so
none of them may record an absolute path from the machine that captured it.
Tools installed system-wide (``/usr/...``) are the only absolute paths allowed.
"""

import re
import unittest
from pathlib import Path


FIXTURES = Path(__file__).resolve().parents[1] / "fixtures"
MACHINE_PATH = re.compile(r"(?i)\b[a-z]:[\\/]|/home/|/Users/|/tmp/")


class EvidencePortability(unittest.TestCase):
    def test_fixtures_exist(self):
        for name in ("active_ml", "dsp", "periodic_ml", "periodic_dsp"):
            self.assertTrue((FIXTURES / name / "manifest.json").is_file(), name)

    def test_no_machine_specific_paths(self):
        offenders = []
        for path in sorted(FIXTURES.rglob("*")):
            if not path.is_file() or path.suffix == ".elf":
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            if MACHINE_PATH.search(text):
                offenders.append(path.relative_to(FIXTURES).as_posix())
        self.assertEqual(offenders, [])


if __name__ == "__main__":
    unittest.main()
