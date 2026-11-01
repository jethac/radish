#!/usr/bin/env python3
"""Repair the verified singleton-axis conversion bug in this Kokoro fixture."""
import argparse
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("pnnx_param", type=Path)
p.add_argument("ncnn_param", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
if a.output.resolve() == a.ncnn_param.resolve():
    p.error("preserve the original converter output; choose a different output path")
means = [line for line in a.pnnx_param.read_text().splitlines()
         if re.match(r"torch\.mean\s", line)]
if len(means) != 108:
    p.error("expected this fixture's 108 mean reductions")
for line in means:
    fields = line.split()
    input_name = fields[4]
    if ("dim=2" not in fields or "keepdim=True" not in fields or
            not re.search(r"#" + re.escape(input_name) + r"=\(1,\d+,\d+\)f32(?:\s|$)", line)):
        p.error("unsupported reduction shape/axis; do not infer a replacement")
lines = a.ncnn_param.read_text().splitlines()
fixed = 0
for i, line in enumerate(lines):
    if not re.match(r"Reduction\s", line):
        continue
    fields = line.split()
    if not all(x in fields for x in ("0=3", "1=0", "-23303=0", "4=1", "5=1")):
        p.error("expected the verified empty-axis mean conversion failure")
    lines[i] = line.replace("-23303=0", "-23303=1,1")
    fixed += 1
if fixed != len(means):
    p.error("mean/reduction inventory mismatch")
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text("\n".join(lines) + "\n")
print(f"Corrected {fixed} means: rank-three batch/time axis 2 becomes ncnn axis 1")
