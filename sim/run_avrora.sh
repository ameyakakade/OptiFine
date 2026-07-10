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
# - Avrora 1.7.115 references the long-removed java.lang.Compiler class and
#   crashes with NoClassDefFoundError on JDK 9+. Needs a JDK 8 JVM -- point
#   JAVA_HOME at one (this repo was validated against Zulu 8).
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <in.s> [mcu] [avrora.jar path]" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

IN_S="$1"
MCU="${2:-atmega128}"

# 3rd positional arg > AVRORA_JAR env var > ./avrora.jar (legacy default,
# kept for continuity) > the tools/setup_linux.sh-vendored copy.
if [ "$#" -ge 3 ]; then
    AVRORA_JAR="$3"
elif [ -n "${AVRORA_JAR:-}" ]; then
    :
elif [ -f "avrora.jar" ]; then
    AVRORA_JAR="avrora.jar"
else
    AVRORA_JAR="$ROOT/tools/avrora.jar"
fi

# JAVA_HOME (documented, legacy) > JAVA8_BIN env var (matches
# run_phase_b.py) > tools/setup_linux.sh's vendored JDK 8 > `java` on
# PATH, which is only correct if it happens to be a JDK 8 (see the note
# above about NoClassDefFoundError on JDK 9+).
if [ -n "${JAVA_HOME:-}" ]; then
    JAVA_BIN="$JAVA_HOME/bin/java"
elif [ -n "${JAVA8_BIN:-}" ]; then
    JAVA_BIN="$JAVA8_BIN"
else
    VENDORED_JDK8="$(find "$ROOT/tools" -maxdepth 1 -name 'jdk8*' -type d 2>/dev/null | sort | tail -1)"
    # Guard on the find result being non-empty before appending -- an
    # empty match would otherwise collapse to the bare path "/bin/java",
    # which is a real file on some systems (e.g. /bin -> /usr/bin) and
    # could silently pass the executable check below with the wrong JDK,
    # reintroducing the JDK-9+ NoClassDefFoundError this is guarding
    # against.
    if [ -n "$VENDORED_JDK8" ] && [ -x "$VENDORED_JDK8/bin/java" ]; then
        JAVA_BIN="$VENDORED_JDK8/bin/java"
    else
        JAVA_BIN="java"
    fi
fi

BASENAME="$(basename "$IN_S" .s)"
OUT_DIR="$(dirname "$IN_S")"
ELF="$OUT_DIR/$BASENAME.elf"

AVR_GCC_BIN="${AVR_GCC:-avr-gcc}"

# Arch's avr-gcc package is built with --prefix=/usr --target=avr, which
# makes it look for avr-libc's headers/libs at /usr/avr/{include,lib} --
# but Arch's avr-libc package instead installs them at
# /usr/lib/avr/{include,lib}, with nothing bridging the two (confirmed:
# `avr-gcc -v` never lists /usr/lib/avr/include in its search path, and a
# plain compile fails with "avr/io.h: No such file or directory"). This
# is not specific to any file this project emits -- it reproduces on
# vanilla `#include <avr/io.h>`. Detect it and add the standard
# workaround only when needed, so a correctly-configured install (the
# common case on Debian/Fedora, or this same fix already applied
# upstream) gets no extra flags.
EXTRA_AVR_GCC_ARGS=()
if [ -d "/usr/lib/avr/include" ] && ! echo | "$AVR_GCC_BIN" -mmcu="$MCU" -E -xc - -include avr/io.h >/dev/null 2>&1; then
    EXTRA_AVR_GCC_ARGS=(-B/usr/lib/avr/lib -isystem /usr/lib/avr/include)
fi

"$AVR_GCC_BIN" -mmcu="$MCU" -nostartfiles "${EXTRA_AVR_GCC_ARGS[@]}" -o "$ELF" "$IN_S"

"$JAVA_BIN" -jar "$AVRORA_JAR" -monitors=energy -mcu="$MCU" "$ELF"
