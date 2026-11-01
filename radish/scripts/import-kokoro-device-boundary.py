#!/usr/bin/env python3
"""Preserve a validated tablet Kokoro boundary and derive matching host CPU references."""

import argparse
from contextlib import contextmanager
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile

import numpy as np
import onnx
import onnxruntime as ort


specification = importlib.util.spec_from_file_location(
    "radish_runtime_export", Path(__file__).with_name("export-kokoro-runtime.py"))
runtime = importlib.util.module_from_spec(specification)
specification.loader.exec_module(runtime)
runtime.WORK = Path(os.environ.get("RADISH_WORK", str(runtime.WORK)))
require, asset = runtime.require, runtime.asset
GRAPH_PINS = {
    "frontend.onnx": (111946804, "586121ea16febcac7431dcda362b29b95581fa60688f26010fe457d6bf75ed18"),
    "harmonic-source.onnx": (13810, "4bdcb9f2f54d6efbfdcefce384300352933cc71fa562930f23eaeb3443528fd2"),
    "decoder.onnx": (213353855, "c2eaa15d5a42332626500a4b7171e4a9ee4d4d3f6b83b9410f47a7521d62d633"),
    "waveform-tail.onnx": (13109, "eeab196008aa712634e5aef86e5d03c7106053da87cded7f02d4ed0185677fcd"),
}
FRONT_PIN = {"bytes": 111933749, "sha256": "56ba27008352a2191b631ebbf269bf4506d690a1b83d8ff8ff5524b11291ddd3"}
GATES = {"core_atol": 0.002, "core_rtol": 0.002, "waveform_atol": 2e-4,
         "waveform_rtol": 2e-3, "waveform_minimum_snr_db": 50.0}
RAW_NAMES = {"ids.i64", "style.f32", "speed.f32", "asr.f32", "f0.f32", "n.f32", "har.f32", "durations.f32"}


@contextmanager
def new_fixture(output):
    require(not output.exists() and not output.is_symlink(), "output must be a new directory")
    output = output.resolve()
    require(output.is_relative_to(runtime.WORK.resolve()) and output != runtime.WORK.resolve(),
            f"private output must be below {runtime.WORK}")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".kokoro-device-import-", dir=output.parent))
    try:
        yield stage
        rename = ctypes.CDLL(None, use_errno=True).renameat2
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        if rename(-100, os.fsencode(stage), -100, os.fsencode(output), 1) != 0:
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error), str(output))
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def read_boundary(directory, max_frames):
    require(directory.is_dir(), "device capture directory is missing")
    record = json.loads((directory / "device-capture.json").read_text())
    front = json.loads((directory / "frontend.json").read_text())
    require(record["format"] == "radish-kokoro-device-frontend-v1" and
            record["inputs"]["format"] == "radish-kokoro-device-frontend-inputs-v1" and
            record["inputs"]["source_sha256"] == runtime.MODEL_SHA256, "unexpected device capture provenance")
    require(asset(directory / "frontend.json") == record["frontend_manifest"] and record["frontend"] == front,
            "device frontend manifest changed")
    require(front["format"] == "radish-kokoro-frontend-boundary-v1" and front["ort_api"] == 24 and
            front["optimization"] == "disabled" and isinstance(front["ort_version"], str), "unexpected device frontend runtime")
    source_pin = {"bytes": GRAPH_PINS["harmonic-source.onnx"][0], "sha256": GRAPH_PINS["harmonic-source.onnx"][1]}
    require(record["inputs"]["graphs"] == {"frontend-only.onnx": FRONT_PIN, "harmonic-source.onnx": source_pin},
            "unexpected device frontend/source graphs")
    count, tokens = front["T"], front["tokens_with_pads"]
    require(type(count) is int and 1 <= count <= max_frames and type(tokens) is int and 3 <= tokens <= 512,
            "invalid device frame/token bounds")
    frames, samples = 120*count+1, 600*count
    require(front["frames"] == frames and front["samples"] == samples and front["sample_rate"] == 24000,
            "inconsistent device boundary dimensions")
    shapes = {"asr": (1, 512, count), "f0": (1, 2*count), "n": (1, 2*count), "har": (1, 22, frames),
              "style": (1, 256), "speed": (1,), "durations": (1, tokens)}
    require(set(record["files"]) == RAW_NAMES and
            {file.name for file in directory.iterdir()} == RAW_NAMES | {"frontend.json", "device-capture.json"},
            "unexpected device capture asset list")
    raw, arrays = {}, {}
    for name in RAW_NAMES:
        file = directory / name
        require(file.is_file() and not file.is_symlink() and asset(file) == record["files"][name],
                f"device raw file changed: {name}")
        raw[name] = file.read_bytes()
        if name == "ids.i64":
            require(len(raw[name]) == 8*tokens, "wrong token byte count")
            continue
        key = name[:-4]
        require(len(raw[name]) == 4*int(np.prod(shapes[key])), f"wrong device scalar count: {name}")
        arrays[key] = np.frombuffer(raw[name], dtype="<f4").reshape(shapes[key])
        require(np.isfinite(arrays[key]).all(), f"nonfinite device values: {name}")
    ids = np.frombuffer(raw["ids.i64"], dtype="<i8").reshape(1, tokens)
    require(ids[0, 0] == 0 and ids[0, -1] == 0 and ((ids[0, 1:-1] >= 1) & (ids[0, 1:-1] <= 177)).all(),
            "invalid padded device token IDs")
    require(float(arrays["speed"][0]) > 0, "invalid device speed")
    duration = arrays["durations"]
    require((duration >= 1).all() and np.equal(duration, np.floor(duration)).all() and
            float(duration.sum(dtype=np.float64)) == count, "device duration sum differs from T")
    require(set(record["inputs"]["inputs"]) == {"ids.i64", "style.f32", "speed.f32"} and
            all(record["files"][name] == expected for name, expected in record["inputs"]["inputs"].items()),
            "device tokens/style/speed differ from validated input provenance")
    for filename, expected in record["evidence"].items():
        require(asset(Path(filename)) == expected, f"device evidence changed: {filename}")
    return record, front, raw, arrays, ids


