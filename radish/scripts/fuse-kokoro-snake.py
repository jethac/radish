#!/usr/bin/env python3
"""Prepare a provisional FP32 Snake graph; no tensor execution or accuracy claim."""

import argparse
from collections import Counter, defaultdict
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile


WORK = Path("/run/media/jethac/Shared/radish")
PARAM_SHA256 = "6499e4996cea79d28e9c72caab1e2f0ca7d9fab4420a6f198c7e197fb06d347f"
SOURCE_INVENTORY = {"Input": 5, "Split": 110, "Crop": 1, "InnerProduct": 58,
                    "Reshape": 168, "Slice": 58, "Convolution1D": 69, "Concat": 5,
                    "InstanceNorm": 54, "BinaryOp": 360, "ReLU": 13, "MemoryData": 96,
                    "UnaryOp": 96, "Interp": 1, "DeconvolutionDepthWise1D": 1,
                    "Deconvolution1D": 2, "Padding": 1}
TARGET_INVENTORY = dict(SOURCE_INVENTORY, BinaryOp=216, RadishSnake=48)
del TARGET_INVENTORY["UnaryOp"]
WEIGHTED = {"InnerProduct", "Convolution1D", "Deconvolution1D", "DeconvolutionDepthWise1D", "MemoryData"}
sys.dont_write_bytecode = True
specification = importlib.util.spec_from_file_location(
    "radish_fixed_fixtures", Path(__file__).with_name("hint-kokoro-shapes.py"))
helpers = importlib.util.module_from_spec(specification)
specification.loader.exec_module(helpers)
require, sha256 = helpers.require, helpers.sha256


def read_graph(data, layers, blobs, inventory):
    lines = data.decode().splitlines(keepends=True)
    require(len(lines) == layers + 2 and [line.strip() for line in lines[:2]] == ["7767517", f"{layers} {blobs}"],
            "unexpected graph header or row count")
    rows, producers, shapes, names = [], {}, {}, set()
    consumers = defaultdict(list)
    for index, line in enumerate(lines[2:]):
        fields = line.split()
        require(len(fields) >= 4, "incomplete graph row")
        kind, name = fields[:2]
        ni, no = map(int, fields[2:4])
        require(ni >= 0 and no > 0 and name not in names and len(fields) >= 4 + ni + no,
                "invalid layer name or arity")
        names.add(name)
        bottoms, tops = fields[4:4+ni], fields[4+ni:4+ni+no]
        require(all(bottom in producers for bottom in bottoms), f"non-topological input at {name}")
        params = {}
        for token in fields[4+ni+no:]:
            key, value = token.split("=", 1)
            require(key not in params, f"duplicate parameter at {name}")
            params[key] = value
        raw = list(map(int, params.pop("-23330").split(",")))
        require(raw[0] == 5*no and len(raw) == 1+5*no, f"invalid shape hint at {name}")
        row = dict(index=index, type=kind, name=name, bottoms=bottoms, tops=tops, params=params, raw=line)
        rows.append(row)
        for slot, top in enumerate(tops):
            dims, width, height, depth, channels = hint = raw[1+5*slot:6+5*slot]
            require(top not in producers and 1 <= dims <= 3 and min(hint[1:]) > 0 and depth == 1
                    and (dims >= 2 or height == 1) and (dims >= 3 or channels == 1),
                    f"noncanonical or duplicate shape: {top}")
            producers[top], shapes[top] = row, hint
        for slot, bottom in enumerate(bottoms):
            consumers[bottom].append((row, slot))
    require(len(producers) == blobs and Counter(row["type"] for row in rows) == inventory,
            "unexpected blob/operator inventory")
    require(set(producers) - set(consumers) == {"out0"} and all(len(value) == 1 for value in consumers.values()),
            "unexpected unused output or implicit fan-out")
    return rows, producers, shapes, consumers


def render(row, shapes):
    fields = [row["type"], row["name"], str(len(row["bottoms"])), str(len(row["tops"]))]
    fields += row["bottoms"] + row["tops"]
    fields += [key + "=" + value for key, value in row["params"].items()]
    hint = [5*len(row["tops"])] + [value for top in row["tops"] for value in shapes[top]]
    return " ".join(fields + ["-23330=" + ",".join(map(str, hint))]) + "\n"


