"""Generate and execute the reproducible Phase B periodic AVR sweep.

The runner deliberately treats Avrora as an energy-report source only.  The
compiler invocation and checked golden fixture provide the output/count/
overrun metadata consumed by ``compare_phase_b``; this script makes no SRAM
readback claim for Avrora.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from io import StringIO
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any, Iterable, Sequence

from compare_phase_b import (
    CSV_COLUMNS,
    TERMINAL_EVIDENCE,
    ComparisonError,
    compare_steady_state_reports,
)
from parse_report import EnergyReport, parse_avrora_energy_output


ROOT = Path(__file__).resolve().parents[1]
TIMEOUT_SECONDS = 120
PRIMARY_COUNT = 4
SUPPLEMENTAL_COUNT = 5
# Retained for the original dry-run/metadata contract tests and for callers
# that refer to the primary experiment by its established name.
EXPECTED_COUNT = PRIMARY_COUNT
EXPECTED_OUTPUT = [0, -3, 18, 27]
DIVISORS = (8, 32, 128, 1024)
COMPUTE_PATHS = ("naive", "optimized")
POLICIES = ("active", "powersave")
BODY_BEGIN = "; BODY BEGIN"
BODY_END = "; BODY END"
AVRORA_VERSION_RE = re.compile(r"^Avrora \[[^\]]+\].*$", re.MULTILINE)
ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
PREDICTED_INFERENCE_CYCLES_RE = re.compile(
    r"periodic per-inference predicted compute cost: (\d+) cycles"
)
TIMER0_PERIOD_CYCLES_PER_DIVISOR = 256


class BodyMismatchError(ValueError):
    """A matched pair's classifier bodies are not byte-for-byte identical."""


class ToolDiscoveryError(RuntimeError):
    """A required local tool cannot be located safely."""


class ProvenanceError(ValueError):
    """A retained observation cannot be proved to match its recorded inputs."""


@dataclass(frozen=True)
class RunSpec:
    divisor: int
    compute_path: str
    policy: str
    inference_count: int = PRIMARY_COUNT

    @property
    def stem(self) -> str:
        suffix = "" if self.inference_count == PRIMARY_COUNT else f"_n{self.inference_count}"
        return f"p{self.divisor}_{self.compute_path}_{self.policy}{suffix}"


@dataclass(frozen=True)
class ToolPaths:
    compiler: Path
    avr_gcc: Path
    java: Path
    avrora_jar: Path


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def path_for_manifest(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path.resolve())


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def body_sha256(assembly: str) -> str:
    """Hash exactly the assembly text strictly between the established markers."""
    begin = assembly.find(BODY_BEGIN)
    end = assembly.find(BODY_END)
    if begin < 0 or end < 0 or end <= begin:
        raise ValueError("assembly is missing ordered BODY markers")
    body_start = begin + len(BODY_BEGIN)
    if body_start < len(assembly) and assembly[body_start:body_start + 2] == "\r\n":
        body_start += 2
    elif body_start < len(assembly) and assembly[body_start] == "\n":
        body_start += 1
    return sha256_text(assembly[body_start:end])


def normalized_count_assembly(assembly: str, inference_count: int) -> str:
    """Replace exactly the requested count literal, and nothing else."""
    count_instruction = re.compile(
        rf"(?m)^(\s*cpi\s+r16,\s*){re.escape(str(inference_count))}(\s*)$"
    )
    matches = list(count_instruction.finditer(assembly))
    if len(matches) != 1:
        raise ProvenanceError(
            f"assembly must contain exactly one cpi r16, {inference_count} count literal"
        )
    return count_instruction.sub(lambda match: f"{match.group(1)}<COUNT>{match.group(2)}", assembly)


def normalized_count_assembly_sha256(assembly: str, inference_count: int) -> str:
    return sha256_text(normalized_count_assembly(assembly, inference_count))


def require_count_normalized_assembly_identity(count_n_assembly: str, count_n: int,
                                               count_n1_assembly: str, count_n1: int) -> str:
    """Require N/N+1 programs to differ only in their requested count literal."""
    count_n_hash = normalized_count_assembly_sha256(count_n_assembly, count_n)
    count_n1_hash = normalized_count_assembly_sha256(count_n1_assembly, count_n1)
    if count_n_hash != count_n1_hash:
        raise ProvenanceError(
            "normalized full assembly hashes differ outside the requested count literal"
        )
    return count_n_hash


def require_matched_body_hashes(active_assembly: str, powersave_assembly: str) -> str:
    active_hash = body_sha256(active_assembly)
    powersave_hash = body_sha256(powersave_assembly)
    if active_hash != powersave_hash:
        raise BodyMismatchError(
            f"matched classifier body hashes differ: active={active_hash} powersave={powersave_hash}"
        )
    return active_hash


def build_specs(divisors: Iterable[int], compute_paths: Iterable[str],
                inference_count: int = PRIMARY_COUNT) -> list[RunSpec]:
    return [
        RunSpec(divisor, compute_path, policy, inference_count)
        for divisor in divisors
        for compute_path in compute_paths
        for policy in POLICIES
    ]


def compiler_command(spec: RunSpec, compiler: str, model: Path, cost_table: Path,
                     golden_input: Path, assembly: Path) -> list[str]:
    command = [
        compiler,
        str(model),
        "--cost-table", str(cost_table),
        "--out", str(assembly),
        "--input", str(golden_input),
        "--periodic-count", str(spec.inference_count),
        "--wait-policy", spec.policy,
        "--timer-prescaler", str(spec.divisor),
    ]
    if spec.compute_path == "optimized":
        command.append("--optimized")
    return command


def _required_path(label: str, requested: str | None, candidates: Iterable[Path]) -> Path:
    choices = [Path(requested)] if requested else list(candidates)
    for candidate in choices:
        if candidate.is_file():
            return candidate.resolve()
    formatted = ", ".join(str(choice) for choice in choices)
    raise ToolDiscoveryError(f"could not locate {label}; checked: {formatted}")


def compiler_candidates() -> tuple[Path, ...]:
    """Return supported single- and multi-config CMake compiler locations,
    across both the MSVC (`optifine.exe`) and Unix (`optifine`) build layouts
    -- CMakeLists.txt itself is generator-agnostic (see compiler/CMakeLists.txt),
    so this list must not assume a Windows host."""
    build_dirs = (
        ROOT / "build",
        ROOT / "build" / "Debug",
        ROOT / "build" / "Release",
        ROOT / "build" / "RelWithDebInfo",
        ROOT / "build" / "compiler",
        ROOT / "compiler" / "build",
        ROOT / "compiler" / "build_linux",
        ROOT / "compiler" / "build_linux" / "Debug",
    )
    exe_names = ("optifine.exe", "optifine")
    return tuple(d / name for d in build_dirs for name in exe_names)


