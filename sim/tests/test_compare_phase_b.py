"""Contract tests for strict Phase B matched-report comparison."""

import sys
import unittest
from pathlib import Path


SIM_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM_DIR))

from compare_phase_b import (  # noqa: E402
    CSV_COLUMNS,
    ComparisonError,
    compare_reports,
    render_csv,
    render_markdown,
)
import compare_phase_b  # noqa: E402
from parse_report import parse_avrora_energy_output  # noqa: E402


ACTIVE_REPORT = """\
Simulated time: 800 cycles
CPU: 2.0E-7 Joule
   Active: 2.0E-7 Joule, 800 cycles
   Power Save: 0.0 Joule, 0 cycles
"""

POWERSAVE_REPORT = """\
Simulated time: 800 cycles
CPU: 5.0E-8 Joule
   Active: 2.0E-8 Joule, 80 cycles
   Power Save: 3.0E-8 Joule, 720 cycles
"""


def metadata(**changes):
    value = {
        "policy": "active",
        "prescaler": 32,
        "inference_count": 4,
        "body_sha256": "a" * 64,
        "completed_count": 4,
        "overrun": False,
        "output": [0, -3, 18, 27],
    }
    value.update(changes)
    return value


def steady_metadata(policy, count, **changes):
    value = metadata(
        policy=policy,
        inference_count=count,
        completed_count=count,
        compute_path="optimized",
        terminal_evidence="compiler/run metadata; no Avrora SRAM readback is used or claimed",
    )
    value.update(changes)
    return value


STEADY_ACTIVE_N4 = """\
Simulated time: 1000 cycles
CPU: 1.0E-7 Joule
   Active: 1.0E-7 Joule, 1000 cycles
   Power Save: 0.0 Joule, 0 cycles
"""

STEADY_ACTIVE_N5 = """\
Simulated time: 9192 cycles
CPU: 2.0E-7 Joule
   Active: 2.0E-7 Joule, 9192 cycles
   Power Save: 0.0 Joule, 0 cycles
"""

STEADY_POWERSAVE_N4 = """\
Simulated time: 1995 cycles
CPU: 7.5E-8 Joule
   Active: 5.0E-8 Joule, 500 cycles
   Power Save: 2.5E-8 Joule, 1495 cycles
"""

STEADY_POWERSAVE_N5 = """\
Simulated time: 10187 cycles
CPU: 1.25E-7 Joule
   Active: 7.5E-8 Joule, 1000 cycles
   Power Save: 5.0E-8 Joule, 9187 cycles
"""


class ParseAvroraEnergyOutputTests(unittest.TestCase):
    def test_parses_simulated_time_without_node_lifetime(self):
        report = parse_avrora_energy_output(
            "Simulated time: 1234 cycles\nCPU: 1.0E-9 Joule\n"
        )

        self.assertEqual(1234, report.total_cycles)
        self.assertEqual({"CPU": 1.0}, report.component_nj)


