#!/usr/bin/env bash
# Sets up the AVR/Avrora toolchain on Linux. Idempotent -- safe to re-run.
#
# Tested versions: Avrora Beta 1.7.115, Temurin JDK 8u504-b01, avr-gcc 16.1.0
# (any avr-gcc that assembles for atmega128 should do: the compiler emits the
# assembly, avr-gcc only assembles and links it).
#
# What this does NOT need root for (downloaded into tools/, gitignored):
#   - avrora.jar (Avrora Beta 1.7.115, exact version pinned -- REPORT.md's
#     and SOURCES.md's decompiled power-model constants are specific to
#     this build; a newer 1.7.11[67] jar is a different, unverified model).
#   - a JDK 8 JVM (Avrora 1.7.115 references the long-removed
#     java.lang.Compiler class and crashes with NoClassDefFoundError on
#     JDK 9+, see sim/run_avrora.sh) -- vendored so this doesn't depend on
#     whatever `java` the host has on PATH.
#
# What this DOES need root for, and why it is not vendored here: avr-gcc's
# own linker (collect2) is built with a hardcoded --with-ld=/usr/bin/avr-ld
# on at least the Arch `extra/avr-gcc` package, and does not honor -B,
# COMPILER_PATH, or PATH overrides for that lookup (confirmed empirically
# in this project -- a relocated copy links avr-as fine but collect2 still
# reports "cannot find 'ld'"), so a system package is used instead.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="$ROOT/tools"
mkdir -p "$TOOLS"

echo "== avr-gcc / avr-libc / avr-binutils =="
if command -v avr-gcc >/dev/null 2>&1; then
    echo "found: $(command -v avr-gcc) ($(avr-gcc --version | head -1))"
else
    echo "NOT FOUND. This needs a real system package (see header comment"
    echo "for why a vendored copy doesn't work). Install one of:"
    echo "  Arch/Manjaro : sudo pacman -S avr-gcc avr-binutils avr-libc"
    echo "  Debian/Ubuntu: sudo apt install gcc-avr avr-libc binutils-avr"
    echo "  Fedora       : sudo dnf install avr-gcc avr-libc avr-binutils"
    echo "Or set AVR_GCC to an existing avr-gcc binary's path."
fi

echo
echo "== JDK 8 (required specifically; see sim/run_avrora.sh) =="
JDK8_DIR="$(find "$TOOLS" -maxdepth 1 -name 'jdk8*' -type d 2>/dev/null | sort | tail -1)"
if [ -n "$JDK8_DIR" ] && [ -x "$JDK8_DIR/bin/java" ]; then
    echo "found: $JDK8_DIR/bin/java ($("$JDK8_DIR/bin/java" -version 2>&1 | head -1))"
else
    echo "downloading Temurin JDK 8 (jdk8u504-b01, linux x64)..."
    URL="https://github.com/adoptium/temurin8-binaries/releases/download/jdk8u504-b01/OpenJDK8U-jdk_x64_linux_hotspot_8u504b01.tar.gz"
    TMP="$TOOLS/jdk8.tar.gz"
    curl -fL --max-time 300 -o "$TMP" "$URL"
    tar xzf "$TMP" -C "$TOOLS"
    rm -f "$TMP"
    JDK8_DIR="$(find "$TOOLS" -maxdepth 1 -name 'jdk8*' -type d | sort | tail -1)"
    echo "installed: $JDK8_DIR"
fi

echo
echo "== avrora.jar (Beta 1.7.115, exact version pinned) =="
if [ -f "$TOOLS/avrora.jar" ]; then
    echo "found: $TOOLS/avrora.jar"
else
    echo "downloading avrora-beta-1.7.115.jar from SourceForge..."
    curl -fL --max-time 120 -o "$TOOLS/avrora.jar" \
        "https://sourceforge.net/projects/avrora/files/avrora-beta-1.7.115.jar/download"
    echo "installed: $TOOLS/avrora.jar"
fi
# The jar this project's energy constants and retained evidence were
# produced with (sim/fixtures/*/manifest.json record the same digest).
AVRORA_SHA256="016021f49d922e73df4d295c05d11fb6aeabdce11974581caadf65387eb1e3eb"
if echo "$AVRORA_SHA256  $TOOLS/avrora.jar" | sha256sum -c --status; then
    echo "sha256 ok: $AVRORA_SHA256"
else
    echo "WARNING: tools/avrora.jar does not have the expected SHA-256" \
         "($AVRORA_SHA256); it is not the Beta 1.7.115 build this project's" \
         "energy constants were calibrated against." >&2
fi

echo
echo "== summary =="
echo "AVR_GCC     : $(command -v avr-gcc || echo 'not found -- install per above, or set AVR_GCC')"
echo "JAVA8_BIN   : $JDK8_DIR/bin/java"
echo "AVRORA_JAR  : $TOOLS/avrora.jar"
echo
echo "sim/run_avrora.sh and the sim/run_*.py scripts auto-discover these three" \
     "under tools/ and via PATH; override any of them with the AVR_GCC," \
     "JAVA8_BIN, or AVRORA_JAR environment variables."
