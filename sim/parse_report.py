"""Parses per-component energy output (CPU, memory) from Avrora's -monitors=energy
report and prints/returns a structured comparison. See spec section 5.6.
"""

import argparse
import re
import sys
from dataclasses import dataclass


@dataclass
class EnergyReport:
    component_nj: dict[str, float]

    @property
    def total_nj(self) -> float:
        return sum(self.component_nj.values())


def parse_avrora_energy_output(text: str) -> EnergyReport:
    # TODO(milestone 5): confirm this regex against real avrora -monitors=energy
    # output once milestone 1 (toolchain bring-up) has produced a sample report.
    component_nj: dict[str, float] = {}
    for match in re.finditer(r"^\s*(\w[\w ]*\w)\s*:\s*([\d.]+)\s*nJ\s*$", text, re.MULTILINE):
        component, value = match.groups()
        component_nj[component] = float(value)
    return EnergyReport(component_nj=component_nj)


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
        print(f"{component}: {value} nJ")
    print(f"total: {report.total_nj} nJ")


if __name__ == "__main__":
    main()
