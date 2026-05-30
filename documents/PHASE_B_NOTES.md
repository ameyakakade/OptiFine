# Phase B research trail: sleep-mode scheduling

Referenced from `sim/fixtures/spike_powersave_timer0.S` ("see the plan file
for the full research trail"). This is that file.

## Why this phase is exploratory, not a fixed deliverable

Spec v2 section 12's "Scope for Phase 1" for sleep-mode scheduling
prescribed a specific mechanism up front: timer-driven wake, run one
inference pass, re-enter sleep, compare against a busy-wait baseline. The
first spike run against that plan (spike 2 below) falsified the specific
wake mechanism it assumed (watchdog-triggered wake) before any of it could
be built into the compiler. Phase B is therefore being run spike-first --
small standalone `.S` fixtures that ask Avrora one question each -- rather
than committing to codegen work against an unverified assumption about
what Avrora actually simulates. This mirrors the project's general
verification discipline (checked against real Avrora output, not assumed
from datasheet/general AVR knowledge), applied to the sleep-mode mechanism
itself before applying it to the compiler.

## Spike log

### Spike 1 -- `sim/fixtures/spike_sleep_isr.S` / `.avrora.txt`

Question: does Avrora simulate AVR interrupts and `sleep` at all, before
any of this is automated by codegen?

Mechanism: Idle mode (`SM2:0=000`), Timer0 fast overflow (clk/1), ISR
increments an SRAM counter, halts after 3 fires.

Result: **works.** Real event-driven wake from Idle via Timer0 overflow
ISR, real energy split by power mode:

| mode | cycles | energy (J) | nJ/cycle |
|---|---:|---:|---:|
| Active | 87 | 2.468635875e-7 | 2.8375 |
| Idle | 710 | 8.901536249999999e-7 | 1.2537 |

Active/Idle current ratio: ~2.26x. Active nJ/cycle here matches the
project's calibrated `cost_table.toml` constant (section 13 of the spec)
exactly, an independent internal-consistency check.

### Spike 2 -- `sim/fixtures/spike_powerdown_wdt.S` / `.avrora.txt`

Question: does Avrora simulate Power-down mode woken by a watchdog-timer
reset?

Mechanism: arm the watchdog (real two-write safety sequence), enter
Power-down (`SM2:0=010`), expect a watchdog-triggered chip reset to
re-enter `reset:` (ATmega128's watchdog is reset-only -- no WDIE/WDIF
interrupt mode exists on this part).

Result: **falsified -- Avrora has no watchdog timer model at all.**
Confirmed by direct observation, not inference: the simulation hangs
indefinitely (no event ever fires to end it). Captured by running it under
a 20s hard timeout -- process was still blocked in `sleep` with zero
simulation events logged when killed. `.avrora.txt` for this spike
therefore records a timeout, not a completed run; that absence of output
*is* the finding.

Consequence: any Phase B wake mechanism has to be something Avrora's
event queue actually models. Avrora also has no general external-interrupt
injection mechanism (there is no host-side way to raise an arbitrary IRQ
mid-simulation) -- ruling out both watchdog wake and "just inject a wake
event" as options.

### Spike 3 -- `sim/fixtures/spike_powersave_timer0.S` / `.avrora.txt`

Question: does Avrora simulate Power-save mode woken by an asynchronous
Timer0 (clocked from a decoupled external source via `ASSR`), the
standard AVR periodic-wake-from-deep-sleep pattern that doesn't depend on
a watchdog?

Mechanism: Timer0 async mode (`ASSR`'s `AS0`, with the required busy-flag
wait before relying on the new clock source), Power-save (`SM2:0=011`),
ISR increments an SRAM counter, halts after 3 fires. (ATmega128-specific
correction: it's Timer0, not Timer2, that has async capability on this
part, unlike ATmega328P -- confirmed via avr-libc's `iom128.h`, not
assumed.)

Result: **works.** Real Power-save time simulated, distinct from both
Active and Idle:

| mode | cycles | energy (J) | nJ/cycle |
|---|---:|---:|---:|
| Active | 88 | 2.497011e-7 | 2.8375 |
| Power-save | 2479 | 1.1499461249999999e-7 | 0.04640 |

Active/Power-save current ratio: ~61.2x -- same order of magnitude as the
~65x figure cited from the datasheet in spike 2's header, and far larger
than Idle's ~2.26x. Active nJ/cycle again matches the calibrated constant
exactly.

## Where this leaves Phase B

The viable mechanism, empirically: **Power-save + asynchronous Timer0**,
not the spec's original watchdog-based sketch. This is a real, evidenced
constraint from Avrora's actual simulated capabilities, not a simplifying
assumption chosen in advance.

Not yet done (deliberately not attempted under this directive -- these are
the next fork, not assumed):
- Wiring a sleep-aware build into the compiler/codegen path (currently
  these are standalone hand-written `.S` spikes, same status Phase A's
  hand-written ms1/ms2 fixtures had before codegen caught up to them).
- The spec's actual comparison target: sleep-aware vs. busy-wait baseline,
  over a simulated span of multiple wake cycles, running the real
  classifier inference per wake rather than just incrementing a counter.
- Updating `cost_table.toml`/the cost model to account for a second power
  state (Power-save) alongside Active, if sleep scheduling gets pulled
  into the cost-based `select()` path rather than staying a fixed program-
  structure choice.
