"""Thin PyTorch -> ONNX export for the demo model. Existing tooling, not
part of the hand-built pipeline -- see spec section 3.
"""

import argparse

import torch
import torch.nn as nn


class TinyClassifier(nn.Module):
    """Matches the OpKind set ingest.c supports: MatMul, Add, Relu, Requantize."""

    def __init__(self, in_features: int = 16, hidden: int = 8, out_features: int = 4):
        super().__init__()
        self.fc1 = nn.Linear(in_features, hidden)
        self.relu = nn.ReLU()
        self.fc2 = nn.Linear(hidden, out_features)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.fc2(self.relu(self.fc1(x)))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="../models/tiny_classifier.onnx")
    parser.add_argument("--in-features", type=int, default=16)
    args = parser.parse_args()

    model = TinyClassifier(in_features=args.in_features)
    model.eval()

    # TODO(milestone 3 prerequisite): INT8 quantization (torch.quantization
    # or torch.ao.quantization) before export -- ingest.c expects a
    # quantized graph (DT_INT8 activations, QuantParams per op).
    dummy_input = torch.randn(1, args.in_features)
    torch.onnx.export(
        model,
        dummy_input,
        args.out,
        input_names=["input"],
        output_names=["output"],
        opset_version=17,
    )
    print(f"exported {args.out}")


if __name__ == "__main__":
    main()
