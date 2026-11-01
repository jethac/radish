#!/usr/bin/env python3
"""Create and CPU-validate an opt-in InstanceNorm variant of the pinned Kokoro core."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


specification = importlib.util.spec_from_file_location(
    "radish_norm_helpers", Path(__file__).with_name("keep-kokoro-norms-fp32.py"))
norm = importlib.util.module_from_spec(specification)
specification.loader.exec_module(norm)
require, sha256, WORK = norm.require, norm.sha256, norm.WORK


def rewrite(data):
    _, original, producers, shapes = norm.read_graph(data)
    _, groups = norm.normalization_chains(original, producers, shapes)
    names = {row["name"] for row in original}
    replaced, arithmetic = {}, set()
    for number, group in enumerate(groups):
        operators = group["operators"]
        arithmetic.update(item["index"] for item in operators)
        subtract = original[operators[1]["index"]]
        division = original[operators[-1]["index"]]
        source = subtract["bottoms"][0]
        width, channels = group["logical_shape"][1:3]
        prefix = f"radish_norm_{number}"
        blob3, normalized = prefix + "_3d", prefix + "_normalized"
        require(blob3 not in producers and normalized not in producers,
                "new normalization blob collides with the original graph")
        shape3 = [3, width, 1, 1, channels]
        shapes[blob3], shapes[normalized] = shape3, shape3
        definitions = [
            ("Reshape", prefix + "_to3d", [source], [blob3], {"0": "-1", "1": "1", "2": str(channels), "31": "2"}),
            ("InstanceNorm", prefix + "_instance", [blob3], [normalized], {"0": str(channels), "1": "1.000000e-5", "2": "0", "31": "2"}),
            ("Reshape", prefix + "_to2d", [normalized], division["tops"], {"0": "-1", "1": str(channels), "31": "2"}),
        ]
        replacements = []
        for slot, (kind, name, bottoms, tops, params) in enumerate(definitions):
            require(name not in names, "new normalization layer name collides")
            names.add(name)
            replacements.append({"index": -1-number*3-slot, "type": kind, "name": name,
                                 "bottoms": bottoms, "tops": tops, "params": params})
        replaced[division["index"]] = replacements
        group["retained_input_alias"] = source
        group["replacement_layers"] = [row["name"] for row in replacements]
        group["rank_three_shape"] = shape3

    rewritten = []
    for row in original:
        rewritten.extend(replaced.get(row["index"], [row]))
    rewritten_producers = {top: row for row in rewritten for top in row["tops"]}
    live_blobs, live_layers, pending = set(), set(), ["out0"]
    while pending:
        blob = pending.pop()
        if blob in live_blobs:
            continue
        live_blobs.add(blob)
        row = rewritten_producers[blob]
        if row["index"] not in live_layers:
            live_layers.add(row["index"])
            pending.extend(row["bottoms"])

    require(not (arithmetic & live_layers), "an original normalization operator still has an external consumer")
    removed = [row for row in original if row["index"] not in live_layers]
    require(all(row["index"] in arithmetic or row["type"] == "Split" for row in removed),
            "rewrite would remove an unrelated or weighted layer")
    kept, final_producers, final_shapes, consumers = [], {}, {}, {}
    for row in rewritten:
        if row["index"] not in live_layers:
            continue
        tops = [top for top in row["tops"] if top in live_blobs]
        require(tops and (tops == row["tops"] or row["type"] == "Split"),
                "only transparent Split may lose dead outputs")
        require(all(bottom in final_producers for bottom in row["bottoms"]), "rewritten graph is not topological")
        for bottom in row["bottoms"]:
            consumers.setdefault(bottom, set()).add(row["name"])
        row = dict(row, tops=tops)
        kept.append(row)
        for top in tops:
            require(top not in final_producers, "duplicate rewritten producer")
            final_producers[top] = (len(kept)-1, len(final_producers))
            final_shapes[top] = shapes[top]
    require(all(len(value) == 1 for value in consumers.values()),
            "rewrite introduced an implicit fan-out instead of retaining Split aliases")
    require(sum(row["type"] == "InstanceNorm" for row in kept) == 54
            and not any(row["type"] == "Reduction" for row in kept), "normalization fusion coverage is incomplete")
    require(len(kept) == 1098 and len(final_producers) == 1307, "unexpected fixed fused graph inventory")
    require([row for row in kept if row["index"] >= 0 and row["type"] != "Split"]
            == [row for row in original if row["index"] not in arithmetic and row["type"] != "Split"],
            "an unrelated layer changed order, inputs, outputs, parameters, or weights")

    lines = ["7767517\n", f"{len(kept)} {len(final_producers)}\n"]
    for row in kept:
        hint = [5*len(row["tops"])]
        for top in row["tops"]:
            hint.extend(final_shapes[top])
        fields = [row["type"], row["name"], str(len(row["bottoms"])), str(len(row["tops"]))]
        fields += row["bottoms"] + row["tops"]
        fields += [key + "=" + value for key, value in row["params"].items()]
        fields += ["-23330=" + ",".join(map(str, hint))]
        lines.append(" ".join(fields) + "\n")
    return "".join(lines).encode(), kept, final_producers, final_shapes, groups, removed


def create(source, output, runner):
    source, runner = source.resolve(), runner.resolve()
    require(runner.is_file() and os.access(runner, os.X_OK), "runner must be an existing host radish-model executable")
    require(not output.exists() and not output.is_symlink(), "output must be new")
    output = output.resolve()
    require(output.is_relative_to(WORK.resolve()) and output != WORK.resolve() and not output.is_relative_to(source),
            "output must be a new private directory below shared Radish work, outside the source")
    data = (source / "decoder.ncnn.param").read_bytes()
    require(hashlib.sha256(data).hexdigest() == norm.PARAM_SHA256, "unexpected pinned hinted graph")
    linked = {}
    for name, (size, digest) in norm.helpers.FIXTURES.items():
        path = source / name
        require(path.is_file() and path.stat().st_size == size and sha256(path) == digest,
                f"unexpected immutable source asset: {name}")
        linked[name] = {"bytes": size, "sha256": digest}
    parameters, rows, producers, shapes, groups, removed = rewrite(data)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".kokoro-instancenorm-", dir=output.parent))
    reserved = False
    try:
        for name in linked:
            os.link(source / name, stage / name)
        (stage / "decoder.ncnn.param").write_bytes(parameters)
        runner_sha = sha256(runner)
        command = [str(runner), "--capture-shapes", "cpu-shapes.tsv", "."]
        with (stage / "cpu-fp32-validation.log").open("x") as log:
            result = subprocess.run(command, cwd=stage, stdout=log, stderr=subprocess.STDOUT, timeout=300, check=False)
        require(result.returncode == 0, "CPU FP32 reference gate failed: " + (stage / "cpu-fp32-validation.log").read_text()[-3000:])
        actual_shapes = norm.helpers.read_shapes(stage / "cpu-shapes.tsv", producers)
        require(actual_shapes == shapes, "captured fused shapes disagree with the graph hints")
        cpu_sha = sha256(stage / "ncnn-cpu-output.f32")
        require(sha256(runner) == runner_sha and sha256(source / "decoder.ncnn.param") == norm.PARAM_SHA256,
                "runner or source graph changed during validation")
        for name, expected in linked.items():
            require(sha256(source / name) == expected["sha256"] and sha256(stage / name) == expected["sha256"],
                    f"immutable source asset changed: {name}")
        report = {
            "format": "radish-kokoro-instancenorm-v1", "source_directory": str(source),
            "source_param_sha256": norm.PARAM_SHA256, "param_sha256": hashlib.sha256(parameters).hexdigest(),
            "required_patch_for_fp16_packed": "0015-ncnn-radish-mixed-storage.patch",
            "required_environment_for_fp16_packed": {"RADISH_NCNN_MIXED_STORAGE": "1"},
            "normalization_chains": len(groups), "layers": len(rows), "blobs": len(producers),
            "instance_norm_affine": 0, "epsilon": 1e-5, "replacement_feature_mask": 2,
            "semantics": "Each rank-two channel/time tensor is reshaped to [w=time,h=1,c=channels], normalized independently per channel, and restored to rank two. Original Split input aliases and downstream learned style affine operations are preserved. Affine-disabled InstanceNorm consumes no weights. New layers retain FP32 GPU storage.",
            "rounding": "InstanceNorm uses reciprocal-square-root coefficients and x*a+b; floating-point rounding can differ from the original subtraction/division chain. Existing full core reference tolerances are unchanged.",
            "groups": groups,
            "removed_layers": [{"index": row["index"], "name": row["name"], "type": row["type"]} for row in removed],
            "linked_files": linked,
            "cpu_validation": {"runner": str(runner), "runner_sha256": runner_sha, "command": command,
                               "working_directory": ".", "output_sha256": cpu_sha,
                               "captured_shapes": len(actual_shapes), "shapes_sha256": sha256(stage / "cpu-shapes.tsv"),
                               "log_sha256": sha256(stage / "cpu-fp32-validation.log"),
                               "result": "Existing full FP32 reference gate passed; every retained and inserted logical shape matched its declared hint."},
            "accuracy_gates": {"core_atol": 0.002, "core_rtol": 0.002},
            "gpu_validation": "Pending. This is an opt-in private graph variant; GPU correctness and performance are not established.",
        }
        (stage / "instancenorm.json").write_text(json.dumps(report, indent=2) + "\n")
        output.mkdir()
        reserved = True
        os.replace(stage, output)
        reserved = False
    finally:
        if stage.exists():
            shutil.rmtree(stage)
        if reserved:
            output.rmdir()
    print(f"Validated {len(groups)} fused normalization chains / {len(rows)} layers / {len(producers)} blobs: {output}", flush=True)
    print(f"CPU FP32 reference passed; output SHA256: {cpu_sha}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="pinned kokoro-core-shapes directory")
    parser.add_argument("output", type=Path, help="new private InstanceNorm variant")
    parser.add_argument("--runner", type=Path, required=True, help="existing host radish-model for unchanged CPU FP32 validation")
    args = parser.parse_args()
    try:
        create(args.source, args.output, args.runner)
    except (OSError, ValueError, KeyError, IndexError, subprocess.TimeoutExpired) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
