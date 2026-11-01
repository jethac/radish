#!/usr/bin/env python3
import argparse, array, hashlib, json, math, re, statistics, struct, sys
from pathlib import Path

ap = argparse.ArgumentParser(description='Read one terminal fixed calibration capture; no device or workload execution.')
ap.add_argument('log', type=Path)
ap.add_argument('raw', type=Path)
ap.add_argument('output', type=Path)
a = ap.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
t = a.log.read_text()
lines = t.splitlines()
metadata = json.loads((a.raw / 'result.json').read_text())
expected_native = {'mad': 8, 'copy': 3}
assert metadata['format'] == 'radish-gpu-calibration-v2' and metadata['status'] == 'passed'
expected_metadata = dict(warmup_per_kind=2, measured_per_kind=3, total_runs=10, expected_samples=10,
                         mad_operations_per_run=268435456, mad_payload_bytes=131072,
                         copy_payload_bytes=13087232, copy_read_write_bytes=26174464, tail_guard_bytes=16,
                         all_outputs_exact=True, all_guards_unchanged=True,
                         all_inputs_read_back_unchanged=True, raw_before_reference=True)
for key, value in expected_metadata.items():
    assert metadata[key] == value, key
assert len(metadata['runs']) == 10
assert 'PASS: GPU calibration v2 deferred readbacks: ten full outputs and tail guards exact; all seed/coefficient/source bytes read back unchanged; native/timestamp qualification separate' in t
assert t.count('GPU_CALIBRATION_RAW_CAPTURE_COMPLETE runs=10 validation=pending') == 1
assert t.count('GPU_CALIBRATION_COMPUTE_SEQUENCE_COMPLETE runs=10 output_readbacks=deferred') == 1
assert metadata['timing_scope'] == 'Record, submit and wait only; all ten output downloads are deferred until after the timed sequence. CPU uploads, reference and file writes excluded. Actual native admission and timestamp/profile qualification are separate.'
stdout_sequence = [line for line in lines if line.startswith(('GPU_CALIBRATION_RUN_BEGIN ', 'GPU_CALIBRATION_RUN_END ', 'GPU_CALIBRATION_COMPUTE_SEQUENCE_COMPLETE ', 'GPU_CALIBRATION_RAW_CAPTURE_COMPLETE '))]
assert len(stdout_sequence) == 22
assert all(stdout_sequence[2*i].startswith('GPU_CALIBRATION_RUN_BEGIN ') and stdout_sequence[2*i+1].startswith('GPU_CALIBRATION_RUN_END ') for i in range(10))
assert stdout_sequence[-2:] == ['GPU_CALIBRATION_COMPUTE_SEQUENCE_COMPLETE runs=10 output_readbacks=deferred', 'GPU_CALIBRATION_RAW_CAPTURE_COMPLETE runs=10 validation=pending']
assert all('record_submit_wait_download_seconds' not in row for row in metadata['runs'])
assert not (a.raw / 'pending.json').exists()


def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line))


def ints(d, keys):
    for key in keys:
        d[key] = int(d[key])
    return d


def geometry(d):
    for key in ['grid', 'local']:
        d[key] = list(map(int, d[key].split(',')))
    assert d['grid'] == [4, 2, 1] and d['local'] == [16, 8, 1]


