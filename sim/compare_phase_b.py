"""Validate and render matched active/Power-save Avrora energy reports.

Terminal metadata is supplied by the compiler/run artifacts.  Avrora's energy
monitor does not provide an SRAM readback, so this module deliberately makes
no claim that the ``completed_count``, ``overrun``, or ``output`` values were
read from Avrora memory.
"""

import argparse
import csv
import json
import re
import sys
from dataclasses import asdict, dataclass
from io import StringIO
from pathlib import Path
from typing import Any, Mapping, Sequence

from parse_report import EnergyReport, parse_avrora_energy_output


CSV_COLUMNS = (
    "prescaler",
    "period_cycles",
    "inferences",
    "active_nj",
    "powersave_nj",
    "saved_nj",
    "saving_pct",
    "nj_per_inference",
    "active_cycles",
    "powersave_cycles",
    "deadline_status",
    "output_status",
)
REQUIRED_METADATA = (
    "policy",
    "prescaler",
    "inference_count",
    "body_sha256",
    "completed_count",
    "overrun",
    "output",
)
STEADY_STATE_REQUIRED_METADATA = REQUIRED_METADATA + (
    "compute_path",
    "terminal_evidence",
)
TERMINAL_EVIDENCE = "compiler/run metadata; no Avrora SRAM readback is used or claimed"
SHA256_RE = re.compile(r"[0-9a-fA-F]{64}\Z")


class ComparisonError(ValueError):
    """The reports are not a valid matched active/Power-save pair."""


@dataclass(frozen=True)
class ComparisonRow:
    prescaler: int
    period_cycles: int
    inferences: int
    active_nj: float
    powersave_nj: float
    saved_nj: float
    saving_pct: float
    nj_per_inference: float
    active_cycles: int
    powersave_cycles: int
    deadline_status: str
    output_status: str

    def values(self) -> list[object]:
        return [asdict(self)[column] for column in CSV_COLUMNS]


def _require_metadata(metadata: Mapping[str, Any], label: str) -> None:
    missing = [field for field in REQUIRED_METADATA if field not in metadata or metadata[field] is None]
    if missing:
        raise ComparisonError(f"{label} metadata missing required field(s): {', '.join(missing)}")

    if metadata["policy"] not in ("active", "powersave"):
        raise ComparisonError(f"{label} metadata has invalid policy")
    if not isinstance(metadata["prescaler"], int) or isinstance(metadata["prescaler"], bool) or metadata["prescaler"] <= 0:
        raise ComparisonError(f"{label} metadata has invalid prescaler")
    for field in ("inference_count", "completed_count"):
        if (not isinstance(metadata[field], int) or isinstance(metadata[field], bool)
                or metadata[field] <= 0):
            raise ComparisonError(f"{label} metadata has invalid {field}")
    if not isinstance(metadata["body_sha256"], str) or not SHA256_RE.fullmatch(metadata["body_sha256"]):
        raise ComparisonError(f"{label} metadata has invalid body_sha256")
    if metadata["overrun"] is not False:
        raise ComparisonError(f"{label} metadata reports overrun")
    if metadata["output"] == "" or metadata["output"] == []:
        raise ComparisonError(f"{label} metadata has empty output")


def _strict_json_equal(left: Any, right: Any) -> bool:
    """Compare JSON values without Python's bool/int equality shortcut."""
    if type(left) is not type(right):
        return False
    if isinstance(left, list):
        return len(left) == len(right) and all(
            _strict_json_equal(left_item, right_item)
            for left_item, right_item in zip(left, right)
        )
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(
            _strict_json_equal(left[key], right[key]) for key in left
        )
    return left == right


def _validate_matched_metadata(active: Mapping[str, Any], powersave: Mapping[str, Any]) -> int:
    _require_metadata(active, "active")
    _require_metadata(powersave, "powersave")

    if active["policy"] != "active":
        raise ComparisonError("active metadata policy must be active")
    if powersave["policy"] != "powersave":
        raise ComparisonError("powersave metadata policy must be powersave")
    for field in ("prescaler", "inference_count", "body_sha256"):
        if active[field] != powersave[field]:
            raise ComparisonError(f"matched reports have unequal {field}")
    if not _strict_json_equal(active["output"], powersave["output"]):
        raise ComparisonError("matched reports have unequal output")
    for label, metadata in (("active", active), ("powersave", powersave)):
        if metadata["completed_count"] != metadata["inference_count"]:
            raise ComparisonError(f"{label} completed_count does not match inference_count")
    return active["inference_count"]


def _power_save_cycles(report: EnergyReport) -> int:
    for state, (_, cycles) in report.cpu_states.items():
        if state.replace("-", " ").casefold() == "power save":
            return cycles
    return 0


def _active_cycles(report: EnergyReport) -> int:
    for state, (_, cycles) in report.cpu_states.items():
        if state.casefold() == "active":
            return cycles
    return 0


