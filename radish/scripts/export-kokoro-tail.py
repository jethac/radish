#!/usr/bin/env python3
"""Export the exact saved Kokoro ONNX waveform tail into portable FP32 parameters."""

import argparse
from contextlib import ExitStack
import hashlib
from pathlib import Path
import struct

import numpy as np
import onnx


MODEL_SHA256 = "e6454fdf4a4b8138d67777683e3ac1e9d90561d7de4a5029831dbce55164d430"
FIXTURE_SHA256 = "944f95f2202263f6a79f39f107438a444f6a3949ed3e0403f3772e14d8627c77"
REFERENCE_SHA256 = "cf90b53cf7427b2b97ac47da41fc2d174d64eb1c21e0ca7240d543fbf58bcdff"
PREFIX = "/decoder/decoder/generator/istft/stft/"
HEADER = struct.Struct("<8s12I32s")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def export(model_path, output, fixture=None, reference_output=None):
    require((fixture is None) == (reference_output is None), "--fixture and --reference-output must be supplied together")
    reference = None
    if fixture is not None:
        require(hashlib.sha256(fixture.read_bytes()).hexdigest() == FIXTURE_SHA256, "unexpected b-en fixture hash")
        with np.load(fixture, allow_pickle=False) as data:
            wav = data["wav"]
            require(wav.dtype == np.float32 and wav.shape == (1, 127800) and np.isfinite(wav).all(),
                    "unexpected original waveform type, dimensions, or nonfinite values")
            reference = wav.reshape(-1).astype("<f4", copy=False).tobytes()
        require(hashlib.sha256(reference).hexdigest() == REFERENCE_SHA256, "unexpected original waveform bytes")
        require(output.resolve() != reference_output.resolve(), "tail and reference outputs must be distinct")
    for path in (output,) if reference_output is None else (output, reference_output):
        require(not path.exists() and not path.is_symlink(), f"output must be new: {path}")
    raw = model_path.read_bytes()
    require(hashlib.sha256(raw).hexdigest() == MODEL_SHA256, "unexpected ONNX model hash")
    model = onnx.load_model_from_string(raw)
    onnx.checker.check_model(model)
    require(len(model.graph.input) == 1, "expected one core input")
    tensor = model.graph.input[0].type.tensor_type
    require(tensor.elem_type == onnx.TensorProto.FLOAT and
            [dimension.dim_value for dimension in tensor.shape.dim] == [1, 22, 25561],
            "unexpected core input type or dimensions")
    constants = {value.name: onnx.numpy_helper.to_array(value) for value in model.graph.initializer}
    require(len(constants) == len(model.graph.initializer), "duplicate initializer names")
    basis = constants["decoder.decoder.generator.stft.istft.stft.inverse_basis"]
    window = constants["decoder.decoder.generator.stft.istft.stft.window_sum"]
    scale = constants[PREFIX + "Constant_19_output_0"]
    threshold = constants[PREFIX + "Constant_9_output_0"]
    for name, value, shape in (("basis", basis, (22, 1, 20)), ("window_sum", window, (20,)),
                               ("scale", scale, ()), ("threshold", threshold, ())):
        require(value.dtype == np.float32 and value.shape == shape and np.isfinite(value).all(),
                f"unexpected {name} type, dimensions, or nonfinite values")
    require(float(scale) == 4.0 and float(threshold) == float(np.finfo(np.float32).tiny),
            "unexpected scaling or normalization threshold")
    require((window >= 0).all(), "negative window normalization value")
    convolutions = [node for node in model.graph.node if node.op_type == "ConvTranspose"]
    require(len(convolutions) == 1, "expected one inverse transform convolution")
    attrs = {value.name: onnx.helper.get_attribute_value(value) for value in convolutions[0].attribute}
    require(attrs == {"dilations": [1], "group": 1, "kernel_shape": [20], "pads": [0, 0], "strides": [5]},
            "unexpected inverse transform convolution")
    require(constants[PREFIX + "Constant_21_output_0"].tolist() == [10] and
            constants[PREFIX + "Constant_26_output_0"].tolist() == [-10] and
            constants[PREFIX + "Constant_25_output_0"].tolist() == [0], "unexpected waveform trimming")
    require(constants["/decoder/decoder/generator/Constant_31_output_0"].tolist() == [11],
            "unexpected magnitude/phase split")
    # v1: magic; version/header bytes/channels/bins/frames/kernel/window/hop/
    # trim-left/trim-right/sample-rate/reserved; original ONNX SHA256; FP32 payload.
    header = HEADER.pack(b"KOKTAIL\0", 1, HEADER.size, 22, 11, 25561, 20, 20, 5, 10, 10, 24000, 0,
                         bytes.fromhex(MODEL_SHA256))
    payload = b"".join(value.astype("<f4", copy=False).tobytes(order="C")
                       for value in (scale, threshold, basis, window))
    require(len(header + payload) == 1936, "unexpected parameter file size")
    assets = [(output, header + payload)]
    if reference is not None:
        assets.append((reference_output, reference))
    created = []
    try:
        with ExitStack() as stack:
            streams = []
            for path, value in assets:
                streams.append((stack.enter_context(path.open("xb")), value))
                created.append(path)
            for stream, value in streams:
                stream.write(value)
    except BaseException:
        for path in created:
            path.unlink()
        raise
    print(f"Exported {len(header + payload)} bytes: {output}")
    print(f"ONNX SHA256: {MODEL_SHA256}")
    print(f"Tail SHA256: {hashlib.sha256(header + payload).hexdigest()}")
    if reference is not None:
        print(f"Exported {len(reference)} original waveform bytes: {reference_output}")
        print(f"Fixture SHA256: {FIXTURE_SHA256}")
        print(f"Reference SHA256: {REFERENCE_SHA256}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path, help="exact private waveform-tail.onnx")
    parser.add_argument("output", type=Path, help="new private kokoro-tail.bin")
    parser.add_argument("--fixture", type=Path, help="exact original b-en.npz; requires --reference-output")
    parser.add_argument("--reference-output", type=Path, help="new private original waveform FP32 file; requires --fixture")
    args = parser.parse_args()
    if (args.fixture is None) != (args.reference_output is None):
        parser.error("--fixture and --reference-output must be supplied together")
    try:
        export(args.model, args.output, args.fixture, args.reference_output)
    except (OSError, ValueError, KeyError, onnx.checker.ValidationError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