profile_lines = [x for x in lines if x.startswith('RADISH_PROFILE dispatch ')]
sample_lines = [x for x in lines if x.startswith('RADISH_GPU_ELAPSED ')]
assert len(profile_lines) == len(sample_lines) == 10
assert not any(x.startswith('RADISH_PROFILE transfer ') for x in lines)
stderr_events = [x for x in lines if x.startswith(('RADISH_PROFILE ', 'RADISH_GPU_ELAPSED '))]
assert stderr_events == [x for pair in zip(sample_lines, profile_lines) for x in pair]
assert all(len(x.encode()) + 1 <= 1024 for x in sample_lines)
expected_order = [(kind, i) for kind in ['mad', 'copy'] for i in range(5)]
begins = [fields(x) for x in lines if x.startswith('GPU_CALIBRATION_RUN_BEGIN ')]
ends = [fields(x) for x in lines if x.startswith('GPU_CALIBRATION_RUN_END ')]
assert [(x['kind'], int(x['index'])) for x in begins] == expected_order
assert [(x['kind'], int(x['index'])) for x in ends] == expected_order
runs = []
for number, (kind, index) in enumerate(expected_order):
    phase = 'warmup' if index < 2 else 'measured'
    start, end = begins[number], ends[number]
    assert start['phase'] == end['phase'] == phase and end['status'] == 'SUBMITTED'
    geometry(start)
    assert (int(start['groups']), int(start['expected_chunks']), int(start['expected_submits'])) == (8, 1, 1)
    p = ints(fields(profile_lines[number]), ['barrier', 'loops', 'instrlen', 'chunks', 'submits', 'ns', 'submit_ns', 'submit_max_ns', 'result'])
    geometry(p)
    assert (p['barrier'], p['loops'], p['chunks'], p['submits'], p['result']) == (0, 1, 1, 1, 0)
    assert p['instrlen'] == expected_native[kind]
    assert 0 < p['submit_ns'] == p['submit_max_ns'] <= p['ns']
    s = ints(fields(sample_lines[number]), ['version', 'sample', 'valid', 'instrlen', 'chunks', 'submits', 'submit_id', 'start_ticks', 'stop_ticks', 'delta_ticks', 'gpu_ns', 'host_start_ns', 'host_done_ns', 'submit_ns', 'result'])
    geometry(s)
    assert s['calibration'] == kind and s['version'] == 1 and s['sample'] == number
    assert s['valid'] == 1 and s['reason'] == 'ok' and s['result'] == 0
    for key in ['pipeline', 'grid', 'local', 'instrlen', 'chunks', 'submits', 'submit_ns']:
        assert s[key] == p[key], (kind, index, key)
    begin_tick, stop_tick = s['start_ticks'], s['stop_ticks']
    assert 0 < begin_tick < stop_tick < 2**64
    assert all(((v >> shift) & 0xffffffff) != 0xffffffff for v in [begin_tick, stop_tick] for shift in [0, 32])
    delta = stop_tick - begin_tick
    assert 0 < delta <= 96000000 and s['delta_ticks'] == delta
    whole_seconds, remainder = divmod(delta, 19200000)
    assert s['gpu_ns'] == whole_seconds * 1000000000 + remainder * 1000000000 // 19200000
    assert 0 <= s['host_start_ns'] < s['host_done_ns'] and s['host_done_ns'] - s['host_start_ns'] == s['submit_ns']
    assert s['gpu_ns'] <= s['submit_ns'] + 1000
    if runs:
        assert s['submit_id'] == runs[-1]['sample']['submit_id'] + 1
    duration = float(end['record_submit_wait_seconds'])
    assert math.isfinite(duration) and duration > 0
    row = metadata['runs'][number]
    assert row['kind'] == kind and row['index'] == index and row['phase'] == phase
    assert row['output'] == f'{kind}-{index:02d}.f32' and row['bytes'] == (131088 if kind == 'mad' else 13087248)
    assert abs(row['record_submit_wait_seconds'] - duration) < 1e-9
    expected_validated = (f'GPU_CALIBRATION_VALIDATED kind=mad index={index} payload_values=32768 guard_values=4 exact=1' if kind == 'mad'
                          else f'GPU_CALIBRATION_VALIDATED kind=copy index={index} payload_bytes=13087232 guard_bytes=16 exact=1')
    assert lines.count(expected_validated) == 1
    rate = (268435456 if kind == 'mad' else 26174464) / s['gpu_ns']
    runs.append(dict(kind=kind, index=index, phase=phase, profile=p, sample=s, whole_call_s=duration,
                     achieved_GFLOP_s=rate if kind == 'mad' else None,
                     achieved_read_plus_write_GB_s=rate if kind == 'copy' else None,
                     payload_GB_s=13087232 / s['gpu_ns'] if kind == 'copy' else None,
                     gpu_fraction_of_host_submit=s['gpu_ns'] / p['submit_ns'],
                     host_submit_minus_gpu_ns=p['submit_ns'] - s['gpu_ns']))
for kind in ['mad', 'copy']:
    assert len({x['profile']['pipeline'] for x in runs if x['kind'] == kind}) == 1
assert runs[0]['profile']['pipeline'] != runs[5]['profile']['pipeline']


def guard(tag):
    return struct.pack('<4I', *[0x3e800000 | (tag * 131 + lane * 17 + 3) for lane in range(4)])