def _validate_reports(active: EnergyReport, powersave: EnergyReport, inferences: int) -> int:
    if active.total_cycles <= 0 or powersave.total_cycles <= 0:
        raise ComparisonError("both reports must include positive Simulated time cycles")
    if active.total_cycles % inferences or powersave.total_cycles % inferences:
        raise ComparisonError("simulated cycles must divide evenly by inference_count")
    active_period = active.total_cycles // inferences
    powersave_period = powersave.total_cycles // inferences
    if active_period != powersave_period:
        raise ComparisonError("matched reports have unequal period_cycles")
    if _active_cycles(active) <= 0 or _active_cycles(powersave) <= 0:
        raise ComparisonError("both reports must include Active cycles")
    if _power_save_cycles(active) != 0:
        raise ComparisonError("active report must not include Power Save cycles")
    if _power_save_cycles(powersave) <= 0:
        raise ComparisonError("powersave report must include positive Power Save cycles")
    if not active.component_nj or not powersave.component_nj:
        raise ComparisonError("both reports must include energy components")
    return active_period


def _require_steady_state_metadata(metadata: Mapping[str, Any], label: str) -> None:
    _require_metadata(metadata, label)
    missing = [field for field in STEADY_STATE_REQUIRED_METADATA if field not in metadata]
    if missing:
        raise ComparisonError(f"{label} metadata missing required field(s): {', '.join(missing)}")
    if metadata["compute_path"] not in ("naive", "optimized"):
        raise ComparisonError(f"{label} metadata has invalid compute_path")
    if metadata["terminal_evidence"] != TERMINAL_EVIDENCE:
        raise ComparisonError(f"{label} metadata must state no Avrora SRAM readback")


def _validate_steady_state_metadata(active_n: Mapping[str, Any], active_n1: Mapping[str, Any],
                                    powersave_n: Mapping[str, Any],
                                    powersave_n1: Mapping[str, Any]) -> int:
    observations = (
        ("active N", active_n, "active"),
        ("active N+1", active_n1, "active"),
        ("powersave N", powersave_n, "powersave"),
        ("powersave N+1", powersave_n1, "powersave"),
    )
    for label, metadata, policy in observations:
        _require_steady_state_metadata(metadata, label)
        if metadata["policy"] != policy:
            raise ComparisonError(f"{label} metadata policy must be {policy}")
        if metadata["completed_count"] != metadata["inference_count"]:
            raise ComparisonError(f"{label} completed_count does not match inference_count")

    count = active_n["inference_count"]
    if count + 1 != active_n1["inference_count"]:
        raise ComparisonError("active observations must have counts N and N+1")
    if powersave_n["inference_count"] != count or powersave_n1["inference_count"] != count + 1:
        raise ComparisonError("powersave observations must have counts N and N+1 matching active")

    reference = active_n
    for label, metadata, _ in observations[1:]:
        for field in ("prescaler", "compute_path", "body_sha256"):
            if metadata[field] != reference[field]:
                raise ComparisonError(f"steady-state observations have unequal {field}")
        if not _strict_json_equal(metadata["output"], reference["output"]):
            raise ComparisonError("steady-state observations have unequal output")
    return count


def _validate_steady_state_report(report: EnergyReport, label: str) -> None:
    if report.total_cycles <= 0:
        raise ComparisonError(f"{label} report must include positive Simulated time cycles")
    if _active_cycles(report) <= 0:
        raise ComparisonError(f"{label} report must include Active cycles")
    if not report.component_nj:
        raise ComparisonError(f"{label} report must include energy components")


def _validate_steady_state_reports(active_n: EnergyReport, active_n1: EnergyReport,
                                   powersave_n: EnergyReport, powersave_n1: EnergyReport) -> int:
    for label, report in (
        ("active N", active_n),
        ("active N+1", active_n1),
        ("powersave N", powersave_n),
        ("powersave N+1", powersave_n1),
    ):
        _validate_steady_state_report(report, label)
    if _power_save_cycles(active_n) != 0 or _power_save_cycles(active_n1) != 0:
        raise ComparisonError("active reports must not include Power Save cycles")
    if _power_save_cycles(powersave_n) <= 0 or _power_save_cycles(powersave_n1) <= 0:
        raise ComparisonError("powersave reports must include positive Power Save cycles")

    active_increment = active_n1.total_cycles - active_n.total_cycles
    powersave_increment = powersave_n1.total_cycles - powersave_n.total_cycles
    if active_increment <= 0 or powersave_increment <= 0:
        raise ComparisonError("steady-state cycle increments must be positive")
    if active_increment != powersave_increment:
        raise ComparisonError("steady-state period_cycles increments differ")
    return active_increment


