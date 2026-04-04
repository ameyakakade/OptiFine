# OptiFine: Energy-Aware Code Generation for Edge ML -- Report

Status: not yet written (milestone 6). This file is filled in after the
end-to-end comparison (milestone 5) produces real numbers.

## Methodology

TODO: describe the pipeline (ONNX -> IR -> candidate generation ->
energy-cost selection -> AVR assembly -> Avrora simulation), and state
plainly that every energy figure is either a cited published number or an
Avrora simulation output -- never a physical measurement.

## Results

TODO: energy comparison between the speed-optimized and energy-aware builds
of the demo model, both run through the same Avrora path.

## Limitations

TODO: state plainly what this project does not prove (see spec section 9 --
non-goals: no physical hardware, no claim of beating a production ML
compiler, no multi-ISA support, limited operator coverage).