def import_boundary(capture, models, output, threads, max_frames):
    require(1 <= max_frames <= 4096, "max frames must be 1..4096")
    capture, models = capture.resolve(), models.resolve()
    require(not output.resolve().is_relative_to(capture) and not output.resolve().is_relative_to(models),
            "do not write the new fixture inside a source capture or model directory")
    record, front, raw, boundary_arrays, ids = read_boundary(capture, max_frames)
    capture_pin = asset(capture / "device-capture.json")
    model_report = runtime.verify_models(models)
    model_manifest_pin = asset(models / "runtime-models.json")
    for name, (size, digest) in GRAPH_PINS.items():
        require(asset(models / name) == {"bytes": size, "sha256": digest}, f"unexpected pinned runtime graph: {name}")
    count, frames, samples = front["T"], front["frames"], front["samples"]
    with new_fixture(output) as stage:
        # No frontend or harmonic-source Run occurs here. Both reference stages
        # consume the exact bytes returned by the tablet, including its har.
        inputs = {runtime.NAMES[key]: boundary_arrays[key] for key in ("asr", "f0", "n", "style", "har")}
        core, core_time = runtime.run_ort(models / "decoder.onnx", inputs, threads)
        reference = core[runtime.NAMES["core"]]
        require(reference.shape == (1, 22, frames), "unexpected host CPU core reference shape")
        tail, tail_time = runtime.run_ort(models / "waveform-tail.onnx", {runtime.NAMES["core"]: reference}, threads)
        waveform = tail[runtime.NAMES["waveform"]]
        require(waveform.shape == (1, samples), "unexpected host CPU waveform reference shape")
        arrays = {**boundary_arrays, "reference": reference, "reference-waveform": waveform}
        for name, data in raw.items():
            (stage / name).write_bytes(data)
            require(asset(stage / name) == record["files"][name], f"import changed device bytes: {name}")
        for key, value in arrays.items():
            if key in ("reference", "reference-waveform"):
                value.astype("<f4", copy=False).tofile(stage / (key + ".f32"))
            np.save(stage / (key + ".npy"), value, allow_pickle=False)
        np.save(stage / "ids.npy", ids, allow_pickle=False)
        files = {file.name: asset(file) for file in sorted(stage.iterdir())}
        require(asset(capture / "device-capture.json") == capture_pin and
                asset(capture / "frontend.json") == record["frontend_manifest"], "device provenance changed during import")
        for name, expected in record["files"].items():
            require(asset(capture / name) == expected, f"immutable device boundary changed: {name}")
        require(asset(models / "runtime-models.json") == model_manifest_pin, "runtime model manifest changed")
        for name, (size, digest) in GRAPH_PINS.items():
            require(asset(models / name) == {"bytes": size, "sha256": digest}, f"runtime graph changed: {name}")
        report = {
            "format": "radish-kokoro-runtime-fixture-v1", "T": count, "frames": frames, "samples": samples,
            "sample_rate": 24000, "source_sha256": runtime.MODEL_SHA256,
            "input_is_original_fixture": False, "original_nonpad_tokens": ids.size-2,
            "selected_nonpad_tokens": ids.size-2, "selection": "validated-device-capture", "speed": float(boundary_arrays["speed"][0]),
            "models_directory": str(models), "models": model_report["graphs"],
            "shapes": {name: list(value.shape) for name, value in arrays.items()}, "files": files,
            "device_capture": {"directory": str(capture), "manifest": capture_pin,
                               "frontend_manifest": record["frontend_manifest"], "raw_files": record["files"],
                               "frontend": front, "evidence": record["evidence"]},
            "runtime": {"onnxruntime": ort.__version__, "onnx": onnx.__version__, "numpy": np.__version__,
                        "provider": "CPUExecutionProvider", "intra_threads": threads, "inter_threads": 1,
                        "graph_optimization": "disabled", "source_crosscheck": "device-boundary-preserved",
                        "frontend_onnxruntime": front["ort_version"], "reference_onnxruntime": ort.__version__,
                        "boundary_policy": "All eight device raw files copied byte-for-byte; frontend/source are not recomputed on the host."},
            "timings": {"device_frontend": front["frontend"], "device_source": front["source"],
                        "device_boundary_seconds": front["boundary_seconds"], "core": core_time, "tail": tail_time},
            "accuracy_gates": GATES,
            "scope": "Host CPU decoder and waveform-tail references conditioned on the exact fresh tablet boundary. "
                     "Device frontend/source and host references retain their separate ORT versions. "
                     "Not an unsplit original-ONNX waveform reference, GPU correctness, or complete synthesis timing claim.",
        }
        (stage / "runtime.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Imported exact tablet boundary: T={count}, frontend ORT{front['ort_version']}, reference ORT{ort.__version__}: {output.resolve()}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="validated device-frontend.sh output directory")
    parser.add_argument("models", type=Path, help="pinned four-graph runtime export directory")
    parser.add_argument("output", type=Path, help="new private fixture under RADISH_WORK")
    parser.add_argument("--threads", type=int, choices=range(1, 7), default=4)
    parser.add_argument("--max-frames", type=int, default=4096, help="maximum T, between 1 and 4096")
    args = parser.parse_args()
    try:
        import_boundary(args.capture, args.models, args.output, args.threads, args.max_frames)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