class ComparePhaseBTests(unittest.TestCase):
    def compare(self, active_metadata=None, powersave_metadata=None,
                active_report=ACTIVE_REPORT, powersave_report=POWERSAVE_REPORT):
        return compare_reports(
            parse_avrora_energy_output(active_report),
            metadata(**(active_metadata or {})),
            parse_avrora_energy_output(powersave_report),
            metadata(policy="powersave", **(powersave_metadata or {})),
        )

    def test_compares_matched_active_and_powersave_reports(self):
        row = self.compare()

        self.assertEqual(32, row.prescaler)
        self.assertEqual(200, row.period_cycles)
        self.assertEqual(4, row.inferences)
        self.assertAlmostEqual(200.0, row.active_nj)
        self.assertAlmostEqual(50.0, row.powersave_nj)
        self.assertAlmostEqual(150.0, row.saved_nj)
        self.assertAlmostEqual(75.0, row.saving_pct)
        self.assertAlmostEqual(12.5, row.nj_per_inference)
        self.assertEqual(800, row.active_cycles)
        self.assertEqual(800, row.powersave_cycles)
        self.assertEqual("met", row.deadline_status)
        self.assertEqual("matched", row.output_status)

    def test_renders_the_required_csv_and_markdown_columns(self):
        row = self.compare()

        self.assertEqual(
            "prescaler,period_cycles,inferences,active_nj,powersave_nj,saved_nj,"
            "saving_pct,nj_per_inference,active_cycles,powersave_cycles,"
            "deadline_status,output_status\n"
            "32,200,4,200,50,150,75,12.5,800,800,met,matched\n",
            render_csv([row]),
        )
        self.assertEqual(
            "| " + " | ".join(CSV_COLUMNS) + " |\n"
            "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n"
            "| 32 | 200 | 4 | 200 | 50 | 150 | 75 | 12.5 | 800 | 800 | met | matched |\n",
            render_markdown([row]),
        )

    def test_rejects_unequal_prescaler_before_energy_calculation(self):
        with self.assertRaisesRegex(ComparisonError, "prescaler"):
            self.compare(powersave_metadata={"prescaler": 128}, active_report="")

    def test_rejects_unequal_inference_count(self):
        with self.assertRaisesRegex(ComparisonError, "inference_count"):
            self.compare(powersave_metadata={"inference_count": 3, "completed_count": 3})

    def test_rejects_unequal_body_hash(self):
        with self.assertRaisesRegex(ComparisonError, "body_sha256"):
            self.compare(powersave_metadata={"body_sha256": "b" * 64})

    def test_rejects_unequal_output(self):
        with self.assertRaisesRegex(ComparisonError, "output"):
            self.compare(powersave_metadata={"output": [1, 2, 3, 4]})

    def test_rejects_unequal_json_output_value_types(self):
        with self.assertRaisesRegex(ComparisonError, "output"):
            self.compare(
                active_metadata={"output": [False]},
                powersave_metadata={"output": [0]},
            )

    def test_rejects_any_overrun(self):
        with self.assertRaisesRegex(ComparisonError, "overrun"):
            self.compare(powersave_metadata={"overrun": True})

    def test_rejects_missing_powersave_cycles(self):
        report_without_sleep = POWERSAVE_REPORT.replace(
            "Power Save: 3.0E-8 Joule, 720 cycles",
            "Power Save: 0.0 Joule, 0 cycles",
        )

        with self.assertRaisesRegex(ComparisonError, "Power Save"):
            self.compare(powersave_report=report_without_sleep)

    def test_rejects_nonpositive_active_energy(self):
        zero_energy_report = ACTIVE_REPORT.replace("CPU: 2.0E-7 Joule", "CPU: 0.0 Joule")

        with self.assertRaisesRegex(ComparisonError, "active energy"):
            self.compare(active_report=zero_energy_report)

    def test_rejects_incomplete_metadata(self):
        with self.assertRaisesRegex(ComparisonError, "completed_count"):
            self.compare(active_metadata={"completed_count": None})

    def test_rejects_completed_count_that_does_not_match_requested_count(self):
        with self.assertRaisesRegex(ComparisonError, "completed_count"):
            self.compare(active_metadata={"completed_count": 3})


class SteadyStateComparisonTests(unittest.TestCase):
    def compare_steady_state(self, active_n4_metadata=None, active_n5_metadata=None,
                             powersave_n4_metadata=None, powersave_n5_metadata=None,
                             active_n4=STEADY_ACTIVE_N4, active_n5=STEADY_ACTIVE_N5,
                             powersave_n4=STEADY_POWERSAVE_N4,
                             powersave_n5=STEADY_POWERSAVE_N5):
        return compare_phase_b.compare_steady_state_reports(
            parse_avrora_energy_output(active_n4),
            steady_metadata("active", 4, **(active_n4_metadata or {})),
            parse_avrora_energy_output(active_n5),
            steady_metadata("active", 5, **(active_n5_metadata or {})),
            parse_avrora_energy_output(powersave_n4),
            steady_metadata("powersave", 4, **(powersave_n4_metadata or {})),
            parse_avrora_energy_output(powersave_n5),
            steady_metadata("powersave", 5, **(powersave_n5_metadata or {})),
        )

    def test_uses_equal_steady_state_increments_despite_995_cycle_offset(self):
        row = self.compare_steady_state()

        self.assertEqual(8192, row.period_cycles)
        self.assertEqual(1, row.inferences)
        self.assertAlmostEqual(100.0, row.active_nj)
        self.assertAlmostEqual(50.0, row.powersave_nj)
        self.assertAlmostEqual(50.0, row.saved_nj)
        self.assertAlmostEqual(50.0, row.saving_pct)
        self.assertAlmostEqual(50.0, row.nj_per_inference)
        self.assertEqual(8192, row.active_cycles)
        self.assertEqual(8192, row.powersave_cycles)

    def test_rejects_unequal_steady_state_increments(self):
        unequal_powersave_n5 = STEADY_POWERSAVE_N5.replace("10187 cycles", "10188 cycles")

        with self.assertRaisesRegex(ComparisonError, "steady-state period_cycles"):
            self.compare_steady_state(powersave_n5=unequal_powersave_n5)

    def test_rejects_completed_count_that_does_not_match_each_observation_count(self):
        with self.assertRaisesRegex(ComparisonError, "completed_count"):
            self.compare_steady_state(active_n5_metadata={"completed_count": 4})

    def test_rejects_four_observation_identity_mismatches(self):
        for field, change in (
            ("body_sha256", {"body_sha256": "b" * 64}),
            ("output", {"output": [0, -3, 18, 28]}),
            ("prescaler", {"prescaler": 128}),
            ("compute_path", {"compute_path": "naive"}),
        ):
            with self.subTest(field=field):
                with self.assertRaisesRegex(ComparisonError, field):
                    self.compare_steady_state(active_n5_metadata=change)


if __name__ == "__main__":
    unittest.main()
