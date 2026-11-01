#!/usr/bin/env python3
import pathlib
import re
import sys

stamp = re.compile(r'^\[\s*(\d+\.\d+)\]')
fault = re.compile(r'hangcheck detected|gpu fault|gpu.*recover|GPU.*fault|GPU.*recover', re.I)

def records(path):
    out = []
    for line in pathlib.Path(path).read_text().splitlines():
        match = stamp.match(line)
        if match:
            out.append((float(match[1]), line))
    return out

before, after = records(sys.argv[1]), records(sys.argv[2])
if not before or not after:
    raise SystemExit('FAIL: missing timestamped kernel log evidence')
cutoff = max(t for t, _ in before)
if max(t for t, _ in after) < cutoff:
    raise SystemExit('FAIL: kernel log timestamp reset; check for reboot')
new_faults = [line for t, line in after if t >= cutoff and fault.search(line)
              and line not in {text for _, text in before}]
if new_faults:
    raise SystemExit('FAIL: new GPU fault/recovery messages:\n' + '\n'.join(new_faults))
print('PASS: no new GPU fault/recovery messages')
