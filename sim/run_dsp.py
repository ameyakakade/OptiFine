#!/usr/bin/env python3
"""Capture, check and compare the canonical complete-DSP baseline.

Raw tool output is retained verbatim, every derived figure is recomputed from
it by ``sim/report_results.py``, and a manifest pins each raw file, each
recorded input and the tools by SHA-256, with the source commit it ran from.

Modes:

  capture --output-dir DIR
      Runs the real pipeline -- ``optifine --dsp`` on the default demo input,
      ``sim/run_avrora.sh``, ``avr-size -A``, and ``test_dsp_pipeline`` for the
      compiler's per-op prediction -- and writes the raw outputs, the
      generated ``results.md``/``results.csv`` and ``manifest.json`` into DIR.
      Refuses a DIR that already holds a manifest, so retained evidence is
      never overwritten.

  check [--output-dir DIR]   (default: sim/fixtures/dsp)
      Starts no tool and writes no file. Verifies every hash in the manifest,
      including the recorded inputs, and that ``results.md``/``results.csv``
      re-derive byte-for-byte from the retained raw files.

  compare DIR_A DIR_B
      Compares two captures: identical assembly, and identical cycles,
      energy, sizes, SRAM and per-op prediction.

The canonical capture lives in ``sim/fixtures/dsp``. Avrora 1.7.115 runs on
the bundled JDK 8 in this repository's workflow: set
``JAVA_HOME=$PWD/tools/jdk8u504-b01`` (a newer JDK from mise fails to start
Avrora's simulator here).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

import report_results

ROOT = Path(__file__).resolve().parents[1]
CANONICAL = ROOT / "sim" / "fixtures" / "dsp"
SCHEMA = "dsp-baseline-v1"

INPUTS = {
    "cost_table": "cost_table.toml",
    "dsp_input": "models/dsp_demo_input.txt",
}
RAW = ("dsp.s", "dsp.compile.txt", "dsp.avrora.txt", "dsp.size.txt", "dsp.ops.txt")
GENERATED = ("results.md", "results.csv")


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rel(path: Path) -> str:
    """Repo-relative for paths in the repository, else the absolute path."""
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path.resolve())


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, **kw)


def first_line(cmd: list[str]) -> str:
    try:
        r = subprocess.run(cmd, text=True, capture_output=True)
        out = (r.stdout or r.stderr).strip().splitlines()
        return out[0] if out else ""
    except OSError as error:
        return f"unavailable: {error}"


def write_generated(out_dir: Path) -> None:
    dsp = report_results.dsp(out_dir)
    (out_dir / "results.md").write_text(report_results.render_dsp_markdown(dsp, out_dir, report_results.active_ml()),
                                        encoding="utf-8")
    (out_dir / "results.csv").write_text(report_results.render_dsp_csv(dsp), encoding="utf-8")


def mode_capture(args: argparse.Namespace) -> int:
    out = args.output_dir.resolve()
    if (out / "manifest.json").exists():
        print(f"run_dsp: {rel(out)} already holds a manifest; captures are never overwritten", file=sys.stderr)
        return 2
    out.mkdir(parents=True, exist_ok=True)
    compiler = args.compiler.resolve()
    pipeline_test = args.pipeline_test.resolve()

    compile_cmd = [str(compiler), "--dsp", "--cost-table", INPUTS["cost_table"],
                   "--input", INPUTS["dsp_input"], "--out", rel(out / "dsp.s")]
    r = run(compile_cmd)
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        return 1
    (out / "dsp.compile.txt").write_text(r.stderr, encoding="utf-8")

    # Repo-relative path, so the raw report names no machine-specific directory.
    sim_cmd = ["bash", "sim/run_avrora.sh", rel(out / "dsp.s")]
    r = run(sim_cmd)
    (out / "dsp.avrora.txt").write_text(r.stdout + r.stderr, encoding="utf-8")
    if r.returncode != 0 or "Simulated time" not in r.stdout:
        print("run_dsp: Avrora run failed (is JAVA_HOME set to tools/jdk8u504-b01?)", file=sys.stderr)
        return 1

    avr_size = os.environ.get("AVR_SIZE", "avr-size")
    size_cmd = [avr_size, "-A", "dsp.elf"]
    r = subprocess.run(size_cmd, cwd=out, text=True, capture_output=True)
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        return 1
    (out / "dsp.size.txt").write_text(r.stdout, encoding="utf-8")

    ops_cmd = [str(pipeline_test)]
    r = run(ops_cmd)
    if r.returncode != 0:
        print(r.stdout + r.stderr, file=sys.stderr)
        return 1
    (out / "dsp.ops.txt").write_text(r.stdout, encoding="utf-8")

    write_generated(out)

    java = Path(os.environ["JAVA_HOME"]) / "bin" / "java" if os.environ.get("JAVA_HOME") else Path("java")
    avrora = ROOT / "tools" / "avrora.jar"
    head = run(["git", "rev-parse", "HEAD"]).stdout.strip()
    dirty = bool(run(["git", "status", "--porcelain", "--", "compiler", "models", "cost_table.toml"]).stdout.strip())
    manifest = {
        "schema": SCHEMA,
        "generated_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "git": {"commit": head, "source_dirty": dirty},
        "commands": {
            "compile": ["compiler/build/optifine"] + compile_cmd[1:],
            "simulate": sim_cmd,
            "size": size_cmd,
            "per_op_prediction": ["compiler/build/tests/test_dsp_pipeline"],
        },
        "inputs": {k: {"path": v, "sha256": sha256_file(ROOT / v)} for k, v in INPUTS.items()},
        "artifacts": {name: sha256_file(out / name) for name in RAW + GENERATED},
        "tools": {
            "compiler_sha256": sha256_file(compiler),
            "avr_gcc": first_line([os.environ.get("AVR_GCC", "avr-gcc"), "--version"]),
            "avr_size": first_line([avr_size, "--version"]),
            "java": first_line([str(java), "-version"]),
            "avrora_jar_sha256": sha256_file(avrora) if avrora.is_file() else "unavailable",
        },
        "evidence_limit": "Avrora-model simulation of ATmega128 at 8 MHz; no physical hardware measurement.",
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"run_dsp: captured {rel(out)}")
    return 0


def mode_check(args: argparse.Namespace) -> int:
    out = args.output_dir.resolve()
    manifest_path = out / "manifest.json"
    if not manifest_path.is_file():
        print(f"run_dsp: no manifest.json in {rel(out)}", file=sys.stderr)
        return 2
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != SCHEMA:
        print(f"run_dsp: unexpected manifest schema {manifest.get('schema')!r}", file=sys.stderr)
        return 2
    failures = []
    for key, rec in manifest["inputs"].items():
        if sha256_file(ROOT / rec["path"]) != rec["sha256"]:
            failures.append(f"recorded input {rec['path']} changed")
    for name, digest in manifest["artifacts"].items():
        if sha256_file(out / name) != digest:
            failures.append(f"artifact {name} does not match its recorded hash")
    dsp = report_results.dsp(out)
    expected_md = report_results.render_dsp_markdown(dsp, out, report_results.active_ml())
    if (out / "results.md").read_text(encoding="utf-8") != expected_md:
        failures.append("results.md does not re-derive from the raw files")
    if (out / "results.csv").read_text(encoding="utf-8") != report_results.render_dsp_csv(dsp):
        failures.append("results.csv does not re-derive from the raw files")
    if failures:
        for f in failures:
            print(f"run_dsp: {f}", file=sys.stderr)
        return 1
    print(f"run_dsp: {rel(out)} verified -- {len(manifest['artifacts'])} artifacts and "
          f"{len(manifest['inputs'])} inputs match, tables re-derive; {dsp['cycles']:,} cycles, "
          f"{dsp['cpu_nj']:,.4f} nJ, {dsp['text']:,} B .text; no tool run, no file written")
    return 0


def mode_compare(args: argparse.Namespace) -> int:
    a, b = args.dir_a.resolve(), args.dir_b.resolve()
    da, db = report_results.dsp(a), report_results.dsp(b)
    same_asm = (a / "dsp.s").read_bytes() == (b / "dsp.s").read_bytes()
    keys = ("cycles", "predicted_cycles", "cpu_nj", "text", "data", "bss", "sram_bytes", "ops")
    diffs = [k for k in keys if da[k] != db[k]]
    print(f"run_dsp: assembly {'identical' if same_asm else 'DIFFERS'}; "
          + ("all derived figures identical" if not diffs else f"differing: {', '.join(diffs)}"))
    return 0 if same_asm and not diffs else 1


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="mode", required=True)
    c = sub.add_parser("capture")
    c.add_argument("--output-dir", type=Path, required=True)
    c.add_argument("--compiler", type=Path, default=ROOT / "compiler" / "build" / "optifine")
    c.add_argument("--pipeline-test", type=Path,
                   default=ROOT / "compiler" / "build" / "tests" / "test_dsp_pipeline")
    k = sub.add_parser("check")
    k.add_argument("--output-dir", type=Path, default=CANONICAL)
    m = sub.add_parser("compare")
    m.add_argument("dir_a", type=Path)
    m.add_argument("dir_b", type=Path)
    args = ap.parse_args(argv)
    try:
        return {"capture": mode_capture, "check": mode_check, "compare": mode_compare}[args.mode](args)
    except report_results.MissingArtifact as error:
        print(f"run_dsp: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
