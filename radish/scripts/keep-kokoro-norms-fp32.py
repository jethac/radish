#!/usr/bin/env python3
"""Keep the pinned Kokoro normalization chains in FP32 GPU storage."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


WORK = Path("/run/media/jethac/Shared/radish")
PARAM_SHA256 = "857d5c2e8f3ef728f33baea250fb58090a099818e8cfec2821613fc6b851eb36"
CPU_SHA256 = "d09792b7b6ffd1e19e654e14e7852380084851f5d3918724f96a708975b268ec"
specification = importlib.util.spec_from_file_location(
    "radish_fixed_shape_helpers", Path(__file__).with_name("hint-kokoro-shapes.py"))
helpers = importlib.util.module_from_spec(specification)
specification.loader.exec_module(helpers)
require, sha256 = helpers.require, helpers.sha256


def read_graph(data):
    lines = data.decode().splitlines(keepends=True)
    require(len(lines) == 1370 and [line.strip() for line in lines[:2]] == ["7767517", "1368 1739"],
            "unexpected pinned graph header")
    rows, producers, shapes, names = [], {}, {}, set()
    for index, line in enumerate(lines[2:]):
        fields = line.split()
        kind, name = fields[:2]
        ni, no = map(int, fields[2:4])
        require(ni >= 0 and no > 0 and name not in names, "invalid layer inventory")
        names.add(name)
        bottoms = fields[4:4+ni]
        tops = fields[4+ni:4+ni+no]
        require(len(tops) == no and all(value in producers for value in bottoms),
                f"broken graph order at {name}")
        params = {}
        for field in fields[4+ni+no:]:
            key, value = field.split("=", 1)
            require(key not in params and key != "31", f"unexpected existing feature mask or repeated parameter: {name}")
            params[key] = value
        raw = list(map(int, params.pop("-23330").split(",")))
        require(raw[0] == 5*no and len(raw) == 1+5*no, f"invalid shape annotation: {name}")
        row = {"index": index, "type": kind, "name": name, "bottoms": bottoms, "tops": tops, "params": params}
        rows.append(row)
        for slot, top in enumerate(tops):
            shape = raw[1+slot*5:6+slot*5]
            dims, width, height, depth, channels = shape
            require(top not in producers and 1 <= dims <= 4 and min(width, height, depth, channels) > 0,
                    f"invalid blob shape or repeated producer: {top}")
            require((dims >= 2 or height == 1) and (dims == 4 or depth == 1) and (dims >= 3 or channels == 1),
                    f"noncanonical shape: {top}")
            producers[top], shapes[top] = row, shape
    require(len(producers) == 1739, "unexpected blob inventory")
    return lines, rows, producers, shapes


def normalization_chains(rows, producers, shapes):
    selected, groups, arithmetic = set(), [], set()

    def resolve(blob, chain):
        while producers[blob]["type"] == "Split":
            row = producers[blob]
            require(len(row["bottoms"]) == 1 and not row["params"], "nontransparent Split")
            source = row["bottoms"][0]
            require(all(shapes[top] == shapes[source] for top in row["tops"]), "Split changes a logical shape")
            chain.add(row["index"])
            blob = source
        return blob

    def operation(blob, kind, params, chain):
        row = producers[resolve(blob, chain)]
        require(row["type"] == kind and row["params"] == params and len(row["tops"]) == 1,
                f"unexpected normalization operator: {row['name']}")
        chain.add(row["index"])
        return row

    mean_params = {"0": "3", "1": "0", "-23303": "1,1", "4": "1", "5": "1"}
    for division in rows:
        if division["type"] != "BinaryOp" or division["params"] != {"0": "3"}:
            continue
        require(len(division["bottoms"]) == 2 and len(division["tops"]) == 1, "unexpected tensor division")
        chain = {division["index"]}
        root = operation(division["bottoms"][1], "UnaryOp", {"0": "5"}, chain)
        require(len(root["bottoms"]) == 1, "sqrt must have one input")
        epsilon = operation(root["bottoms"][0], "BinaryOp", {"0": "0", "1": "1", "2": "1.000000e-5"}, chain)
        require(len(epsilon["bottoms"]) == 1 and float(epsilon["params"]["2"]) == 1e-5, "wrong normalization epsilon")
        variance = operation(epsilon["bottoms"][0], "Reduction", mean_params, chain)
        require(len(variance["bottoms"]) == 1, "variance mean must have one input")
        square = operation(variance["bottoms"][0], "BinaryOp", {"0": "2"}, chain)
        require(len(square["bottoms"]) == 2, "square must multiply two aliases")
        centered = resolve(square["bottoms"][0], chain)
        require(resolve(square["bottoms"][1], chain) == centered and
                resolve(division["bottoms"][0], chain) == centered, "square/div numerator aliases differ")
        subtract = operation(centered, "BinaryOp", {"0": "1"}, chain)
        require(len(subtract["bottoms"]) == 2, "centering must subtract the mean")
        mean = operation(subtract["bottoms"][1], "Reduction", mean_params, chain)
        require(len(mean["bottoms"]) == 1, "centering mean must have one input")
        source = resolve(subtract["bottoms"][0], chain)
        require(resolve(mean["bottoms"][0], chain) == source, "mean and subtraction use different inputs")
        shape = shapes[source]
        require(shape[0] == 2 and shape[1] > 1, "normalization must reduce the time axis of a rank-two scalar tensor")
        reduced = [2, 1, shape[2], 1, 1]
        for row in (mean, variance, epsilon, root):
            require(shapes[row["tops"][0]] == reduced, f"wrong reduced shape: {row['name']}")
        for row in (subtract, square, division):
            require(shapes[row["tops"][0]] == shape, f"wrong normalization shape: {row['name']}")
        operators = [mean, subtract, square, variance, epsilon, root, division]
        indexes = {row["index"] for row in operators}
        require(len(indexes) == 7 and not (indexes & arithmetic), "normalization operators overlap")
        arithmetic.update(indexes)
        selected.update(chain)
        groups.append({"input_blob": source, "output_blob": division["tops"][0],
                       "epsilon_blob": epsilon["tops"][0], "logical_shape": shape,
                       "operators": [{"index": row["index"], "name": row["name"], "type": row["type"]} for row in operators],
                       "split_indexes": sorted(chain-indexes)})
    reductions = {row["index"] for row in rows if row["type"] == "Reduction"}
    roots = {row["index"] for row in rows if row["type"] == "UnaryOp" and row["params"] == {"0": "5"}}
    epsilons = {row["index"] for row in rows if row["type"] == "BinaryOp" and row["params"].get("2") == "1.000000e-5"}
    require(len(groups) == 54 and len(reductions) == 108 and reductions <= arithmetic,
            "expected exactly 54 normalization chains covering all 108 means")
    require(len(roots) == 54 and len(epsilons) == 54 and roots <= arithmetic and epsilons <= arithmetic,
            "sqrt/epsilon coverage differs from normalization chains")
    require(any(group["epsilon_blob"] == "437" and group["output_blob"] == "439" for group in groups),
            "diagnosed first long harmonic normalization is missing")
    return selected, groups


def create(source, output, runner):
    source, runner = source.resolve(), runner.resolve()
    require(runner.is_file() and os.access(runner, os.X_OK), "runner must be an existing host radish-model executable")
    require(not output.exists() and not output.is_symlink(), "output must be new")
    output = output.resolve()
    require(output.is_relative_to(WORK.resolve()) and output != WORK.resolve() and not output.is_relative_to(source),
            "output must be a new private directory below shared Radish work, outside the source")
    data = (source / "decoder.ncnn.param").read_bytes()
    require(hashlib.sha256(data).hexdigest() == PARAM_SHA256, "unexpected pinned hinted graph")
    linked = {}
    for name, (size, digest) in helpers.FIXTURES.items():
        path = source / name
        require(path.is_file() and path.stat().st_size == size and sha256(path) == digest,
                f"unexpected immutable source asset: {name}")
        linked[name] = {"bytes": size, "sha256": digest}
    lines, rows, producers, shapes = read_graph(data)
    selected, groups = normalization_chains(rows, producers, shapes)
    for index in selected:
        lines[index+2] = lines[index+2].rstrip("\n") + " 31=2\n"
    parameters = "".join(lines).encode()
    require(parameters.replace(b" 31=2\n", b"\n") == data, "unexpected change beyond precision masks")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".kokoro-norm32-", dir=output.parent))
    reserved = False
    try:
        for name in linked:
            os.link(source / name, stage / name)
        (stage / "decoder.ncnn.param").write_bytes(parameters)
        runner_sha = sha256(runner)
        command = [str(runner), "."]
        with (stage / "cpu-fp32-validation.log").open("x") as log:
            result = subprocess.run(command, cwd=stage, stdout=log, stderr=subprocess.STDOUT, timeout=300, check=False)
        require(result.returncode == 0, "CPU FP32 reference gate failed: " + (stage / "cpu-fp32-validation.log").read_text()[-2000:])
        cpu_sha = sha256(stage / "ncnn-cpu-output.f32")
        require(cpu_sha == CPU_SHA256, f"CPU FP32 output changed: {cpu_sha}")
        require(sha256(runner) == runner_sha and sha256(source / "decoder.ncnn.param") == PARAM_SHA256,
                "runner or source graph changed during validation")
        for name, expected in linked.items():
            require(sha256(source / name) == expected["sha256"] and sha256(stage / name) == expected["sha256"],
                    f"immutable source asset changed: {name}")
        report = {
            "format": "radish-kokoro-norm32-v1", "source_directory": str(source),
            "source_param_sha256": PARAM_SHA256, "param_sha256": hashlib.sha256(parameters).hexdigest(),
            "required_patch": "0015-ncnn-radish-mixed-storage.patch",
            "required_environment": {"RADISH_NCNN_MIXED_STORAGE": "1"},
            "feature_mask": 2, "normalization_chains": len(groups), "masked_layers": len(selected),
            "masked_split_layers": sum(rows[index]["type"] == "Split" for index in selected),
            "semantics": "Standard parameter31 bit1 disables packed/native FP16 storage for complete normalization chains and Split aliases; Vulkan and FP32 arithmetic remain enabled. Patch0015 supplies explicit GPU storage conversions at their boundaries.",
            "layers": [{"index": index, "name": rows[index]["name"], "type": rows[index]["type"]} for index in sorted(selected)],
            "groups": groups, "linked_files": linked,
            "cpu_validation": {"runner": str(runner), "runner_sha256": runner_sha, "command": command,
                               "working_directory": ".", "output_sha256": cpu_sha,
                               "result": "Full existing FP32 reference gate passed; output is byte-identical to the pinned unmasked CPU baseline."},
            "accuracy_gates": {"core_atol": 0.002, "core_rtol": 0.002},
            "gpu_validation": "Pending. This graph requires RADISH_NCNN_MIXED_STORAGE=1 and patch0015 before packed-FP16 GPU execution.",
        }
        (stage / "norm32.json").write_text(json.dumps(report, indent=2) + "\n")
        output.mkdir()
        reserved = True
        os.replace(stage, output)
        reserved = False
    finally:
        if stage.exists():
            shutil.rmtree(stage)
        if reserved:
            output.rmdir()
    print(f"Validated {len(groups)} normalization chains / {len(selected)} masked layers: {output}", flush=True)
    print(f"CPU FP32 output unchanged: {CPU_SHA256}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="pinned kokoro-core-shapes directory")
    parser.add_argument("output", type=Path, help="new private normalization-FP32 variant")
    parser.add_argument("--runner", type=Path, required=True, help="existing host radish-model for unchanged CPU FP32 validation")
    args = parser.parse_args()
    try:
        create(args.source, args.output, args.runner)
    except (OSError, ValueError, KeyError, IndexError, subprocess.TimeoutExpired) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
