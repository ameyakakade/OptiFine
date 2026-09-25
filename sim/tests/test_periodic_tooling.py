"""Unit tests for the periodic-scheduling and capture tooling.

None of these needs retained evidence or starts a tool: each test builds its
own inputs, and tool processes are patched out where a code path could
reach one.
"""

import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch


SIM_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM_DIR))

import run_periodic_ml as runner  # noqa: E402
import run_active_ml  # noqa: E402


class PeriodicSweepTooling(unittest.TestCase):
    def test_compiler_discovery_includes_visual_studio_debug_output(self):
        expected = runner.ROOT / "build" / "Debug" / "optifine.exe"

        self.assertIn(expected, runner.compiler_candidates())
    def test_dry_run_prints_each_matrix_command_once_without_subprocesses(self):
        output = io.StringIO()

        with patch.object(runner.subprocess, "run") as run_mock:
            with redirect_stdout(output):
                exit_code = runner.main(["--dry-run"])

        commands = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(0, exit_code)
        self.assertEqual(16, len(commands))
        self.assertEqual(16, len({tuple(command) for command in commands}))
        self.assertFalse(run_mock.called)
    def test_supplemental_dry_run_prints_16_count_5_commands_without_subprocesses(self):
        output = io.StringIO()

        with patch.object(runner.subprocess, "run") as run_mock:
            with redirect_stdout(output):
                exit_code = runner.main(["--dry-run-supplemental"])

        commands = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(0, exit_code)
        self.assertEqual(16, len(commands))
        self.assertEqual(16, len({tuple(command) for command in commands}))
        self.assertTrue(all(command[command.index("--periodic-count") + 1] == "5" for command in commands))
        self.assertTrue(all(command[command.index("--out") + 1].endswith("_n5.S") for command in commands))
        self.assertFalse(run_mock.called)
    def test_mismatched_classifier_bodies_are_rejected_before_simulation(self):
        active = "; BODY BEGIN\n    nop\n; BODY END\n"
        powersave = "; BODY BEGIN\n    break\n; BODY END\n"

        with self.assertRaisesRegex(runner.BodyMismatchError, "body hashes differ"):
            runner.require_matched_body_hashes(active, powersave)
    def test_manifest_serialization_omits_internal_energy_report_objects(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest_path = Path(directory) / "manifest.json"
            runner.write_json(
                manifest_path,
                {"runs": [{"status": "reported", "_report": runner.EnergyReport(total_cycles=9)}]},
            )

            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

        self.assertEqual("reported", manifest["runs"][0]["status"])
        self.assertNotIn("_report", manifest["runs"][0])
    def test_compiler_metadata_keeps_divisor_8_terminal_declaration_separate_from_static_deadline(self):
        assembly = """\
; BODY BEGIN
    nop
; BODY END
    cpi r16, 4
; META completed_count_addr=0x0100
; META overrun_addr=0x0101
; META output_addr=0x0102 output_len=4
"""
        compiler_stderr = "periodic per-inference predicted compute cost: 5344 cycles, 15163.600 nJ\n"

        metadata, evidence = runner.compiler_metadata(
            runner.RunSpec(8, "optimized", "active"),
            assembly,
            runner.EXPECTED_OUTPUT,
            compiler_stderr,
        )

        self.assertFalse(metadata["overrun"])
        self.assertEqual(4, metadata["completed_count"])
        self.assertEqual(2048, evidence["timer_period_cycles"])
    def test_extracts_ansi_colored_avrora_banner_for_manifest_provenance(self):
        banner = "\x1b[1;34mAvrora \x1b[1;00m[\x1b[1;34mBeta 1.7.115\x1b[1;00m] - test\n"

        self.assertEqual("Avrora [Beta 1.7.115] - test", runner.avrora_version(banner))
    def test_rejects_full_assembly_drift_outside_body_after_count_normalization(self):
        count_four = """\
startup:
    ldi r18, 1
; BODY BEGIN
    nop
; BODY END
    cpi r16, 4
"""
        count_five_with_startup_drift = count_four.replace("ldi r18, 1", "ldi r18, 2").replace(
            "cpi r16, 4", "cpi r16, 5"
        )

        with self.assertRaisesRegex(runner.ProvenanceError, "normalized full assembly"):
            runner.require_count_normalized_assembly_identity(
                count_four, 4, count_five_with_startup_drift, 5
            )
    def test_static_deadline_classification_does_not_replace_terminal_metadata(self):
        assembly = """\
; BODY BEGIN
    nop
; BODY END
    cpi r16, 4
; META completed_count_addr=0x0100
; META overrun_addr=0x0101
; META output_addr=0x0102 output_len=4
"""
        stderr = "periodic per-inference predicted compute cost: 5344 cycles, 15163.600 nJ\n"

        metadata, _ = runner.compiler_metadata(
            runner.RunSpec(8, "optimized", "active"),
            assembly,
            runner.EXPECTED_OUTPUT,
            stderr,
        )

        self.assertEqual(4, metadata["completed_count"])
        self.assertFalse(metadata["overrun"])
        self.assertTrue(runner.static_deadline_evidence(
            runner.RunSpec(8, "optimized", "active"), stderr
        )["rejected"])
    def test_command_identity_survives_the_recording_machines_path_style(self):
        """The same command recorded on Windows and rebuilt on Linux must agree.

        Note the tool argument itself is NOT normalised away: `optifine.exe`
        and `optifine` are genuinely different names, and the caller always
        rebuilds the expected command using the *recorded* tool path, so
        element 0 matches by construction rather than by leniency.
        """
        tool = r"build\optifine.exe"
        windows = [
            tool, r"C:\src\OptiFine\models\m.onnx",
            "--cost-table", r"C:\src\OptiFine\cost_table.toml",
            "--out", r"C:\src\OptiFine\sim\fixtures\periodic_ml\p32_naive_active.S",
            "--timer-prescaler", "32",
        ]
        posix = [
            tool, "models/m.onnx",
            "--cost-table", "cost_table.toml",
            "--out", "sim/fixtures/periodic_ml/p32_naive_active.S",
            "--timer-prescaler", "32",
        ]
        self.assertEqual(runner.command_identity(windows),
                         runner.command_identity(posix))
    def test_command_identity_still_rejects_a_changed_flag_value(self):
        base = ["/o/optifine", "/o/m.onnx", "--timer-prescaler", "32"]
        other = ["/o/optifine", "/o/m.onnx", "--timer-prescaler", "128"]
        self.assertNotEqual(runner.command_identity(base),
                            runner.command_identity(other))
    def test_command_identity_still_rejects_a_different_input_file(self):
        base = ["/o/optifine", "/o/models/m.onnx", "--timer-prescaler", "32"]
        other = ["/o/optifine", "/o/models/other.onnx", "--timer-prescaler", "32"]
        self.assertNotEqual(runner.command_identity(base),
                            runner.command_identity(other))
    def test_reproduce_retained_on_a_fresh_directory_points_at_run_new(self):
        with tempfile.TemporaryDirectory() as directory:
            errors = io.StringIO()
            with redirect_stderr(errors):
                exit_code = runner.main(["reproduce-retained", "--output-dir", directory])

            self.assertEqual(2, exit_code)
            self.assertIn("run-new", errors.getvalue())

    def test_run_new_refuses_a_directory_that_holds_a_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "manifest.json"
            manifest.write_text("{}\n", encoding="utf-8")
            errors = io.StringIO()
            with patch.object(runner, "execute", side_effect=AssertionError("tool process started")), \
                    redirect_stderr(errors):
                exit_code = runner.main(["run-new", "--output-dir", directory])

            self.assertEqual(2, exit_code)
            self.assertIn("refuses to overwrite", errors.getvalue())
            self.assertEqual("{}\n", manifest.read_text(encoding="utf-8"))


class CaptureTooling(unittest.TestCase):
    def test_active_capture_refuses_a_directory_that_holds_a_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "manifest.json").write_text("{}\n", encoding="utf-8")
            with redirect_stderr(io.StringIO()):
                self.assertEqual(run_active_ml.main(["capture", "--output-dir", directory]), 2)


if __name__ == "__main__":
    unittest.main()
