# Literature Survey and Positioning

This document places OptiFine's two contributions against the prior work it
either **competes against** or **proving-correct**, and states precisely how
each comparison is drawn. It also records OptiFine's central contention with
the recent TinyML energy literature.

Reading notes:

- **"Competing against"** = prior work making a claim OptiFine could displace
  or is measured against on a comparable axis.
- **"Proving-correct"** = prior work whose result OptiFine's own experiment
  corroborates (confirms, rather than contradicts).
- Every absolute energy number cited is a published figure or an Avrora
  simulation output; the project makes no physical measurements (spec
  section 3).

---

## 1. The central contention: energy != latency once sleep states exist

### 1.1 The claim being contested

Heim, Biri, Qu and Thiele, *"Measuring what Really Matters: Optimizing Neural
Networks for TinyML"* (arXiv:2104.10645, 2021) measured inference on ARM
Cortex-M MCUs (STM32 L4/F4/F7, TensorFlow Lite Micro) with an external energy
monitor. Their headline empirical result is an almost perfect linear
correlation between inference latency and energy consumption:

- r = 0.9946 across optimizations at the whole-NN level (Figs. 3 and 6);
- r = 0.9995 at the individual-layer level (Fig. 7, Appendix B.2).

From this they conclude: *"the inference latency is a perfect proxy for the
energy consumption of the investigated MCUs"* and, because the "simple"
Cortex-M architecture "does not feature dynamic voltage scaling or
power-gated sub-components," *"all of the presented results regarding speedups
and ratios for latency also apply to energy consumption."*

### 1.2 Why the claim is only true in a single, continuously-active regime

The Heim et al. measurement holds **within one active-mode inference**. Their
benchmark runs inference back-to-back and correlates the energy of that active
burst with its latency. The linearity they observe is real and unsurprising
for a processor whose power draw is roughly constant per active cycle: if a
program always executes in Active mode, then

```
energy  =  E_active/cycle x cycles
```

and cycles is proportional to elapsed time (latency), so energy collapses to
latency times a constant. Their explicit scope note is the tell: they
attribute the linearity to the absence of DVS and power-gated components
*below the active state*. Their experiment never exercises a state below
Active, so their model has no term for one.

### 1.3 Where OptiFine breaks the linearity

The moment a sleep/stop instruction is inserted, the elapsed wall-clock
latency no longer tracks energy, because an idle cycle is not an Active
cycle. OptiFine's Phase B targets exactly this missing term: a compiler
inserts ATmega128 Power-save mode (asynchronous Timer0 wake) between
inference bursts.

On OptiFine's calibrated ATmega128 model (Avrora's own power values,
3.0 V / 7.5667 mA active, see `SOURCES.md`):

| state | energy per cycle (nJ) |
|---|---:|
| Active | 2.8375 |
| Power-save (sleep) | ~0.0464 |

The active-to-sleep ratio is ~**61.2x**, not 1x. Consequently the two
statements that are equivalent under Heim et al. become decoupled:

1. **Identical elapsed latency, arbitrary energy.** Two deployments with the
   same wall-clock duty cycle can differ in energy by changing only the
   sleep-state per-cycle cost (a chip property Heim et al. never vary).
2. **Identical active-cycle count, arbitrary energy.** Fixed compute with
   different idle fractions produces energy proportional to the *sum of
   active and (cheap) sleep cycles*:

```
saving%  ~=  (1 - c_sleep / c_active) x idle_fraction   ~=  0.984 x idle_fraction
```

Here `c_active = 2.8375 nJ/cycle`, `c_sleep = 0.0464 nJ/cycle`,
`idle_fraction` is the fraction of period spent outside BODY. OptiFine
measures up to **96.3% energy savings** at Timer0 prescaler 1024, i.e. at a
~98.3% idle fraction. This is a direct counter-example to "latency is a
perfect proxy for energy" as a **general statement over the MCU power-state
space**: the latency of the idle interval is unbounded while its energy cost
is bounded by the sleep-state current. Heim et al. measured only the active
subspace; OptiFine demonstrates the claim fails the moment the processor is
allowed to leave Active mode.

### 1.4 Why this is a fair, non-strawman contention

- OptiFine **accepts** the Heim et al. measured data (r ~ 0.995 within active
  mode) — it does not dispute correlation coefficients on Cortex-M.
