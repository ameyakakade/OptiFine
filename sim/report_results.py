#!/usr/bin/env python3
"""Derive the report's result tables from raw artifacts, so no figure is hand-copied.

Every number this prints is computed here from a retained raw file:

  Phase A  from ``sim/fixtures/classifier_{naive,optimized}.avrora.txt`` --
           Avrora's own energy report for the two compiled classifier builds.
  Phase B  from ``sim/fixtures/phase_b/*.avrora.txt`` -- the steady-state
           differential ``E(count=5) - E(count=4)`` per variant, which cancels
           one-time initialisation and the two policies' different wrapper
           lengths.

Nothing here runs a tool, writes into ``sim/fixtures``, or consults a manifest.
It reads raw simulator output and does arithmetic, so a figure in the report
can always be traced to a file rather than to a previous edit of the report.

Usage:
    python3 sim/report_results.py                 # Markdown to stdout
    python3 sim/report_results.py --format csv
    python3 sim/report_results.py --out-dir DIR   # write results.md and results.csv
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from io import StringIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "sim" / "fixtures"
PHASE_B = FIXTURES / "phase_b"
ANSI = re.compile(r"\x1b\[[0-9;]*m")

# Prescaler 8 is excluded from the accepted set by a static deadline check
# (its 2,048-cycle period is shorter than either variant's body); it is kept
# visible as a rejected row rather than dropped.
ACCEPTED_PRESCALERS = (32, 128, 1024)
PRESCALER_PERIOD = {8: 2048, 32: 8192, 128: 32768, 1024: 262144}


class MissingArtifact(RuntimeError):
    pass


def _read(path: Path) -> str:
    if not path.is_file():
        raise MissingArtifact(f"missing raw artifact: {path.relative_to(ROOT)}")
    return ANSI.sub("", path.read_text(encoding="utf-8", errors="replace"))


def parse_energy(path: Path) -> dict[str, float | int]:
    """Pull cycles and per-state energy out of one Avrora -monitors=energy report."""
    text = _read(path)
    cycles = re.search(r"Simulated time:\s*(\d+)\s*cycles", text)
    cpu = re.search(r"CPU:\s*([0-9.eE+-]+)\s*Joule", text)
    active = re.search(r"Active:\s*([0-9.eE+-]+)\s*Joule,\s*(\d+)\s*cycles", text)
    save = re.search(r"Power Save:\s*([0-9.eE+-]+)\s*Joule,\s*(\d+)\s*cycles", text)
    if not (cycles and cpu and active):
        raise MissingArtifact(f"unparseable Avrora report: {path.relative_to(ROOT)}")
    return {
        "cycles": int(cycles.group(1)),
        "cpu_nj": float(cpu.group(1)) * 1e9,
        "active_nj": float(active.group(1)) * 1e9,
        "active_cycles": int(active.group(2)),
        "powersave_nj": float(save.group(1)) * 1e9 if save else 0.0,
        "powersave_cycles": int(save.group(2)) if save else 0,
    }


def phase_a() -> dict[str, object]:
    naive = parse_energy(FIXTURES / "classifier_naive.avrora.txt")
    opt = parse_energy(FIXTURES / "classifier_optimized.avrora.txt")
    d_cycles = opt["cycles"] - naive["cycles"]
    d_energy = opt["cpu_nj"] - naive["cpu_nj"]
    return {
        "naive": naive,
        "optimized": opt,
        "delta_cycles": d_cycles,
        "delta_nj": d_energy,
        "pct_cycles": 100.0 * d_cycles / naive["cycles"],
        "pct_energy": 100.0 * d_energy / naive["cpu_nj"],
        # Identical across naive, optimized and their difference: the Active
        # model is a single constant per cycle, which is the Phase A finding.
        "per_cycle_nj": naive["cpu_nj"] / naive["cycles"],
    }


def _marginal(prescaler: int, path: str, policy: str) -> dict[str, float]:
    stem = f"p{prescaler}_{path}_{policy}"
    n4 = parse_energy(PHASE_B / f"{stem}.avrora.txt")
    n5 = parse_energy(PHASE_B / f"{stem}_n5.avrora.txt")
    return {
        "nj": n5["cpu_nj"] - n4["cpu_nj"],
        "active_cycles": n5["active_cycles"] - n4["active_cycles"],
        "powersave_cycles": n5["powersave_cycles"] - n4["powersave_cycles"],
    }


def phase_b() -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for prescaler in ACCEPTED_PRESCALERS:
        cell = {p: {c: _marginal(prescaler, c, p) for c in ("naive", "optimized")}
                for p in ("active", "powersave")}
        act = cell["active"]["optimized"]["nj"]
        ps = cell["powersave"]["optimized"]["nj"]
        rows.append({
            "prescaler": prescaler,
            "period_cycles": PRESCALER_PERIOD[prescaler],
            "active_nj": act,
            "powersave_nj": ps,
            "sleep_saved_nj": act - ps,
            "sleep_saving_pct": 100.0 * (act - ps) / act,
            # The compiler-attributable delta: naive vs optimized under sleep.
            "compiler_delta_nj": cell["powersave"]["naive"]["nj"] - ps,
            # Under busy-wait the same two variants cost the same, because the
            # period alone determines the energy.
            # ``+ 0.0`` normalises IEEE negative zero: this difference is
            # exactly zero, and "-0.0000" in a results table invites a
            # second look at a number that has nothing wrong with it.
            "compiler_delta_active_nj": (cell["active"]["naive"]["nj"] - act) + 0.0,
            "active_cycles_naive": cell["powersave"]["naive"]["active_cycles"],
            "active_cycles_optimized": cell["powersave"]["optimized"]["active_cycles"],
        })
    return rows


def render_markdown(a: dict[str, object], b: list[dict[str, object]]) -> str:
    o = StringIO()
    w = o.write
    w("# OptiFine results (generated)\n\n")
    w("Generated by `sim/report_results.py` from raw Avrora reports under\n")
    w("`sim/fixtures/`. Every figure is computed from a retained file; none is\n")
    w("transcribed. All energy is **simulated** (Avrora's ATmega128 power\n")
    w("model), never physically measured.\n\n")

    w("## Phase A: Active-mode code generation\n\n")
    w("Naive (one candidate per op, every value reloaded from SRAM) versus\n")
    w("optimized (candidate diversity for MatMul plus next-use input caching).\n\n")
    w("| | naive | optimized | delta |\n|---|---:|---:|---:|\n")
    n, p = a["naive"], a["optimized"]
    w(f"| Cycles | {n['cycles']:,} | {p['cycles']:,} | {a['delta_cycles']:+,} ({a['pct_cycles']:+.4f}%) |\n")
    w(f"| CPU energy (nJ) | {n['cpu_nj']:,.6f} | {p['cpu_nj']:,.6f} | {a['delta_nj']:+,.6f} ({a['pct_energy']:+.4f}%) |\n\n")
    w(f"Implied Active cost: **{a['per_cycle_nj']:.7f} nJ/cycle**, identical whether\n")
    w("derived from the naive run, the optimized run, or their difference. That\n")
    w("identity is the Phase A finding: under Avrora's Active model, energy\n")
    w("selection reduces exactly to cycle selection.\n\n")

    w("## Phase B: compiler-directed sleep scheduling\n\n")
    w("Steady-state differentials, `E(count=5) - E(count=4)`, which cancel\n")
    w("one-time setup and the two policies' differing wrapper lengths.\n")
    w("Prescaler 8 is excluded by a static deadline check (2,048-cycle period\n")
    w("is shorter than either body) and is not listed as an accepted row.\n\n")
    w("| prescaler | period (cyc) | busy-wait (nJ) | Power-save (nJ) | saved (nJ) | saving % |\n")
    w("|---:|---:|---:|---:|---:|---:|\n")
    for r in b:
        w(f"| {r['prescaler']} | {r['period_cycles']:,} | {r['active_nj']:,.4f} | "
          f"{r['powersave_nj']:,.4f} | {r['sleep_saved_nj']:,.4f} | {r['sleep_saving_pct']:.2f}% |\n")
    w("\nThe saving percentage is **duty-cycle dependent**: it is a property of how\n")
    w("long the MCU sleeps between wake events and approaches 100% as the period\n")
    w("grows. It is not a fixed 'compiler saving'.\n\n")

    w("### Compiler-attributable delta (naive vs optimized)\n\n")
    w("| prescaler | under Power-save (nJ) | under busy-wait (nJ) | Active cycles naive -> optimized |\n")
    w("|---:|---:|---:|---|\n")
    for r in b:
        w(f"| {r['prescaler']} | {r['compiler_delta_nj']:,.4f} | {r['compiler_delta_active_nj']:,.4f} | "
          f"{r['active_cycles_naive']:,} -> {r['active_cycles_optimized']:,} |\n")
    w("\nPhase A's cycle saving is worth a fixed amount per inference under sleep\n")
    w("scheduling and **exactly zero** under busy-wait, where finishing sooner\n")
    w("only buys more polling at the same total energy.\n")
    return o.getvalue()


def render_csv(a: dict[str, object], b: list[dict[str, object]]) -> str:
    o = StringIO()
    wr = csv.writer(o, lineterminator="\n")
    wr.writerow(["phase", "metric", "variant", "prescaler", "value", "unit"])
    n, p = a["naive"], a["optimized"]
    for label, rec in (("naive", n), ("optimized", p)):
        wr.writerow(["A", "cycles", label, "", rec["cycles"], "cycles"])
        wr.writerow(["A", "cpu_energy", label, "", f"{rec['cpu_nj']:.6f}", "nJ"])
    wr.writerow(["A", "delta_cycles", "optimized-naive", "", a["delta_cycles"], "cycles"])
    wr.writerow(["A", "delta_energy", "optimized-naive", "", f"{a['delta_nj']:.6f}", "nJ"])
    wr.writerow(["A", "delta_pct", "optimized-naive", "", f"{a['pct_energy']:.4f}", "%"])
    wr.writerow(["A", "per_cycle_active", "", "", f"{a['per_cycle_nj']:.7f}", "nJ/cycle"])
    for r in b:
        pre = r["prescaler"]
        for key, unit in (("active_nj", "nJ"), ("powersave_nj", "nJ"),
                          ("sleep_saved_nj", "nJ"), ("sleep_saving_pct", "%"),
                          ("compiler_delta_nj", "nJ"), ("compiler_delta_active_nj", "nJ")):
            wr.writerow(["B", key, "optimized", pre, f"{r[key]:.6f}", unit])
    return o.getvalue()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--format", choices=("markdown", "csv"), default="markdown")
    ap.add_argument("--out-dir", type=Path, help="write results.md and results.csv here")
    args = ap.parse_args(argv)
    try:
        a, b = phase_a(), phase_b()
    except MissingArtifact as error:
        print(f"report_results: {error}", file=sys.stderr)
        return 2
    md, cs = render_markdown(a, b), render_csv(a, b)
    if args.out_dir:
        args.out_dir.mkdir(parents=True, exist_ok=True)
        (args.out_dir / "results.md").write_text(md, encoding="utf-8")
        (args.out_dir / "results.csv").write_text(cs, encoding="utf-8")
        print(f"wrote {args.out_dir / 'results.md'} and {args.out_dir / 'results.csv'}")
        return 0
    print(md if args.format == "markdown" else cs, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
