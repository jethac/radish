#!/usr/bin/env python3
"""Build and validate ncnn shape hints for one freshly captured Kokoro runtime input."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess


WORK = Path("/run/media/jethac/Shared/radish")
MODEL_SHA256 = "b13da99ce02f03fb6b605720c46d09b746063ddb651bfaf5156a770204ffc4e9"
WEIGHTS_SHA256 = "0f77cf77ff79a7724662f2e79a4d50284eb498abb2e519469a188e33f6c051ae"
WEIGHTS_BYTES = 213042160
GRAPHS = {"frontend.onnx", "harmonic-source.onnx", "decoder.onnx", "waveform-tail.onnx"}
GATES = {"core_atol": 0.002, "core_rtol": 0.002, "waveform_atol": 2e-4,
         "waveform_rtol": 2e-3, "waveform_minimum_snr_db": 50.0}
specification = importlib.util.spec_from_file_location(
    "radish_fixed_shape_helpers", Path(__file__).with_name("hint-kokoro-shapes.py"))
helpers = importlib.util.module_from_spec(specification)
specification.loader.exec_module(helpers)
require, sha256 = helpers.require, helpers.sha256


def asset(path):
    return {"bytes": path.stat().st_size, "sha256": sha256(path)}


def validate_capture(capture):
    report = json.loads((capture / "runtime.json").read_text())
    require(report["format"] == "radish-kokoro-runtime-fixture-v1" and
            report["source_sha256"] == MODEL_SHA256, "unexpected runtime capture provenance")
    require(report["accuracy_gates"] == GATES, "runtime accuracy gates changed")
    count = report["T"]
    require(type(count) is int and count >= 1, "invalid runtime T")
    frames, samples = 120*count+1, 600*count
    require(report["frames"] == frames and report["samples"] == samples and report["sample_rate"] == 24000,
            "inconsistent runtime dimensions")
    tokens = report["selected_nonpad_tokens"] + 2
    require(3 <= tokens <= 512, "invalid runtime token count")
    shapes = {"asr": [1, 512, count], "f0": [1, 2*count], "n": [1, 2*count],
              "style": [1, 256], "har": [1, 22, frames], "reference": [1, 22, frames],
              "reference-waveform": [1, samples], "durations": [1, tokens], "speed": [1]}
    require(report["shapes"] == shapes, "unexpected runtime tensor shapes")
    filenames = {name + suffix for name in shapes for suffix in (".f32", ".npy")} | {"ids.i64", "ids.npy"}
    require(set(report["files"]) == filenames, "unexpected capture asset list")
    for name, expected in report["files"].items():
        require(asset(capture / name) == expected, f"changed runtime capture asset: {name}")
    for name, dimensions in shapes.items():
        elements = 1
        for dimension in dimensions:
            elements *= dimension
        require(report["files"][name + ".f32"]["bytes"] == 4*elements, f"wrong scalar byte count: {name}")
    require(report["files"]["ids.i64"]["bytes"] == 8*tokens, "wrong token byte count")
    source_check = report["runtime"]["source_crosscheck"]
    if source_check == "device-boundary-preserved":
        provenance = report["device_capture"]
        directory = Path(provenance["directory"])
        require(asset(directory / "device-capture.json") == provenance["manifest"], "device capture provenance changed")
        device = json.loads((directory / "device-capture.json").read_text())
        front = json.loads((directory / "frontend.json").read_text())
        require(device["format"] == "radish-kokoro-device-frontend-v1" and
                device["inputs"]["source_sha256"] == MODEL_SHA256 and device["frontend"] == front and
                front["format"] == "radish-kokoro-frontend-boundary-v1" and front["ort_api"] == 24 and
                front["optimization"] == "disabled" and front["sample_rate"] == 24000 and
                provenance["frontend"] == front and
                asset(directory / "frontend.json") == device["frontend_manifest"] == provenance["frontend_manifest"],
                "unexpected imported device frontend provenance")
        require(front["T"] == count and front["frames"] == frames and front["samples"] == samples and
                front["tokens_with_pads"] == tokens and
                report["runtime"]["frontend_onnxruntime"] == front["ort_version"], "device frontend dimensions/runtime changed")
        raw_names = {"ids.i64", "style.f32", "speed.f32", "asr.f32", "f0.f32", "n.f32", "har.f32", "durations.f32"}
        require(set(device["files"]) == raw_names and device["files"] == provenance["raw_files"],
                "unexpected device boundary file inventory")
        for name, expected in device["files"].items():
            require(asset(directory / name) == expected == report["files"][name],
                    f"imported raw boundary differs from device: {name}")
    else:
        require(source_check == "bit-identical", "source crosscheck is missing")
    require(set(report["models"]) == GRAPHS, "incomplete runtime graph provenance")
    model_directory = Path(report["models_directory"])
    models = json.loads((model_directory / "runtime-models.json").read_text())
    require(models["format"] == "radish-kokoro-runtime-models-v1" and
            models["source_sha256"] == MODEL_SHA256 and models["graphs"] == report["models"],
            "runtime graph manifest differs from capture")
    for name, expected in report["models"].items():
        require(asset(model_directory / name) == {key: expected[key] for key in ("bytes", "sha256")},
                f"changed runtime graph: {name}")
    return report


def read_shapes(path, producers, count, frames):
    lines = path.read_text().splitlines()
    require(lines and lines[0] == "ncnn-logical-shapes-v1", "unexpected shape capture format")
    shapes = {}
    for line in lines[1:]:
        fields = line.split("\t")
        require(len(fields) == 8, "malformed logical shape record")
        layer, blob = map(int, fields[:2])
        name, shape = fields[2], list(map(int, fields[3:]))
        require(name in producers and producers[name] == (layer, blob) and name not in shapes,
                f"wrong or duplicate shape producer/index: {name}")
        dims, width, height, depth, channels = shape
        require(1 <= dims <= 4 and min(width, height, depth, channels) > 0, f"invalid shape for {name}")
        require((dims >= 2 or height == 1) and (dims == 4 or depth == 1) and (dims >= 3 or channels == 1),
                f"noncanonical shape for {name}")
        shapes[name] = shape
    require(shapes.keys() == producers.keys(), "capture must contain all 1739 logical output blobs")
    inputs = {"in0": [2, count, 512, 1, 1], "in1": [1, 2*count, 1, 1, 1],
              "in2": [1, 2*count, 1, 1, 1], "in3": [1, 256, 1, 1, 1],
              "in4": [2, frames, 22, 1, 1]}
    require(all(shapes[name] == expected for name, expected in inputs.items()), "wrong runtime input shapes")
    require(shapes["out0"] == [2, frames, 22, 1, 1], "wrong runtime output shape")
    return shapes, inputs


def run_cpu(runner, directory, log_name, capture=False):
    command = [str(runner)]
    if capture:
        command += ["--capture-shapes", "logical-shapes.tsv"]
    command += ["."]
    with (directory / log_name).open("x") as log:
        result = subprocess.run(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT, check=False)
    require(result.returncode == 0, f"CPU runner failed ({result.returncode}); see {directory / log_name}")
    digest = sha256(directory / "ncnn-cpu-output.f32")
    print(f"{log_name}: full fresh ORT-reference gate passed; CPU SHA256 {digest}", flush=True)
    return command, digest


def prepare(runner, capture, template, output):
    runner, capture, template = runner.resolve(), capture.resolve(), template.resolve()
    require(runner.is_file() and os.access(runner, os.X_OK), "runner must be an executable host radish-model")
    require(not output.exists() and not output.is_symlink(), "output must be a new directory")
    output = output.resolve()
    require(output.is_relative_to(WORK.resolve()) and output != WORK.resolve(), f"output must be below {WORK}")
    require(not output.is_relative_to(capture) and not output.is_relative_to(template),
            "do not create outputs inside the source capture or template")
    report = validate_capture(capture)
    capture_manifest_sha = sha256(capture / "runtime.json")
    runner_sha = sha256(runner)
    parameters = (template / "decoder.ncnn.param").read_bytes()
    require(hashlib.sha256(parameters).hexdigest() == helpers.SOURCE_SHA256, "unexpected unhinted layout template")
    require(asset(template / "decoder.ncnn.bin") == {"bytes": WEIGHTS_BYTES, "sha256": WEIGHTS_SHA256},
            "unexpected ncnn template weights")
    lines, rows, producers = helpers.read_graph(parameters)
    output.mkdir(parents=True, exist_ok=False)
    for name in report["files"]:
        if name.endswith((".f32", ".i64")):
            os.link(capture / name, output / name)
        else:
            shutil.copyfile(capture / name, output / name)
    shutil.copyfile(capture / "runtime.json", output / "runtime.json")
    os.link(template / "decoder.ncnn.bin", output / "decoder.ncnn.bin")
    (output / "decoder.ncnn.param").write_bytes(parameters)
    print(f"Capturing runtime T={report['T']} shapes; failure evidence stays in {output}", flush=True)
    capture_command, before_sha = run_cpu(runner, output, "cpu-shape-capture.log", capture=True)
    shutil.copyfile(output / "ncnn-cpu-output.f32", output / "ncnn-cpu-before-shape-hints.f32")
    shapes, input_shapes = read_shapes(output / "logical-shapes.tsv", producers, report["T"], report["frames"])
    annotations = []
    for index, (row, tops) in enumerate(rows):
        values = [value for name in tops for value in shapes[name]]
        lines[index+2] = lines[index+2].rstrip("\n") + f" -23330={len(values)}," + ",".join(map(str, values)) + "\n"
        annotations.append({"layer_index": index, "type": row[0], "name": row[1],
                            "outputs": [{"blob_index": producers[name][1], "name": name,
                                         "dims_w_h_d_c": shapes[name]} for name in tops]})
    hinted = "".join(lines).encode()
    (output / "decoder.ncnn.param").write_bytes(hinted)
    validation_command, after_sha = run_cpu(runner, output, "cpu-shape-validation.log")
    require(after_sha == before_sha, "shape annotation changed CPU output; both outputs/logs retained")
    require(sha256(runner) == runner_sha, "runner binary changed during shape validation")
    require(sha256(capture / "runtime.json") == capture_manifest_sha and
            sha256(output / "runtime.json") == capture_manifest_sha, "runtime provenance changed")
    require(sha256(template / "decoder.ncnn.param") == helpers.SOURCE_SHA256 and
            sha256(template / "decoder.ncnn.bin") == WEIGHTS_SHA256, "immutable template changed")
    for name, expected in report["files"].items():
        require(asset(capture / name) == expected and asset(output / name) == expected,
                f"immutable runtime asset changed: {name}")
    manifest = {
        "format": "radish-kokoro-runtime-ncnn-v1", "T": report["T"], "frames": report["frames"],
        "samples": report["samples"], "sample_rate": 24000, "source_sha256": MODEL_SHA256,
        "capture_directory": str(capture), "capture_manifest_sha256": capture_manifest_sha,
        "template_directory": str(template), "source_param_sha256": helpers.SOURCE_SHA256,
        "weights_sha256": WEIGHTS_SHA256, "hinted_param_sha256": hashlib.sha256(hinted).hexdigest(),
        "runner": str(runner), "runner_sha256": runner_sha,
        "capture_command": capture_command, "validation_command": validation_command, "commands_workdir": ".",
        "shape_capture_sha256": sha256(output / "logical-shapes.tsv"),
        "annotated_layers": len(rows), "annotated_blobs": len(shapes),
        "input_dims_w_h_d_c": input_shapes, "output_dims_w_h_d_c": shapes["out0"],
        "cpu_before_sha256": before_sha, "cpu_after_sha256": after_sha, "accuracy_gates": GATES,
        "reference_waveform": "Fresh original ORT tail on the same newly computed boundary; not old b-en waveform.",
        "validation": "Both scalar CPU runs pass the fresh ORT-reference gate and produce byte-identical full outputs.",
        "gpu_validation": "Pending; CPU validation does not validate Vulkan specializations.",
    }
    (output / "shape-hints.json").write_text(json.dumps({**manifest, "annotations": annotations}, indent=2) + "\n")
    (output / "runtime-ncnn.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Validated {len(shapes)} shapes / {len(rows)} layers: {output}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runner", type=Path, help="built dynamic host radish-model")
    parser.add_argument("capture", type=Path, help="fresh export-kokoro-runtime.py capture directory")
    parser.add_argument("template", type=Path, help="pinned unhinted kokoro-core-layouts directory")
    parser.add_argument("output", type=Path, help="new private runtime ncnn directory on the same filesystem")
    args = parser.parse_args()
    try:
        prepare(args.runner, args.capture, args.template, args.output)
    except (OSError, ValueError, KeyError, IndexError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