def resolve_tools(args: argparse.Namespace) -> ToolPaths:
    # Every candidate list is env-var override first, then a bundled copy
    # under tools/ (what setup.sh/setup.ps1 populate), then whatever the
    # platform's PATH offers, then legacy single-machine fallback paths kept
    # only for continuity with earlier Windows-only runs recorded in the
    # committed manifest/fixtures -- never assume the current host is that
    # machine.
    avr_gcc_candidates = [Path(os.environ["AVR_GCC"])] if "AVR_GCC" in os.environ else []
    avr_gcc_candidates += [ROOT / "tools" / "avr-gcc" / "bin" / "avr-gcc",
                            ROOT / "tools" / "avr-gcc" / "bin" / "avr-gcc.exe"]
    if found := shutil.which("avr-gcc"):
        avr_gcc_candidates.append(Path(found))
    avr_gcc_candidates += [Path("/usr/bin/avr-gcc"),
                            Path(r"C:\Users\yespa\optifine-tools\bin\avr-gcc.exe")]

    # JDK 8 specifically -- Avrora 1.7.115 references the removed
    # java.lang.Compiler class and crashes with NoClassDefFoundError on
    # JDK 9+ (see sim/run_avrora.sh), so a bundled/known-JDK-8 path must be
    # tried before a bare `java` on PATH, which is very likely a newer JDK.
    java_candidates = [Path(os.environ["JAVA8_BIN"])] if "JAVA8_BIN" in os.environ else []
    java_candidates += sorted(ROOT.glob("tools/jdk8*/bin/java"))
    java_candidates += sorted(ROOT.glob("tools/jdk8*/bin/java.exe"))
    java_candidates.append(Path(r"C:\Users\yespa\.jabba\jdk\zulu@8.0.492\bin\java.exe"))
    if found := shutil.which("java"):
        java_candidates.append(Path(found))

    avrora_candidates = [Path(os.environ["AVRORA_JAR"])] if "AVRORA_JAR" in os.environ else []
    avrora_candidates += [ROOT / "tools" / "avrora.jar",
                          Path(r"C:\Users\yespa\optifine-tools\avrora.jar")]

    return ToolPaths(
        compiler=_required_path("OptiFine compiler", args.compiler, compiler_candidates()),
        avr_gcc=_required_path("avr-gcc", args.avr_gcc, avr_gcc_candidates),
        java=_required_path("JDK 8 java", args.java, java_candidates),
        avrora_jar=_required_path("avrora.jar", args.avrora_jar, avrora_candidates),
    )


