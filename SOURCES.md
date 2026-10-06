# Sources

Every entry in `cost_table.toml` must be justified by an entry here before
it is treated as real. As of the cost-table sourcing pass no `PLACEHOLDER`
entries remain: all 9 table entries carry an Avrora-derived figure and a
citation. No physically-measured numbers are
used anywhere in this project -- only cited published figures (datasheet,
paper) or Avrora's simulated energy monitor.

## Methodology

No source available to this project -- not the official AVR datasheets
(which publish only aggregate Active/Idle/Power-down current vs. clock
frequency, never a per-instruction table), not JouleTrack (a well-cited
instruction-level energy paper; found per-instruction current variance
small relative to baseline active current on the processors it studied),
and not Avrora's own energy monitor (confirmed empirically: `nop`x4 and
`ldi`/`add`/`mov` at equal cycle counts produced bit-identical simulated
energy) -- supports differentiating energy by instruction *type* within
CPU active mode. Every `energy_nj` figure below is therefore:

```
energy_nj = cycles(instruction) x per_cycle_energy_nj
```

where `per_cycle_energy_nj` is a single constant calibrated to Avrora's
own ATmega128 power model (see "Per-cycle energy constant" below -- an
earlier revision of this file derived it from a datasheet figure instead,
and that is superseded), and `cycles(instruction)` comes from the AVR Instruction
Set Manual. This still captures a real physical effect (LD_SRAM/ST_SRAM/MUL
cost 2x a register op because they take 2x as many cycles), just not via
per-opcode current variation, because no cited source establishes that
variation exists for this MCU class.

### Per-cycle energy constant

**Superseded datasheet constant:** an earlier version of this file derived
the per-cycle constant from the ATmega128 datasheet's Active 8MHz/5V row
(17mA typical, giving 10.625 nJ/cycle). The first real end-to-end run of the
naive classifier through Avrora showed predicted and real *cycle* counts
matching almost exactly (6129 predicted vs. 6130 simulated -- the +1 is the
fixed harness overhead of the final `break`, see
`sim/fixtures/active_ml/smoke.avrora.txt`), but
predicted and real *energy* diverging by a factor of ~3.75x (65,120.625 nJ
predicted vs. 17,393.951625 nJ simulated). That ruled out a bug in cycle
accounting and pointed at the per-cycle constant itself.

Decompiling `avrora.jar`'s `avrora.sim.mcu.ATMega128.class` (via `jar xf`
+ `javap -v`, both part of the JDK 8 already used to run Avrora)
found the constant pool holds seven `double` current values for the
ATmega128's power states; the largest (0.0075667, i.e. 7.5667mA) is Active
mode. `avrora.sim.energy.Energy.class`'s constant pool holds `3.0d`, the
assumed supply voltage. `3.0V x 0.0075667A / 8,000,000 Hz = 2.8375125E-9 J
= 2.8375 nJ/cycle` (rounded) matches Avrora's real simulated energy
divided by real simulated cycles to full floating-point precision, on two
independently-generated programs (`sim/smoke.s`: 22.7001 nJ / 8 cycles
= 2.8375125; the naive classifier, `sim/fixtures/active_ml/naive.s`:
17393.951625 nJ / 6130 cycles = 2.8375125000000003).

- **Source:** Avrora Beta 1.7.115, `avrora.sim.mcu.ATMega128.class` and
  `avrora.sim.energy.Energy.class` (decompiled bytecode constant pool;
  `avrora.jar` ships no readable source or resource file with these
  values, only compiled `.class` files -- see reproduction steps below).
- **Why this constant instead of the datasheet's:** Avrora is this
  project's simulation reference -- every
  reported cycle/energy number in REPORT.md ultimately comes from Avrora's
  simulation, not from the compiler's own predicted numbers. Calibrating
  `cost_table.toml` to Avrora's own internal assumption makes the
  compiler's predicted energy match its own benchmark by construction,
  which matters for the compiler's *own* candidate-selection logic
  (`select_min_energy`) to be evaluating candidates on the same basis
  Avrora will ultimately score them on. The datasheet's 5V/17mA figure
  remains a real, correctly-cited number -- it just describes a different
  operating point (5V, matching a specific typical-application row) than
  what Avrora's built-in model assumes (3.0V, an ATmega128L-range supply
  voltage), and this project needs internal consistency with its own
  benchmark more than it needs datasheet fidelity for an operating point
  Avrora doesn't actually simulate.
