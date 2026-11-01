#!/usr/bin/env python3
"""Extract Kokoro's first style projection with its original weights and input."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


p = argparse.ArgumentParser(description=__doc__)
p.add_argument("source", type=Path, help="validated Kokoro neural-core directory")
p.add_argument("output", type=Path, help="new output directory on private scratch")
a = p.parse_args()
assert not a.output.exists(), "preserve existing output; choose a new directory"
lines = (a.source / "decoder.ncnn.param").read_text().splitlines()
assert lines[0] == "7767517"
records = [line.split() for line in lines[2:] if line.strip()]
projection = next(i for i, r in enumerate(records) if r[1] == "linear_86")
assert all(r[0] in {"Input", "Split", "Crop"} for r in records[:projection])
assert records[projection] == ["InnerProduct", "linear_86", "1", "1", "67", "68",
                               "0=1028", "1=1", "2=131584"]
crop = next(r for r in records if r[1] == "slice_0")
assert crop == ["Crop", "slice_0", "1", "1", "in3", "9", "-23310=1,128",
                "-23311=1,0", "-23309=1,0"]
split = next(r for r in records if r[1] == "splitncnn_2")
assert split[:5] == ["Split", "splitncnn_2", "1", "58", "9"] and "67" in split[5:]
model_path = a.source / "decoder.ncnn.bin"
size = 4 + 4 * (131584 + 1028)
with model_path.open("rb") as f:
    model = f.read(size)
assert len(model) == size and model[:4] == b"\x00" * 4, "expected first FP32 weight tag"
values = np.frombuffer(model, dtype="<f4", offset=4)
weights = values[:131584].reshape(1028, 128)
bias = values[131584:]
style_path = a.source / "style.f32"
style = np.fromfile(style_path, dtype="<f4")
assert style.shape == (256,) and np.isfinite(style).all() and np.isfinite(values).all()
reference = weights.astype(np.float64) @ style[:128].astype(np.float64) + bias.astype(np.float64)
assert np.isfinite(reference).all()
cpu_path = a.source / "ncnn-blob-68.f32"
assert cpu_path.is_file(), "first validate and retain the original CPU graph's blob68"
cpu = np.fromfile(cpu_path, dtype="<f4")
assert cpu.shape == reference.shape and np.isfinite(cpu).all()
error = np.abs(cpu.astype(np.float64) - reference)
assert np.all(error <= 2e-5 + 2e-5 * np.abs(reference)), "original graph/reference mismatch"

a.output.mkdir(parents=True)
(a.output / "decoder.ncnn.param").write_text(
    "7767517\n3 3\n"
    "Input in3 0 1 in3\n"
    "Crop slice_0 1 1 in3 9 -23310=1,128 -23311=1,0 -23309=1,0\n"
    "InnerProduct linear_86 1 1 9 68 0=1028 1=1 2=131584\n")
(a.output / "decoder.ncnn.bin").write_bytes(model)
(a.output / "style.f32").write_bytes(style_path.read_bytes())
reference.astype("<f4").tofile(a.output / "reference.f32")
report = {
    "source_param_sha256": digest(a.source / "decoder.ncnn.param"),
    "source_weights_sha256": digest(model_path),
    "source_input_sha256": digest(style_path),
    "source_cpu_blob_sha256": digest(cpu_path),
    "weights_prefix_bytes": size,
    "input": "in3", "output": "68", "output_floats": 1028,
    "reference": "independent float64 dot of original FP32 weights/input plus bias, stored FP32",
    "source_cpu_max_abs_error": float(error.max()),
    "scope": "exact first style projection only; full neural-core/model requirements unchanged",
    "files": {name: digest(a.output / name) for name in
              ("decoder.ncnn.param", "decoder.ncnn.bin", "style.f32", "reference.f32")},
}
(a.output / "preparation.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
