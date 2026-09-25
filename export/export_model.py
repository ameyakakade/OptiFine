"""PyTorch -> ONNX export for the demo model, producing the INT8-quantized
graph ingest.c parses (OpKind set: Input, Const, MatMul, Add, Relu,
Requantize, Output).

torch.onnx.export produces the actual float graph (existing tooling) -- but PyTorch's own quantization + ONNX export pipeline
wraps every quantized op in exporter-internal noise (Cast/ConstantOfShape/
Identity boilerplate) and fuses Linear layers into Gemm rather than
separate MatMul/Add. Neither is fixable via qconfig; it's the legacy
exporter's symbolic lowering for quantized ops. So instead: export the
plain float model (clean Gemm+Relu+Gemm, no quantization involved), then
rewrite that real graph by hand into the exact op set this project needs,
using onnx.helper/numpy_helper -- still real ONNX tooling, just not routed
through torch's automatic quantization export.

Quantization scheme: per-tensor, symmetric (zero_point always 0 -- this
project doesn't implement affine zero-point correction in the matmul
accumulation, a deliberate scope cut, not an oversight). Every node carries
four custom attributes ingest.c reads directly, so the parser needs no
shape-inference or scale-propagation logic of its own:
  - scale (float): this node's output tensor's dequantization scale
  - zero_point (int): always 0 here, but read as a real field
  - dtype (int): DType enum ordinal (0 = Int8, 1 = Int32), matches ir.h
  - shape (ints): this node's output tensor shape
"""

import argparse

import numpy as np
import onnx
import torch
import torch.nn as nn
from onnx import helper, numpy_helper

DT_INT8 = 0
DT_INT32 = 1


class TinyClassifier(nn.Module):
    """Matches the OpKind set ingest.c supports: MatMul, Add, Relu, Requantize."""

    def __init__(self, in_features: int = 16, hidden: int = 8, out_features: int = 4):
        super().__init__()
        self.fc1 = nn.Linear(in_features, hidden)
        self.relu = nn.ReLU()
        self.fc2 = nn.Linear(hidden, out_features)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.fc2(self.relu(self.fc1(x)))


def quantize_weight(w_float: np.ndarray) -> tuple[np.ndarray, float]:
    """Symmetric per-tensor INT8 quantization."""
    max_abs = float(np.max(np.abs(w_float)))
    scale = max_abs / 127.0 if max_abs > 0 else 1.0
    q = np.round(w_float / scale).clip(-127, 127).astype(np.int8)
    return q, scale


def quantize_bias(b_float: np.ndarray, acc_scale: float) -> np.ndarray:
    """Bias quantized to match its MatMul's accumulator scale, so Add can
    operate directly on the INT32 accumulator with no rescaling."""
    return np.round(b_float / acc_scale).clip(-(2**31), 2**31 - 1).astype(np.int32)


def meta_node(op_type: str, inputs: list, outputs: list, name: str,
              scale: float, zero_point: int, dtype: int, shape: list) -> onnx.NodeProto:
    node = helper.make_node(op_type, inputs, outputs, name=name)
    node.attribute.extend([
        helper.make_attribute("scale", float(scale)),
        helper.make_attribute("zero_point", int(zero_point)),
        helper.make_attribute("dtype", int(dtype)),
        helper.make_attribute("shape", [int(d) for d in shape]),
    ])
    return node


