#!/usr/bin/env bash
# Assembles an emitted .s file and runs it through Avrora's energy monitor.
# Milestone 1: confirm avr-gcc and avrora.jar run locally before any
# project code exists (see spec section 7).
set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <in.s> <platform> [avrora.jar path]" >&2
    exit 2
fi

IN_S="$1"
PLATFORM="$2"
AVRORA_JAR="${3:-avrora.jar}"

BASENAME="$(basename "$IN_S" .s)"
OUT_DIR="$(dirname "$IN_S")"
ELF="$OUT_DIR/$BASENAME.elf"

avr-gcc -mmcu="$PLATFORM" -nostartfiles -o "$ELF" "$IN_S"

java -jar "$AVRORA_JAR" -monitors=energy -platform="$PLATFORM" "$ELF"