def seed(i):
    return ((i * 37 + (i // 32) * 13) % 257) - 128


steps = [1, -1, 3, -3]
expected_mad = struct.pack('<32768f', *[(seed(i) + 4096 * steps[i % 4]) / 4096 for i in range(32768)])
expected_seeds = struct.pack('<32768f', *[seed(i) / 4096 for i in range(32768)]) + guard(1)
expected_coeff = struct.pack('<8f', *([1.0] * 4 + [step / 4096 for step in steps]))
copy_words = array.array('I', (0x3f000000 | ((i * 2654435761 + 17) & 0x7fffff) | (0x80000000 if i & 1 else 0) for i in range(3271808)))
assert copy_words.itemsize == 4
if sys.byteorder != 'little':
    copy_words.byteswap()
expected_copy = copy_words.tobytes()
del copy_words
assert len(expected_mad) == 131072 and len(expected_copy) == 13087232
sizes = {**{f'mad-{i:02d}.f32':131088 for i in range(5)}, **{f'copy-{i:02d}.f32':13087248 for i in range(5)},
         'seeds-before.f32':131088, 'seeds-after.f32':131088, 'coeff-before.f32':32, 'coeff-after.f32':32,
         'copy-source-before.f32':13087248, 'copy-source-after.f32':13087248}
assert len(sizes) == 16 and sum(sizes.values()) == 92528416
assert set(p.name for p in a.raw.iterdir()) == set(sizes) | {'result.json'}
raw_pins = {}
for name, size in sizes.items():
    path = a.raw / name
    assert path.is_file() and not path.is_symlink() and path.stat().st_size == size
    data = path.read_bytes()
    if name.startswith('mad-'):
        index = int(name[4:6]); expected = expected_mad + guard(10 + index)
    elif name.startswith('copy-') and name[5:7].isdigit():
        index = int(name[5:7]); expected = expected_copy + guard(15 + index)
    elif name.startswith('seeds-'):
        expected = expected_seeds
    elif name.startswith('coeff-'):
        expected = expected_coeff
    else:
        assert name.startswith('copy-source-'); expected = expected_copy + guard(2)
    assert data == expected, ('independent raw oracle', name)
    raw_pins[name] = dict(bytes=size, sha256=sha(path))

summaries = {}
for kind in ['mad', 'copy']:
    group = [r for r in runs if r['kind'] == kind and r['phase'] == 'measured']
    summary = {}
    for key, values in [('gpu_ns', [r['sample']['gpu_ns'] for r in group]),
                        ('host_submit_ns', [r['profile']['submit_ns'] for r in group]),
                        ('whole_call_s', [r['whole_call_s'] for r in group]),
                        ('achieved_GFLOP_s' if kind == 'mad' else 'achieved_read_plus_write_GB_s',
                         [r['achieved_GFLOP_s'] if kind == 'mad' else r['achieved_read_plus_write_GB_s'] for r in group])]:
        summary[key] = dict(values=values, mean=statistics.mean(values), median=statistics.median(values), minimum=min(values), maximum=max(values))
    summaries[kind] = summary
comparison = dict(conv_gpu_queued_ns=7160924, conv_useful_GFLOP_s=76775424 / 7160924,
                  conv_issued_GFLOP_s=92274688 / 7160924,
                  mad_median_over_conv_useful=summaries['mad']['achieved_GFLOP_s']['median'] / (76775424 / 7160924),
                  mad_median_over_conv_issued=summaries['mad']['achieved_GFLOP_s']['median'] / (92274688 / 7160924),
                  scope='Calibration uses normal oneEXEC/eightgroups and one chunk/submit; convolution uses eight sequential EXEC/chunks and seven later restores. This is not matched-packet subtraction or a unique attribution of the gap.')
report = dict(status='PASS', timing_scope=metadata['timing_scope'], log=str(a.log), log_sha256=sha(a.log), expected_native_instrlen=expected_native,
              compiler_admission='Separate actual native/resource review required; these profile lengths alone do not establish register/private-memory/dataflow safety.',
              result_json_sha256=sha(a.raw / 'result.json'), profile_totals=dict(dispatches=10, chunks=10, submits=10, samples=10),
              numerical=dict(independent_raw_oracles=True, mad_outputs=163840, copy_output_bytes=65436160,
                             output_guard_bytes=160, source_bytes_unchanged=True, raw_files=16, raw_bytes=92528416),
              raw_pins=raw_pins, runs=runs, summaries=summaries, comparison=comparison,
              limitations=['The0036 perturbation check applies only to its accepted convolution; these new achieved rates include diagnostic overhead.',
                           'Rates are achieved lower bounds, not hardware ceilings, physical DRAM bandwidth, or measured occupancy.',
                           'GPU intervals include retained command work and may include scheduling; host whole calls exclude all deferred CPU clone readback.',
                           'Only two warmups and three measured values perkind; all retained, no fastest-only or statistical confidence claim.',
                           'Source pins, services, faults, temperature and whole-window clock records require separate runner audit.'])
with a.output.open('x') as f:
    json.dump(report, f, indent=2); f.write('\n')
print(json.dumps({k:report[k] for k in ['profile_totals','numerical','summaries','comparison']}, indent=2))