def build_quantized_graph(float_model_path: str, in_features: int, out_path: str) -> None:
    m = onnx.load(float_model_path)
    initializers = {init.name: numpy_helper.to_array(init) for init in m.graph.initializer}
    w1f, b1f = initializers["fc1.weight"], initializers["fc1.bias"]
    w2f, b2f = initializers["fc2.weight"], initializers["fc2.bias"]
    hidden, out_features = w1f.shape[0], w2f.shape[0]

    model = TinyClassifier(in_features, hidden, out_features)
    model.eval()
    with torch.no_grad():
        model.fc1.weight.copy_(torch.from_numpy(w1f.copy()))
        model.fc1.bias.copy_(torch.from_numpy(b1f.copy()))
        model.fc2.weight.copy_(torch.from_numpy(w2f.copy()))
        model.fc2.bias.copy_(torch.from_numpy(b2f.copy()))

        calib = torch.randn(64, in_features)
        fc1_out = model.fc1(calib)
        relu_out = model.relu(fc1_out)
        final_out = model.fc2(relu_out)

    def scale_of(t: torch.Tensor) -> float:
        max_abs = float(t.abs().max())
        return max_abs / 127.0 if max_abs > 0 else 1.0

    s_x = scale_of(calib)
    s_1 = scale_of(relu_out)
    s_out = scale_of(final_out)

    w1q, s_w1 = quantize_weight(w1f)
    w2q, s_w2 = quantize_weight(w2f)
    acc1_scale = s_x * s_w1
    acc2_scale = s_1 * s_w2
    b1q = quantize_bias(b1f, acc1_scale)
    b2q = quantize_bias(b2f, acc2_scale)

    initz = [
        numpy_helper.from_array(w1q, name="fc1.weight_q"),
        numpy_helper.from_array(b1q, name="fc1.bias_q"),
        numpy_helper.from_array(w2q, name="fc2.weight_q"),
        numpy_helper.from_array(b2q, name="fc2.bias_q"),
    ]

    # Input and Const are explicit NodeProtos too (zero real inputs, same as
    # every other op), not ONNX-native graph-level formal inputs -- that
    # keeps every IrOp uniform: scale/zero_point/dtype/shape always comes
    # from a node's own attributes, never from a ValueInfoProto (which has
    # no attribute mechanism to carry a scale). graph.initializer still
    # holds the actual weight/bias bytes, matched to its Const node by the
    # shared tensor name.
    nodes = [
        meta_node("Input", [], ["input"], "Input",
                  s_x, 0, DT_INT8, [1, in_features]),
        meta_node("Const", [], ["fc1.weight_q"], "fc1/Weight",
                  s_w1, 0, DT_INT8, [hidden, in_features]),
        meta_node("Const", [], ["fc1.bias_q"], "fc1/Bias",
                  acc1_scale, 0, DT_INT32, [hidden]),
        meta_node("MatMul", ["input", "fc1.weight_q"], ["fc1/matmul_out"], "fc1/MatMul",
                  acc1_scale, 0, DT_INT32, [1, hidden]),
        meta_node("Add", ["fc1/matmul_out", "fc1.bias_q"], ["fc1/add_out"], "fc1/Add",
                  acc1_scale, 0, DT_INT32, [1, hidden]),
        meta_node("Relu", ["fc1/add_out"], ["fc1/relu_out"], "fc1/Relu",
                  acc1_scale, 0, DT_INT32, [1, hidden]),
        meta_node("Requantize", ["fc1/relu_out"], ["fc1/requant_out"], "fc1/Requantize",
                  s_1, 0, DT_INT8, [1, hidden]),
        meta_node("Const", [], ["fc2.weight_q"], "fc2/Weight",
                  s_w2, 0, DT_INT8, [out_features, hidden]),
        meta_node("Const", [], ["fc2.bias_q"], "fc2/Bias",
                  acc2_scale, 0, DT_INT32, [out_features]),
        meta_node("MatMul", ["fc1/requant_out", "fc2.weight_q"], ["fc2/matmul_out"], "fc2/MatMul",
                  acc2_scale, 0, DT_INT32, [1, out_features]),
        meta_node("Add", ["fc2/matmul_out", "fc2.bias_q"], ["fc2/add_out"], "fc2/Add",
                  acc2_scale, 0, DT_INT32, [1, out_features]),
        meta_node("Requantize", ["fc2/add_out"], ["fc2/requant_out"], "fc2/Requantize",
                  s_out, 0, DT_INT8, [1, out_features]),
        meta_node("Output", ["fc2/requant_out"], ["output"], "Output",
                  s_out, 0, DT_INT8, [1, out_features]),
    ]

    graph_output = helper.make_tensor_value_info("output", onnx.TensorProto.INT8, [1, out_features])
    graph = helper.make_graph(nodes, "optifine_tiny_classifier", [], [graph_output], initz)
    quantized_model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
    quantized_model.ir_version = 8

    # onnx.checker enforces standard op schemas (e.g. it rejects custom
    # attributes on MatMul/Add/Relu, and doesn't know op_type "Requantize"/
    # "Output" at all) -- this file is real ONNX-schema protobuf for our own
    # parser to read, not a graph meant to run on a conformant ONNX runtime,
    # so strict schema checking is the wrong tool here. Round-trip through
    # onnx.load instead, to confirm the bytes we wrote actually parse back.
    onnx.save(quantized_model, out_path)
    reloaded = onnx.load(out_path)
    assert len(reloaded.graph.node) == len(nodes), "round-trip node count mismatch"
    assert len(reloaded.graph.initializer) == len(initz), "round-trip initializer count mismatch"

    print(f"exported {out_path} (round-trip verified: {len(nodes)} nodes, {len(initz)} initializers)")
    print(f"  input scale={s_x:.6f}")
    print(f"  fc1: weight scale={s_w1:.6f} acc scale={acc1_scale:.6f} requant scale={s_1:.6f}")
    print(f"  fc2: weight scale={s_w2:.6f} acc scale={acc2_scale:.6f} requant scale={s_out:.6f}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="../models/tiny_classifier.onnx")
    parser.add_argument("--in-features", type=int, default=16)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    model = TinyClassifier(in_features=args.in_features)
    model.eval()

    float_path = args.out + ".float.tmp.onnx"
    dummy_input = torch.randn(1, args.in_features)
    torch.onnx.export(
        model,
        (dummy_input,),
        float_path,
        input_names=["input"],
        output_names=["output"],
        opset_version=17,
        dynamo=False,
    )

    build_quantized_graph(float_path, args.in_features, args.out)

    import os
    os.remove(float_path)


if __name__ == "__main__":
    main()