- **Reproduction:** `cd <tmpdir> && jar xf <path-to-avrora.jar>
  avrora/sim/mcu/ATMega128.class avrora/sim/energy/Energy.class &&
  javap -v avrora/sim/mcu/ATMega128.class | grep "= Double"` (lists the
  seven current constants) `&& javap -v avrora/sim/energy/Energy.class |
  grep "= Double"` (lists the voltage constant, `3.0d`, among others).
  Requires a JDK's `jar`/`javap` tools (the JDK 8 `sim/run_avrora.sh`
  already requires has both).
- **Derivation:** `energy_per_cycle = V x I / f = 3.0V x 0.0075667A /
  8,000,000 Hz = 2.8375125E-9 J = 2.8375 nJ` (rounded to 4 significant
  figures, matching this project's existing rounding convention).

### Cycle counts

- **Source:** AVR Instruction Set Manual, Microchip document
  DS40002198B (2021), AVRe core column (covers ATmega128).
  https://ww1.microchip.com/downloads/en/DeviceDoc/AVR-InstructionSet-Manual-DS40002198.pdf
  - ADD (section 6.2, "Cycles" table): 1 cycle
  - SUB (section 6.119): 1 cycle
  - MOV (section 6.75): 1 cycle
  - LDI (section 6.69): 1 cycle
  - MUL (section 6.77): 2 cycles
  - LDS -- used for LD_SRAM (section 6.70): 2 cycles
  - STS -- used for ST_SRAM (section 6.117): 2 cycles
  - FMULS -- used for FIXED_MUL_Q15 (section 6.56): 2 cycles
  - COMPLEX_ADD: not a single opcode -- a complex add is two real adds
    (re+re, im+im), i.e. 2x ADD = 2 cycles.

The compiler's cycle model is a separate per-opcode table from the same
manual, `kAvrCycles` in `compiler/src/codegen/cost_category.c`, covering
every opcode the compiler emits (conditional branches at their taken cost;
the loop pricing corrects each loop's one fall-through). Cycle counts come
only from that table and energy only from `cost_table.toml` (plus the
per-cycle constant for `lpm`, `rjmp` and `break`, which no table category
expresses); `test_pricing` checks the two agree under the current table and
that cycle counts do not move when energies do.

## Table

| Instruction | Cycles | Energy (nJ) | Source |
|---|---|---|---|
| ADD | 1 | 2.8375 | Avrora ATmega128 power model (3.0V, 7.5667mA) x AVR ISM section 6.2 |
| SUB | 1 | 2.8375 | Avrora ATmega128 power model x AVR ISM section 6.119 |
| MOV | 1 | 2.8375 | Avrora ATmega128 power model x AVR ISM section 6.75 |
| LDI | 1 | 2.8375 | Avrora ATmega128 power model x AVR ISM section 6.69 |
| MUL | 2 | 5.675 | Avrora ATmega128 power model x AVR ISM section 6.77 |
| LD_SRAM | 2 | 5.675 | Avrora ATmega128 power model x AVR ISM section 6.70 (LDS) |
| ST_SRAM | 2 | 5.675 | Avrora ATmega128 power model x AVR ISM section 6.117 (STS) |
| FIXED_MUL_Q15 | 2 | 5.675 | Avrora ATmega128 power model x AVR ISM section 6.56 (FMULS) -- DSP path |
| COMPLEX_ADD | 2 | 5.675 | Avrora ATmega128 power model x 2x AVR ISM section 6.2 (ADD) -- DSP path |

Superseded values (5V/17mA datasheet-derived, see above): ADD/SUB/MOV/LDI
= 10.625 nJ, MUL/LD_SRAM/ST_SRAM/FIXED_MUL_Q15/COMPLEX_ADD = 21.25 nJ.
