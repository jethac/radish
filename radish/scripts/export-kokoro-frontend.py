#!/usr/bin/env python3
"""Reproduce the pinned standalone CPU frontend/source graphs from original Kokoro."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path

import onnx
from onnx.utils import Extractor


specification = importlib.util.spec_from_file_location(
    "radish_runtime_export", Path(__file__).with_name("export-kokoro-runtime.py"))
runtime = importlib.util.module_from_spec(specification)
specification.loader.exec_module(runtime)
runtime.WORK = Path(os.environ.get("RADISH_WORK", str(runtime.WORK)))
require = runtime.require
PINS = {
    "frontend-only.onnx": (111933749, "56ba27008352a2191b631ebbf269bf4506d690a1b83d8ff8ff5524b11291ddd3"),
    "harmonic-source.onnx": (13810, "4bdcb9f2f54d6efbfdcefce384300352933cc71fa562930f23eaeb3443528fd2"),
}


def export(model_path, output):
    require(runtime.sha256(model_path) == runtime.MODEL_SHA256, "unexpected original FP32 model hash")
    model = onnx.load(model_path)
    require([value.name for value in model.graph.input] == ["input_ids", "style", "speed"],
            "unexpected original model inputs")
    require(not any("Random" in node.op_type for node in model.graph.node), "unexpected random operator")
    # Preserve the existing four-graph export and its pins. Derive the exact
    # previously validated frontend-only artifact through the same intermediate.
    combined = runtime.extract(model, list(model.graph.input),
                               [runtime.tensor(key) for key in ("asr", "f0", "n", "har", "durations")], "frontend")
    require(hashlib.sha256(combined.SerializeToString()).hexdigest() ==
            "586121ea16febcac7431dcda362b29b95581fa60688f26010fe457d6bf75ed18",
            "combined frontend serialization changed; inspect ONNX version")
    frontend = Extractor(combined).extract_model([value.name for value in combined.graph.input],
        [runtime.NAMES[key] for key in ("asr", "f0", "n", "durations")])
    source = runtime.extract(model, [runtime.tensor("f0")],
                             [runtime.tensor("src"), runtime.tensor("har")], "harmonic-source")
    with runtime.new_private_directory(output) as stage:
        records = {}
        for name, graph in (("frontend-only.onnx", frontend), ("harmonic-source.onnx", source)):
            onnx.checker.check_model(graph)
            onnx.save(graph, stage / name)
            record = runtime.asset(stage / name)
            require((record["bytes"], record["sha256"]) == PINS[name],
                    f"{name} differs from the validated graph; inspect ONNX version")
            records[name] = {**record, "nodes": len(graph.graph.node),
                             "initializers": len(graph.graph.initializer),
                             "inputs": [value.name for value in graph.graph.input],
                             "outputs": [value.name for value in graph.graph.output]}
        report = {"format": "radish-kokoro-frontend-models-v1", "source": str(model_path.resolve()),
                  "source_sha256": runtime.MODEL_SHA256, "onnx": onnx.__version__, "graphs": records,
                  "scope": "CPU frontend-only graph followed by the original harmonic source; existing four-graph runtime export unchanged."}
        (stage / "frontend-models.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Reproduced pinned frontend/source graphs: {output.resolve()}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path, help="pinned original FP32 Kokoro model with durations")
    parser.add_argument("output", type=Path, help="new private directory under RADISH_WORK")
    args = parser.parse_args()
    try:
        export(args.model, args.output)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
