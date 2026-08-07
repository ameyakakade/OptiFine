"""Contract tests for the Phase B sweep orchestrator."""

import io
import json
import copy
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch


SIM_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIM_DIR))

import run_phase_b  # noqa: E402


class PhaseBSweepDryRunTests(unittest.TestCase):
    def test_compiler_discovery_includes_visual_studio_debug_output(self):
        expected = run_phase_b.ROOT / "build" / "Debug" / "optifine.exe"

        self.assertIn(expected, run_phase_b.compiler_candidates())

    def test_dry_run_prints_each_matrix_command_once_without_subprocesses(self):
        output = io.StringIO()

        with patch.object(run_phase_b.subprocess, "run") as run_mock:
            with redirect_stdout(output):
                exit_code = run_phase_b.main(["--dry-run"])

        commands = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(0, exit_code)
        self.assertEqual(16, len(commands))
        self.assertEqual(16, len({tuple(command) for command in commands}))
        self.assertFalse(run_mock.called)

    def test_supplemental_dry_run_prints_16_count_5_commands_without_subprocesses(self):
        output = io.StringIO()

        with patch.object(run_phase_b.subprocess, "run") as run_mock:
            with redirect_stdout(output):
                exit_code = run_phase_b.main(["--dry-run-supplemental"])

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

        with self.assertRaisesRegex(run_phase_b.BodyMismatchError, "body hashes differ"):
            run_phase_b.require_matched_body_hashes(active, powersave)

    def test_manifest_serialization_omits_internal_energy_report_objects(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest_path = Path(directory) / "manifest.json"
            run_phase_b.write_json(
                manifest_path,
                {"runs": [{"status": "reported", "_report": run_phase_b.EnergyReport(total_cycles=9)}]},
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

        metadata, evidence = run_phase_b.compiler_metadata(
            run_phase_b.RunSpec(8, "optimized", "active"),
            assembly,
            run_phase_b.EXPECTED_OUTPUT,
            compiler_stderr,
        )

        self.assertFalse(metadata["overrun"])
        self.assertEqual(4, metadata["completed_count"])
        self.assertEqual(2048, evidence["timer_period_cycles"])

    def test_extracts_ansi_colored_avrora_banner_for_manifest_provenance(self):
        banner = "\x1b[1;34mAvrora \x1b[1;00m[\x1b[1;34mBeta 1.7.115\x1b[1;00m] - test\n"

        self.assertEqual("Avrora [Beta 1.7.115] - test", run_phase_b.avrora_version(banner))


class PhaseBProvenanceTests(unittest.TestCase):
    def canonical_manifest(self):
        path = run_phase_b.ROOT / "sim" / "fixtures" / "phase_b" / "manifest.json"
        return json.loads(path.read_text(encoding="utf-8"))

    def test_rejects_retained_model_hash_mismatch_before_calculation(self):
        manifest = self.canonical_manifest()
        entry = copy.deepcopy(next(
            item for item in manifest["primary_runs"] if item["id"] == "p32_optimized_active"
        ))
        entry["input_hashes"]["model"] = "0" * 64

        with self.assertRaisesRegex(run_phase_b.ProvenanceError, "model hash"):
            run_phase_b.validate_recorded_provenance(
                run_phase_b.RunSpec(32, "optimized", "active", 4),
                entry,
                manifest["inputs"],
                manifest["tools"],
                manifest["target"],
            )

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

        with self.assertRaisesRegex(run_phase_b.ProvenanceError, "normalized full assembly"):
            run_phase_b.require_count_normalized_assembly_identity(
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

        metadata, _ = run_phase_b.compiler_metadata(
            run_phase_b.RunSpec(8, "optimized", "active"),
            assembly,
            run_phase_b.EXPECTED_OUTPUT,
            stderr,
        )

        self.assertEqual(4, metadata["completed_count"])
        self.assertFalse(metadata["overrun"])
        self.assertTrue(run_phase_b.static_deadline_evidence(
            run_phase_b.RunSpec(8, "optimized", "active"), stderr
        )["rejected"])

    def test_schema_v2_reproduces_from_raw_artifacts_without_tool_subprocesses(self):
        source = run_phase_b.ROOT / "sim" / "fixtures" / "phase_b"
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "phase_b"
            shutil.copytree(source, copied)
            output = io.StringIO()
            with patch.object(run_phase_b, "execute", side_effect=AssertionError("tool process started")):
                with redirect_stdout(output):
                    exit_code = run_phase_b.main(["--output-dir", str(copied)])

        self.assertEqual(0, exit_code)
        self.assertIn("reproduction", output.getvalue())

    def test_schema_v2_reproduction_rejects_missing_hash_and_tampered_p8_without_writing(self):
        source = run_phase_b.ROOT / "sim" / "fixtures" / "phase_b"
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "phase_b"
            shutil.copytree(source, copied)
            manifest_path = copied / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            next(item for item in manifest["primary_runs"] if item["id"] == "p32_optimized_active").pop(
                "artifact_provenance"
            )
            p8 = next(item for item in manifest["primary_runs"] if item["id"] == "p8_optimized_active")
            p8["metadata"]["completed_count"] = 1
            p8["metadata"]["overrun"] = True
            manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            before = manifest_path.read_bytes()

            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                exit_code = run_phase_b.main(["--output-dir", str(copied)])

            self.assertEqual(2, exit_code)
            self.assertEqual(before, manifest_path.read_bytes())

    def test_clean_schema_v2_reproduction_validates_without_writing_manifest_or_tables(self):
        source = run_phase_b.ROOT / "sim" / "fixtures" / "phase_b"
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "phase_b"
            shutil.copytree(source, copied)
            tracked = [
                copied / "manifest.json",
                copied / "primary.csv",
                copied / "primary.md",
                copied / "two_by_two.csv",
                copied / "two_by_two.md",
            ]
            before = {path.name: path.read_bytes() for path in tracked}
            output = io.StringIO()
            with patch.object(run_phase_b, "execute", side_effect=AssertionError("tool process started")), \
                    patch.object(run_phase_b, "write_json", side_effect=AssertionError("manifest write started")), \
                    patch.object(run_phase_b, "write_primary_tables", side_effect=AssertionError("table write started")), \
                    patch.object(run_phase_b, "write_two_by_two_tables", side_effect=AssertionError("table write started")), \
                    redirect_stdout(output):
                exit_code = run_phase_b.main(["--output-dir", str(copied)])

            self.assertEqual(0, exit_code)
            self.assertIn("no files written", output.getvalue())
            self.assertEqual(before, {path.name: path.read_bytes() for path in tracked})



class PhaseBModeSeparationTests(unittest.TestCase):
    """The three modes are separate contracts, not one overloaded entry point."""

    def test_command_identity_survives_the_recording_machines_path_style(self):
        """The same command recorded on Windows and rebuilt on Linux must agree.

        Note the tool argument itself is NOT normalised away: `optifine.exe`
        and `optifine` are genuinely different names, and the caller always
        rebuilds the expected command using the *recorded* tool path, so
        element 0 matches by construction rather than by leniency.
        """
        tool = r"D:\_CODING\OptiFine\optifine.exe"
        windows = [
            tool, r"D:\_CODING\OptiFine\models\m.onnx",
            "--cost-table", r"D:\_CODING\OptiFine\cost_table.toml",
            "--out", r"D:\_CODING\OptiFine\sim\fixtures\phase_b\p32_naive_active.S",
            "--timer-prescaler", "32",
        ]
        posix = [
            tool, "/home/u/OptiFine/models/m.onnx",
            "--cost-table", "/home/u/OptiFine/cost_table.toml",
            "--out", "/home/u/OptiFine/sim/fixtures/phase_b/p32_naive_active.S",
            "--timer-prescaler", "32",
        ]
        self.assertEqual(run_phase_b.command_identity(windows),
                         run_phase_b.command_identity(posix))

    def test_command_identity_still_rejects_a_changed_flag_value(self):
        base = ["/o/optifine", "/o/m.onnx", "--timer-prescaler", "32"]
        other = ["/o/optifine", "/o/m.onnx", "--timer-prescaler", "128"]
        self.assertNotEqual(run_phase_b.command_identity(base),
                            run_phase_b.command_identity(other))

    def test_command_identity_still_rejects_a_different_input_file(self):
        base = ["/o/optifine", "/o/models/m.onnx", "--timer-prescaler", "32"]
        other = ["/o/optifine", "/o/models/other.onnx", "--timer-prescaler", "32"]
        self.assertNotEqual(run_phase_b.command_identity(base),
                            run_phase_b.command_identity(other))

    def test_reproduction_succeeds_when_the_recorded_toolchain_is_absent(self):
        """The canonical result must not be pinned to the machine that produced it.

        reproduce-retained reads retained assembly and Avrora output; it never
        invokes the compiler, assembler or simulator, so an absent or differing
        toolchain is reportable metadata, not grounds for refusal.
        """
        source = run_phase_b.ROOT / "sim" / "fixtures" / "phase_b"
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "phase_b"
            shutil.copytree(source, copied)
            # No mutation needed: the canonical manifest records the Windows
            # paths of the machine that produced it, which are absent here.
            # That is precisely the situation this mode must tolerate.
            output = io.StringIO()
            with patch.object(run_phase_b, "execute", side_effect=AssertionError("tool process started")), \
                    redirect_stdout(output):
                exit_code = run_phase_b.main(["reproduce-retained", "--output-dir", str(copied)])

            self.assertEqual(0, exit_code)
            self.assertIn("not present on this machine", output.getvalue())
            self.assertIn("ARTIFACT-level reproduction", output.getvalue())

    def test_reproduction_still_rejects_a_changed_experimental_input(self):
        """Scoping the tool gate must not have loosened the input gate."""
        source = run_phase_b.ROOT / "sim" / "fixtures" / "phase_b"
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "phase_b"
            shutil.copytree(source, copied)
            manifest = json.loads((copied / "manifest.json").read_text(encoding="utf-8"))
            manifest["inputs"]["model"]["sha256"] = "0" * 64
            (copied / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

            errors = io.StringIO()
            with redirect_stderr(errors):
                exit_code = run_phase_b.main(["reproduce-retained", "--output-dir", str(copied)])

            self.assertEqual(2, exit_code)
            self.assertIn("model hash does not match", errors.getvalue())

    def test_run_new_refuses_to_overwrite_an_existing_experiment(self):
        source = run_phase_b.ROOT / "sim" / "fixtures" / "phase_b"
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "phase_b"
            shutil.copytree(source, copied)
            before = (copied / "manifest.json").read_bytes()
            errors = io.StringIO()
            with patch.object(run_phase_b, "execute", side_effect=AssertionError("tool process started")), \
                    redirect_stderr(errors):
                exit_code = run_phase_b.main(["run-new", "--output-dir", str(copied)])

            self.assertEqual(2, exit_code)
            self.assertIn("refuses to overwrite", errors.getvalue())
            self.assertEqual(before, (copied / "manifest.json").read_bytes())

    def test_supplemental_refuses_the_canonical_fixture_directory(self):
        errors = io.StringIO()
        with patch.object(run_phase_b, "execute", side_effect=AssertionError("tool process started")), \
                redirect_stderr(errors):
            exit_code = run_phase_b.main(["supplemental"])

        self.assertEqual(2, exit_code)
        self.assertIn("canonical fixture directory", errors.getvalue())

    def test_reproduce_retained_on_a_fresh_directory_points_at_run_new(self):
        with tempfile.TemporaryDirectory() as directory:
            errors = io.StringIO()
            with redirect_stderr(errors):
                exit_code = run_phase_b.main(["reproduce-retained", "--output-dir", directory])

            self.assertEqual(2, exit_code)
            self.assertIn("run-new", errors.getvalue())

if __name__ == "__main__":
    unittest.main()
