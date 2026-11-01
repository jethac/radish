#!/usr/bin/env python3
"""Extract the existing Kokoro decoder at its saved CPU/GPU boundary."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time

import numpy as np
import onnx
from onnx import TensorProto, helper
from onnx.utils import Extractor

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("model", type=Path)
p.add_argument("fixture", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--neural-core", action="store_true",
               help="leave harmonic-source construction and inverse STFT on the CPU")
p.add_argument("--reference", action="store_true", help="run the extracted graph on CPU ONNX Runtime")
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
boundary = [
    ("asr", "/encoder/MatMul_1_output_0"),
    ("f0", "/encoder/Reshape_output_0"),
    ("n", "/encoder/Reshape_1_output_0"),
    ("style", "style"),
]
output_name = "waveform"
if a.neural_core:
    boundary.append(("har", "/decoder/decoder/generator/Concat_3_output_0"))
    output_name = "/decoder/decoder/generator/conv_post/Conv_output_0"
fixture = np.load(a.fixture)
model = onnx.load(a.model)
shapes = {}
for key, name in boundary:
    value = fixture[key]
    assert value.dtype == np.float32, (key, value.dtype)
    shapes[key] = list(value.shape)
    info = helper.make_tensor_value_info(name, TensorProto.FLOAT, value.shape)
    for existing in list(model.graph.value_info) + list(model.graph.input):
        if existing.name == name:
            existing.CopyFrom(info)
            break
    else:
        model.graph.value_info.append(info)
    np.save(a.output / (key + ".npy"), value)
    value.astype("<f4").tofile(a.output / (key + ".f32"))
if a.neural_core:
    output_info = helper.make_tensor_value_info(output_name, TensorProto.FLOAT,
                                                [1, 22, "frames"])
    for existing in model.graph.value_info:
        if existing.name == output_name:
            existing.CopyFrom(output_info)
            break
    else:
        model.graph.value_info.append(output_info)
decoder = Extractor(model).extract_model([name for _, name in boundary], [output_name])
onnx.checker.check_model(decoder)
dest = a.output / "decoder.onnx"
onnx.save(decoder, dest)
if a.neural_core:
    tail = Extractor(model).extract_model([output_name], ["waveform"])
    onnx.checker.check_model(tail)
    onnx.save(tail, a.output / "waveform-tail.onnx")

def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

report = {
    "source": str(a.model.resolve()), "source_sha256": digest(a.model),
    "fixture": str(a.fixture.resolve()), "fixture_sha256": digest(a.fixture),
    "decoder_sha256": digest(dest), "shapes": shapes,
    "output": output_name, "neural_core": a.neural_core,
    "operators": dict(sorted(Counter(n.op_type for n in decoder.graph.node).items())),
    "onnx": onnx.__version__, "numpy": np.__version__,
    "scope": "fixed fixture shape; extraction and ONNX validation only; not ncnn compatibility",
}
if a.neural_core:
    report["waveform_tail_sha256"] = digest(a.output / "waveform-tail.onnx")
if a.reference:
    import onnxruntime as ort
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    options.inter_op_num_threads = 1
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    session = ort.InferenceSession(str(dest), options, providers=["CPUExecutionProvider"])
    start = time.monotonic()
    result = session.run(None, {name: fixture[key] for key, name in boundary})[0]
    seconds = time.monotonic() - start
    assert np.isfinite(result).all(), "CPU reference contains non-finite outputs"
    np.save(a.output / "reference.npy", result)
    result.astype("<f4").tofile(a.output / "reference.f32")
    report["reference"] = {"runtime": ort.__version__, "seconds": seconds,
                           "shape": list(result.shape),
                           "sha256": digest(a.output / "reference.f32")}
(a.output / "preparation.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
