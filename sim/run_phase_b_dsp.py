#!/usr/bin/env python3
"""Phase B over the DSP pipeline: busy-wait versus Power-save, four prescalers.

The DSP counterpart of ``sim/run_phase_b.py``, and deliberately built from its
parts rather than beside them (milestone-6 plan, Task 14): the same periodic
wrapper (``optifine --dsp --periodic-count N --wait-policy ...
--timer-prescaler ...``), the same four Timer0 divisors, the same assembler and
Avrora invocation, the same body/count identity checks, the same
compiler-side static deadline evidence, and the same steady-state
``E(N+1) - E(N)`` comparison (``compare_phase_b.compare_steady_state_reports``).
What differs is only what the ML runner bakes in: the workload is ``--dsp`` on
``models/dsp_demo_input.txt`` with the declared output
``models/dsp_demo_golden_output.txt``; there is no naive/optimized axis (the
DSP ops have one lowering each, recorded as compute path ``naive``); and the
static deadline rule is applied as stated -- a divisor is rejected when the
compiler-predicted body is not shorter than the Timer0 overflow interval --
rather than hard-coded to divisor 8.

One comparison rule is widened, for DSP only (the ML comparator is unchanged).
The busy-wait loop polls the tick flag in an 8-cycle loop, so each wake is
detected up to 8 cycles late, and with the DSP body's length that detection
phase does not settle: busy-wait N->N+1 increments measured 262,141 and
262,149 cycles around the exact 262,144-cycle Power-save increment (the ML
body happens to hit a fixed point, so its increments match exactly). A pair
the strict comparator rejects for unequal increments alone is accepted when
the Power-save increment equals the Timer0 period exactly and the busy-wait
increment lies within the poll loop's 8 cycles of it; the busy-wait energy,
all Active cycles at constant power, is then scaled to the exact period. The
raw increments and the factor are recorded with the pair.

As for the ML experiment, output and completion are compiler/run
declarations: no simulated SRAM is read back. That the wrapper carries the
frozen DSP program unchanged, and that repeated bodies reproduce its output,
is shown in ``test_dsp_pipeline``.

Modes:
  reproduce-retained  (default) revalidate ``sim/fixtures/phase_b_dsp`` from its
                      raw files: recorded inputs, every artifact hash, the pair
                      evaluation and the tables. Starts no tool, writes no file.
  run-new             run the 16-run sweep with the current toolchain into a
                      fresh ``--output-dir``; refuses one that holds a manifest.

Avrora 1.7.115 runs on the bundled JDK 8 in this workflow (found automatically
under ``tools/jdk8*``, as ``run_phase_b.py`` does).
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Any, Sequence

from compare_phase_b import (
    CSV_COLUMNS,
    TERMINAL_EVIDENCE,
    ComparisonError,
    ComparisonRow,
    _active_cycles,
    _power_save_cycles,
    compare_steady_state_reports,
)
from parse_report import parse_avrora_energy_output
from run_phase_b import (
    DIVISORS,
    POLICIES,
    PRIMARY_COUNT,
    SUPPLEMENTAL_COUNT,
    TIMEOUT_SECONDS,
    ProvenanceError,
    RunSpec,
    _tool_versions_for,
    body_sha256,
    compiler_metadata,
    execute,
    path_for_manifest,
    render_primary_tables,
    render_two_by_two_tables,
    report_dict,
    require_count_normalized_assembly_identity,
    resolve_tools,
    run_supplemental_avrora,
    sha256_file,
    sha256_text,
    strict_json_equal,
    utc_now,
    write_json,
)

ROOT = Path(__file__).resolve().parents[1]
CANONICAL = ROOT / "sim" / "fixtures" / "phase_b_dsp"
SCHEMA = "phase-b-dsp-steady-state-v1"
#: The DSP ops have one lowering each (no --optimized); the comparison's
#: compute_path field records that single path.
COMPUTE_PATH = "naive"
#: The busy-wait loop as periodic.c emits it, and its cycle count when the
#: tick is not yet set: cli 1 + lds 2 + tst 1 + brne (not taken) 1 + sei 1 +
#: rjmp 2 = 8. A wake is detected up to this many cycles late.
BUSY_WAIT_LOOP = ("cli", "lds r16, {tick}", "tst r16", "brne tick_ready", "sei", "rjmp wait_for_tick")
POLL_LOOP_CYCLES = 8
TIMER0_PERIOD_CYCLES_PER_DIVISOR = 256
INPUTS = {
    "dsp_input": "models/dsp_demo_input.txt",
    "golden_output": "models/dsp_demo_golden_output.txt",
    "cost_table": "cost_table.toml",
}


def stem(spec: RunSpec) -> str:
    suffix = "" if spec.inference_count == PRIMARY_COUNT else f"_n{spec.inference_count}"
    return f"p{spec.divisor}_dsp_{spec.policy}{suffix}"


def specs(divisors: Sequence[int]) -> list[RunSpec]:
    return [RunSpec(d, COMPUTE_PATH, p, n)
            for d in divisors for p in POLICIES for n in (PRIMARY_COUNT, SUPPLEMENTAL_COUNT)]


def golden_output() -> list[int]:
    values = [int(tok) for line in (ROOT / INPUTS["golden_output"]).read_text(encoding="utf-8").splitlines()
              if not line.lstrip().startswith("#") for tok in line.split()]
    if len(values) != 8:
        raise ValueError(f"{INPUTS['golden_output']} must hold 8 values, got {len(values)}")
    return values


def relative(command: Sequence[str]) -> list[str]:
    """A command as recorded: paths inside the repository made repo-relative,
    so the manifest names no machine-specific directory."""
    out = []
    for token in command:
        p = Path(token)
        out.append(path_for_manifest(p) if p.is_absolute() and str(p).startswith(str(ROOT)) else token)
    return out


def compile_command(compiler: Path, spec: RunSpec, assembly: str) -> list[str]:
    return [str(compiler), "--dsp", "--cost-table", INPUTS["cost_table"], "--input", INPUTS["dsp_input"],
            "--out", assembly, "--periodic-count", str(spec.inference_count),
            "--wait-policy", spec.policy, "--timer-prescaler", str(spec.divisor)]


def run_entry(spec: RunSpec, out_dir: Path, tools, tool_versions, hashes, output) -> dict[str, Any]:
    name = stem(spec)
    assembly = out_dir / f"{name}.S"
    cmd = compile_command(tools.compiler, spec, path_for_manifest(assembly))
    entry: dict[str, Any] = {
        "id": name, "workload": "dsp", "timestamp": utc_now(),
        "divisor": spec.divisor, "prescaler": spec.divisor, "compute_path": COMPUTE_PATH,
        "policy": spec.policy, "expected_count": spec.inference_count,
        "command": {"compiler": relative(cmd)},
        "input_hashes": hashes, "tool_versions": tool_versions, "status": "pending",
    }
    result = execute(cmd)
    (out_dir / f"{name}.compile.txt").write_text(result["stderr"], encoding="utf-8")
    if result["returncode"] != 0 or result["timed_out"] or not assembly.is_file():
        entry.update(status="rejected", rejection_reason="compiler did not produce assembly")
        return entry
    text = assembly.read_text(encoding="utf-8")
    metadata, evidence = compiler_metadata(spec, text, output, result["stderr"])
    entry.update({"body_sha256": metadata["body_sha256"], "metadata": metadata,
                  "metadata_evidence": evidence, "status": "compiled", "_assembly": text})
    # Same assembler and Avrora invocation as the ML sweep, on repo-relative
    # paths so Avrora's own "Loading ..." line names no local directory.
    manifest_tools = {"avrora": {}}
    run_supplemental_avrora(entry, Path(path_for_manifest(out_dir)), tools, manifest_tools)
    for key in ("avr_gcc", "avrora"):
        if key in entry["command"]:
            entry["command"][key] = relative(entry["command"][key])
        entry.pop(key, None)
    entry.pop("avr_gcc_stderr", None)
    entry.pop("avrora_stderr", None)
    if entry["status"] == "reported":
        entry["avrora_version"] = manifest_tools["avrora"].get("version")
        avr_size = os.environ.get("AVR_SIZE", "avr-size")
        size = execute([avr_size, "-A", path_for_manifest(out_dir / f"{name}.elf")])
        (out_dir / f"{name}.size.txt").write_text(size["stdout"], encoding="utf-8")
        entry["command"]["size"] = [avr_size, "-A", path_for_manifest(out_dir / f"{name}.elf")]
    return entry


def artifact_hashes(out_dir: Path, name: str) -> dict[str, str]:
    return {ext: sha256_file(out_dir / f"{name}{ext}")
            for ext in (".S", ".compile.txt", ".avrora.txt", ".size.txt")
            if (out_dir / f"{name}{ext}").is_file()}


def busy_wait_loop_is_standard(assembly: str) -> bool:
    """The emitted busy-wait loop is exactly the 8-cycle loop the tolerance
    assumes (the tick address is whatever the wrapper reserved)."""
    lines = [l.strip() for l in assembly.splitlines()]
    try:
        i = lines.index("wait_for_tick:")
    except ValueError:
        return False
    loop = lines[i + 1:i + 1 + len(BUSY_WAIT_LOOP)]
    if len(loop) != len(BUSY_WAIT_LOOP) or not loop[1].startswith("lds r16, 0x"):
        return False
    tick = loop[1].split(", ")[1]
    return loop == [x.format(tick=tick) for x in BUSY_WAIT_LOOP]


def compare_with_poll_tolerance(divisor: int, a4, a5, p4, p5) -> tuple[ComparisonRow, dict[str, Any]]:
    """compare_steady_state_reports, except that a busy-wait increment within
    POLL_LOOP_CYCLES of an exact-period Power-save increment is accepted, with
    busy-wait energy scaled to the period. Every other check is the ML one."""
    try:
        return compare_steady_state_reports(a4["_report"], a4["metadata"], a5["_report"], a5["metadata"],
                                            p4["_report"], p4["metadata"], p5["_report"], p5["metadata"]), {}
    except ComparisonError as error:
        if str(error) != "steady-state period_cycles increments differ":
            raise
    period = TIMER0_PERIOD_CYCLES_PER_DIVISOR * divisor
    a_cyc = a5["_report"].total_cycles - a4["_report"].total_cycles
    p_cyc = p5["_report"].total_cycles - p4["_report"].total_cycles
    if p_cyc != period:
        raise ComparisonError(f"Power-save increment {p_cyc} is not the {period}-cycle Timer0 period")
    if abs(a_cyc - period) > POLL_LOOP_CYCLES:
        raise ComparisonError(f"busy-wait increment {a_cyc} is outside the {POLL_LOOP_CYCLES}-cycle poll window")
    if not (busy_wait_loop_is_standard(a4["_assembly"]) and busy_wait_loop_is_standard(a5["_assembly"])):
        raise ComparisonError("busy-wait loop is not the 8-cycle loop the tolerance assumes")
    for r in (a4["_report"], a5["_report"]):
        if _power_save_cycles(r) != 0 or _active_cycles(r) != r.total_cycles:
            raise ComparisonError("busy-wait reports must be Active cycles only")
    a_nj = a5["_report"].total_nj - a4["_report"].total_nj
    p_nj = p5["_report"].total_nj - p4["_report"].total_nj
    scale = period / a_cyc
    active_nj = a_nj * scale
    saved = active_nj - p_nj
    row = ComparisonRow(prescaler=divisor, period_cycles=period, inferences=1, active_nj=active_nj,
                        powersave_nj=p_nj, saved_nj=saved, saving_pct=100.0 * saved / active_nj,
                        nj_per_inference=p_nj, active_cycles=period, powersave_cycles=p_cyc,
                        deadline_status="met", output_status="matched")
    note = {"rule": f"busy-wait increment within {POLL_LOOP_CYCLES} cycles of the exact Timer0 period; "
                    "busy-wait energy scaled to the period",
            "busy_wait_increment_cycles": a_cyc, "busy_wait_increment_nj": a_nj,
            "power_save_increment_cycles": p_cyc, "scale": scale}
    return row, note


def deadline(entry: dict[str, Any], out_dir: Path) -> dict[str, Any]:
    """The ML runner's static deadline evidence, from the retained compiler
    output; rejected when the predicted body is not shorter than the period."""
    from run_phase_b import static_deadline_evidence
    spec = RunSpec(entry["divisor"], COMPUTE_PATH, entry["policy"], entry["expected_count"])
    return static_deadline_evidence(spec, (out_dir / f"{entry['id']}.compile.txt").read_text(encoding="utf-8"))


def evaluate_pairs(entries: list[dict[str, Any]], out_dir: Path, divisors: Sequence[int]) -> list[dict[str, Any]]:
    by_key = {(e["divisor"], e["policy"], e["expected_count"]): e for e in entries}
    pairs = []
    for d in divisors:
        a4, a5 = by_key[(d, "active", PRIMARY_COUNT)], by_key[(d, "active", SUPPLEMENTAL_COUNT)]
        p4, p5 = by_key[(d, "powersave", PRIMARY_COUNT)], by_key[(d, "powersave", SUPPLEMENTAL_COUNT)]
        four = [a4, a5, p4, p5]
        pair: dict[str, Any] = {"divisor": d, "compute_path": "dsp", "counts": [PRIMARY_COUNT, SUPPLEMENTAL_COUNT],
                                "status": "rejected"}
        try:
            if any(e["status"] != "reported" for e in four):
                raise ProvenanceError("not all four observations produced an Avrora report")
            pair["active_count_normalized_assembly_sha256"] = require_count_normalized_assembly_identity(
                a4["_assembly"], PRIMARY_COUNT, a5["_assembly"], SUPPLEMENTAL_COUNT)
            pair["powersave_count_normalized_assembly_sha256"] = require_count_normalized_assembly_identity(
                p4["_assembly"], PRIMARY_COUNT, p5["_assembly"], SUPPLEMENTAL_COUNT)
            for field in ("input_hashes", "tool_versions"):
                if any(not strict_json_equal(e[field], a4[field]) for e in four[1:]):
                    raise ProvenanceError(f"four observations have unequal {field}")
            bodies = {e["body_sha256"] for e in four}
            if len(bodies) != 1:
                raise ProvenanceError("four matched active/Power-save N/N+1 BODY hashes differ")
            pair["body_sha256"] = bodies.pop()
        except ProvenanceError as error:
            pair["rejection_reason"] = str(error)
            for e in four:
                e.update(status="rejected", rejection_reason=str(error))
            pairs.append(pair)
            continue
        evidence = deadline(a4, out_dir)
        pair["static_deadline_evidence"] = evidence
        if evidence["rejected"]:
            reason = (f"static deadline rejection: compiler predicted BODY cycles "
                      f"{evidence['predicted_body_cycles']} are not below the "
                      f"{evidence['timer_period_cycles']}-cycle Timer0 overflow interval "
                      "(compute-bound, no idle window); no Avrora SRAM readback is claimed")
            pair["rejection_reason"] = reason
            for e in four:
                e.update(status="rejected", rejection_reason=reason)
            pairs.append(pair)
            continue
        try:
            row, note = compare_with_poll_tolerance(d, a4, a5, p4, p5)
        except ComparisonError as error:
            pair["rejection_reason"] = str(error)
            for e in four:
                e.update(status="rejected", rejection_reason=str(error))
        else:
            pair.update(status="accepted", comparison=row, comparison_method="steady-state N+1 minus N deltas")
            if note:
                pair["poll_jitter_normalization"] = note
            for e in four:
                e["status"] = "accepted"
        pairs.append(pair)
    return pairs


def tables(pairs, entries) -> dict[str, str]:
    for e in entries:           # the tables show the workload, not the lowering path
        e["compute_path"] = "dsp"
    p_csv, p_md = render_primary_tables(pairs)
    t_csv, t_md = render_two_by_two_tables(entries)
    for e in entries:
        e["compute_path"] = COMPUTE_PATH
    return {"primary.csv": p_csv, "primary.md": p_md, "two_by_two.csv": t_csv, "two_by_two.md": t_md}


def mode_run_new(args: argparse.Namespace) -> int:
    out_dir = args.output_dir.resolve()
    if (out_dir / "manifest.json").is_file():
        raise ProvenanceError(f"{path_for_manifest(out_dir)} already holds a manifest; choose a fresh "
                              "--output-dir or use reproduce-retained")
    out_dir.mkdir(parents=True, exist_ok=True)
    os.chdir(ROOT)
    tools = resolve_tools(args)
    tool_versions = _tool_versions_for(tools)
    for record in tool_versions.values():
        record["path"] = path_for_manifest(Path(record["path"]))
        if "version_command" in record:
            record["version_command"] = relative(record["version_command"])
    inputs = {k: {"path": v, "sha256": sha256_file(ROOT / v)} for k, v in INPUTS.items()}
    hashes = {k: r["sha256"] for k, r in inputs.items()}
    output = golden_output()
    entries = [run_entry(s, out_dir, tools, tool_versions, hashes, output) for s in specs(args.divisors)]
    pairs = evaluate_pairs(entries, out_dir, args.divisors)
    generated = tables(pairs, entries)
    for name, text in generated.items():
        (out_dir / name).write_text(text, encoding="utf-8")
    for e in entries:
        e["artifacts_sha256"] = artifact_hashes(out_dir, e["id"])
        e["parsed_report"] = report_dict(e["_report"]) if "_report" in e else None
    for p in pairs:                  # as run_phase_b.py stores them
        if "comparison" in p:
            p["comparison"] = dict(zip(CSV_COLUMNS, p["comparison"].values()))
    head = execute(["git", "-C", str(ROOT), "rev-parse", "HEAD"])["stdout"].strip()
    dirty = bool(execute(["git", "-C", str(ROOT), "status", "--porcelain", "--", "compiler", "models",
                          "cost_table.toml"])["stdout"].strip())
    manifest = {
        "schema": SCHEMA, "generated_at": utc_now(), "workload": "dsp", "target": "atmega128",
        "timeout_seconds": TIMEOUT_SECONDS, "primary_count": PRIMARY_COUNT,
        "supplemental_count": SUPPLEMENTAL_COUNT, "divisors": list(args.divisors),
        "expected_output": output, "evidence_limit": TERMINAL_EVIDENCE,
        "git": {"commit": head, "source_dirty": dirty},
        "inputs": inputs, "tools": tool_versions, "runs": entries, "pairs": pairs,
        "tables_sha256": {name: sha256_text(text) for name, text in generated.items()},
    }
    write_json(out_dir / "manifest.json", manifest)
    accepted = sum(p["status"] == "accepted" for p in pairs)
    print(f"phase-b dsp: {len(entries)} runs, {accepted} accepted pair(s), {len(pairs) - accepted} rejected; "
          f"wrote {path_for_manifest(out_dir)}")
    return 0


def mode_reproduce_retained(args: argparse.Namespace) -> int:
    import json
    out_dir = args.output_dir.resolve()
    manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != SCHEMA or manifest.get("target") != "atmega128":
        raise ProvenanceError("not a phase-b-dsp manifest for atmega128")
    for label, record in manifest["inputs"].items():
        if sha256_file(ROOT / record["path"]) != record["sha256"]:
            raise ProvenanceError(f"recorded {label} ({record['path']}) changed")
    if manifest["expected_output"] != golden_output():
        raise ProvenanceError("expected output differs from the golden output file")
    entries = manifest["runs"]
    divisors = manifest["divisors"]
    for e in entries:
        for ext, digest in e["artifacts_sha256"].items():
            if sha256_file(out_dir / f"{e['id']}{ext}") != digest:
                raise ProvenanceError(f"{e['id']}{ext} does not match its recorded hash")
        spec = RunSpec(e["divisor"], COMPUTE_PATH, e["policy"], e["expected_count"])
        text = (out_dir / f"{e['id']}.S").read_text(encoding="utf-8")
        stderr = (out_dir / f"{e['id']}.compile.txt").read_text(encoding="utf-8")
        metadata, _ = compiler_metadata(spec, text, manifest["expected_output"], stderr)
        if metadata != e["metadata"] or body_sha256(text) != e["body_sha256"]:
            raise ProvenanceError(f"{e['id']} metadata does not re-derive from its assembly")
        e["_assembly"] = text
        e["_report"] = parse_avrora_energy_output((out_dir / f"{e['id']}.avrora.txt").read_text(encoding="utf-8"))
        e["status"] = "reported"
        e.pop("rejection_reason", None)
    pairs = evaluate_pairs(entries, out_dir, divisors)
    retained_status = [(p["divisor"], p["status"]) for p in manifest["pairs"]]
    if [(p["divisor"], p["status"]) for p in pairs] != retained_status:
        raise ProvenanceError("pair evaluation differs from the retained manifest")
    for name, text in tables(pairs, entries).items():
        if (out_dir / name).read_text(encoding="utf-8") != text or sha256_text(text) != manifest["tables_sha256"][name]:
            raise ProvenanceError(f"retained {name} differs from the regenerated table")
    accepted = [p for p in pairs if p["status"] == "accepted"]
    print(f"phase-b dsp reproduction: {len(entries)} retained runs, {len(accepted)} accepted pair(s) "
          f"({', '.join(str(p['divisor']) for p in accepted)}), {len(pairs) - len(accepted)} rejected; "
          "no simulator executed and no files written")
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", nargs="?", default="reproduce-retained", choices=("reproduce-retained", "run-new"))
    ap.add_argument("--compiler")
    ap.add_argument("--avr-gcc")
    ap.add_argument("--java")
    ap.add_argument("--avrora-jar")
    ap.add_argument("--output-dir", type=Path, default=CANONICAL)
    ap.add_argument("--divisors", nargs="+", type=int, choices=DIVISORS, default=list(DIVISORS))
    args = ap.parse_args(argv)
    try:
        return {"run-new": mode_run_new, "reproduce-retained": mode_reproduce_retained}[args.mode](args)
    except (ProvenanceError, FileNotFoundError, KeyError, ValueError) as error:
        print(f"run_phase_b_dsp: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
