## DSP: complete 64-point Q15 pipeline

Derived from the raw files in `sim/fixtures/dsp` (captured by `sim/run_dsp.py`
from `optifine --dsp` on `models/dsp_demo_input.txt`). Energy is Avrora-model
**simulated** active energy at 8 MHz; time is cycles divided by that clock. Neither
is a physical measurement.

| metric | value |
|---|---:|
| Cycles (Avrora) | 148,335 |
| Cycles (compiler prediction) | 148,335 |
| Prediction error | +0 cycles |
| Time @ 8 MHz | 18.542 ms |
| Simulated active energy (Avrora) | 420,902.42 nJ (420.90 µJ) |
| Implied Active cost | 2.8375125 nJ/cycle |
| Linked flash (.text + .data) | 12,800 B (9.8% of 128 KiB) |
| .text / .data / .bss | 12,800 / 0 / 0 B |
| SRAM (graph tensors + scratch) | 2,517 B (61.5% of 4,096 B, 1,579 B free) |

The compiler's own estimate, 420,900.562 nJ, prices the same cycles at
`cost_table.toml`'s rounded 2.8375 nJ/cycle; the table above uses Avrora's reported energy.

### Where the cycles and bytes go (compiler prediction, per op)

| part | cycles | % of cycles | code bytes |
|---|---:|---:|---:|
| Input/Const setup (clr r2, embedded input, window coefficients) | 769 | 0.52% | 1,538 |
| Window | 2,560 | 1.73% | 4,608 |
| BitReverse | 896 | 0.60% | 1,792 |
| FFT (x6 stages) | 73,512 | 49.56% | 4,230 |
| Magnitude | 51,590 | 34.78% | 246 |
| PeakExtract | 18,943 | 12.77% | 128 |
| Output | 64 | 0.04% | 128 |
| break (+ twiddle table data) | 1 | 0.00% | 130 |
| **total** | **148,335** | 100.00% | **12,800** |

The setup row is production code, not harness: the program embeds its 64 input
samples and 64 window coefficients at compile time, as the ML path embeds its
demo input. The break row's bytes include the 128-byte twiddle table after it.

### ML and DSP side by side

The two workloads compute different things, so this shows the backend's breadth,
not the relative efficiency of the two algorithms.

| | ML classifier (optimized) | DSP pipeline |
|---|---:|---:|
| Cycles | 6,018 | 148,335 |
| Time @ 8 MHz | 0.752 ms | 18.542 ms |
| Simulated active energy | 17,076.15 nJ | 420,902.42 nJ |
