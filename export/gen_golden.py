"""Independent numpy/onnx reference for the tiny_classifier.onnx pipeline,
used to golden-test compiler/src/codegen/lower.c's generated AVR code (via
compiler/tests/avr_interp.c + test_lower.c) against a second, independently
computed implementation -- not just re-running the same C code twice.

Reuses the same fixed-point rescale *algorithm* lower.c uses (16-bit Q15
multiplier + shift, derived via frexp/round rather than ldexp since Python
gives exact arbitrary-precision integers, so the multiply+shift itself
needs no byte-decomposition trick the way the C/AVR side does) -- this is
deliberate, not circular: matching the algorithm means this script tests
whether lower.c's *implementation* of that algorithm is correct (register
allocation, carry chains, sign extension), while the model/weights/scales
themselves come independently from the real .onnx file, not from lower.c.

models/tiny_classifier_golden_input.txt was chosen (see that file's header)
specifically because this exact computation keeps every Requantize stage
within int8 range for this model's real calibrated scales -- MinMax
calibration cannot guarantee that for arbitrary inputs (see lower.c's
lower_verify_demo_forward_pass), so this script will raise on an input that
doesn't already satisfy that.
"""

import argparse
import math

import numpy as np
import onnx
from onnx import numpy_helper


def read_int8_file(path: str) -> list[int]:
    values = []
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0]
            values.extend(int(tok) for tok in line.split())
    return values


def node_attr(node, name):
    for a in node.attribute:
        if a.name == name:
            if a.type == onnx.AttributeProto.FLOAT:
                return a.f
            if a.type == onnx.AttributeProto.INT:
                return a.i
    raise KeyError(name)


def fixed_multiplier(ratio: float) -> tuple[int, int]:
    """Mirrors lower.c's compute_fixed_multiplier exactly (see that
    function's comments): decomposes a positive ratio into a 16-bit Q15-
    style mantissa M in [16384,32767] and a shift, via frexp."""
    if not (ratio > 0.0) or not math.isfinite(ratio):
        raise ValueError(f"ratio {ratio} is not a positive finite number")
    mantissa, e = math.frexp(ratio)
    m = round(mantissa * 32768.0)
    if m >= 32768:
        m = 16384
        e += 1
    shift = 15 - e
    if shift < 0 or shift > 40:
        raise ValueError(f"ratio {ratio} needs shift={shift}, outside supported [0,40]")
    return m, shift


def requantize(acc: np.ndarray, m: int, shift: int) -> np.ndarray:
    """Elementwise fixed-point rescale, same rounding as lower.c: round
    towards nearest via a pre-shift additive term, then arithmetic shift.
    Refuses (raises) if any element would overflow int8, matching
    lower_verify_demo_forward_pass's refuse-to-compile behavior rather than
    silently wrapping or saturating."""
    product = acc.astype(np.int64) * np.int64(m)
    round_term = np.int64(1 << (shift - 1)) if shift >= 1 else np.int64(0)
    shifted = (product + round_term) >> shift
    if np.any(shifted > 127) or np.any(shifted < -127):
        bad = shifted[(shifted > 127) | (shifted < -127)]
        raise ValueError(f"requantize overflow: {bad.tolist()} outside int8 range")
    return shifted


def run_reference(model_path: str, input_path: str) -> list[int]:
    m = onnx.load(model_path)
    inits = {i.name: numpy_helper.to_array(i) for i in m.graph.initializer}
    nodes = {n.name: n for n in m.graph.node}

    x = np.array(read_int8_file(input_path), dtype=np.int64)

    w1 = inits["fc1.weight_q"].astype(np.int64)
    b1 = inits["fc1.bias_q"].astype(np.int64)
    w2 = inits["fc2.weight_q"].astype(np.int64)
    b2 = inits["fc2.bias_q"].astype(np.int64)

    acc1_scale = node_attr(nodes["fc1/MatMul"], "scale")
    s_1 = node_attr(nodes["fc1/Requantize"], "scale")
    acc2_scale = node_attr(nodes["fc2/MatMul"], "scale")
    s_out = node_attr(nodes["fc2/Requantize"], "scale")

    acc1 = w1 @ x + b1
    relu1 = np.maximum(acc1, 0)
    m1, s1 = fixed_multiplier(acc1_scale / s_1)
    req1 = requantize(relu1, m1, s1)

    acc2 = w2 @ req1 + b2
    m2, s2 = fixed_multiplier(acc2_scale / s_out)
    req2 = requantize(acc2, m2, s2)

    return [int(v) for v in req2]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", default="../models/tiny_classifier.onnx")
    parser.add_argument("--input", default="../models/tiny_classifier_golden_input.txt")
    parser.add_argument("--out", default="../models/tiny_classifier_golden_output.txt")
    args = parser.parse_args()

    output = run_reference(args.model, args.input)
    print(f"golden output: {output}")

    with open(args.out, "w") as f:
        f.write(
            "# Independently computed (export/gen_golden.py, numpy) reference output for\n"
            "# tiny_classifier_golden_input.txt through this exact model's real calibrated\n"
            "# weights/scales. compiler/tests/test_lower.c compares the actual AVR-emitted\n"
            "# code (executed via compiler/tests/avr_interp.c) against this within +/-1 LSB.\n"
        )
        f.write(" ".join(str(v) for v in output) + "\n")


if __name__ == "__main__":
    main()
