#!/usr/bin/env python3
"""Export dynamic Kokoro CPU/frontend graphs and capture fresh runtime boundaries."""

import argparse
from contextlib import contextmanager
import gc
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import time

import numpy as np
import onnx
from onnx import TensorProto, helper


WORK = Path("/run/media/jethac/Shared/radish")
MODEL_SHA256 = "b13da99ce02f03fb6b605720c46d09b746063ddb651bfaf5156a770204ffc4e9"
FIXTURE_SHA256 = "944f95f2202263f6a79f39f107438a444f6a3949ed3e0403f3772e14d8627c77"
NAMES = {
    "asr": "/encoder/MatMul_1_output_0",
    "f0": "/encoder/Reshape_output_0",
    "n": "/encoder/Reshape_1_output_0",
    "style": "style",
    "har": "/decoder/decoder/generator/Concat_3_output_0",
    "src": "/decoder/decoder/generator/m_source/l_tanh/Tanh_output_0",
    "core": "/decoder/decoder/generator/conv_post/Conv_output_0",
    "durations": "/encoder/Clip_output_0",
    "waveform": "waveform",
}
SHAPES = {
    "asr": [1, 512, "T"], "f0": [1, "twice_T"], "n": [1, "twice_T"],
    "style": [1, 256], "har": [1, 22, "frames"], "src": [1, "samples", 1],
    "core": [1, 22, "frames"], "durations": [1, "sequence_length"],
    "waveform": [1, "samples"],
}
GRAPHS = ("frontend.onnx", "harmonic-source.onnx", "decoder.onnx", "waveform-tail.onnx")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def asset(path):
    return {"bytes": path.stat().st_size, "sha256": sha256(path)}


@contextmanager
def new_private_directory(path):
    require(not path.is_symlink() and not path.exists(), f"output must be new: {path}")
    output = path.resolve()
    require(output.is_relative_to(WORK.resolve()) and output != WORK.resolve(),
            f"private outputs must be below {WORK}")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".kokoro-runtime-", dir=output.parent))
    reserved = False
    try:
        yield temporary
        output.mkdir()
        reserved = True
        os.replace(temporary, output)
        reserved = False
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
        if reserved:
            output.rmdir()


def tensor(key):
    return helper.make_tensor_value_info(NAMES[key], TensorProto.FLOAT, SHAPES[key])


def extract(model, inputs, outputs, name):
    producers = {value: node for node in model.graph.node for value in node.output}
    initializers = {value.name for value in model.graph.initializer}
    boundary = {value.name for value in inputs}
    needed, pending = set(), [value.name for value in outputs]
    while pending:
        value = pending.pop()
        if not value or value in needed or value in boundary:
            continue
        needed.add(value)
        if value in producers:
            pending.extend(producers[value].input)
        else:
            require(value in initializers, f"unresolved graph input: {value}")
    nodes = [node for node in model.graph.node if any(value in needed for value in node.output)]
    graph = helper.make_graph(nodes, name, inputs, outputs,
                              [value for value in model.graph.initializer if value.name in needed])
    result = helper.make_model(graph, opset_imports=model.opset_import, ir_version=model.ir_version)
    onnx.checker.check_model(result)
    return result


def export(model_path, output):
    require(sha256(model_path) == MODEL_SHA256, "unexpected original FP32 model hash")
    model = onnx.load(model_path)
    require(not any("Random" in node.op_type for node in model.graph.node), "unexpected random operator")
    require([value.name for value in model.graph.input] == ["input_ids", "style", "speed"],
            "unexpected original model inputs")
    specifications = [
        (GRAPHS[0], list(model.graph.input), [tensor(key) for key in ("asr", "f0", "n", "har", "durations")]),
        (GRAPHS[1], [tensor("f0")], [tensor("src"), tensor("har")]),
        (GRAPHS[2], [tensor(key) for key in ("asr", "f0", "n", "style", "har")], [tensor("core")]),
        (GRAPHS[3], [tensor("core")], [tensor("waveform")]),
    ]
    with new_private_directory(output) as stage:
        records = {}
        for filename, inputs, outputs in specifications:
            graph = extract(model, inputs, outputs, filename[:-5])
            onnx.save(graph, stage / filename)
            records[filename] = {**asset(stage / filename), "nodes": len(graph.graph.node),
                                 "initializers": len(graph.graph.initializer),
                                 "inputs": [value.name for value in inputs],
                                 "outputs": [value.name for value in outputs]}
            print(f"Exported {filename}: {records[filename]['nodes']} nodes, {records[filename]['bytes']} bytes", flush=True)
            del graph
        report = {
            "format": "radish-kokoro-runtime-models-v1", "source": str(model_path.resolve()),
            "source_sha256": MODEL_SHA256, "graphs": records, "onnx": onnx.__version__,
            "shape_rule": "F0/N=2*T; har/core frames=120*T+1; waveform samples=600*T; batch=1",
            "scope": "Dynamic original CPU frontend/source/core/tail graphs; not ncnn conversion or device validation.",
        }
        (stage / "runtime-models.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Private dynamic models: {output.resolve()}", flush=True)


