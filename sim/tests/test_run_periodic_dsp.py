"""Contract tests for the retained periodic DSP sweep and its derivation."""

import csv
import shutil
import sys
import tempfile
import unittest
from io import StringIO
from pathlib import Path


SIM_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM_DIR))

import report_results  # noqa: E402
import run_periodic_dsp  # noqa: E402


class RetainedSweep(unittest.TestCase):
    def test_reproduces_without_tools(self):
        self.assertEqual(run_periodic_dsp.main(["reproduce-retained"]), 0)

    def test_all_sixteen_runs_retained(self):
        d = run_periodic_dsp.CANONICAL
        for p in (8, 32, 128, 1024):
            for policy in ("active", "powersave"):
                for suffix in ("", "_n5"):
                    for ext in (".S", ".compile.txt", ".avrora.txt", ".size.txt"):
                        self.assertTrue((d / f"p{p}_dsp_{policy}{suffix}{ext}").is_file())

    def test_derived_figures(self):
        rows = {r["prescaler"]: r for r in report_results.periodic_dsp()["rows"]}
        for p in (8, 32, 128):
            self.assertEqual(rows[p]["status"], "compute-bound (no idle window)")
            self.assertEqual(rows[p]["compute_cycles"], 147_565)
        r = rows[1024]
        self.assertEqual(r["status"], "accepted")
        self.assertEqual((r["period_cycles"], r["idle_cycles"]), (262_144, 114_579))
        self.assertEqual(r["busy_wait_increment_cycles"], 262_141)   # the poll-loop jitter, recorded
        self.assertAlmostEqual(r["powersave_nj"], 424_239.108675, places=4)
        self.assertAlmostEqual(r["saving_pct"], 42.9661, places=3)
        self.assertEqual(r["text"], 13_172)
        self.assertEqual(r["static_sram"], 2_521)

    def test_scaled_busy_wait_matches_the_ml_measurement(self):
        # Busy-waiting burns a whole period of Active cycles whatever the
        # workload, so the DSP figure scaled to the period must equal the ML
        # sweep's directly simulated busy-wait energy at the same prescaler.
        dsp = {r["prescaler"]: r for r in report_results.periodic_dsp()["rows"]}[1024]
        ml = {r["prescaler"]: r for r in report_results.periodic_ml()}[1024]
        self.assertAlmostEqual(dsp["active_nj"], ml["active_nj"], places=6)

    def test_committed_table_agrees_with_derivation(self):
        rows = list(csv.DictReader(StringIO((run_periodic_dsp.CANONICAL / "primary.csv").read_text(encoding="utf-8"))))
        by_p = {int(r["prescaler"]): r for r in rows}
        derived = {r["prescaler"]: r for r in report_results.periodic_dsp()["rows"]}
        self.assertEqual(by_p[1024]["status"], "accepted")
        self.assertAlmostEqual(float(by_p[1024]["saved_nj"]), derived[1024]["saved_nj"], places=6)
        for p in (8, 32, 128):
            self.assertEqual(by_p[p]["status"], "rejected")


class TamperedSweep(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.cap = self.tmp / "periodic_dsp"
        shutil.copytree(run_periodic_dsp.CANONICAL, self.cap, ignore=shutil.ignore_patterns("*.elf"))

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_edited_raw_report_is_refused(self):
        path = self.cap / "p1024_dsp_powersave_n5.avrora.txt"
        path.write_text(path.read_text(encoding="utf-8").replace("1460211", "1460212"), encoding="utf-8")
        self.assertEqual(run_periodic_dsp.main(["reproduce-retained", "--output-dir", str(self.cap)]), 1)

    def test_edited_table_is_refused(self):
        path = self.cap / "primary.md"
        path.write_text(path.read_text(encoding="utf-8").replace("accepted", "rejected", 1), encoding="utf-8")
        self.assertEqual(run_periodic_dsp.main(["reproduce-retained", "--output-dir", str(self.cap)]), 1)


if __name__ == "__main__":
    unittest.main()