def rewrite(data):
    require(hashlib.sha256(data).hexdigest() == PARAM_SHA256, "unexpected pinned InstanceNorm graph")
    rows, producers, shapes, consumers = read_graph(data, 1098, 1307, SOURCE_INVENTORY)
    groups, removed, replacements, split_tops, constants = [], set(), {}, {}, set()

    def operation(row, kind, params, inputs):
        require(row["type"] == kind and row["params"] == params and len(row["bottoms"]) == inputs
                and len(row["tops"]) == 1, f"unexpected Snake operator: {row['name']}")
        return row

    def next_row(row):
        require(len(row["tops"]) == 1 and len(consumers[row["tops"][0]]) == 1, "Snake intermediate has another consumer")
        return consumers[row["tops"][0]][0][0]

    for sine in rows:
        if sine["type"] != "UnaryOp" or sine["params"].get("0") != "9":
            continue
        operation(sine, "UnaryOp", {"0": "9"}, 1)
        first = operation(producers[sine["bottoms"][0]], "BinaryOp", {"0": "2"}, 2)
        square = operation(next_row(sine), "UnaryOp", {"0": "4"}, 1)
        second = operation(next_row(square), "BinaryOp", {"0": "2"}, 2)
        final = operation(next_row(second), "BinaryOp", {"0": "0"}, 2)
        require(next_row(first) is sine and square["bottoms"] == sine["tops"]
                and second["bottoms"][1] == square["tops"][0]
                and final["bottoms"][1] == second["tops"][0], "Snake edge/operand order mismatch")
        alpha, x = first["bottoms"]
        reciprocal, residual = second["bottoms"][0], final["bottoms"][0]
        split = producers[x]
        require(split["type"] == "Split" and not split["params"] and len(split["bottoms"]) == 1
                and len(split["tops"]) == 2 and set(split["tops"]) == {x, residual}
                and producers[residual] is split and split["index"] not in split_tops,
                "Snake residual must be the other output of one distinct transparent Split")
        require(consumers[x] == [(first, 1)] and consumers[residual] == [(final, 0)],
                "Snake input aliases have unrelated consumers")
        logical = shapes[x]
        require(logical in ([2, 4260, 256, 1, 1], [2, 25561, 128, 1, 1])
                and shapes[residual] == logical and shapes[split["bottoms"][0]] == logical,
                "unexpected pinned Snake shape")
        chain = [first, sine, square, second, final]
        indexes = {row["index"] for row in chain}
        require(len(indexes) == 5 and not indexes.intersection(removed | set(replacements))
                and all(shapes[row["tops"][0]] == logical for row in chain), "overlapping or shape-changing Snake chain")
        for blob, consumer in [(alpha, first), (reciprocal, second)]:
            constant = producers[blob]
            operation(constant, "MemoryData", {"0": "1", "1": str(logical[2])}, 0)
            require(blob not in constants and shapes[blob] == [2, 1, logical[2], 1, 1]
                    and consumers[blob] == [(consumer, 0)], "Snake constant shape/ownership mismatch")
            constants.add(blob)
        removed.update(row["index"] for row in chain[:-1])
        replacements[final["index"]] = dict(final, type="RadishSnake", bottoms=[x, alpha, reciprocal], params={})
        split_tops[split["index"]] = [x]
        groups.append(dict(layers=[row["name"] for row in chain], indices=[row["index"] for row in chain],
                           input_alias=x, residual_alias=residual, source=split["bottoms"][0], split=split["name"],
                           alpha=alpha, reciprocal=reciprocal, output=final["tops"][0], shape=logical))
    require(len(groups) == len(split_tops) == 48 and len(removed) == 192 and len(constants) == 96,
            "expected exactly 48 disjoint Snake chains and 96 original constants")
    require(Counter(group["shape"][2] for group in groups) == {128: 24, 256: 24}, "unexpected Snake shape families")
    require({row["tops"][0] for row in rows if row["type"] == "MemoryData"} == constants,
            "not all original MemoryData rows are retained Snake constants")
    lines = ["7767517\n", "906 1067\n"]
    for row in rows:
        if row["index"] in removed:
            continue
        if row["index"] in replacements:
            lines.append(render(replacements[row["index"]], shapes))
        elif row["index"] in split_tops:
            lines.append(render(dict(row, tops=split_tops[row["index"]]), shapes))
        else:
            lines.append(row["raw"])
    rewritten = "".join(lines).encode()
    kept, final_producers, final_shapes, _ = read_graph(rewritten, 906, 1067, TARGET_INVENTORY)
    weighted = [row for row in rows if row["type"] in WEIGHTED]
    require(len(weighted) == 226 and [row["raw"] for row in kept if row["type"] in WEIGHTED]
            == [row["raw"] for row in weighted], "weighted rows or their order changed")
    require(all(final_shapes[name] == shapes[name] for name in final_shapes), "retained shape changed")
    expected_removed = {row["tops"][0] for row in rows if row["index"] in removed}
    expected_removed.update(group["residual_alias"] for group in groups)
    require(set(producers) - set(final_producers) == expected_removed and len(expected_removed) == 240,
            "unexpected retained blob-name changes")
    unchanged = {row["name"]: row["raw"] for row in rows
                 if row["index"] not in removed | set(replacements) | set(split_tops)}
    require(all(row["raw"] == unchanged[row["name"]] for row in kept if row["name"] in unchanged),
            "unrelated row changed")
    return rewritten, groups, weighted