def verify_models(directory):
    report = json.loads((directory / "runtime-models.json").read_text())
    require(report["format"] == "radish-kokoro-runtime-models-v1" and report["source_sha256"] == MODEL_SHA256,
            "unexpected runtime model provenance")
    require(set(report["graphs"]) == set(GRAPHS), "incomplete runtime model set")
    for filename in GRAPHS:
        expected = report["graphs"][filename]
        require(asset(directory / filename) == {key: expected[key] for key in ("bytes", "sha256")},
                f"changed runtime graph: {filename}")
    return report


def run_ort(path, inputs, threads):
    import onnxruntime as ort
    options = ort.SessionOptions()
    options.intra_op_num_threads = threads
    options.inter_op_num_threads = 1
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    options.add_session_config_entry("session.intra_op.allow_spinning", "0")
    start = time.monotonic()
    session = ort.InferenceSession(str(path), options, providers=["CPUExecutionProvider"])
    loaded = time.monotonic()
    values = session.run(None, inputs)
    finished = time.monotonic()
    names = [value.name for value in session.get_outputs()]
    del session
    gc.collect()
    require(all(value.dtype == np.float32 and np.isfinite(value).all() for value in values),
            f"non-FP32 or nonfinite result from {path.name}")
    print(f"Ran {path.name}: load {loaded-start:.3f}s, inference {finished-loaded:.3f}s", flush=True)
    return dict(zip(names, values)), {"load_seconds": loaded-start, "inference_seconds": finished-loaded}