def execute(command: Sequence[str]) -> dict[str, Any]:
    """Run an argument array with the fixed experiment timeout, never a shell."""
    try:
        completed = subprocess.run(
            list(command),
            check=False,
            capture_output=True,
            text=True,
            timeout=TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        return {"returncode": None, "stdout": stdout, "stderr": stderr, "timed_out": True}
    return {
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "timed_out": False,
    }


def tool_record(path: Path, version_command: Sequence[str]) -> dict[str, Any]:
    version = execute(version_command)
    return {
        "path": str(path),
        "sha256": sha256_file(path),
        "version_command": list(version_command),
        "version_returncode": version["returncode"],
        "version_stdout": version["stdout"],
        "version_stderr": version["stderr"],
        "version_timed_out": version["timed_out"],
    }


def avrora_version(output: str) -> str | None:
    """Return Avrora's banner after removing its terminal color escapes."""
    match = AVRORA_VERSION_RE.search(ANSI_ESCAPE_RE.sub("", output))
    return None if match is None else match.group(0)


def expected_output_from_fixture(path: Path) -> list[int]:
    values = [
        int(token)
        for line in path.read_text(encoding="utf-8").splitlines()
        if not line.lstrip().startswith("#")
        for token in line.split()
    ]
    if values != EXPECTED_OUTPUT:
        raise ValueError(f"golden output fixture must be {EXPECTED_OUTPUT}, got {values}")
    return values


def static_deadline_evidence(spec: RunSpec, compiler_stderr: str) -> dict[str, Any]:
    """Return compiler-side schedulability evidence, never terminal runtime state."""
    predicted_match = PREDICTED_INFERENCE_CYCLES_RE.search(compiler_stderr)
    if predicted_match is None:
        raise ValueError("compiler stderr lacks periodic per-inference cycle metadata")
    predicted_inference_cycles = int(predicted_match.group(1))
    timer_period_cycles = TIMER0_PERIOD_CYCLES_PER_DIVISOR * spec.divisor
    return {
        "source": "static compiler schedule analysis; not Avrora SRAM readback",
        "predicted_body_cycles": predicted_inference_cycles,
        "timer_period_cycles": timer_period_cycles,
        "rejected": predicted_inference_cycles >= timer_period_cycles,
    }


def compiler_metadata(spec: RunSpec, assembly: str, output: list[int],
                      compiler_stderr: str) -> tuple[dict[str, Any], dict[str, Any]]:
    body_hash = body_sha256(assembly)
    count_instruction = f"cpi r16, {spec.inference_count}"
    metadata_markers = {
        "completed_count": "; META completed_count_addr=",
        "overrun": "; META overrun_addr=",
        "output": "; META output_addr=",
    }
    checks = {
        "count_limit_instruction_present": count_instruction in assembly,
        "terminal_metadata_markers_present": {
            key: marker in assembly for key, marker in metadata_markers.items()
        },
    }
    if not checks["count_limit_instruction_present"] or not all(
        checks["terminal_metadata_markers_present"].values()
    ):
        raise ValueError("compiler output lacks periodic terminal metadata")
    static_evidence = static_deadline_evidence(spec, compiler_stderr)
    metadata = {
        "policy": spec.policy,
        "prescaler": spec.divisor,
        "compute_path": spec.compute_path,
        "inference_count": spec.inference_count,
        "body_sha256": body_hash,
        # These are compiler/run declarations derived from the emitted count
        # stop condition, not an observation of terminal SRAM. Static
        # infeasibility is kept in static_deadline_evidence() instead.
        "completed_count": spec.inference_count,
        "overrun": False,
        "output": output,
        "terminal_evidence": TERMINAL_EVIDENCE,
    }
    evidence = {
        "source": TERMINAL_EVIDENCE,
        "completed_count": f"--periodic-count {spec.inference_count} plus emitted {count_instruction}; not an Avrora SRAM readback",
        "overrun": "compiler/run terminal metadata declaration; static deadline classification is recorded separately; no simulator memory readback",
        "output": "models/tiny_classifier_golden_output.txt for the model/input supplied to the compiler",
        "predicted_inference_cycles": static_evidence["predicted_body_cycles"],
        "timer_period_cycles": static_evidence["timer_period_cycles"],
        "structural_checks": checks,
    }
    return metadata, evidence


def report_dict(report: EnergyReport) -> dict[str, Any]:
    return {
        "total_cycles": report.total_cycles,
        "total_nj": report.total_nj,
        "component_nj": report.component_nj,
        "cpu_states": {
            state: {"nj": energy, "cycles": cycles}
            for state, (energy, cycles) in report.cpu_states.items()
        },
    }


def state_cycles(report: EnergyReport | None, state: str) -> int | None:
    if report is None:
        return None
    for name, (_, cycles) in report.cpu_states.items():
        if name.replace("-", " ").casefold() == state.casefold():
            return cycles
    return 0


def write_json(path: Path, content: Any) -> None:
    def serializable(value: Any) -> Any:
        if isinstance(value, dict):
            return {
                key: serializable(item)
                for key, item in value.items()
                if not key.startswith("_")
            }
        if isinstance(value, list):
            return [serializable(item) for item in value]
        return value

    path.write_text(json.dumps(serializable(content), indent=2, sort_keys=True) + "\n", encoding="utf-8")


def render_primary_tables(pairs: list[dict[str, Any]]) -> tuple[str, str]:
    columns = ("compute_path",) + CSV_COLUMNS + ("status", "rejection_reason")
    csv_output = StringIO()
    writer = csv.writer(csv_output, lineterminator="\n")
    writer.writerow(columns)
    markdown_rows: list[list[object]] = []
    for pair in pairs:
        row = pair.get("comparison")
        values: list[object] = [pair["compute_path"]]
        if row is None:
            values.extend([pair["divisor"]] + [""] * (len(CSV_COLUMNS) - 1))
        else:
            values.extend(row.values())
        values.extend([pair["status"], pair.get("rejection_reason", "")])
        writer.writerow(values)
        markdown_rows.append(values)
    header = "| " + " | ".join(columns) + " |\n"
    divider = "| " + " | ".join("---" for _ in columns) + " |\n"
    body = "".join("| " + " | ".join(str(value) for value in row) + " |\n" for row in markdown_rows)
    return csv_output.getvalue(), header + divider + body


def write_primary_tables(output_dir: Path, pairs: list[dict[str, Any]]) -> dict[str, str]:
    csv_text, markdown_text = render_primary_tables(pairs)
    primary_csv = output_dir / "primary.csv"
    primary_md = output_dir / "primary.md"
    primary_csv.write_text(csv_text, encoding="utf-8")
    primary_md.write_text(markdown_text, encoding="utf-8")
    return {"primary_csv": path_for_manifest(primary_csv), "primary_markdown": path_for_manifest(primary_md)}


def render_two_by_two_tables(entries: list[dict[str, Any]]) -> tuple[str, str]:
    columns = (
        "prescaler", "compute_path", "policy", "count", "total_nj", "total_cycles",
        "active_cycles", "power_save_cycles", "status", "rejection_reason",
    )
    rows: list[list[object]] = []
    for entry in entries:
        report = entry.get("_report")
        rows.append([
            entry["divisor"], entry["compute_path"], entry["policy"], entry["expected_count"],
            None if report is None else report.total_nj,
            None if report is None else report.total_cycles,
            state_cycles(report, "Active"), state_cycles(report, "Power Save"),
            entry["status"], entry.get("rejection_reason", "") if entry["status"] == "rejected" else "",
        ])
    csv_output = StringIO()
    writer = csv.writer(csv_output, lineterminator="\n")
    writer.writerow(columns)
    writer.writerows(rows)
    markdown = "| " + " | ".join(columns) + " |\n"
    markdown += "| " + " | ".join("---" for _ in columns) + " |\n"
    markdown += "".join("| " + " | ".join(str(value) for value in row) + " |\n" for row in rows)
    return csv_output.getvalue(), markdown


def write_two_by_two_tables(output_dir: Path, entries: list[dict[str, Any]]) -> dict[str, str]:
    csv_text, markdown_text = render_two_by_two_tables(entries)
    table_csv = output_dir / "two_by_two.csv"
    table_md = output_dir / "two_by_two.md"
    table_csv.write_text(csv_text, encoding="utf-8")
    table_md.write_text(markdown_text, encoding="utf-8")
    return {"two_by_two_csv": path_for_manifest(table_csv), "two_by_two_markdown": path_for_manifest(table_md)}


def input_records(model: Path, golden_input: Path, golden_output: Path,
                  cost_table: Path) -> dict[str, dict[str, str]]:
    return {
        "model": {"path": path_for_manifest(model), "sha256": sha256_file(model)},
        "input": {"path": path_for_manifest(golden_input), "sha256": sha256_file(golden_input)},
        "golden_output": {"path": path_for_manifest(golden_output), "sha256": sha256_file(golden_output)},
        "cost_table": {"path": path_for_manifest(cost_table), "sha256": sha256_file(cost_table)},
    }


def input_hashes(inputs: dict[str, dict[str, str]]) -> dict[str, str]:
    return {label: record["sha256"] for label, record in inputs.items()}


def manifest_record_path(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def same_path(left: str, right: str) -> bool:
    return os.path.normcase(str(Path(left).resolve())) == os.path.normcase(str(Path(right).resolve()))


def _looks_like_path(token: str) -> bool:
    return "/" in token or "\\" in token


def command_identity(command: Sequence[str]) -> list[str]:
    """A command reduced to what is portable across machines.

    Retained commands record absolute paths from the machine that produced
    them (``D:\\_CODING\\...``, ``C:\\Users\\...``).  Comparing those against
    paths rebuilt on another machine can only ever fail, which would make a
    canonical experiment reproducible on exactly one computer.  What actually
    carries identity is the *shape*: which flags, in which order, naming which
    files.  Path-valued arguments therefore compare by basename, handling both
    separators so a Windows-recorded command and a POSIX-rebuilt one reduce to
    the same thing.  Flags and literal values (counts, policies, prescalers)
    still compare verbatim -- nothing about the experiment's identity is
    relaxed, only the machine it happened to run on.
    """
    reduced: list[str] = []
    for token in command:
        if _looks_like_path(token):
            reduced.append(PureWindowsPath(token).name if "\\" in token else PurePosixPath(token).name)
        else:
            reduced.append(token)
    return reduced


def require_command_identity(recorded: Any, expected: Sequence[str], label: str) -> None:
    if not isinstance(recorded, list) or command_identity(recorded) != command_identity(expected):
        raise ProvenanceError(f"retained {label} command differs from observation identity")


def strict_json_equal(left: Any, right: Any) -> bool:
    if type(left) is not type(right):
        return False
    if isinstance(left, list):
        return len(left) == len(right) and all(
            strict_json_equal(left_item, right_item)
            for left_item, right_item in zip(left, right)
        )
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(
            strict_json_equal(left[key], right[key]) for key in left
        )
    return left == right


def _require_recorded_tool_identity(recorded: dict[str, Any], expected: dict[str, Any], label: str) -> None:
    if not isinstance(recorded, dict) or "path" not in recorded or "sha256" not in recorded:
        raise ProvenanceError(f"retained {label} tool identity is incomplete")
    if not same_path(recorded["path"], expected["path"]):
        raise ProvenanceError(f"retained {label} tool path differs")
    if recorded["sha256"] != expected["sha256"]:
        raise ProvenanceError(f"retained {label} tool hash differs")


def validate_recorded_provenance(spec: RunSpec, entry: dict[str, Any],
                                 inputs: dict[str, dict[str, str]],
                                 tools: dict[str, Any], target: str,
                                 expected_output: list[int] | None = None) -> None:
    """Validate retained declared provenance without rewriting any field."""
    if target != "atmega128":
        raise ProvenanceError("retained target must be atmega128")
    expected_output = EXPECTED_OUTPUT if expected_output is None else expected_output
    for field, expected in (
        ("id", spec.stem), ("run_set", "primary" if spec.inference_count == PRIMARY_COUNT else "supplemental"),
        ("divisor", spec.divisor), ("prescaler", spec.divisor),
        ("compute_path", spec.compute_path), ("policy", spec.policy),
        ("expected_count", spec.inference_count),
    ):
        if entry.get(field) != expected:
            raise ProvenanceError(f"retained {field} differs from requested observation identity")
    recorded_hashes = entry.get("input_hashes")
    if not isinstance(recorded_hashes, dict):
        raise ProvenanceError("retained input hashes are missing")
    for label, record in inputs.items():
        if recorded_hashes.get(label) != record["sha256"]:
            raise ProvenanceError(f"retained {label} hash differs")
    recorded_tools = entry.get("tool_versions")
    if not isinstance(recorded_tools, dict):
        raise ProvenanceError("retained tool identities are missing")
    for label in ("compiler", "avr_gcc", "java", "avrora"):
        _require_recorded_tool_identity(recorded_tools.get(label), tools[label], label)
    metadata = entry.get("metadata")
    if not isinstance(metadata, dict):
        raise ProvenanceError("retained metadata is missing")
    for field, expected in (
        ("policy", spec.policy), ("prescaler", spec.divisor),
        ("compute_path", spec.compute_path), ("inference_count", spec.inference_count),
        ("completed_count", spec.inference_count), ("overrun", False),
        ("output", expected_output), ("terminal_evidence", TERMINAL_EVIDENCE),
    ):
        if not strict_json_equal(metadata.get(field), expected):
            raise ProvenanceError(f"retained metadata {field} differs")
    if entry.get("body_sha256") != metadata.get("body_sha256"):
        raise ProvenanceError("retained BODY hash fields differ")
    paths = entry.get("paths")
    commands = entry.get("command")
    if not isinstance(paths, dict) or not isinstance(commands, dict):
        raise ProvenanceError("retained paths or command records are missing")
    compiler_expected = compiler_command(
        spec,
        tools["compiler"]["path"],
        manifest_record_path(inputs["model"]["path"]),
        manifest_record_path(inputs["cost_table"]["path"]),
        manifest_record_path(inputs["input"]["path"]),
        manifest_record_path(paths["assembly"]),
    )
    require_command_identity(commands.get("compiler"), compiler_expected, "compiler")
    assembler_expected = [
        tools["avr_gcc"]["path"], "-mmcu=atmega128", "-nostartfiles", "-o",
        str(manifest_record_path(paths["elf"])), str(manifest_record_path(paths["assembly"])),
    ]
    require_command_identity(commands.get("avr_gcc"), assembler_expected, "avr-gcc")
    avrora_expected = [
        tools["java"]["path"], "-jar", tools["avrora"]["path"], "-monitors=energy",
        "-mcu=atmega128", str(manifest_record_path(paths["elf"])),
    ]
    require_command_identity(commands.get("avrora"), avrora_expected, "Avrora")


def observation_artifact_paths(output_dir: Path, entry: dict[str, Any]) -> tuple[Path, Path]:
    paths = entry["paths"]
    assembly = output_dir / Path(paths["assembly"]).name
    avrora_output = output_dir / Path(paths["avrora_output"]).name
    if not assembly.is_file() or not avrora_output.is_file():
        raise ProvenanceError(f"retained raw artifacts are missing for {entry['id']}")
    return assembly, avrora_output


def validate_observation_artifacts(spec: RunSpec, entry: dict[str, Any], output_dir: Path) -> tuple[str, EnergyReport]:
    """Validate raw assembly/report hashes and parsed fields against the manifest."""
    assembly_path, avrora_path = observation_artifact_paths(output_dir, entry)
    assembly = assembly_path.read_text(encoding="utf-8")
    raw_report = avrora_path.read_text(encoding="utf-8")
    artifact = entry.get("artifact_provenance")
    if not isinstance(artifact, dict):
        raise ProvenanceError("retained artifact provenance is missing")
    expected = {
        "full_assembly_sha256": sha256_text(assembly),
        "count_normalized_assembly_sha256": normalized_count_assembly_sha256(assembly, spec.inference_count),
        "avrora_output_sha256": sha256_text(raw_report),
    }
    for field, value in expected.items():
        if artifact.get(field) != value:
            raise ProvenanceError(f"retained {field} differs from raw artifact")
    if entry["body_sha256"] != body_sha256(assembly):
        raise ProvenanceError("retained BODY hash differs from raw assembly")
    report = parse_avrora_energy_output(raw_report)
    if not strict_json_equal(entry.get("parsed_report"), report_dict(report)):
        raise ProvenanceError("retained parsed report differs from raw Avrora output")
    return assembly, report


def validate_recorded_inputs(manifest: dict[str, Any]) -> None:
    """Check the target and every recorded experimental input, starting no tool.

    These are the files a reproduction actually consumes -- the model, the
    golden input and output, the cost table -- so a mismatch here means the
    experiment's *data* changed underneath the retained artifacts and nothing
    downstream can be trusted.  This gate is mandatory in every mode.
    """
    if manifest.get("target") != "atmega128":
        raise ProvenanceError("manifest target must be atmega128")
    for label, record in manifest.get("inputs", {}).items():
        path = manifest_record_path(record["path"])
        if not path.is_file() or sha256_file(path) != record.get("sha256"):
            raise ProvenanceError(f"recorded {label} hash does not match current file")


def describe_tool_environment(manifest: dict[str, Any]) -> list[tuple[str, str]]:
    """Report, per tool, whether this machine holds the byte-identical binary.

    Deliberately a report rather than a gate.  ``reproduce-retained`` reads
    retained assembly and Avrora output and recomputes hashes over them; it
    never invokes the compiler, the assembler or the simulator, so requiring
    those binaries to be byte-identical would refuse a perfectly sound
    reproduction for a reason that has no bearing on it -- and would pin the
    canonical result to a single machine.  ``run-new`` and ``supplemental`` do
    run the tools, and record the hashes of whichever ones they actually used.
    """
    rows: list[tuple[str, str]] = []
    for label, record in sorted(manifest.get("tools", {}).items()):
        path = Path(record["path"])
        if not path.is_file():
            rows.append((label, "not present on this machine"))
        elif sha256_file(path) != record.get("sha256"):
            rows.append((label, "present but differs from the recorded binary"))
        else:
            rows.append((label, "byte-identical to the recorded binary"))
    return rows


def require_recorded_tools_present(manifest: dict[str, Any]) -> None:
    """Hard gate for modes that re-execute the recorded toolchain."""
    for label, record in manifest.get("tools", {}).items():
        path = Path(record["path"])
        if not path.is_file() or sha256_file(path) != record.get("sha256"):
            raise ProvenanceError(f"recorded {label} tool hash does not match current file")


def diagnostics_record(output_dir: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for name, status, description in (
        (
            "smoke_attempt_1_json_failure",
            "preserved failed smoke attempt",
            "The first smoke attempt stopped at JSON serialization; its generated raw artifacts remain intact.",
        ),
        (
            "diagnostic_count5_p32_optimized",
            "preserved Phase 3 diagnostic",
            "The earlier count-5 p32 optimized falsification run remains available; the canonical all-matrix count-5 run is recorded separately.",
        ),
    ):
        directory = output_dir / name
        records.append({
            "id": name,
            "status": status if directory.is_dir() else "not found",
            "description": description,
            "path": path_for_manifest(directory),
            "artifacts": [path_for_manifest(path) for path in sorted(directory.iterdir())] if directory.is_dir() else [],
        })
    return records


def primary_entry_from_retained(spec: RunSpec, retained: dict[str, Any], output_dir: Path,
                                output: list[int], tool_versions: dict[str, Any],
                                hashes: dict[str, str]) -> dict[str, Any]:
    """Normalize a retained count-4 observation without rerunning any tool."""
    assembly = output_dir / f"{spec.stem}.S"
    avrora_output = output_dir / f"{spec.stem}.avrora.txt"
    if not assembly.is_file() or not avrora_output.is_file():
        raise FileNotFoundError(f"retained primary artifacts missing for {spec.stem}")
    assembly_text = assembly.read_text(encoding="utf-8")
    compiler_stderr = retained.get("compiler_stderr", "")
    metadata, evidence = compiler_metadata(spec, assembly_text, output, compiler_stderr)
    report = parse_avrora_energy_output(avrora_output.read_text(encoding="utf-8"))
    entry = dict(retained)
    legacy_rejection = entry.pop("rejection_reason", None)
    if legacy_rejection:
        entry["legacy_full_lifetime_comparator_rejection"] = legacy_rejection
    entry.update({
        "id": spec.stem,
        "run_set": "primary",
        "divisor": spec.divisor,
        "prescaler": spec.divisor,
        "compute_path": spec.compute_path,
        "policy": spec.policy,
        "expected_count": spec.inference_count,
        "body_sha256": metadata["body_sha256"],
        "metadata": metadata,
        "metadata_evidence": evidence,
        "compiler_stderr": compiler_stderr,
        "parsed_report": report_dict(report),
        "input_hashes": hashes,
        "tool_versions": tool_versions,
        "status": "reported",
        "_assembly": assembly_text,
        "_report": report,
    })
    entry.setdefault("paths", {
        "assembly": path_for_manifest(assembly),
        "elf": path_for_manifest(output_dir / f"{spec.stem}.elf"),
        "avrora_output": path_for_manifest(avrora_output),
    })
    return entry


def supplemental_entry(spec: RunSpec, output_dir: Path, tools: ToolPaths, model: Path,
                       cost_table: Path, golden_input: Path, output: list[int],
                       tool_versions: dict[str, Any], hashes: dict[str, str],
                       run_set: str = "supplemental") -> dict[str, Any]:
    assembly = output_dir / f"{spec.stem}.S"
    elf = output_dir / f"{spec.stem}.elf"
    avrora_output = output_dir / f"{spec.stem}.avrora.txt"
    compiler_cmd = compiler_command(spec, str(tools.compiler), model, cost_table, golden_input, assembly)
    entry: dict[str, Any] = {
        "id": spec.stem,
        "run_set": run_set,
        "timestamp": utc_now(),
        "divisor": spec.divisor,
        "prescaler": spec.divisor,
        "compute_path": spec.compute_path,
        "policy": spec.policy,
        "expected_count": spec.inference_count,
        "paths": {
            "assembly": path_for_manifest(assembly),
            "elf": path_for_manifest(elf),
            "avrora_output": path_for_manifest(avrora_output),
        },
        "command": {"compiler": compiler_cmd},
        "compiler_stderr": "",
        "input_hashes": hashes,
        "tool_versions": tool_versions,
        "status": "pending",
    }
    compiler_result = execute(compiler_cmd)
    entry["compiler"] = compiler_result
    entry["compiler_stderr"] = compiler_result["stderr"]
    if compiler_result["returncode"] != 0 or compiler_result["timed_out"] or not assembly.is_file():
        entry["status"] = "rejected"
        entry["rejection_reason"] = "compiler did not produce assembly"
        return entry
    assembly_text = assembly.read_text(encoding="utf-8")
    try:
        metadata, evidence = compiler_metadata(spec, assembly_text, output, compiler_result["stderr"])
    except ValueError as error:
        entry["status"] = "rejected"
        entry["rejection_reason"] = str(error)
        return entry
    entry.update({
        "body_sha256": metadata["body_sha256"],
        "metadata": metadata,
        "metadata_evidence": evidence,
        "status": "compiled",
        "_assembly": assembly_text,
    })
    return entry


def run_supplemental_avrora(entry: dict[str, Any], output_dir: Path, tools: ToolPaths,
                            manifest_tools: dict[str, Any]) -> None:
    if entry["status"] != "compiled":
        return
    assembly = output_dir / f"{entry['id']}.S"
    elf = output_dir / f"{entry['id']}.elf"
    avrora_output = output_dir / f"{entry['id']}.avrora.txt"
    assemble_cmd = [str(tools.avr_gcc), "-mmcu=atmega128", "-nostartfiles", "-o", str(elf), str(assembly)]
    entry["command"]["avr_gcc"] = assemble_cmd
    assembly_result = execute(assemble_cmd)
    entry["avr_gcc"] = assembly_result
    entry["avr_gcc_stderr"] = assembly_result["stderr"]
    if assembly_result["returncode"] != 0 or assembly_result["timed_out"]:
        entry["status"] = "rejected"
        entry["rejection_reason"] = "avr-gcc assembly failed"
        return
    avrora_cmd = [str(tools.java), "-jar", str(tools.avrora_jar), "-monitors=energy", "-mcu=atmega128", str(elf)]
    entry["command"]["avrora"] = avrora_cmd
    avrora_result = execute(avrora_cmd)
    avrora_output.write_text(avrora_result["stdout"], encoding="utf-8")
    entry["avrora"] = avrora_result
    entry["avrora_stderr"] = avrora_result["stderr"]
    if version := avrora_version(avrora_result["stdout"]):
        manifest_tools["avrora"]["version"] = version
    if avrora_result["returncode"] != 0 or avrora_result["timed_out"]:
        entry["status"] = "rejected"
        entry["rejection_reason"] = "Avrora did not terminate successfully"
        return
    try:
        report = parse_avrora_energy_output(avrora_result["stdout"])
    except ValueError as error:
        entry["status"] = "rejected"
        entry["rejection_reason"] = f"could not parse Avrora report: {error}"
        return
    entry["parsed_report"] = report_dict(report)
    entry["_report"] = report
    entry["status"] = "reported"


def _entry_map(entries: Iterable[dict[str, Any]]) -> dict[tuple[int, str, str, int], dict[str, Any]]:
    return {
        (entry["divisor"], entry["compute_path"], entry["policy"], entry["expected_count"]): entry
        for entry in entries
    }


def mark_rejected(entries: Iterable[dict[str, Any]], reason: str) -> None:
    for entry in entries:
        entry["status"] = "rejected"
        entry["rejection_reason"] = reason


def rehydrate_recorded_observation(spec: RunSpec, entry: dict[str, Any], output_dir: Path,
                                   manifest: dict[str, Any]) -> dict[str, Any]:
    """Validate and rehydrate one stored observation without replacing provenance."""
    validate_recorded_provenance(
        spec, entry, manifest["inputs"], manifest["tools"], manifest["target"],
        manifest["expected_output"],
    )
    assembly, report = validate_observation_artifacts(spec, entry, output_dir)
    entry["_assembly"] = assembly
    entry["_report"] = report
    entry["status"] = "reported"
    entry.pop("rejection_reason", None)
    return entry


def evaluate_steady_state_pairs(manifest: dict[str, Any], entries: list[dict[str, Any]],
                                divisors: Iterable[int], compute_paths: Iterable[str]) -> None:
    """Perform all four-observation identity checks before energy calculations."""
    observations = _entry_map(entries)
    pairs: list[dict[str, Any]] = []
    for divisor in divisors:
        for compute_path in compute_paths:
            active_n = observations[(divisor, compute_path, "active", PRIMARY_COUNT)]
            active_n1 = observations[(divisor, compute_path, "active", SUPPLEMENTAL_COUNT)]
            powersave_n = observations[(divisor, compute_path, "powersave", PRIMARY_COUNT)]
            powersave_n1 = observations[(divisor, compute_path, "powersave", SUPPLEMENTAL_COUNT)]
            matched = [active_n, active_n1, powersave_n, powersave_n1]
            pair: dict[str, Any] = {
                "divisor": divisor,
                "compute_path": compute_path,
                "counts": [PRIMARY_COUNT, SUPPLEMENTAL_COUNT],
                "status": "rejected",
            }
            try:
                pair["active_count_normalized_assembly_sha256"] = require_count_normalized_assembly_identity(
                    active_n["_assembly"], PRIMARY_COUNT, active_n1["_assembly"], SUPPLEMENTAL_COUNT
                )
                pair["powersave_count_normalized_assembly_sha256"] = require_count_normalized_assembly_identity(
                    powersave_n["_assembly"], PRIMARY_COUNT, powersave_n1["_assembly"], SUPPLEMENTAL_COUNT
                )
                for field in ("input_hashes", "tool_versions"):
                    if any(not strict_json_equal(entry[field], matched[0][field]) for entry in matched[1:]):
                        raise ProvenanceError(f"four observations have unequal {field}")
                body_hashes = {entry["body_sha256"] for entry in matched}
                if len(body_hashes) != 1:
                    raise ProvenanceError("four matched active/Power-save N/N+1 BODY hashes differ")
                pair["body_sha256"] = body_hashes.pop()
            except ProvenanceError as error:
                pair["rejection_reason"] = str(error)
                mark_rejected(matched, pair["rejection_reason"])
                pairs.append(pair)
                continue
            if divisor == 8:
                assessment = active_n["static_deadline_evidence"]
                if not assessment["rejected"]:
                    raise ProvenanceError("p8 must retain its static deadline rejection")
                reason = (
                    f"static deadline rejection: compiler predicted BODY cycles "
                    f"{assessment['predicted_body_cycles']} exceed the 2048-cycle Timer0 overflow interval; "
                    "no Avrora SRAM readback is claimed"
                )
                pair["status"] = "rejected"
                pair["rejection_reason"] = reason
                pair["static_deadline_evidence"] = assessment
                mark_rejected(matched, reason)
                pairs.append(pair)
                continue
            try:
                comparison = compare_steady_state_reports(
                    active_n["_report"], active_n["metadata"],
                    active_n1["_report"], active_n1["metadata"],
                    powersave_n["_report"], powersave_n["metadata"],
                    powersave_n1["_report"], powersave_n1["metadata"],
                )
            except ComparisonError as error:
                pair["rejection_reason"] = str(error)
                mark_rejected(matched, str(error))
            else:
                pair["status"] = "accepted"
                pair["comparison"] = comparison
                pair["comparison_method"] = "steady-state N+1 minus N deltas"
                for entry in matched:
                    entry["status"] = "accepted"
            pairs.append(pair)
    manifest["pairs"] = pairs


def validate_regenerated_tables(output_dir: Path, pairs: list[dict[str, Any]],
                                entries: list[dict[str, Any]]) -> None:
    """Regenerate table bytes in memory and require retained files to match."""
    primary_csv, primary_markdown = render_primary_tables(pairs)
    two_by_two_csv, two_by_two_markdown = render_two_by_two_tables(entries)
    expected = {
        "primary.csv": primary_csv,
        "primary.md": primary_markdown,
        "two_by_two.csv": two_by_two_csv,
        "two_by_two.md": two_by_two_markdown,
    }
    for name, content in expected.items():
        path = output_dir / name
        if not path.is_file() or path.read_text(encoding="utf-8") != content:
            raise ProvenanceError(f"retained {name} differs from in-memory regenerated results")


def reproduce_schema_v2(args: argparse.Namespace, manifest: dict[str, Any]) -> int:
    """Revalidate canonical raw artifacts without compiler, assembler, or Avrora calls."""
    output_dir = Path(args.output_dir).resolve()
    validate_recorded_inputs(manifest)
    primary_specs = build_specs(args.divisors, args.compute_paths, PRIMARY_COUNT)
    supplemental_specs = build_specs(args.divisors, args.compute_paths, SUPPLEMENTAL_COUNT)
    primary_by_id = {entry["id"]: entry for entry in manifest.get("primary_runs", [])}
    supplemental_by_id = {entry["id"]: entry for entry in manifest.get("supplemental_runs", [])}
    primary_entries = [
        rehydrate_recorded_observation(spec, primary_by_id[spec.stem], output_dir, manifest)
        for spec in primary_specs
    ]
    supplemental_entries = [
        rehydrate_recorded_observation(spec, supplemental_by_id[spec.stem], output_dir, manifest)
        for spec in supplemental_specs
    ]
    evaluate_steady_state_pairs(manifest, primary_entries + supplemental_entries,
                                args.divisors, args.compute_paths)
    validate_regenerated_tables(output_dir, manifest["pairs"], primary_entries + supplemental_entries)
    accepted_pairs = sum(pair["status"] == "accepted" for pair in manifest["pairs"])
    rejected_pairs = len(manifest["pairs"]) - accepted_pairs
    print(
        f"phase-b steady-state reproduction: {len(primary_entries)} retained primary runs, "
        f"{len(supplemental_entries)} retained supplemental runs, {accepted_pairs} accepted pairs, "
        f"{rejected_pairs} rejected pairs; no simulator executed and no files written"
    )
    rows = describe_tool_environment(manifest)
    identical = sum(1 for _, state in rows if state.startswith("byte-identical"))
    print("  toolchain vs. the recorded run:")
    for label, state in rows:
        print(f"    {label:9s} {state}")
    if identical != len(rows):
        print(
            "  note: this is an ARTIFACT-level reproduction. The retained assembly and\n"
            "        Avrora output were revalidated byte-for-byte and the result tables\n"
            "        re-derived from them, but the original toolchain was not re-executed,\n"
            "        so this does not by itself establish toolchain-level reproducibility."
        )
    return 0


def _load_experiment_inputs(args: argparse.Namespace) -> tuple[Path, Path, Path, Path, Path, list[int]]:
    output_dir = Path(args.output_dir).resolve()
    model = Path(args.model).resolve()
    golden_input = Path(args.input).resolve()
    golden_output = Path(args.golden_output).resolve()
    cost_table = Path(args.cost_table).resolve()
    for required in (model, golden_input, golden_output, cost_table):
        if not required.is_file():
            raise FileNotFoundError(required)
    return output_dir, model, golden_input, golden_output, cost_table, expected_output_from_fixture(golden_output)


def mode_reproduce_retained(args: argparse.Namespace) -> int:
    """Revalidate a canonical retained experiment. Runs no tool, writes no file."""
    output_dir, *_ = _load_experiment_inputs(args)
    manifest_path = output_dir / "manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(
            f"no manifest.json in {output_dir}: reproduce-retained revalidates an existing "
            "canonical experiment. To produce a new one, use: run-new --output-dir <fresh dir>"
        )
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != "phase-b-steady-state-v2":
        raise ProvenanceError(
            "only a canonical phase-b-steady-state-v2 manifest may be reproduced; "
            "refusing to rebuild legacy retained provenance"
        )
    return reproduce_schema_v2(args, manifest)


def _tool_versions_for(tools: ToolPaths) -> dict[str, Any]:
    return {
        "compiler": {
            "path": str(tools.compiler),
            "sha256": sha256_file(tools.compiler),
            "version": "identified by executable SHA-256; compiler has no version CLI",
        },
        "avr_gcc": tool_record(tools.avr_gcc, [str(tools.avr_gcc), "--version"]),
        "java": tool_record(tools.java, [str(tools.java), "-version"]),
        "avrora": {"path": str(tools.avrora_jar), "sha256": sha256_file(tools.avrora_jar)},
    }


def mode_run_new(args: argparse.Namespace) -> int:
    """Run a complete new experiment with the CURRENT toolchain, into a fresh directory.

    Deliberately refuses a directory that already holds a manifest.  A new
    experiment and a retained canonical one are different things, and writing
    one on top of the other would destroy the provenance that makes the
    retained one worth keeping.  The manifest this writes records the hashes
    of the tools that actually ran here, so it never claims to reproduce
    anything -- it is its own experiment, comparable to the canonical one but
    not a substitute for it.
    """
    output_dir, model, golden_input, golden_output, cost_table, output = _load_experiment_inputs(args)
    manifest_path = output_dir / "manifest.json"
    if manifest_path.is_file():
        raise ProvenanceError(
            f"{manifest_path} already exists: run-new refuses to overwrite an existing "
            "experiment. Choose a fresh --output-dir, or use reproduce-retained to "
            "revalidate what is already there."
        )
    output_dir.mkdir(parents=True, exist_ok=True)
    tools = resolve_tools(args)
    tool_versions = _tool_versions_for(tools)
    inputs = input_records(model, golden_input, golden_output, cost_table)
    hashes = input_hashes(inputs)

    primary_specs = build_specs(args.divisors, args.compute_paths, PRIMARY_COUNT)
    supplemental_specs = build_specs(args.divisors, args.compute_paths, SUPPLEMENTAL_COUNT)
    primary_entries = [
        supplemental_entry(spec, output_dir, tools, model, cost_table, golden_input, output,
                           tool_versions, hashes, run_set="primary")
        for spec in primary_specs
    ]
    manifest: dict[str, Any] = {
        "schema": "phase-b-steady-state-v2",
        "generated_at": utc_now(),
        "provenance_kind": "new-experiment",
        "timeout_seconds": TIMEOUT_SECONDS,
        "target": "atmega128",
        "primary_count": PRIMARY_COUNT,
        "supplemental_count": SUPPLEMENTAL_COUNT,
        "expected_output": output,
        "evidence_limit": TERMINAL_EVIDENCE,
        "inputs": inputs,
        "tools": tool_versions,
        "primary_runs": primary_entries,
        "supplemental_runs": [],
        "pairs": [],
        "diagnostics": diagnostics_record(output_dir),
    }
    return _finish_executing_sweep(args, manifest, manifest_path, output_dir, tools, model,
                                   cost_table, golden_input, output, tool_versions, hashes,
                                   primary_entries, supplemental_specs, "new sweep",
                                   primaries_need_avrora=True)


def mode_supplemental(args: argparse.Namespace) -> int:
    """Add the count-5 sweep on top of an existing count-4 manifest in the same directory."""
    output_dir, model, golden_input, golden_output, cost_table, output = _load_experiment_inputs(args)
    manifest_path = output_dir / "manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(
            f"no manifest.json in {output_dir}: the count-5 supplemental sweep extends an "
            "existing count-4 experiment. Produce one first with: run-new --output-dir <fresh dir>"
        )
    if output_dir == (ROOT / "sim" / "fixtures" / "phase_b").resolve():
        raise ProvenanceError(
            "refusing to run the supplemental sweep into the canonical fixture directory; "
            "it would overwrite retained artifacts. Copy them to a scratch directory first."
        )
    retained_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    validate_recorded_inputs(retained_manifest)
    require_recorded_tools_present(retained_manifest)
    tools = resolve_tools(args)
    tool_versions = _tool_versions_for(tools)
    inputs = input_records(model, golden_input, golden_output, cost_table)
    hashes = input_hashes(inputs)
    retained_by_id = {entry["id"]: entry for entry in retained_manifest.get("runs", [])
                      or retained_manifest.get("primary_runs", [])}

    primary_specs = build_specs(args.divisors, args.compute_paths, PRIMARY_COUNT)
    supplemental_specs = build_specs(args.divisors, args.compute_paths, SUPPLEMENTAL_COUNT)
    primary_entries = [
        primary_entry_from_retained(spec, retained_by_id[spec.stem], output_dir, output,
                                    tool_versions, hashes)
        for spec in primary_specs
    ]
    manifest: dict[str, Any] = {
        "schema": "phase-b-steady-state-v2",
        "generated_at": utc_now(),
        "provenance_kind": "supplemental-extension",
        "timeout_seconds": TIMEOUT_SECONDS,
        "target": "atmega128",
        "primary_count": PRIMARY_COUNT,
        "supplemental_count": SUPPLEMENTAL_COUNT,
        "expected_output": output,
        "evidence_limit": TERMINAL_EVIDENCE,
        "inputs": inputs,
        "tools": tool_versions,
        "primary_runs": primary_entries,
        "supplemental_runs": [],
        "pairs": [],
        "diagnostics": diagnostics_record(output_dir),
    }
    return _finish_executing_sweep(args, manifest, manifest_path, output_dir, tools, model,
                                   cost_table, golden_input, output, tool_versions, hashes,
                                   primary_entries, supplemental_specs, "supplemental sweep")


def _finish_executing_sweep(args: argparse.Namespace, manifest: dict[str, Any],
                            manifest_path: Path, output_dir: Path, tools: ToolPaths,
                            model: Path, cost_table: Path, golden_input: Path,
                            output: list[int], tool_versions: dict[str, Any],
                            hashes: dict[str, str], primary_entries: list[dict[str, Any]],
                            supplemental_specs: list[RunSpec], label: str,
                            primaries_need_avrora: bool = False) -> int:
    """Execute the count-5 sweep, pair it against the count-4 runs, and write results.

    Shared by run-new and supplemental: both end with the same work once their
    primary (count-4) entries exist, whether those were just executed or read
    from a retained manifest.  This body was previously unreachable -- it sat
    after an unconditional ``raise`` in ``run_sweep`` -- so no mode could
    generate new data at all.
    """
    supplemental_entries = [
        supplemental_entry(spec, output_dir, tools, model, cost_table, golden_input, output,
                           tool_versions, hashes)
        for spec in supplemental_specs
    ]
    manifest["supplemental_runs"] = supplemental_entries

    observations = _entry_map(primary_entries + supplemental_entries)
    for divisor in args.divisors:
        for compute_path in args.compute_paths:
            matched = [
                observations[(divisor, compute_path, policy, count)]
                for policy in POLICIES
                for count in (PRIMARY_COUNT, SUPPLEMENTAL_COUNT)
            ]
            pair: dict[str, Any] = {
                "divisor": divisor,
                "compute_path": compute_path,
                "counts": [PRIMARY_COUNT, SUPPLEMENTAL_COUNT],
                "status": "rejected",
            }
            if any(entry["status"] not in ("reported", "compiled") for entry in matched):
                pair["rejection_reason"] = "all four N/N+1 observations must compile before matched BODY validation"
                mark_rejected(matched, pair["rejection_reason"])
            else:
                body_hashes = {entry["body_sha256"] for entry in matched}
                if len(body_hashes) != 1:
                    pair["rejection_reason"] = "four matched active/Power-save N/N+1 BODY hashes differ before Avrora"
                    mark_rejected(matched, pair["rejection_reason"])
                else:
                    pair["body_sha256"] = body_hashes.pop()
                    pair["status"] = "ready_for_avrora"
            manifest["pairs"].append(pair)
    write_json(manifest_path, manifest)

    # run-new compiles its own count-4 entries, so they still need simulating;
    # supplemental takes them from a retained manifest that already carries
    # their reports, and must not re-run them.
    pending = (primary_entries + supplemental_entries) if primaries_need_avrora else supplemental_entries
    for entry in pending:
        run_supplemental_avrora(entry, output_dir, tools, manifest["tools"])
    write_json(manifest_path, manifest)

    for pair in manifest["pairs"]:
        matched = [
            observations[(pair["divisor"], pair["compute_path"], policy, count)]
            for policy in POLICIES
            for count in (PRIMARY_COUNT, SUPPLEMENTAL_COUNT)
        ]
        if pair["status"] != "ready_for_avrora":
            continue
        if any(entry["status"] != "reported" for entry in matched):
            pair["status"] = "rejected"
            pair["rejection_reason"] = "all four N/N+1 observations require successful Avrora reports"
            mark_rejected(matched, pair["rejection_reason"])
            continue
        if pair["divisor"] == 8:
            predicted = max(entry["metadata_evidence"]["predicted_inference_cycles"] for entry in matched)
            reason = (
                f"static deadline rejection: compiler predicted BODY cycles {predicted} exceed "
                "the 2048-cycle Timer0 overflow interval; no Avrora SRAM readback is claimed"
            )
            pair["status"] = "rejected"
            pair["rejection_reason"] = reason
            mark_rejected(matched, reason)
            continue
        active_n = observations[(pair["divisor"], pair["compute_path"], "active", PRIMARY_COUNT)]
        active_n1 = observations[(pair["divisor"], pair["compute_path"], "active", SUPPLEMENTAL_COUNT)]
        powersave_n = observations[(pair["divisor"], pair["compute_path"], "powersave", PRIMARY_COUNT)]
        powersave_n1 = observations[(pair["divisor"], pair["compute_path"], "powersave", SUPPLEMENTAL_COUNT)]
        try:
            comparison = compare_steady_state_reports(
                active_n["_report"], active_n["metadata"],
                active_n1["_report"], active_n1["metadata"],
                powersave_n["_report"], powersave_n["metadata"],
                powersave_n1["_report"], powersave_n1["metadata"],
            )
        except ComparisonError as error:
            pair["status"] = "rejected"
            pair["rejection_reason"] = str(error)
            mark_rejected(matched, str(error))
        else:
            pair["status"] = "accepted"
            pair["comparison"] = comparison
            pair["comparison_method"] = "steady-state N+1 minus N deltas"
            for entry in matched:
                entry["status"] = "accepted"

    manifest["tables"] = {}
    manifest["tables"].update(write_primary_tables(output_dir, manifest["pairs"]))
    manifest["tables"].update(write_two_by_two_tables(output_dir, primary_entries + supplemental_entries))
    for entry in primary_entries + supplemental_entries:
        entry.pop("_assembly", None)
        entry.pop("_report", None)
    for pair in manifest["pairs"]:
        comparison = pair.get("comparison")
        if comparison is not None:
            pair["comparison"] = dict(zip(CSV_COLUMNS, comparison.values()))
    write_json(manifest_path, manifest)
    accepted_pairs = sum(pair["status"] == "accepted" for pair in manifest["pairs"])
    rejected_pairs = len(manifest["pairs"]) - accepted_pairs
    print(
        f"phase-b steady-state {label}: {len(primary_entries)} primary runs, "
        f"{len(supplemental_entries)} supplemental runs, {accepted_pairs} accepted pairs, "
        f"{rejected_pairs} rejected pairs -> {manifest_path}"
    )
    return 0

MODES = {
    "reproduce-retained": mode_reproduce_retained,
    "run-new": mode_run_new,
    "supplemental": mode_supplemental,
}


def parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "modes:\n"
            "  reproduce-retained  (default) revalidate a canonical retained experiment from its\n"
            "                      raw artifacts. Starts no tool and writes no file. Reports\n"
            "                      whether this machine's toolchain matches the recorded one,\n"
            "                      but does not require it to.\n"
            "  run-new             run a complete new experiment with the CURRENT toolchain into\n"
            "                      a fresh --output-dir. Refuses a directory that already holds a\n"
            "                      manifest. Records the hashes of the tools that actually ran.\n"
            "  supplemental        extend an existing count-4 experiment with the count-5 sweep,\n"
            "                      in the same directory. Requires the recorded toolchain.\n"
        ),
    )
    parser.add_argument("mode", nargs="?", default="reproduce-retained", choices=sorted(MODES),
                        help="what to do (default: reproduce-retained)")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the count-4 compiler commands and exit; starts no tool")
    parser.add_argument("--dry-run-supplemental", action="store_true",
                        help="print the count-5 compiler commands and exit; starts no tool")
    parser.add_argument("--compiler")
    parser.add_argument("--avr-gcc")
    parser.add_argument("--java")
    parser.add_argument("--avrora-jar")
    parser.add_argument("--model", type=Path, default=ROOT / "models" / "tiny_classifier.onnx")
    parser.add_argument("--input", type=Path, default=ROOT / "models" / "tiny_classifier_golden_input.txt")
    parser.add_argument("--golden-output", type=Path, default=ROOT / "models" / "tiny_classifier_golden_output.txt")
    parser.add_argument("--cost-table", type=Path, default=ROOT / "cost_table.toml")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "sim" / "fixtures" / "phase_b")
    parser.add_argument("--divisors", nargs="+", type=int, choices=DIVISORS, default=list(DIVISORS))
    parser.add_argument("--compute-paths", nargs="+", choices=COMPUTE_PATHS, default=list(COMPUTE_PATHS))
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    if args.dry_run or args.dry_run_supplemental:
        count = SUPPLEMENTAL_COUNT if args.dry_run_supplemental else PRIMARY_COUNT
        specs = build_specs(args.divisors, args.compute_paths, count)
        for spec in specs:
            assembly = Path(args.output_dir) / f"{spec.stem}.S"
            command = compiler_command(spec, args.compiler or "optifine.exe", Path(args.model), Path(args.cost_table), Path(args.input), assembly)
            print(json.dumps(command))
        return 0
    try:
        return MODES[args.mode](args)
    except (FileNotFoundError, ToolDiscoveryError, ValueError) as error:
        print(f"run_phase_b: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