- The contention is about the **generality** they assert ("perfect proxy for
  the investigated MCUs" / "results... also apply to energy"), not about
  their numbers.
- ATmega128 and nearly all MCUs expose multiple sleep modes (Idle,
  Power-down, Power-save, Standby), so a state below Active is the norm
  rather than a Cortex-M anomaly. Their architectural premise ("no
  power-gated sub-components") is not portable to the AVR class OptiFine
  targets.
- OptiFine does **not** contest the claim *within* active mode. Phase A is
  explicit corroborating evidence: Phase A's energy model reduces to
  `cycles x 2.8375 nJ`, so Phase A's energy selection is mathematically
  identical to cycle selection, and its measured gain (~1.83%, see
  `REPORT.md`) is a pure cycle reduction — exactly the "only reduce latency"
  lever Heim et al. predict is the only one available in active mode. Phase
  A **agrees with** Heim et al.; Phase B is where OptiFine **diverges**.

### 1.5 Honest caveats on the cross-platform comparison

- Phase A/B are validated in the **cycle-accurate Avrora simulator**, not on
  physical hardware (Phase B hardware correlation is deferred). Heim et al.
  measured on real boards with an external energy monitor. The savable,
  ratio-driven conclusions (ratio of active to sleep per-cycle cost; %
  savings vs. idle fraction; deadline feasibility) transfer; absolute µJ
  numbers do not (board quiescent current, regulator, leakage).
- The ~61x ratio and the ~0.984 slope are derived from Avrora's ATmega128
  power model, not from a datasheet or a measurement. They are internally
  consistent with Avrora (the project's simulation reference) but are a
  structural argument against the *generality* of Heim et al.'s claim, not a
  measurement that beats their measured energy numbers on Cortex-M.

---

## 2. Phase B: compiler-directed sleep scheduling — competitors

| Ref. | Contribution | Layer | Target | Guarantee | Validation | What to compare (axis) |
|---|---|---|---|---|---|---|
| **Huang & Ghiasi**, ACM TODAES 12(3) Art. 27, 2007 (doi:10.1145/1255456.1255464) | Compiler-inserted power-mode switches with **deadline guarantees** via static analysis | compiler | ARM (65 nm), DVS+ABB | deadline | analytic + sim | Mechanism identity (statically guaranteed mode switch minimizing energy); % saving (~33%) vs. OptiFine's duty-cycle saving |
| **Hsu & Kremer**, PLDI 2003 | Compiler DVS where slack exists | compiler | VLIW/RISC | — | analytic | "saving scales with slack/idle" — the analytic ancestor of `saving ~= idle_fraction` |
| **Wanner et al.**, DATE 2011 (doi:10.1109/DATE.2011.5763133) | Duty-cycling scheduling to save energy | **OS/RTOS** (TinyOS/FreeRTOS, real boards) | MSP430-class | — | hardware | Layer (OS vs. OptiFine's compiler); adaptive vs. OptiFine's static compile-time; 3–22x active-time reduction vs. OptiFine's % idle-fraction saving; sleep-cost-ratio insight |
| **Maeng & Lucia** (CatNap), PLDI 2020 | Feasibility/schedule validation for intermittent energy | scheduling | ARM/FRAM | feasibility test | hardware | Scheduling philosophy (feasibility check before run) vs. OptiFine's static deadline rejection |
| **AVR42787 / sleep-mode app notes** (Microchip/Atmel) | Sleep modes + RTC/asynchronous wake mechanism | firmware | AVR | — | hardware | Engineering baseline for the mechanism (must be cited as prior art) |

**Head-to-head recipe (per row).** Port each comparison onto the same axes —
target ISA, workload, metric (% saving / nJ / cycles), deadline guarantee,
feasibility method, sim-vs-hardware. Then either (a) reproduce the prior
technique's *scenario* on OptiFine's AVR toolchain, or (b) re-derive the
prior result's *analytic bound* (e.g. Hsu & Kremer's slack argument; Huang &
Ghiasi's mode-switch guarantee) in OptiFine's terms (the
`saving ~= 0.984 x idle_fraction` identity). Claim **mechanism/regime
novelty**, never "we beat X%" across a different ISA and a sim/hardware gap.

OptiFine's defensible position, stated without a superlative: it combines
and contrasts **two energy-aware compiler layers on one AVR backend** --
Active-mode machine-code optimisation (Phase A) and compiler-directed
low-power-state scheduling (Phase B) -- and reports the experimental
contrast between the two regimes. The contribution is that contrast and
the engineering integration, not the invention of energy-aware
compilation (Tiwari et al. 1994 onward) nor of sleep scheduling (Hsu &
Kremer 2003, Huang & Ghiasi 2007, Wanner et al. 2011), both of which
long predate this project.

Two claims this project must **not** make. First, that its saving is "the
largest" for compiler-directed sleep scheduling on AVR: the Phase B
percentage is duty-cycle-parameterized and approaches 100% as the wake
period grows, so a larger number is always purchasable by sleeping longer
and means nothing on its own. Second, that the ~61x per-cycle ratio is
*measured*: it is derived from Avrora's own ATmega128 power model (see
section 1.5), and no physical measurement exists anywhere in this
project.

---

## 3. Phase A: instruction-level energy selection — competitors

| Ref. | Contribution | Target | Reported result | Why it matters / what to compare |
|---|---|---|---|---|
| **Surakka et al.**, Proc. Estonian Acad. Sci. Eng. 11(4):347-357, 2005 (doi:10.3176/eng.2005.4.07) | Instruction/register-order selection for energy | **8-bit AVR (AT90S8515)** | ~0.5% savings | The only prior instruction-level-energy work on an AVR; closest head-to-head for Phase A. Reproduce their register-order experiment on ATmega128 and compare vs. OptiFine's ~1.83% on the same ISA family. |
| **Lee, Tiwari, Malik, Fujita**, IEEE TVLSI 5(3), 1997 (doi:10.1109/92.555992) | Instruction-selection-by-energy scheduling | DSP | 26–73% (scheduling) | The canonical "select instructions by energy" paper. Mechanism ancestor of Phase A, though on a DSP with more scheduling freedom. Corollary: per-ISA savings are not portable. |
| **Tiwari, Malik, Wolfe**, IEEE TVLSI 2(4), 1994 (doi:10.1109/92.335012) | Instruction-level power model | embedded RISC/CISC | — | **Proving-correct**: `cost_table.toml` and the whole methodology descend from this. OptiFine confirms (on AVR) that instruction *cycle count* dominates energy when no per-opcode current variation is available to cited sources. |
| **Pallister et al.**, arXiv:1303.6485 | GCC-flag energy search | ARM | best flag is not portable | Supports OptiFine's per-ISA calibration stance; argues against exporting savings numbers across toolchains. |

**Phase A is NOT a contention with Heim et al.** See §1.4: Phase A's energy
model is linear in cycles, so it agrees with their "energy is latency"
finding within active mode. Reporting Phase A as a Phase-A-vs-TinyML
"contention" would be technically false and is deliberately not made here.

---

## 4. Proving-correct references

| Ref. | Claim OptiFine corroborates |
|---|---|
| **Titzer, Lee, Palsberg**, IPSN 2005 (doi:10.1109/IPSN.2005.1440978) | Observes tiny networked-sensor programs sleep ~96–99% of the time — the *reason* Phase B's duty-cycle saving is large; OptiFine's 96.3% saving at prescaler 1024 matches this regime. |
| **Xie et al.**, PLDI 2008 | Analytic limit of compile-time mode scheduling — OptiFine's `saving ~= 0.984 x idle_fraction` sits within this bound. |
| **Tiwari et al.** 1994 (as above) | Per-instruction cycle-count energy methodology. |
| **Pallister et al.** 2013 (as above) | Savings are not portable across ISA/toolchain — OptiFine's refusal to extrapolate a single number across platforms follows this. |

---

## 5. Compact bibliography

1. L. Heim, A. Biri, Z. Qu, L. Thiele, "Measuring what Really Matters:
   Optimizing Neural Networks for TinyML," arXiv:2104.10645, 2021.
   https://doi.org/10.48550/arxiv.2104.10645
2. P.-K. Huang and S. Ghiasi, "Efficient and Scalable Compiler-Directed
   Energy Optimization for Realtime Applications," ACM TODAES 12(3),
   Article 27, 2007. doi:10.1145/1255456.1255464
3. C.-H. Hsu and U. Kremer, "The design, implementation, and evaluation of a
   compiler algorithm for CPU energy reduction," PLDI 2003.
4. L. Wanner et al., "Variability-aware duty cycle scheduling in low power
   embedded systems," DATE 2011. doi:10.1109/DATE.2011.5763133
5. K. Maeng and B. Lucia, "Adaptive low-overhead scheduling for periodic and
   reactive intermittent execution" (CatNap), PLDI 2020.
6. V. Tiwari, S. Malik, A. Wolfe, "Power analysis of embedded software: a
   first step towards software power minimization," IEEE TVLSI 2(4), 1994.
   doi:10.1109/92.335012
7. M. Lee, V. Tiwari, S. Malik, M. Fujita, "Power analysis and minimization
   techniques for embedded DSP software," IEEE TVLSI 5(3), 1997.
   doi:10.1109/92.555992
8. K. Surakka, T. Mikkonen, H.-M. Jarvinen, T. Vuorela, J. Vanhala,
   "Towards Compiler Backend Optimization for Low Energy Consumption at
   Instruction Level," Proceedings of the Estonian Academy of Sciences,
   Engineering, 11(4), 347-357, 2005. doi:10.3176/eng.2005.4.07
9. J. Pallister, S. Hollis, J. Bennett, "Identifying compiler options to
   minimize energy consumption for embedded platforms," arXiv:1303.6485,
   2013.
10. B. Titzer, D. Lee, J. Palsberg, "Avrora: scalable sensor network
    simulation with precise timing," IPSN 2005. doi:10.1109/IPSN.2005.1440978
11. Microchip, AVR Instruction Set Manual, DS40002198B (2021).
    https://ww1.microchip.com/downloads/en/DeviceDoc/AVR-InstructionSet-Manual-DS40002198.pdf
12. Microchip/Atmel, "Using the AVR's sleep modes" app notes (e.g. AVR42787);
    Atmel AVR sleep-mode RTC wake documentation.
13. Atmel/Microchip, "ATmega128/L 8-bit AVR Microcontroller with 128KBytes
    In-System Programmable Flash" device datasheet. The normative source
    for every device-specific mechanism Phase B depends on: Power-save
    sleep mode and the MCUCR SM2:0 encoding, Timer/Counter0 asynchronous
    operation via ASSR.AS0, the Timer0 overflow interrupt and its
    prescaler divisors, and the device's SRAM/flash sizes. Cited for
    *semantics*, not for energy: this project's per-cycle constants come
    from Avrora's model (SOURCES.md), not from the datasheet's electrical
    characteristics tables.
