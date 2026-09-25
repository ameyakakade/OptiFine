"""Contract tests for the retained active-mode ML evidence and its derivation."""

import shutil
import sys
import tempfile
import unittest
from pathlib import Path


SIM_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM_DIR))

import report_results  # noqa: E402
import run_active_ml  # noqa: E402


class RetainedCapture(unittest.TestCase):
    def test_retained_capture_verifies(self):
        # Hashes, recorded inputs, and the recorded cycle counts. Runs no tool.
        self.assertEqual(run_active_ml.main(["check"]), 0)

    def test_frozen_figures(self):
        a = report_results.active_ml()
        self.assertEqual(a["naive"]["cycles"], 6_130)
        self.assertEqual(a["optimized"]["cycles"], 6_018)
        self.assertEqual(a["delta_cycles"], -112)
        self.assertAlmostEqual(a["naive"]["cpu_nj"], 17_393.951625, places=6)
        self.assertAlmostEqual(a["optimized"]["cpu_nj"], 17_076.150225, places=6)
        self.assertAlmostEqual(a["per_cycle_nj"], 2.8375125, places=9)

    def test_smoke_calibration(self):
        # 7 one-cycle instructions report 8 cycles: the terminating break.
        smoke = report_results.parse_energy(run_active_ml.CANONICAL / "smoke.avrora.txt")
        self.assertEqual(smoke["cycles"], 8)


class TamperedCapture(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.cap = self.tmp / "active_ml"
        shutil.copytree(run_active_ml.CANONICAL, self.cap, ignore=shutil.ignore_patterns("*.elf"))

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_edited_report_is_refused(self):
        path = self.cap / "optimized.avrora.txt"
        path.write_text(path.read_text(encoding="utf-8").replace("6018", "6017"), encoding="utf-8")
        self.assertEqual(run_active_ml.main(["check", "--output-dir", str(self.cap)]), 1)

    def test_capture_refuses_an_existing_manifest(self):
        self.assertEqual(run_active_ml.main(["capture", "--output-dir", str(self.cap)]), 2)


if __name__ == "__main__":
    unittest.main()
