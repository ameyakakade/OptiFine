# Phase B design: compiler-directed duty-cycle scheduling

Date: 2026-08-28

## Purpose

Phase B explores whether compiler-directed sleep scheduling can reduce the
total energy of a periodic inference application beyond the savings already
obtained from instruction selection and register allocation. It treats this
as a system-level energy question, distinct from Phase A's compute-only
optimization.

The primary research question is:

> Can compiler-directed duty-cycle scheduling reduce total application
> energy while preserving inference output, invocation count, and timing
> requirements?

The experiment uses Power-save mode and the ATmega128 asynchronous Timer0.
This mechanism has already been validated in Avrora. Watchdog wake from
Power-down is excluded because Avrora 1.7.115 does not model the watchdog and
the corresponding feasibility spike never wakes.

## Hypotheses

H1. For periodic workloads with slack between inference deadlines, replacing
active waiting with Power-save reduces total energy without changing the
inference result or number of completed inferences.

H2. Savings increase as the idle fraction of each period increases.

H3. Phase A compute optimization and Phase B sleep scheduling are additive:
instruction optimization reduces energy while inference executes, whereas
sleep scheduling reduces energy between executions.

H4. At sufficiently short periods, sleep management overhead can eliminate
or reverse the benefit. The experiment must measure this crossover instead
of assuming that sleep always wins.

## Compared variants

The primary comparison has two variants:

| Variant | Inference implementation | Between invocations |
|---|---|---|
| Active baseline | Phase A optimized classifier | Remain Active until the same asynchronous Timer0 event |
| Sleep-aware | Identical classifier | Enter Power-save and wake on asynchronous Timer0 |

Both variants use the same generated classifier body, model parameters,
input tensor, timer configuration, number of periods, interrupt service
routine, and termination condition. The only intended difference is the
processor state during the waiting interval.

A secondary 2x2 experiment separates and combines the two optimization axes:

| Compute path | Waiting policy |
|---|---|
| Naive Phase A codegen | Active wait |
| Optimized Phase A codegen | Active wait |
| Naive Phase A codegen | Power-save |
| Optimized Phase A codegen | Power-save |

The primary matched-pair comparison is required first. The 2x2 experiment is
run afterward to test additivity and avoid mixing two causal changes in the
main result.

## Experimental controls

Each matched run must satisfy all of the following:

- same ATmega128 target, clock, supply assumptions, and Avrora version;
- same asynchronous Timer0 configuration and period;
- same number of timer events and classifier invocations;
- same model, constants, input bytes, and output address;
- same ISR bookkeeping and stop condition;
- bit-identical final classifier output;
- no missed or overlapping inference deadlines;
- initialization energy reported separately from the repeated steady-state
  window.

The active baseline must wait for the timer interrupt rather than use an
independently calibrated software delay. This keeps the wake cadence and
interrupt overhead identical and isolates the energy effect of selecting
Active versus Power-save during the same wait interval.

## Duty-cycle sweep

One convenient period is not sufficient evidence. Runs will cover at least
four period settings:

1. near-saturation: minimal positive slack after inference;
2. short slack;
3. medium slack;
4. long slack.

Exact timer settings will be selected after measuring the generated
classifier's execution time and must be realizable by asynchronous Timer0.
Each setting is expressed in both timer configuration and effective CPU
cycles. The sweep must include any observed crossover where sleep overhead
outweighs sleep-state savings.

## Generated-program architecture

The implementation adds a periodic execution wrapper around an unchanged
generated classifier body:

1. initialize SRAM constants, input, counter, and asynchronous Timer0;
2. enable the Timer0 overflow interrupt;
3. wait for each timer event using the selected waiting policy;
4. invoke the generated classifier exactly once;
5. preserve or record the output and increment the completed-inference count;
6. repeat until the configured count is reached;
7. execute `break` so Avrora produces a finite report.

The wrapper must keep sleep policy separate from classifier lowering. This
allows the same classifier assembly to be embedded in both variants and
prevents Phase B from silently becoming another instruction-selection
comparison.

The existing validated spike is retained as mechanism evidence. Its filename
will be corrected from `spike_powersave_timer2` to
`spike_powersave_timer0`, because ATmega128 asynchronous operation belongs
to Timer0.

## Metrics and analysis

For every run, record:

- total simulated cycles;
- Active, Idle, and Power-save cycles;
- energy by CPU state and total energy;
- initialization energy and repeated-window energy where separable;
- completed inference count;
- energy per inference;
- inference execution time and available slack;
- final output bytes and correctness result;
- absolute and percentage energy difference from the matched baseline.

The principal effect size is:

`saving (%) = 100 * (E_active_wait - E_power_save) / E_active_wait`

Energy per inference is reported alongside total energy so runs with
different observation lengths are not compared incorrectly. Phase A's 1.83%
compute-energy saving is kept separate from Phase B's duty-cycle result, then
the 2x2 experiment reports their combined effect.

## Validation and failure handling

Before collecting energy results:

1. verify that both programs assemble and terminate in Avrora;
2. verify equal timer-event and inference counts;
3. verify bit-identical outputs;
4. inspect disassembly to confirm the classifier body is unchanged;
5. confirm that only the sleep-aware run accumulates Power-save cycles;
6. reject any run with a missed deadline, overlapping inference, unexpected
   reset, or nonterminating simulation.

The compiler must reject a requested period that cannot accommodate one
inference plus interrupt and scheduling overhead. It must not report an
energy result for an invalid real-time schedule.

## Research-paper gap closure

Phase B will add the following evidence to `REPORT.md` and the living spec:

- a precise distinction between compute-energy optimization and duty-cycle
  scheduling;
- a causal matched-workload baseline instead of comparing programs with
  different work or timing;
- an empirical duty-cycle sweep rather than a single favorable operating
  point;
- output-equivalence and deadline checks as validity conditions;
- separate reporting of initialization and steady-state energy;
- a documented simulator limitation: Avrora's missing watchdog model rules
  out the originally considered Power-down/watchdog mechanism;
- a documented target-specific correction: ATmega128 uses asynchronous
  Timer0 for the validated Power-save wake path;
- simulator-to-hardware limitations, including that Avrora estimates energy
  from modeled power states and is not a substitute for physical-board power
  measurements;
- reproducible assembly, Avrora reports, parser outputs, timer settings, and
  formulas for every table in the paper.

Claims will be scoped accordingly: the result demonstrates energy savings in
a cycle-accurate ATmega128 simulation under a periodic workload model. It
does not claim universal savings, instruction-specific physical power
differences, or measured hardware energy.

## Deliverables and acceptance criteria

Deliverables:

- reusable periodic active-wait and Power-save wrappers;
- generated matched assembly fixtures and captured Avrora reports;
- a duty-cycle sweep result table;
- the 2x2 compute-policy/wait-policy comparison;
- updated `REPORT.md`, specification amendments, source citations, and
  reproduction commands.

Phase B is complete when:

- every matched pair performs identical useful work and produces identical
  output;
- at least four valid duty-cycle points have completed in Avrora;
- energy and timing metrics are reproducibly parsed from saved reports;
- the crossover or absence of a crossover is reported honestly;
- combined Phase A + Phase B behavior is measured;
- all methodological limitations and failed mechanism investigations are
  represented in the final paper trail.

