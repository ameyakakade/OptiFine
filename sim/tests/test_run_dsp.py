"""Contract tests for the canonical DSP baseline and its derivation."""

import shutil
import sys
import tempfile
import unittest
from pathlib import Path


SIM_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM_DIR))

import report_results  # noqa: E402
import run_dsp  # noqa: E402


class CanonicalBaseline(unittest.TestCase):
    def test_retained_capture_verifies(self):
        # Hashes, recorded inputs, and byte-for-byte re-derivation of the
        # committed tables. Runs no tool.
        self.assertEqual(run_dsp.main(["check"]), 0)

    def test_frozen_figures(self):
        d = report_results.dsp()
        self.assertEqual(d["cycles"], 148_335)
        self.assertEqual(d["predicted_cycles"], 148_335)
        self.assertEqual(d["error_cycles"], 0)
        self.assertEqual((d["text"], d["data"], d["bss"]), (12_800, 0, 0))
        self.assertEqual(d["sram_bytes"], 2_517)
        self.assertEqual(d["table_bytes"], 128)
        self.assertAlmostEqual(d["cpu_nj"], 420_902.4166875, places=6)
        self.assertAlmostEqual(d["time_ms"], 18.541875, places=9)
        self.assertAlmostEqual(d["per_cycle_nj"], 2.8375125, places=9)
        parts = {name: cyc for name, cyc, _ in d["breakdown"]}
        self.assertEqual(parts["FFT (x6 stages)"], 73_512)
        self.assertEqual(parts["Magnitude"], 51_590)
        self.assertEqual(parts["PeakExtract"], 18_943)
        self.assertEqual(sum(parts.values()), 148_335)


class TamperedCapture(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.cap = self.tmp / "dsp"
        shutil.copytree(run_dsp.CANONICAL, self.cap)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def _edit(self, name, old, new, count=1):
        path = self.cap / name
        text = path.read_text(encoding="utf-8")
        self.assertIn(old, text)
        path.write_text(text.replace(old, new, count), encoding="utf-8")

    def test_prediction_mismatch_is_refused(self):
        # Avrora disagreeing with the compiler is an inconsistent capture,
        # never a silently reported number. (The raw report wraps numbers in
        # ANSI colour codes, so the bare number is edited, everywhere.)
        self._edit("dsp.avrora.txt", "148335", "148336", count=-1)
        with self.assertRaises(report_results.MissingArtifact):
            report_results.dsp(self.cap)

    def test_size_mismatch_is_refused(self):
        self._edit("dsp.size.txt", "12800", "12802")
        with self.assertRaises(report_results.MissingArtifact):
            report_results.dsp(self.cap)

    def test_edited_table_fails_check(self):
        self._edit("results.md", "148,335", "148,000")
        self.assertEqual(run_dsp.main(["check", "--output-dir", str(self.cap)]), 1)

    def test_compare_identical_captures(self):
        self.assertEqual(run_dsp.main(["compare", str(run_dsp.CANONICAL), str(self.cap)]), 0)
        self._edit("dsp.s", "break", "break ")
        self.assertEqual(run_dsp.main(["compare", str(run_dsp.CANONICAL), str(self.cap)]), 1)


if __name__ == "__main__":
    unittest.main()
