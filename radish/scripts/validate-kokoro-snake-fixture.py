#!/usr/bin/env python3
"""Recheck a CPU-validated Snake fixture and print frozen evidence; never write files."""

import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import re
import sys


PARAM_SHA256 = "e01c56a4f23d6d3c8f92dbecb0c3fc37f6f74e9f305f58b82d5196278a450d38"
WEIGHTS_SHA256 = "0f77cf77ff79a7724662f2e79a4d50284eb498abb2e519469a188e33f6c051ae"
REFERENCE_SHA256 = "4aabc5a98f714ac385561dbbc604b0d40f8a9027e645fcc38096c02c20ea395c"
COUNT = 562342
ATOL = RTOL = 0.002
INPUTS = {
    "asr.f32": {"bytes": 436224, "sha256": "0ed2c9c4aa1899c0e6ca10d70a739f3cec00d731e392930c02ade852bf94515b"},
    "f0.f32": {"bytes": 1704, "sha256": "16572fb20769acd644b3076d54a5c8e09ba7b19da73306b8f63808d0098f9e1c"},
    "n.f32": {"bytes": 1704, "sha256": "32ea97959622ceef6632794fd4dcdafc2bd8a3984c70449b81093c9fe19a86b8"},
    "style.f32": {"bytes": 1024, "sha256": "30a2c76520b76bce395b3e2c33592c56322dffb50d70f7f0f7aeaef968407d66"},
    "har.f32": {"bytes": 2249368, "sha256": "65c949af78751bbe2c7736c02ba97cd4f7d17ebd551c8c8c65125e6adaa0bbe3"},
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def asset(path):
    require(path.is_file() and not path.is_symlink(), "missing or symlinked fixture file: " + str(path))
    before = path.stat()
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    after = path.stat()
    require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) ==
            (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns),
            "fixture changed during hashing: " + str(path))
    return {"bytes": after.st_size, "sha256": digest.hexdigest()}


def checked_bytes(path, expected):
    data = path.read_bytes()
    require(len(data) == expected["bytes"] and hashlib.sha256(data).hexdigest() == expected["sha256"],
            "fixture changed after hashing: " + str(path))
    return data


def floats(data):
    require(len(data) == COUNT * 4, "CPU/reference output must contain exactly 562342 FP32 values")
    values = array.array("f")
    require(values.itemsize == 4, "host float is not 32 bits")
    values.frombytes(data)
    if sys.byteorder != "little":
        values.byteswap()
    return values


def validate(directory):
    directory = Path(directory).resolve()
    files = {name: asset(directory / name) for name in
             ("snake-validation.json", "decoder.ncnn.param", "decoder.ncnn.bin", "reference.f32",
              "snake-cpu-validation.log", "ncnn-cpu-packed-output.f32") + tuple(INPUTS)}
    marker = json.loads(checked_bytes(directory / "snake-validation.json", files["snake-validation.json"]))
    require(marker["format"] == "radish-kokoro-snake-validation-v1"
            and marker["status"] == "cpu-reference-passed", "missing successful full CPU validation marker")
    for field, filename, digest in (("param_sha256", "decoder.ncnn.param", PARAM_SHA256),
                                    ("weights_sha256", "decoder.ncnn.bin", WEIGHTS_SHA256),
                                    ("reference_sha256", "reference.f32", REFERENCE_SHA256)):
        require(marker[field] == digest and files[filename]["sha256"] == digest,
                "unexpected pinned Snake asset: " + filename)
    require(files["decoder.ncnn.bin"]["bytes"] == 213042160, "unexpected weight byte count")
    require(all(files[name] == expected for name, expected in INPUTS.items()), "fixed Snake input bytes changed")
    cpu = marker["cpu_validation"]
    require(cpu["backend"] == "cpu-packed" and cpu["outputs"] == COUNT and cpu["mismatches"] == 0
            and cpu["atol"] == ATOL and cpu["rtol"] == RTOL, "wrong CPU backend/count/comparison gates")
    require(isinstance(cpu["runner_sha256"], str) and re.fullmatch(r"[0-9a-f]{64}", cpu["runner_sha256"]),
            "missing validated CPU runner hash")
    require(cpu["log_sha256"] == files["snake-cpu-validation.log"]["sha256"]
            and cpu["output_sha256"] == files["ncnn-cpu-packed-output.f32"]["sha256"],
            "CPU validation evidence hash mismatch")
    log = checked_bytes(directory / "snake-cpu-validation.log", files["snake-cpu-validation.log"]).decode()
    require(re.search(r"^ncnn Kokoro neural core CPU packed: [^\n]*, 562342 outputs, [^\n]*, "
                      r"mismatches 0, atol=0\.002 rtol=0\.002, packing=on threads=[1-6]$", log, re.MULTILINE)
            and "PASS: Kokoro neural-core runtime inputs, full FP32-reference output comparison; source/iSTFT outside graph" in log,
            "CPU log lacks the complete packed full-reference pass")
    output = floats(checked_bytes(directory / "ncnn-cpu-packed-output.f32", files["ncnn-cpu-packed-output.f32"]))
    reference = floats(checked_bytes(directory / "reference.f32", files["reference.f32"]))
    maximum = error_power = reference_power = 0.0
    for i, (value, expected) in enumerate(zip(output, reference)):
        require(math.isfinite(value) and math.isfinite(expected), "nonfinite CPU/reference value at " + str(i))
        error = abs(value - expected)
        require(error <= ATOL + RTOL * abs(expected), "CPU/reference mismatch at " + str(i))
        maximum = max(maximum, error)
        error_power += error * error
        reference_power += expected * expected
    require(reference_power > 0, "reference has zero signal power")
    return {"format": "radish-kokoro-snake-fixture-check-v1", "directory": str(directory),
            "marker": marker, "files": files,
            "cpu_comparison": {"outputs": COUNT, "mismatches": 0, "atol": ATOL, "rtol": RTOL,
                               "max_abs": maximum, "relative_rms": math.sqrt(error_power / reference_power)}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="fixture with a completed CPU-evidence snake-validation.json")
    args = parser.parse_args()
    try:
        print(json.dumps(validate(args.directory), indent=2, sort_keys=True))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
