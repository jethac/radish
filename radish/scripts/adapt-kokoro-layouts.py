#!/usr/bin/env python3
"""Keep this fixed Kokoro fixture's 60 vector unsqueezes on ncnn Vulkan.

Use existing Reshape layers for the verified rank-one ExpandDims semantics.
This is deliberately a guarded fixture adaptation, not a general graph pass.
The source directory is read-only; the new directory shares immutable inputs
and weights through hard links, but never shares a writable inference output.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path


SOURCE_SHA256 = "17207ccee1c3146084692f2b034cf7b68ba9108ad2d718ec44988f5e6f862fd6"
STYLE_LENGTHS = [1028, 2048, 2180, 2048, 2180, 2048, 2180, 2048, 2180, 1024] + [512] * 24 + [256] * 24
FIXTURE_BYTES = {
    "decoder.ncnn.bin": 213042160,
    "asr.f32": 512 * 213 * 4,
    "f0.f32": 426 * 4,
    "n.f32": 426 * 4,
    "style.f32": 256 * 4,
    "har.f32": 22 * 25561 * 4,
    "reference.f32": 562342 * 4,
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def adapt(source, output):
    require(source.is_dir(), "source must be the fixed-core fixture directory")
    require(not output.exists(), "output must be a new directory; preserve existing artifacts")
    param = source / "decoder.ncnn.param"
    before_hash = sha256(param)
    require(before_hash == SOURCE_SHA256, "unexpected graph hash; audit new graphs before adapting them")
    lines = param.read_text().splitlines(keepends=True)
    require([line.strip() for line in lines[:2]] == ["7767517", "1368 1739"], "unexpected ncnn header")
    require(len(lines) == 1370, "expected exactly 1368 layer rows")
    rows = [line.split() for line in lines[2:]]
    layers = {}
    producers = {}
    for index, row in enumerate(rows):
        require(len(row) >= 4, f"malformed layer {index}")
        name, inputs, outputs = row[1], int(row[2]), int(row[3])
        require(name not in layers, f"duplicate layer {name}")
        require(len(row) >= 4 + inputs + outputs, f"malformed blobs for {name}")
        require(all(blob in producers for blob in row[4:4 + inputs]), f"unknown input to {name}")
        for blob in row[4 + inputs:4 + inputs + outputs]:
            require(blob not in producers, f"duplicate producer for {blob}")
            producers[blob] = row
        layers[name] = (index, row)
    require(len(producers) == 1739, "unexpected blob count")
    require(sum(row[0] == "ExpandDims" for row in rows) == 60, "expected exactly 60 ExpandDims")
    require(sum(row[0] == "InnerProduct" for row in rows) == 58, "expected exactly 58 style projections")

    # The runner creates in3 as a 256-element vector. Crop selects its first
    # 128 values and Split preserves rank, so every projection returns rank 1.
    require(layers["in3"][1] == ["Input", "in3", "0", "1", "in3"], "unexpected style Input")
    require(layers["slice_0"][1] == ["Crop", "slice_0", "1", "1", "in3", "9",
                                          "-23310=1,128", "-23311=1,0", "-23309=1,0"], "unexpected style crop")
    require(layers["splitncnn_2"][1] == ["Split", "splitncnn_2", "1", "58", "9"]
            + [str(blob) for blob in range(10, 68)], "unexpected style split")

    changes = []

    def replace(name, expected_index, expected_row, params, shape):
        index, row = layers[name]
        require(index == expected_index and row == expected_row, f"unexpected graph/axes for {name}")
        replacement = ["Reshape"] + row[1:6] + params
        before = lines[index + 2].rstrip("\n")
        after = " ".join(replacement)
        lines[index + 2] = after + "\n"
        changes.append({"line": index + 3, "layer_index": index, "name": name,
                        "input_shape": [shape[0]], "output_ncnn_wh": shape[1:],
                        "before": before, "after": after})

    for offset, length in enumerate(STYLE_LENGTHS):
        projection = f"linear_{86 + offset}"
        bottom, top = str(68 + offset), str(126 + offset)
        expected = ["InnerProduct", projection, "1", "1", str(67 - offset), bottom,
                    f"0={length}", "1=1", f"2={128 * length}"]
        require(layers[projection][1] == expected and producers[bottom] == expected,
                f"unexpected 128-value projection provenance for {projection}")
        name = f"unsqueeze_{252 + offset}"
        # ExpandDims axis 1 on a vector is ncnn w=1, h=N. Reshape infers N.
        replace(name, 67 + offset, ["ExpandDims", name, "1", "1", bottom, top, "-23303=1,1"],
                ["0=1", "1=-1"], [length, 1, length])

    for offset, input_name in enumerate(("in1", "in2")):
        require(layers[input_name][1] == ["Input", input_name, "0", "1", input_name],
                f"unexpected fixed input {input_name}")
        name = f"unsqueeze_{310 + offset}"
        # The runner supplies f0/n as 426-element vectors. Axis 0 is w=N,h=1.
        replace(name, 185 + offset, ["ExpandDims", name, "1", "1", input_name, str(305 + offset), "-23303=1,0"],
                ["0=-1", "1=1"], [426, 426, 1])

    linked = {}
    for name, size in FIXTURE_BYTES.items():
        path = source / name
        require(path.is_file() and path.stat().st_size == size, f"unexpected fixture size for {name}")
        linked[name] = {"bytes": size, "sha256": sha256(path)}
    # Carry the existing full-waveform reference and exact ONNX tail when present.
    for name in ("reference-waveform.f32", "waveform-tail.onnx"):
        path = source / name
        if path.is_file():
            linked[name] = {"bytes": path.stat().st_size, "sha256": sha256(path)}

    transformed = "".join(lines).encode()
    after_hash = hashlib.sha256(transformed).hexdigest()
    manifest = {
        "source_directory": str(source), "source_param_sha256": before_hash,
        "adapted_param_sha256": after_hash, "changed_layers": len(changes),
        "semantics": "58 axis-1 vectors become w=1,h=N; 2 axis-0 vectors become w=N,h=1",
        "linked_files": linked, "changes": changes,
        "validation": "Graph adaptation only; run the full CPU reference comparison before device use.",
    }
    # No source writes, and no hard link to ncnn-cpu-output.f32: the runner
    # overwrites that filename when it saves its independently computed result.
    output.mkdir(parents=True, exist_ok=False)
    for name in linked:
        os.link(source / name, output / name)
    (output / "decoder.ncnn.param").write_bytes(transformed)
    (output / "layout-adaptation.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"source param sha256:  {before_hash}")
    print(f"adapted param sha256: {after_hash}")
    print(f"weights sha256 (unchanged hard link): {linked['decoder.ncnn.bin']['sha256']}")
    for change in changes:
        print(f"line {change['line']} layer {change['layer_index']}:")
        print(f"- {change['before']}")
        print(f"+ {change['after']}")
    print(f"Adapted {len(changes)} layers; manifest: {output / 'layout-adaptation.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="original fixed-core fixture directory")
    parser.add_argument("output", type=Path, help="new directory on the same filesystem (hard links)")
    args = parser.parse_args()
    try:
        adapt(args.source.resolve(), args.output.resolve())
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
