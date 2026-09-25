#!/usr/bin/env python3
"""Capture and check the active-mode ML evidence: naive versus optimized.

The two classifier builds (``optifine`` without and with ``--optimized``) on
the golden input, each assembled and run through Avrora's energy monitor, plus
the hand-written bring-up program ``sim/smoke.s`` that calibrates the
harness (its 7 instructions report 8 cycles: the terminating ``break`` is the
one extra cycle every program carries). Raw tool output is retained verbatim;
``sim/report_results.py`` derives every figure from it, and a manifest pins
each raw file, each recorded input and the tools by SHA-256, with the source
commit the capture ran from.

Modes:

  capture --output-dir DIR
      Runs the compiler and ``sim/run_avrora.sh`` and writes the raw outputs
      and ``manifest.json`` into DIR. Refuses a DIR that already holds a
      manifest, so retained evidence is never overwritten.

  check [--output-dir DIR]   (default: sim/fixtures/active_ml)
      Starts no tool and writes no file. Verifies every hash in the manifest,
      including the recorded inputs, and that the retained reports still give
      the recorded cycle counts.

Avrora 1.7.115 needs JDK 8: set ``JAVA_HOME=$PWD/tools/jdk8u504-b01`` (the JDK
``tools/setup_linux.sh`` installs).
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

import report_results
from run_dsp import first_line, rel, run, sha256_file
from run_periodic_ml import git_state, tool_arg, utc_now

ROOT = Path(__file__).resolve().parents[1]
CANONICAL = ROOT / "sim" / "fixtures" / "active_ml"
SCHEMA = "active-ml-baseline-v1"

INPUTS = {
    "model": "models/tiny_classifier.onnx",
    "input": "models/tiny_classifier_golden_input.txt",
    "cost_table": "cost_table.toml",
    "smoke": "sim/smoke.s",
}
VARIANTS = {"naive": [], "optimized": ["--optimized"]}
RAW = ("naive.s", "naive.compile.txt", "naive.avrora.txt",
       "optimized.s", "optimized.compile.txt", "optimized.avrora.txt",
       "smoke.s", "smoke.avrora.txt")


def mode_capture(args: argparse.Namespace) -> int:
    out = args.output_dir.resolve()
    if (out / "manifest.json").exists():
        print(f"run_active_ml: {rel(out)} already holds a manifest; captures are never overwritten",
              file=sys.stderr)
        return 2
    out.mkdir(parents=True, exist_ok=True)
    compiler = args.compiler.resolve()
    commands: dict[str, list[str]] = {}
    for name, flags in VARIANTS.items():
        cmd = [str(compiler), INPUTS["model"], "--cost-table", INPUTS["cost_table"],
               "--input", INPUTS["input"], *flags, "--out", rel(out / f"{name}.s")]
        r = run(cmd)
        if r.returncode != 0:
            print(r.stderr, file=sys.stderr)
            return 1
        (out / f"{name}.compile.txt").write_text(r.stderr, encoding="utf-8")
        commands[f"compile_{name}"] = [tool_arg(compiler)] + cmd[1:]
    shutil.copyfile(ROOT / INPUTS["smoke"], out / "smoke.s")
    for name in ("naive", "optimized", "smoke"):
        # Repo-relative path, so Avrora's "Loading ..." line names no local directory.
        cmd = ["bash", "sim/run_avrora.sh", rel(out / f"{name}.s")]
        r = run(cmd)
        (out / f"{name}.avrora.txt").write_text(r.stdout + r.stderr, encoding="utf-8")
        if r.returncode != 0 or "Simulated time" not in r.stdout:
            print("run_active_ml: Avrora run failed (is JAVA_HOME set to tools/jdk8u504-b01?)",
                  file=sys.stderr)
            return 1
        commands[f"simulate_{name}"] = cmd
    a = report_results.active_ml(out)
    java = Path(os.environ["JAVA_HOME"]) / "bin" / "java" if os.environ.get("JAVA_HOME") else Path("java")
    avrora = ROOT / "tools" / "avrora.jar"
    manifest = {
        "schema": SCHEMA,
        "generated_at": utc_now(),
        "git": git_state(),
        "commands": commands,
        "inputs": {k: {"path": v, "sha256": sha256_file(ROOT / v)} for k, v in INPUTS.items()},
        "artifacts": {name: sha256_file(out / name) for name in RAW},
        "cycles": {"naive": a["naive"]["cycles"], "optimized": a["optimized"]["cycles"],
                   "smoke": report_results.parse_energy(out / "smoke.avrora.txt")["cycles"]},
        "tools": {
            "compiler_sha256": sha256_file(compiler),
            "avr_gcc": first_line([os.environ.get("AVR_GCC", "avr-gcc"), "--version"]),
            "java": first_line([str(java), "-version"]),
            "avrora_jar_sha256": sha256_file(avrora) if avrora.is_file() else "unavailable",
        },
        "evidence_limit": "Avrora-model simulation of ATmega128 at 8 MHz; no physical hardware measurement.",
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"run_active_ml: captured {rel(out)} -- naive {a['naive']['cycles']:,} cycles, "
          f"optimized {a['optimized']['cycles']:,} cycles")
    return 0


def mode_check(args: argparse.Namespace) -> int:
    out = args.output_dir.resolve()
    manifest_path = out / "manifest.json"
    if not manifest_path.is_file():
        print(f"run_active_ml: no manifest.json in {rel(out)}", file=sys.stderr)
        return 2
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != SCHEMA:
        print(f"run_active_ml: unexpected manifest schema {manifest.get('schema')!r}", file=sys.stderr)
        return 2
    failures = []
    for rec in manifest["inputs"].values():
        if sha256_file(ROOT / rec["path"]) != rec["sha256"]:
            failures.append(f"recorded input {rec['path']} changed")
    for name, digest in manifest["artifacts"].items():
        if sha256_file(out / name) != digest:
            failures.append(f"artifact {name} does not match its recorded hash")
    if (out / "smoke.s").read_bytes() != (ROOT / INPUTS["smoke"]).read_bytes():
        failures.append("smoke.s differs from sim/smoke.s")
    a = report_results.active_ml(out)
    cycles = {"naive": a["naive"]["cycles"], "optimized": a["optimized"]["cycles"],
              "smoke": report_results.parse_energy(out / "smoke.avrora.txt")["cycles"]}
    if cycles != manifest["cycles"]:
        failures.append(f"reports give {cycles}, manifest records {manifest['cycles']}")
    if failures:
        for f in failures:
            print(f"run_active_ml: {f}", file=sys.stderr)
        return 1
    print(f"run_active_ml: {rel(out)} verified -- {len(manifest['artifacts'])} artifacts and "
          f"{len(manifest['inputs'])} inputs match; naive {cycles['naive']:,} cycles, optimized "
          f"{cycles['optimized']:,} cycles ({a['pct_cycles']:+.2f}%); no tool run, no file written")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="mode", required=True)
    c = sub.add_parser("capture")
    c.add_argument("--output-dir", type=Path, required=True)
    c.add_argument("--compiler", type=Path, default=ROOT / "compiler" / "build" / "optifine")
    k = sub.add_parser("check")
    k.add_argument("--output-dir", type=Path, default=CANONICAL)
    args = ap.parse_args(argv)
    try:
        return {"capture": mode_capture, "check": mode_check}[args.mode](args)
    except report_results.MissingArtifact as error:
        print(f"run_active_ml: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
