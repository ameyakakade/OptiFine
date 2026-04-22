#!/usr/bin/env bash
# Assembles an emitted .s file and runs it through Avrora's energy monitor.
# Milestone 1: bring-up validated against vanilla Avrora Beta 1.7.115.
#
# Reality notes (found during milestone 1, keep in sync with REPORT.md):
# - Avrora 1.7.115 has no atmega328p/atmega2560 MCU classes; the closest
#   supported target is ATmega128 (`-mcu=atmega128`). We compile for and
#   simulate ATmega128 and state this deviation plainly in the report.
# - The energy monitor reports per-power-mode CPU energy in Joule, so the
#   parser (parse_report.py) converts to nJ.
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <in.s> [mcu] [avrora.jar path]" >&2
    exit 2
fi

IN_S="$1"
MCU="${2:-atmega128}"
AVRORA_JAR="${3:-avrora.jar}"
JAVA_BIN="${JAVA_HOME:+$JAVA_HOME/bin/java}"
JAVA_BIN="${JAVA_BIN:-java}"

BASENAME="$(basename "$IN_S" .s)"
OUT_DIR="$(dirname "$IN_S")"
ELF="$OUT_DIR/$BASENAME.elf"

avr-gcc -mmcu="$MCU" -nostartfiles -o "$ELF" "$IN_S"

"$JAVA_BIN" -jar "$AVRORA_JAR" -monitors=energy -mcu="$MCU" "$ELF"