def compare_reports(active_report: EnergyReport, active_metadata: Mapping[str, Any],
                    powersave_report: EnergyReport, powersave_metadata: Mapping[str, Any]) -> ComparisonRow:
    """Return one row only when both reports are a valid matched pair.

    Metadata checks intentionally happen before report-energy calculations.
    """
    inferences = _validate_matched_metadata(active_metadata, powersave_metadata)
    period_cycles = _validate_reports(active_report, powersave_report, inferences)

    active_nj = active_report.total_nj
    powersave_nj = powersave_report.total_nj
    if active_nj <= 0:
        raise ComparisonError("active energy must be positive")
    saved_nj = active_nj - powersave_nj
    return ComparisonRow(
        prescaler=active_metadata["prescaler"],
        period_cycles=period_cycles,
        inferences=inferences,
        active_nj=active_nj,
        powersave_nj=powersave_nj,
        saved_nj=saved_nj,
        saving_pct=100.0 * saved_nj / active_nj,
        nj_per_inference=powersave_nj / inferences,
        active_cycles=active_report.total_cycles,
        powersave_cycles=powersave_report.total_cycles,
        deadline_status="met",
        output_status="matched",
    )


def compare_steady_state_reports(active_n_report: EnergyReport, active_n_metadata: Mapping[str, Any],
                                 active_n1_report: EnergyReport, active_n1_metadata: Mapping[str, Any],
                                 powersave_n_report: EnergyReport, powersave_n_metadata: Mapping[str, Any],
                                 powersave_n1_report: EnergyReport, powersave_n1_metadata: Mapping[str, Any]) -> ComparisonRow:
    """Compare one extra matched inference without dividing whole lifetimes.

    All terminal values are compiler/run metadata with an explicit no-SRAM-
    readback provenance. Energy and cycle results are the N+1 minus N deltas.
    """
    _validate_steady_state_metadata(
        active_n_metadata, active_n1_metadata, powersave_n_metadata, powersave_n1_metadata
    )
    period_cycles = _validate_steady_state_reports(
        active_n_report, active_n1_report, powersave_n_report, powersave_n1_report
    )
    active_nj = active_n1_report.total_nj - active_n_report.total_nj
    powersave_nj = powersave_n1_report.total_nj - powersave_n_report.total_nj
    if active_nj <= 0 or powersave_nj <= 0:
        raise ComparisonError("steady-state energy increments must be positive")
    saved_nj = active_nj - powersave_nj
    return ComparisonRow(
        prescaler=active_n_metadata["prescaler"],
        period_cycles=period_cycles,
        inferences=1,
        active_nj=active_nj,
        powersave_nj=powersave_nj,
        saved_nj=saved_nj,
        saving_pct=100.0 * saved_nj / active_nj,
        nj_per_inference=powersave_nj,
        active_cycles=active_n1_report.total_cycles - active_n_report.total_cycles,
        powersave_cycles=powersave_n1_report.total_cycles - powersave_n_report.total_cycles,
        deadline_status="met",
        output_status="matched",
    )


def _format_value(value: object) -> object:
    if isinstance(value, float):
        return format(value, ".12g")
    return value


def render_csv(rows: Sequence[ComparisonRow]) -> str:
    output = StringIO()
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow(CSV_COLUMNS)
    for row in rows:
        writer.writerow([_format_value(value) for value in row.values()])
    return output.getvalue()


def render_markdown(rows: Sequence[ComparisonRow]) -> str:
    header = "| " + " | ".join(CSV_COLUMNS) + " |\n"
    divider = "| " + " | ".join("---" for _ in CSV_COLUMNS) + " |\n"
    body = "".join(
        "| " + " | ".join(str(_format_value(value)) for value in row.values()) + " |\n"
        for row in rows
    )
    return header + divider + body


def _load_metadata(path: Path) -> Mapping[str, Any]:
    with path.open(encoding="utf-8") as source:
        metadata = json.load(source)
    if not isinstance(metadata, dict):
        raise ComparisonError(f"metadata file must contain a JSON object: {path}")
    return metadata


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("active_report", type=Path)
    parser.add_argument("active_metadata", type=Path)
    parser.add_argument("powersave_report", type=Path)
    parser.add_argument("powersave_metadata", type=Path)
    parser.add_argument("--format", choices=("csv", "markdown"), default="csv")
    args = parser.parse_args()

    try:
        active_report = parse_avrora_energy_output(args.active_report.read_text(encoding="utf-8"))
        powersave_report = parse_avrora_energy_output(args.powersave_report.read_text(encoding="utf-8"))
        row = compare_reports(
            active_report,
            _load_metadata(args.active_metadata),
            powersave_report,
            _load_metadata(args.powersave_metadata),
        )
    except (ComparisonError, OSError, json.JSONDecodeError) as error:
        parser.error(str(error))

    print(render_csv([row]) if args.format == "csv" else render_markdown([row]), end="")


if __name__ == "__main__":
    main()