def create(source, output):
    source = source.resolve()
    require(not output.exists() and not output.is_symlink(), "output must be new")
    output = output.resolve()
    require(output.is_relative_to(WORK.resolve()) and output != WORK.resolve() and not output.is_relative_to(source),
            "output must be a new private directory below shared Radish work, outside the source")
    data = (source / "decoder.ncnn.param").read_bytes()
    parameters, groups, weighted = rewrite(data)
    linked = {}
    for name, (size, digest) in helpers.FIXTURES.items():
        path = source / name
        require(path.is_file() and path.stat().st_size == size and sha256(path) == digest,
                f"unexpected immutable source asset: {name}")
        linked[name] = {"bytes": size, "sha256": digest}
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".kokoro-snake-", dir=output.parent))
    reserved = False
    try:
        for name in linked:
            os.link(source / name, stage / name)
        (stage / "decoder.ncnn.param").write_bytes(parameters)
        require(sha256(source / "decoder.ncnn.param") == PARAM_SHA256, "source graph changed")
        for name, expected in linked.items():
            require(sha256(stage / name) == expected["sha256"], f"immutable linked asset changed: {name}")
        report = dict(format="radish-kokoro-snake-provisional-v1", status="provisional",
                      source_directory=str(source), source_param_sha256=PARAM_SHA256,
                      param_sha256=hashlib.sha256(parameters).hexdigest(), layers=906, blobs=1067,
                      inventory=TARGET_INVENTORY, snake_chains=48, retained_memorydata=96,
                      unchanged_weighted_rows=[row["name"] for row in weighted], retained_single_output_splits=48,
                      removed_arithmetic_layers=192, removed_blob_names=240, groups=groups, linked_files=linked,
                      required_custom_layer="RadishSnake", precision="FP32 only; original alpha and stored reciprocal tensors retained",
                      arithmetic="alpha*x; sin; square; stored_reciprocal*square; x+product, in original operand order",
                      validation="Structural rewrite only. CPU full-reference, dynamic metadata shapes and GPU checks pending.",
                      cpu_validation="Pending; do not treat this manifest as a validated fixture.",
                      gpu_validation="Pending; no GPU correctness or performance claim.",
                      accuracy_gates={"core_atol": 0.002, "core_rtol": 0.002})
        (stage / "snake-provisional.json").write_text(json.dumps(report, indent=2) + "\n")
        output.mkdir()
        reserved = True
        os.replace(stage, output)
        reserved = False
    finally:
        if stage.exists():
            shutil.rmtree(stage)
        if reserved:
            output.rmdir()
    print(f"PROVISIONAL: 48 Snake chains / 906 layers / 1067 blobs: {output}")
    print(f"Graph SHA256: {hashlib.sha256(parameters).hexdigest()}; no tensors executed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="pinned kokoro-core-shapes-instancenorm fixture")
    parser.add_argument("output", type=Path, help="new private candidate directory; never overwrites a fixture")
    args = parser.parse_args()
    try:
        create(args.source, args.output)
    except (OSError, ValueError, KeyError, IndexError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