def capture(models, input_path, output, speed, threads, token_limit, max_frames):
    import onnxruntime as ort
    models_report = verify_models(models)
    with np.load(input_path, allow_pickle=False) as data:
        ids, style = data["ids"], data["style"]
    require(ids.dtype == np.int64 and (ids.ndim == 1 or (ids.ndim == 2 and ids.shape[0] == 1)),
            "ids must be int64 [L] or [1,L]")
    ids = ids.reshape(-1)
    require(3 <= len(ids) <= 512 and ids[0] == 0 and ids[-1] == 0 and
            ((ids[1:-1] >= 1) & (ids[1:-1] <= 177)).all(), "invalid padded Kokoro token IDs")
    original_tokens = len(ids) - 2
    if token_limit is not None:
        require(1 <= token_limit <= original_tokens, "token limit must select an existing nonempty token prefix")
        ids = np.concatenate((ids[:token_limit+1], np.zeros(1, dtype=np.int64)))
    ids = np.ascontiguousarray(ids.reshape(1, -1))
    require(style.dtype == np.float32 and style.shape in ((256,), (1, 256)) and np.isfinite(style).all(),
            "style must be finite FP32 [256] or [1,256]")
    style = np.ascontiguousarray(style.reshape(1, 256))
    require(np.isfinite(speed) and 0 < speed <= np.finfo(np.float32).max, "speed must be positive finite FP32")
    speed_array = np.array([speed], dtype=np.float32)
    require(float(speed_array[0]) > 0, "speed underflows FP32")
    input_hash = sha256(input_path)
    with new_private_directory(output) as stage:
        front, front_time = run_ort(models / GRAPHS[0],
                                    {"input_ids": ids, "style": style, "speed": speed_array}, threads)
        boundary = {key: front[NAMES[key]] for key in ("asr", "f0", "n", "har")}
        boundary["style"] = style
        asr = boundary["asr"]
        require(asr.ndim == 3 and asr.shape[:2] == (1, 512), "unexpected asr shape")
        count = int(asr.shape[2])
        require(1 <= count <= max_frames, f"duration {count} exceeds requested bound {max_frames}")
        frames, samples = 120*count+1, 600*count
        expected = {"asr": (1, 512, count), "f0": (1, 2*count), "n": (1, 2*count),
                    "style": (1, 256), "har": (1, 22, frames)}
        require(all(value.shape == expected[key] for key, value in boundary.items()), "inconsistent frontend shapes")
        durations = front[NAMES["durations"]]
        require(durations.shape == ids.shape and (durations >= 1).all() and
                np.equal(durations, np.floor(durations)).all() and float(durations.sum(dtype=np.float64)) == count,
                "durations do not match tokens and decoder length")
        source, source_time = run_ort(models / GRAPHS[1], {NAMES["f0"]: boundary["f0"]}, threads)
        require(source[NAMES["src"]].shape == (1, samples, 1) and
                np.array_equal(source[NAMES["har"]], boundary["har"]),
                "separate source graph differs from frontend har")
        del source
        core, core_time = run_ort(models / GRAPHS[2], {NAMES[key]: value for key, value in boundary.items()}, threads)
        reference = core[NAMES["core"]]
        require(reference.shape == (1, 22, frames), "unexpected CPU core output shape")
        tail, tail_time = run_ort(models / GRAPHS[3], {NAMES["core"]: reference}, threads)
        waveform = tail[NAMES["waveform"]]
        require(waveform.shape == (1, samples), "unexpected CPU waveform output shape")
        arrays = {**boundary, "reference": reference, "reference-waveform": waveform,
                  "durations": durations, "speed": speed_array}
        for name, value in arrays.items():
            value.astype("<f4", copy=False).tofile(stage / (name + ".f32"))
            np.save(stage / (name + ".npy"), value, allow_pickle=False)
        ids.astype("<i8", copy=False).tofile(stage / "ids.i64")
        np.save(stage / "ids.npy", ids, allow_pickle=False)
        files = {path.name: asset(path) for path in sorted(stage.iterdir())}
        report = {
            "format": "radish-kokoro-runtime-fixture-v1", "T": count, "frames": frames,
            "samples": samples, "sample_rate": 24000, "source_sha256": MODEL_SHA256,
            "input_npz": str(input_path.resolve()), "input_npz_sha256": input_hash,
            "input_is_original_fixture": input_hash == FIXTURE_SHA256,
            "original_nonpad_tokens": original_tokens, "selected_nonpad_tokens": ids.size-2,
            "selection": "all" if token_limit is None else "prefix", "speed": float(speed_array[0]),
            "models_directory": str(models.resolve()), "models": models_report["graphs"],
            "shapes": {name: list(value.shape) for name, value in arrays.items()}, "files": files,
            "runtime": {"onnxruntime": ort.__version__, "onnx": onnx.__version__, "numpy": np.__version__,
                        "provider": "CPUExecutionProvider", "intra_threads": threads, "inter_threads": 1,
                        "graph_optimization": "disabled", "source_crosscheck": "bit-identical"},
            "timings": {"frontend": front_time, "source_crosscheck": source_time, "core": core_time, "tail": tail_time},
            "accuracy_gates": {"core_atol": 0.002, "core_rtol": 0.002,
                               "waveform_atol": 2e-4, "waveform_rtol": 2e-3, "waveform_minimum_snr_db": 50.0},
            "scope": "Fresh CPU frontend/source boundary and original CPU core/tail references using those exact tensors. "
                     "Only ids/style are read from the input archive; saved old boundaries/waveforms are not reused. "
                     "Not an unsplit-model identity, ncnn, device, or end-to-end performance claim.",
        }
        (stage / "runtime.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Captured fresh boundary and references: T={count}, frames={frames}, samples={samples}: {output.resolve()}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    export_parser = commands.add_parser("export", help="export four dynamic graphs from the pinned original model")
    export_parser.add_argument("model", type=Path)
    export_parser.add_argument("output", type=Path, help="new private models directory below shared Radish work")
    capture_parser = commands.add_parser("capture", help="compute fresh inputs and CPU references from NPZ ids/style")
    capture_parser.add_argument("models", type=Path)
    capture_parser.add_argument("inputs", type=Path, help="NPZ containing padded int64 ids and FP32 style; other arrays ignored")
    capture_parser.add_argument("output", type=Path, help="new private fixture directory below shared Radish work")
    capture_parser.add_argument("--speed", type=float, default=1.0)
    capture_parser.add_argument("--threads", type=int, choices=range(1, 7), default=4)
    capture_parser.add_argument("--token-limit", type=int, help="use this many nonpad prefix tokens, preserving both pads")
    capture_parser.add_argument("--max-frames", type=int, default=4096, help="reject a longer frontend duration before core inference")
    args = parser.parse_args()
    try:
        if args.command == "export":
            export(args.model, args.output)
        else:
            require(args.max_frames >= 1, "max frames must be positive")
            capture(args.models, args.inputs, args.output, args.speed, args.threads, args.token_limit, args.max_frames)
    except (OSError, ValueError, KeyError, onnx.checker.ValidationError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
