#!/usr/bin/env python3
"""Capture and validate exact upstream ncnn shape hints for the fixed Kokoro core."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


SOURCE_SHA256 = "8dd76d964aaf563cdf619f1f749db588f9b3694fef55b0e75d4e17cf2f414e43"
CPU_SHA256 = "d09792b7b6ffd1e19e654e14e7852380084851f5d3918724f96a708975b268ec"
FIXTURES = {
    "decoder.ncnn.bin": (213042160, "0f77cf77ff79a7724662f2e79a4d50284eb498abb2e519469a188e33f6c051ae"),
    "asr.f32": (436224, "0ed2c9c4aa1899c0e6ca10d70a739f3cec00d731e392930c02ade852bf94515b"),
    "f0.f32": (1704, "16572fb20769acd644b3076d54a5c8e09ba7b19da73306b8f63808d0098f9e1c"),
    "n.f32": (1704, "32ea97959622ceef6632794fd4dcdafc2bd8a3984c70449b81093c9fe19a86b8"),
    "style.f32": (1024, "30a2c76520b76bce395b3e2c33592c56322dffb50d70f7f0f7aeaef968407d66"),
    "har.f32": (2249368, "65c949af78751bbe2c7736c02ba97cd4f7d17ebd551c8c8c65125e6adaa0bbe3"),
    "reference.f32": (2249368, "4aabc5a98f714ac385561dbbc604b0d40f8a9027e645fcc38096c02c20ea395c"),
    "reference-waveform.f32": (511200, "7091f0558cab281051919d3315f7d3a2ad7d542487c7f6f438ee884e6a529df1"),
    "waveform-tail.onnx": (16216, "e6454fdf4a4b8138d67777683e3ac1e9d90561d7de4a5029831dbce55164d430"),
}
INPUT_SHAPES = {"in0": [2, 213, 512, 1, 1], "in1": [1, 426, 1, 1, 1],
                "in2": [1, 426, 1, 1, 1], "in3": [1, 256, 1, 1, 1],
                "in4": [2, 25561, 22, 1, 1]}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_graph(data):
    lines = data.decode().splitlines(keepends=True)
    require([line.strip() for line in lines[:2]] == ["7767517", "1368 1739"], "unexpected graph header")
    require(len(lines) == 1370, "unexpected layer count")
    rows, producers = [], {}
    for index, line in enumerate(lines[2:]):
        row = line.split()
        inputs, outputs = int(row[2]), int(row[3])
        require(all(name in producers for name in row[4:4 + inputs]), f"unknown input at layer {index}")
        require(not any(token.startswith("-23330=") for token in row), "source already has shape hints")
        tops = row[4 + inputs:4 + inputs + outputs]
        require(len(tops) == outputs, f"missing output at layer {index}")
        for name in tops:
            require(name not in producers, f"duplicate blob {name}")
            producers[name] = (index, len(producers))
        rows.append((row, tops))
    require(len(producers) == 1739, "unexpected blob count")
    return lines, rows, producers


def read_shapes(path, producers):
    lines = path.read_text().splitlines()
    require(lines[0] == "ncnn-logical-shapes-v1", "unexpected capture format")
    shapes = {}
    for line in lines[1:]:
        fields = line.split("\t")
        require(len(fields) == 8, "malformed shape record")
        layer, blob = map(int, fields[:2])
        name = fields[2]
        shape = list(map(int, fields[3:]))
        require(name in producers and producers[name] == (layer, blob), f"wrong producer/index for {name}")
        require(name not in shapes, f"duplicate shape for {name}")
        dims, w, h, d, c = shape
        require(1 <= dims <= 4 and min(w, h, d, c) > 0, f"unknown or invalid shape for {name}")
        require((dims >= 2 or h == 1) and (dims == 4 or d == 1) and (dims >= 3 or c == 1),
                f"noncanonical logical shape for {name}")
        shapes[name] = shape
    require(shapes.keys() == producers.keys(), "capture must contain all 1739 output blobs")
    require(all(shapes[name] == shape for name, shape in INPUT_SHAPES.items()), "unexpected fixed input shapes")
    require(shapes["out0"] == [2, 25561, 22, 1, 1], "unexpected final output shape")
    return shapes


def run_cpu(runner, directory, log_name, capture=None):
    command = [str(runner)]
    if capture is not None:
        command += ["--capture-shapes", str(capture)]
    command.append(str(directory))
    with (directory / log_name).open("x") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
    require(result.returncode == 0, f"CPU runner failed ({result.returncode}); see {directory / log_name}")
    output_hash = sha256(directory / "ncnn-cpu-output.f32")
    require(output_hash == CPU_SHA256, f"CPU output changed from validated layout baseline: {output_hash}")
    print(f"{log_name}: full reference passed; CPU output identical to baseline {output_hash}", flush=True)
    return command


def annotate(runner, source, output):
    require(runner.is_file() and os.access(runner, os.X_OK), "runner must be a built host radish-model executable")
    require(source.is_dir() and not output.exists(), "source must exist and output must be a new directory")
    data = (source / "decoder.ncnn.param").read_bytes()
    require(hashlib.sha256(data).hexdigest() == SOURCE_SHA256, "unexpected source graph; audit new graphs before annotating")
    lines, rows, producers = read_graph(data)
    linked = {}
    for name, (size, digest) in FIXTURES.items():
        path = source / name
        require(path.is_file() and path.stat().st_size == size and sha256(path) == digest,
                f"unexpected fixture contents: {name}")
        linked[name] = {"bytes": size, "sha256": digest}
    output.mkdir(parents=True, exist_ok=False)
    for name in linked:
        os.link(source / name, output / name)
    (output / "decoder.ncnn.param").write_bytes(data)
    capture = output / "logical-shapes.tsv"
    print(f"Capturing all logical shapes on CPU into {capture}", flush=True)
    capture_command = run_cpu(runner, output, "cpu-shape-capture.log", capture)
    shapes = read_shapes(capture, producers)
    annotations = []
    for index, (row, tops) in enumerate(rows):
        values = [value for name in tops for value in shapes[name]]
        lines[index + 2] = lines[index + 2].rstrip("\n") + f" -23330={len(values)}," + ",".join(map(str, values)) + "\n"
        annotations.append({"layer_index": index, "type": row[0], "name": row[1],
                            "outputs": [{"blob_index": producers[name][1], "name": name,
                                         "dims_w_h_d_c": shapes[name]} for name in tops]})
    transformed = "".join(lines).encode()
    (output / "decoder.ncnn.param").write_bytes(transformed)
    print(f"Annotated {len(rows)} layers and {len(shapes)} blobs; validating hinted CPU graph", flush=True)
    validation_command = run_cpu(runner, output, "cpu-shape-validation.log")
    for name, (_, digest) in FIXTURES.items():
        require(sha256(output / name) == digest, f"immutable fixture changed: {name}")
    manifest = {
        "source_directory": str(source), "source_param_sha256": SOURCE_SHA256,
        "hinted_param_sha256": hashlib.sha256(transformed).hexdigest(),
        "shape_capture_sha256": sha256(capture), "runner_sha256": sha256(runner),
        "capture_command": capture_command, "validation_command": validation_command,
        "annotated_layers": len(rows), "annotated_blobs": len(shapes), "linked_files": linked,
        "input_dims_w_h_d_c": INPUT_SHAPES, "output_dims_w_h_d_c": shapes["out0"],
        "shape_semantics": "Logical scalar ncnn dimensions; load_param derives Vulkan packing. Exact fixed fixture only.",
        "validation": "Both CPU runs passed the full reference comparison and were byte-identical to the validated layout baseline.",
        "cpu_output_sha256": CPU_SHA256,
        "gpu_validation": "Pending; CPU validation does not validate specialized Vulkan shaders.",
        "annotations": annotations,
    }
    (output / "shape-hints.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Validated shape metadata: {output / 'shape-hints.json'}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runner", type=Path, help="host radish-model executable with --capture-shapes")
    parser.add_argument("source", type=Path, help="validated kokoro-core-layouts fixture")
    parser.add_argument("output", type=Path, help="new kokoro-core-shapes directory on the same filesystem")
    args = parser.parse_args()
    try:
        annotate(args.runner.resolve(), args.source.resolve(), args.output.resolve())
    except (OSError, ValueError, KeyError, IndexError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
