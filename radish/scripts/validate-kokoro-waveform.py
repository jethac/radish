#!/usr/bin/env python3
"""Validate Kokoro conv_post output through the original exported waveform tail."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import onnx


p = argparse.ArgumentParser(description=__doc__)
p.add_argument("core", type=Path, help="scalar FP32 conv_post output, channels x frames")
p.add_argument("fixture", type=Path, help="original boundary fixture with wav reference")
p.add_argument("output", type=Path, help="private FP32 waveform output")
p.add_argument("--tail-model", type=Path, help="extracted waveform-tail.onnx; defaults beside core")
a = p.parse_args()
raw = np.fromfile(a.core, dtype="<f4")
assert raw.size % 22 == 0 and raw.size > 22 and np.isfinite(raw).all()
frames = raw.size // 22
core = raw.reshape(22, frames).astype(np.float64)
with np.load(a.fixture) as fixture:
    reference = fixture["wav"].reshape(-1).astype(np.float64)
assert reference.size == (frames - 1) * 5 and np.isfinite(reference).all()

# Fixed FP32 model-output gate, declared before evaluating errors. The source
# export uses exp(log magnitude), sin(raw phase), a stored inverse basis, and
# a finite window_sum tensor. Preserve that export's normalization exactly.
absolute_tolerance = 2e-4
relative_tolerance = 2e-3
minimum_snr_db = 50.0
magnitude = np.exp(core[:11])
phase = np.sin(core[11:])
spectrum = magnitude * (np.cos(phase) + 1j * np.sin(phase))
tail_path = a.tail_model or a.core.parent / "waveform-tail.onnx"
tail = onnx.load(tail_path)
constants = {x.name: onnx.numpy_helper.to_array(x) for x in tail.graph.initializer}
prefix = "/decoder/decoder/generator/istft/stft/"
basis = constants["decoder.decoder.generator.stft.istft.stft.inverse_basis"]
window_sum = constants["decoder.decoder.generator.stft.istft.stft.window_sum"]
scale = float(constants[prefix + "Constant_19_output_0"])
threshold = float(constants[prefix + "Constant_9_output_0"])
assert basis.shape == (22, 1, 20) and window_sum.shape == (20,) and scale == 4.0
convolution = next(n for n in tail.graph.node if n.op_type == "ConvTranspose")
attrs = {x.name: onnx.helper.get_attribute_value(x) for x in convolution.attribute}
assert attrs == {"dilations": [1], "group": 1, "kernel_shape": [20], "pads": [0, 0], "strides": [5]}
assert constants[prefix + "Constant_21_output_0"].tolist() == [10]
assert constants[prefix + "Constant_26_output_0"].tolist() == [-10]
blocks = basis[:, 0, :].astype(np.float64).T @ np.concatenate((spectrum.real, spectrum.imag))
total = 20 + 5 * (frames - 1)
waveform = np.zeros(total, dtype=np.float64)
for sample in range(20):
    waveform[sample:sample + 5 * frames:5] += blocks[sample]
# The ONNX Gather/Div/ScatterND updates only indices present in its stored
# 20-value window_sum. It does not synthesize a full overlap-add envelope.
indices = np.flatnonzero(window_sum > threshold)
waveform[indices] /= window_sum[indices]
waveform = (waveform * scale)[10:-10]
assert waveform.shape == reference.shape and np.isfinite(waveform).all()
error = waveform - reference
failed = int(np.count_nonzero(np.abs(error) > absolute_tolerance + relative_tolerance * np.abs(reference)))
signal_power = float(reference @ reference)
error_power = float(error @ error)
assert signal_power > 0
snr = 10 * np.log10(signal_power / max(error_power, 1e-30))
report = {
    "core_sha256": hashlib.sha256(a.core.read_bytes()).hexdigest(),
    "fixture_sha256": hashlib.sha256(a.fixture.read_bytes()).hexdigest(),
    "tail_model_sha256": hashlib.sha256(tail_path.read_bytes()).hexdigest(),
    "samples": int(waveform.size), "sample_rate": 24000,
    "max_abs_error": float(np.abs(error).max()), "snr_db": float(snr),
    "absolute_tolerance": absolute_tolerance, "relative_tolerance": relative_tolerance,
    "minimum_snr_db": minimum_snr_db, "mismatches": failed,
    "scope": "saved harmonic-source fixture and CPU waveform tail; not end-to-end runtime or speed evidence",
}
print(json.dumps(report, indent=2))
assert failed == 0 and snr >= minimum_snr_db, "waveform accuracy gate failed"
waveform.astype("<f4").tofile(a.output)
print("PASS: all waveform samples and SNR gate; CPU inverse-STFT tail")
