#!/usr/bin/env python3
"""Derive the report's result tables from raw artifacts, so no figure is hand-copied.

Every number this prints is computed here from a retained raw file:

  Phase A  from ``sim/fixtures/classifier_{naive,optimized}.avrora.txt`` --
           Avrora's own energy report for the two compiled classifier builds.
  Phase B  from ``sim/fixtures/phase_b/*.avrora.txt`` -- the steady-state
           differential ``E(count=5) - E(count=4)`` per variant, which cancels
           one-time initialisation and the two policies' different wrapper
           lengths.
  DSP      from ``sim/fixtures/dsp/`` -- the complete ``optifine --dsp``
           program's Avrora report, the compiler's own cost printout,
           ``avr-size -A`` of the linked ELF, and ``test_dsp_pipeline``'s
           per-op prediction, captured by ``sim/run_dsp.py``.

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
DSP = FIXTURES / "dsp"

#: Target clock and memory sizes the DSP figures are expressed against.
CLOCK_HZ = 8_000_000
FLASH_BYTES = 128 * 1024
SRAM_BYTES = 4096
ANSI = re.compile(r"\x1b\[[0-9;]*m")

# Prescaler 8 is excluded from the accepted set by a static deadline check
# (its 2,048-cycle period is shorter than either variant's body); it is kept
# visible as a rejected row rather than dropped.
ACCEPTED_PRESCALERS = (32, 128, 1024)
PRESCALER_PERIOD = {8: 2048, 32: 8192, 128: 32768, 1024: 262144}


#: Residues below this are floating-point noise, not energy.
#:
#: The busy-wait naive/optimized difference is genuinely zero -- both variants
#: burn the full wake period in Active mode -- but the two Avrora reports print
#: that identical energy as slightly different decimal strings, so parsing them
#: as doubles leaves a residue around 1e-11 nJ. Rendering that as "-0.0000"
#: would invite a second look at a number that is exactly what it should be.
#: One femto-nJ is some twelve orders of magnitude below a single Active cycle
#: (2.8375 nJ), so nothing physical can hide beneath this threshold.
FLOAT_NOISE_NJ = 1e-9


def _snap(value: float) -> float:
    return 0.0 if abs(value) < FLOAT_NOISE_NJ else value


class MissingArtifact(RuntimeError):
    pass


def _shown(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def _read(path: Path) -> str:
    if not path.is_file():
        raise MissingArtifact(f"missing raw artifact: {_shown(path)}")
    return ANSI.sub("", path.read_text(encoding="utf-8", errors="replace"))


def parse_energy(path: Path) -> dict[str, float | int]:
    """Pull cycles and per-state energy out of one Avrora -monitors=energy report."""
    text = _read(path)
    cycles = re.search(r"Simulated time:\s*(\d+)\s*cycles", text)
    cpu = re.search(r"CPU:\s*([0-9.eE+-]+)\s*Joule", text)
    active = re.search(r"Active:\s*([0-9.eE+-]+)\s*Joule,\s*(\d+)\s*cycles", text)
    save = re.search(r"Power Save:\s*([0-9.eE+-]+)\s*Joule,\s*(\d+)\s*cycles", text)
    if not (cycles and cpu and active):
        raise MissingArtifact(f"unparseable Avrora report: {_shown(path)}")
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
            "compiler_delta_active_nj": _snap(cell["active"]["naive"]["nj"] - act),
            "active_cycles_naive": cell["powersave"]["naive"]["active_cycles"],
            "active_cycles_optimized": cell["powersave"]["optimized"]["active_cycles"],
        })
    return rows


def _need(match: re.Match | None, what: str, path: Path) -> re.Match:
    if not match:
        raise MissingArtifact(f"{what} not found in {_shown(path)}")
    return match


def dsp(directory: Path = DSP) -> dict[str, object]:
    """The complete DSP program's figures, from the raw files of one capture.

    Cross-checks rather than trusts: the per-op predictions must sum to the
    compiler's total, that total must equal Avrora's cycles, and the per-op
    code bytes plus the twiddle table must equal the linked .text size.
    """
    sim = parse_energy(directory / "dsp.avrora.txt")

    cp = directory / "dsp.compile.txt"
    compile_text = _read(cp)
    region = lambda label: int(_need(re.search(rf"^dsp {label}[^:]*:\s*(\d+) cycles", compile_text, re.M),
                                     f"'dsp {label}' line", cp).group(1))
    predicted = int(_need(re.search(r"^total:\s*(\d+) cycles,\s*([0-9.]+) nJ", compile_text, re.M),
                          "'total:' line", cp).group(1))
    predicted_nj = float(re.search(r"^total:\s*\d+ cycles,\s*([0-9.]+) nJ", compile_text, re.M).group(1))
    sram = int(_need(re.search(r"^sram:\s*(\d+) bytes", compile_text, re.M), "'sram:' line", cp).group(1))

    sp = directory / "dsp.size.txt"
    size_text = _read(sp)
    section = lambda name: int(m.group(1)) if (m := re.search(rf"^\{name}\s+(\d+)\s", size_text, re.M)) else 0
    text, data, bss = section(".text"), section(".data"), section(".bss")
    if text == 0:
        raise MissingArtifact(f".text not found in {_shown(sp)}")

    op_path = directory / "dsp.ops.txt"
    ops_text = _read(op_path)
    ops = [(m.group(1).strip(), int(m.group(2)), int(m.group(3)), int(m.group(5)))
           for m in re.finditer(r"^    (\S.*?)\s+(\d+)\s+(\d+) B (\+ data|      ) +(\d+)$", ops_text, re.M)]
    table = int(_need(re.search(r"^      \(table data\)\s+(\d+) B$", ops_text, re.M),
                      "table data line", op_path).group(1))
    if not ops:
        raise MissingArtifact(f"per-op lines not found in {_shown(op_path)}")

    op_cycles = sum(o[3] for o in ops)
    code = sum(o[2] for o in ops)
    checks = {
        "per-op prediction sums to the compiler total": op_cycles == predicted,
        "compiler total equals Avrora cycles": predicted == sim["cycles"],
        "per-op code + table equals linked .text": code + table == text,
        "Avrora active cycles cover the whole run": sim["active_cycles"] == sim["cycles"],
    }
    failed = [k for k, ok in checks.items() if not ok]
    if failed:
        raise MissingArtifact("inconsistent DSP capture: " + "; ".join(failed))

    group = lambda names: sum(o[3] for o in ops if o[0] in names)
    fft = [o for o in ops if o[0].startswith("FFT ")]
    breakdown = [
        ("Input/Const setup (clr r2, embedded input, window coefficients)", group({"clr r2", "Input", "Const"}),
         sum(o[2] for o in ops if o[0] in {"clr r2", "Input", "Const"})),
        ("Window", group({"Window"}), sum(o[2] for o in ops if o[0] == "Window")),
        ("BitReverse", group({"BitReverse"}), sum(o[2] for o in ops if o[0] == "BitReverse")),
        (f"FFT (x{len(fft)} stages)", sum(o[3] for o in fft), sum(o[2] for o in fft)),
        ("Magnitude", group({"Magnitude"}), sum(o[2] for o in ops if o[0] == "Magnitude")),
        ("PeakExtract", group({"PeakExtract"}), sum(o[2] for o in ops if o[0] == "PeakExtract")),
        ("Output", group({"Output"}), sum(o[2] for o in ops if o[0] == "Output")),
        ("break (+ twiddle table data)", group({"break + twiddle table"}),
         sum(o[2] for o in ops if o[0] == "break + twiddle table") + table),
    ]
    assert sum(r[1] for r in breakdown) == predicted and sum(r[2] for r in breakdown) == text

    return {
        "cycles": sim["cycles"],
        "cpu_nj": sim["cpu_nj"],
        "per_cycle_nj": sim["cpu_nj"] / sim["cycles"],
        "time_ms": 1000.0 * sim["cycles"] / CLOCK_HZ,
        "predicted_cycles": predicted,
        "predicted_nj": predicted_nj,
        "error_cycles": sim["cycles"] - predicted,
        "initialization_cycles": region("initialization"),
        "pipeline_cycles": region("pipeline"),
        "termination_cycles": region("termination"),
        "text": text, "data": data, "bss": bss,
        "flash_pct": 100.0 * (text + data) / FLASH_BYTES,
        "sram_bytes": sram,
        "sram_pct": 100.0 * sram / SRAM_BYTES,
        "sram_free": SRAM_BYTES - sram,
        "table_bytes": table,
        "ops": ops,
        "breakdown": breakdown,
    }


def render_dsp_markdown(d: dict[str, object], directory: Path = DSP, a: dict[str, object] | None = None) -> str:
    o = StringIO()
    w = o.write
    w("## DSP: complete 64-point Q15 pipeline\n\n")
    w(f"Derived from the raw files in `{_shown(directory)}` (captured by `sim/run_dsp.py`\n")
    w("from `optifine --dsp` on `models/dsp_demo_input.txt`). Energy is Avrora-model\n")
    w(f"**simulated** active energy at {CLOCK_HZ // 1_000_000} MHz; time is cycles divided by that clock. Neither\n")
    w("is a physical measurement.\n\n")
    w("| metric | value |\n|---|---:|\n")
    w(f"| Cycles (Avrora) | {d['cycles']:,} |\n")
    w(f"| Cycles (compiler prediction) | {d['predicted_cycles']:,} |\n")
    w(f"| Prediction error | {d['error_cycles']:+,} cycles |\n")
    w(f"| Time @ {CLOCK_HZ // 1_000_000} MHz | {d['time_ms']:.3f} ms |\n")
    w(f"| Simulated active energy (Avrora) | {d['cpu_nj']:,.2f} nJ ({d['cpu_nj'] / 1000:,.2f} µJ) |\n")
    w(f"| Implied Active cost | {d['per_cycle_nj']:.7f} nJ/cycle |\n")
    w(f"| Linked flash (.text + .data) | {d['text'] + d['data']:,} B ({d['flash_pct']:.1f}% of {FLASH_BYTES // 1024} KiB) |\n")
    w(f"| .text / .data / .bss | {d['text']:,} / {d['data']:,} / {d['bss']:,} B |\n")
    w(f"| SRAM (graph tensors + scratch) | {d['sram_bytes']:,} B ({d['sram_pct']:.1f}% of {SRAM_BYTES:,} B, "
      f"{d['sram_free']:,} B free) |\n\n")
    w(f"The compiler's own estimate, {d['predicted_nj']:,.3f} nJ, prices the same cycles at\n")
    w("`cost_table.toml`'s rounded 2.8375 nJ/cycle; the table above uses Avrora's reported energy.\n\n")
    w("### Where the cycles and bytes go (compiler prediction, per op)\n\n")
    w("| part | cycles | % of cycles | code bytes |\n|---|---:|---:|---:|\n")
    for name, cyc, size in d["breakdown"]:
        w(f"| {name} | {cyc:,} | {100.0 * cyc / d['cycles']:.2f}% | {size:,} |\n")
    w(f"| **total** | **{d['predicted_cycles']:,}** | 100.00% | **{d['text']:,}** |\n\n")
    w("The setup row is production code, not harness: the program embeds its 64 input\n")
    w("samples and 64 window coefficients at compile time, as the ML path embeds its\n")
    w("demo input. The break row's bytes include the 128-byte twiddle table after it.\n")
    if a is not None:
        p = a["optimized"]
        w("\n### ML and DSP side by side\n\n")
        w("The two workloads compute different things, so this shows the backend's breadth,\n")
        w("not the relative efficiency of the two algorithms.\n\n")
        w("| | ML classifier (optimized) | DSP pipeline |\n|---|---:|---:|\n")
        w(f"| Cycles | {p['cycles']:,} | {d['cycles']:,} |\n")
        w(f"| Time @ {CLOCK_HZ // 1_000_000} MHz | {1000.0 * p['cycles'] / CLOCK_HZ:.3f} ms | {d['time_ms']:.3f} ms |\n")
        w(f"| Simulated active energy | {p['cpu_nj']:,.2f} nJ | {d['cpu_nj']:,.2f} nJ |\n")
    return o.getvalue()


def render_dsp_csv(d: dict[str, object]) -> str:
    o = StringIO()
    wr = csv.writer(o, lineterminator="\n")
    wr.writerow(["phase", "metric", "variant", "prescaler", "value", "unit"])
    for key, value, unit in (
        ("cycles", d["cycles"], "cycles"),
        ("predicted_cycles", d["predicted_cycles"], "cycles"),
        ("prediction_error", d["error_cycles"], "cycles"),
        ("time", f"{d['time_ms']:.6f}", "ms"),
        ("cpu_energy", f"{d['cpu_nj']:.6f}", "nJ"),
        ("per_cycle_active", f"{d['per_cycle_nj']:.7f}", "nJ/cycle"),
        ("text", d["text"], "B"), ("data", d["data"], "B"), ("bss", d["bss"], "B"),
        ("sram", d["sram_bytes"], "B"),
    ):
        wr.writerow(["DSP", key, "complete", "", value, unit])
    for name, cyc, size in d["breakdown"]:
        wr.writerow(["DSP", "part_cycles", name, "", cyc, "cycles"])
        wr.writerow(["DSP", "part_code_bytes", name, "", size, "B"])
    return o.getvalue()


def render_markdown(a: dict[str, object], b: list[dict[str, object]], d: dict[str, object] | None = None) -> str:
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
    if d is not None:
        w("\n")
        w(render_dsp_markdown(d, DSP, a))
    return o.getvalue()


def render_csv(a: dict[str, object], b: list[dict[str, object]], d: dict[str, object] | None = None) -> str:
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
    if d is not None:
        o.write(render_dsp_csv(d).split("\n", 1)[1])
    return o.getvalue()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--format", choices=("markdown", "csv"), default="markdown")
    ap.add_argument("--out-dir", type=Path, help="write results.md and results.csv here")
    args = ap.parse_args(argv)
    try:
        a, b, d = phase_a(), phase_b(), dsp()
    except MissingArtifact as error:
        print(f"report_results: {error}", file=sys.stderr)
        return 2
    md, cs = render_markdown(a, b, d), render_csv(a, b, d)
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
