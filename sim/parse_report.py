"""Parses per-component energy output from Avrora's -monitors=energy report
and prints/returns a structured comparison. See spec section 6.7.

Real Avrora Beta 1.7.115 output (validated in milestone 1, see
sim/fixtures/bringup_smoke.avrora.txt):

    =={ Energy consumption results for node 0 }===================================
    Node lifetime: 8 cycles,  1.0E-6 seconds

    CPU: 2.27001E-8 Joule
       Active: 2.27001E-8 Joule, 8 cycles
       Idle: 0.0 Joule, 0 cycles
       ...

Notes baked into this parser:
- Output contains ANSI color escapes even when piped; they are stripped.
- Avrora reports Joule; all values returned here are converted to nJ.
- Top-level component lines (e.g. "CPU:") are unindented; power-mode state
  lines are indented and captured separately under cpu_states.
"""

import argparse
import re
import sys
from dataclasses import dataclass, field

ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")

# Top-level component: "CPU: 2.27001E-8 Joule"
COMPONENT_RE = re.compile(r"^([A-Za-z][\w ]*?):\s*([0-9.Ee+-]+)\s*Joule\s*$")
# Indented power-mode state: "   Active: 2.27001E-8 Joule, 8 cycles"
STATE_RE = re.compile(
    r"^\s+([A-Za-z][\w ]*?):\s*([0-9.Ee+-]+)\s*Joule(?:,\s*(\d+)\s*cycles)?\s*$"
)
# "Simulated time: 8 cycles" / "Node lifetime: 8 cycles, 1.0E-6 seconds"
LIFETIME_RE = re.compile(r"Node lifetime:\s*(\d+)\s*cycles")


def _strip_ansi(text: str) -> str:
    return ANSI_ESCAPE.sub("", text)


@dataclass
class EnergyReport:
    component_nj: dict[str, float] = field(default_factory=dict)
    cpu_states: dict[str, tuple[float, int]] = field(default_factory=dict)  # nJ, cycles
    total_cycles: int = 0

    @property
    def total_nj(self) -> float:
        return sum(self.component_nj.values())


def parse_avrora_energy_output(text: str) -> EnergyReport:
    report = EnergyReport()
    for raw_line in _strip_ansi(text).splitlines():
        if m := LIFETIME_RE.search(raw_line):
            report.total_cycles = int(m.group(1))
            continue
        if m := COMPONENT_RE.match(raw_line):
            component, joules = m.groups()
            report.component_nj[component.strip()] = float(joules) * 1e9
            continue
        if m := STATE_RE.match(raw_line):
            state, joules, cycles = m.groups()
            report.cpu_states[state.strip()] = (float(joules) * 1e9, int(cycles or 0))
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report_path")
    args = parser.parse_args()

    with open(args.report_path) as f:
        report = parse_avrora_energy_output(f.read())

    if not report.component_nj:
        print("no energy figures parsed -- check the report format", file=sys.stderr)
        sys.exit(1)

    for component, value in report.component_nj.items():
        print(f"{component}: {value:.6f} nJ")
    for state, (nj, cycles) in report.cpu_states.items():
        if cycles:
            print(f"  cpu/{state}: {nj:.6f} nJ over {cycles} cycles")
    print(f"cycles: {report.total_cycles}")
    print(f"total: {report.total_nj:.6f} nJ")


if __name__ == "__main__":
    main()
