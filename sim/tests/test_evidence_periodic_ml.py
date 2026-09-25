"""Checks of the retained periodic ML evidence in sim/fixtures/periodic_ml."""

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

import run_periodic_ml as runner  # noqa: E402


class RetainedPeriodicMlEvidence(unittest.TestCase):
    def canonical_manifest(self):
        path = runner.CANONICAL / "manifest.json"
        return json.loads(path.read_text(encoding="utf-8"))
    def test_rejects_retained_model_hash_mismatch_before_calculation(self):
        manifest = self.canonical_manifest()
        entry = copy.deepcopy(next(
            item for item in manifest["primary_runs"] if item["id"] == "p32_optimized_active"
        ))
        entry["input_hashes"]["model"] = "0" * 64

        with self.assertRaisesRegex(runner.ProvenanceError, "model hash"):
            runner.validate_recorded_provenance(
                runner.RunSpec(32, "optimized", "active", 4),
                entry,
                manifest["inputs"],
                manifest["tools"],
                manifest["target"],
            )
    def test_reproduces_from_raw_artifacts_without_tool_subprocesses(self):
        source = runner.CANONICAL
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "periodic_ml"
            shutil.copytree(source, copied)
            output = io.StringIO()
            with patch.object(runner, "execute", side_effect=AssertionError("tool process started")):
                with redirect_stdout(output):
                    exit_code = runner.main(["--output-dir", str(copied)])

        self.assertEqual(0, exit_code)
        self.assertIn("reproduction", output.getvalue())
    def test_reproduction_rejects_missing_hash_and_tampered_p8_without_writing(self):
        source = runner.CANONICAL
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "periodic_ml"
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
                exit_code = runner.main(["--output-dir", str(copied)])

            self.assertEqual(2, exit_code)
            self.assertEqual(before, manifest_path.read_bytes())
    def test_clean_reproduction_validates_without_writing_manifest_or_tables(self):
        source = runner.CANONICAL
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "periodic_ml"
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
            with patch.object(runner, "execute", side_effect=AssertionError("tool process started")), \
                    patch.object(runner, "write_json", side_effect=AssertionError("manifest write started")), \
                    patch.object(runner, "write_primary_tables", side_effect=AssertionError("table write started")), \
                    patch.object(runner, "write_two_by_two_tables", side_effect=AssertionError("table write started")), \
                    redirect_stdout(output):
                exit_code = runner.main(["--output-dir", str(copied)])

            self.assertEqual(0, exit_code)
            self.assertIn("no files written", output.getvalue())
            self.assertEqual(before, {path.name: path.read_bytes() for path in tracked})
    def test_reproduction_succeeds_when_the_recorded_toolchain_is_absent(self):
        """The canonical result must not be pinned to the machine that produced it.

        reproduce-retained reads retained assembly and Avrora output; it never
        invokes the compiler, assembler or simulator, so an absent or differing
        toolchain is reportable metadata, not grounds for refusal.
        """
        source = runner.CANONICAL
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "periodic_ml"
            shutil.copytree(source, copied)
            # Point every recorded tool at a path that does not exist, as on a
            # machine without the recording toolchain installed.
            manifest_path = copied / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            records = [manifest["tools"]] + [entry["tool_versions"] for entry in
                                              manifest["primary_runs"] + manifest["supplemental_runs"]]
            for tools in records:
                for record in tools.values():
                    record["path"] = "absent/" + record["path"].lstrip("/")
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            output = io.StringIO()
            with patch.object(runner, "execute", side_effect=AssertionError("tool process started")), \
                    redirect_stdout(output):
                exit_code = runner.main(["reproduce-retained", "--output-dir", str(copied)])

            self.assertEqual(0, exit_code)
            self.assertIn("not present on this machine", output.getvalue())
            self.assertIn("ARTIFACT-level reproduction", output.getvalue())
    def test_reproduction_still_rejects_a_changed_experimental_input(self):
        """Scoping the tool gate must not have loosened the input gate."""
        source = runner.CANONICAL
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "periodic_ml"
            shutil.copytree(source, copied)
            manifest = json.loads((copied / "manifest.json").read_text(encoding="utf-8"))
            manifest["inputs"]["model"]["sha256"] = "0" * 64
            (copied / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

            errors = io.StringIO()
            with redirect_stderr(errors):
                exit_code = runner.main(["reproduce-retained", "--output-dir", str(copied)])

            self.assertEqual(2, exit_code)
            self.assertIn("model hash does not match", errors.getvalue())
    def test_run_new_refuses_to_overwrite_an_existing_experiment(self):
        source = runner.CANONICAL
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory) / "periodic_ml"
            shutil.copytree(source, copied)
            before = (copied / "manifest.json").read_bytes()
            errors = io.StringIO()
            with patch.object(runner, "execute", side_effect=AssertionError("tool process started")), \
                    redirect_stderr(errors):
                exit_code = runner.main(["run-new", "--output-dir", str(copied)])

            self.assertEqual(2, exit_code)
            self.assertIn("refuses to overwrite", errors.getvalue())
            self.assertEqual(before, (copied / "manifest.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
