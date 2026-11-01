#!/usr/bin/env python3
import argparse, hashlib, json, math, re, statistics, struct
from pathlib import Path

ap = argparse.ArgumentParser(description='Check one terminal balanced MAD capture; no device or workload execution.')
ap.add_argument('log', type=Path)
ap.add_argument('raw', type=Path)
ap.add_argument('output', type=Path)
a = ap.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
t = a.log.read_text()
lines = t.splitlines()
result_path = a.raw / 'result.json'
assert result_path.is_file() and not result_path.is_symlink()
metadata = json.loads(result_path.read_text())
expected_native = {'mad': 8, 'mad-unroll4': 12}
schedule = ['mad', 'mad-unroll4', 'mad-unroll4', 'mad'] * 3
assert metadata['format'] == 'radish-gpu-mad-unroll-v1' and metadata['status'] == 'passed'
expected_metadata = dict(warmup_runs=4, measured_runs=8, total_runs=12, expected_samples=12,
                         operations_per_run=268435456, payload_bytes=131072, tail_guard_bytes=16,
                         total_validated_values=393216, all_outputs_exact=True,
                         all_output_payloads_bit_identical=True, all_guards_unchanged=True,
                         all_inputs_read_back_unchanged=True, per_run_poison=True,
                         distinct_gpu_destinations=True, raw_before_reference=True)
for key, value in expected_metadata.items():
    assert type(metadata[key]) is type(value) and metadata[key] == value, key
assert len(metadata['runs']) == 12
pass_line = 'PASS: GPU MAD unroll balanced probe warmup_runs=4 measured_runs=8 outputs_per_run=32768 all_outputs=393216; all output payloads exact and bit-identical, per-run guards unchanged, GPU inputs read back unchanged; native/timestamp qualification separate'
assert lines.count(pass_line) == 1
complete_line = 'GPU_MAD_UNROLL_COMPUTE_SEQUENCE_COMPLETE runs=12 output_readbacks=deferred'
raw_line = 'GPU_MAD_UNROLL_RAW_CAPTURE_COMPLETE runs=12 files=16 bytes=1835296 validation=pending'
assert lines.count(complete_line) == lines.count(raw_line) == 1
assert metadata['timing_scope'] == 'Record, submit and wait only; all twelve output downloads are deferred until after the timed sequence. CPU uploads, poison, reference and file writes excluded. Actual native admission and timestamp/profile qualification are separate.'
stdout_sequence = [line for line in lines if line.startswith(('GPU_MAD_UNROLL_RUN_BEGIN ', 'GPU_MAD_UNROLL_RUN_END ', 'GPU_MAD_UNROLL_COMPUTE_SEQUENCE_COMPLETE ', 'GPU_MAD_UNROLL_RAW_CAPTURE_COMPLETE '))]
assert len(stdout_sequence) == 26
assert all(stdout_sequence[2*i].startswith('GPU_MAD_UNROLL_RUN_BEGIN ') and stdout_sequence[2*i+1].startswith('GPU_MAD_UNROLL_RUN_END ') for i in range(12))
assert stdout_sequence[-2:] == [complete_line, raw_line]
assert all('record_submit_wait_download_seconds' not in row for row in metadata['runs'])
assert not (a.raw / 'pending.json').exists()
poison_lines = [x for x in lines if x.startswith('GPU_MAD_UNROLL_OUTPUT_POISON ')]
assert poison_lines == [f'GPU_MAD_UNROLL_OUTPUT_POISON index={i} payload_values=32768 guard_values=4 changing_nan=1 uploaded_before_sequence=1' for i in range(12)]
assert max(lines.index(x) for x in poison_lines) < lines.index(stdout_sequence[0])


def fields(line):
    pairs = re.findall(r'(\w+)=([^ ]+)', line)
    assert len(dict(pairs)) == len(pairs), ('duplicate fields', line)
    return dict(pairs)


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
assert len(profile_lines) == len(sample_lines) == 12
assert not any(x.startswith('RADISH_PROFILE transfer ') for x in lines)
stderr_events = [x for x in lines if x.startswith(('RADISH_PROFILE ', 'RADISH_GPU_ELAPSED '))]
assert stderr_events == [x for pair in zip(sample_lines, profile_lines) for x in pair]
assert all(len(x.encode()) + 1 <= 1024 for x in sample_lines)
expected_order = [(kind, i) for i, kind in enumerate(schedule)]
begins = [fields(x) for x in lines if x.startswith('GPU_MAD_UNROLL_RUN_BEGIN ')]
ends = [fields(x) for x in lines if x.startswith('GPU_MAD_UNROLL_RUN_END ')]
assert [(x['kind'], int(x['index'])) for x in begins] == expected_order
assert [(x['kind'], int(x['index'])) for x in ends] == expected_order
runs = []
for index, kind in enumerate(schedule):
    phase = 'warmup' if index < 4 else 'measured'
    unroll = 1 if kind == 'mad' else 4
    iterations = 4096 // unroll
    start, end = begins[index], ends[index]
    assert start['phase'] == end['phase'] == phase and end['status'] == 'SUBMITTED'
    assert int(start['iterations']) == iterations and int(start['updates_per_chain']) == 4096
    geometry(start)
    assert (int(start['groups']), int(start['expected_chunks']), int(start['expected_submits'])) == (8, 1, 1)
    p = ints(fields(profile_lines[index]), ['barrier', 'loops', 'instrlen', 'chunks', 'submits', 'ns', 'submit_ns', 'submit_max_ns', 'result'])
    geometry(p)
    assert (p['barrier'], p['loops'], p['chunks'], p['submits'], p['result']) == (0, 1, 1, 1, 0)
    assert p['instrlen'] == expected_native[kind]
    assert 0 < p['submit_ns'] == p['submit_max_ns'] <= p['ns']
    s = ints(fields(sample_lines[index]), ['version', 'sample', 'valid', 'instrlen', 'chunks', 'submits', 'submit_id', 'start_ticks', 'stop_ticks', 'delta_ticks', 'gpu_ns', 'host_start_ns', 'host_done_ns', 'submit_ns', 'result'])
    geometry(s)
    assert s['calibration'] == kind and s['version'] == 1 and s['sample'] == index
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
    row = metadata['runs'][index]
    assert row['kind'] == kind and type(row['index']) is int and row['index'] == index and row['phase'] == phase
    assert type(row['unroll']) is int and row['unroll'] == unroll
    assert type(row['iterations']) is int and row['iterations'] == iterations
    assert row['output'] == f'run-{index:02d}.f32' and row['bytes'] == 131088
    assert math.isfinite(row['record_submit_wait_seconds']) and abs(row['record_submit_wait_seconds'] - duration) < 1e-9
    expected_validated = f'GPU_MAD_UNROLL_VALIDATED kind={kind} index={index} payload_values=32768 guard_values=4 exact=1 payload_bit_identical_to_run0=1'
    assert lines.count(expected_validated) == 1 and lines.index(expected_validated) > lines.index(raw_line)
    rate = 268435456 / s['gpu_ns']
    runs.append(dict(kind=kind, index=index, phase=phase, unroll=unroll, iterations=iterations,
                     profile=p, sample=s, whole_call_s=duration, achieved_GFLOP_s=rate,
                     gpu_fraction_of_host_submit=s['gpu_ns'] / p['submit_ns'],
                     host_submit_minus_gpu_ns=p['submit_ns'] - s['gpu_ns']))
for kind in expected_native:
    assert len({x['profile']['pipeline'] for x in runs if x['kind'] == kind}) == 1
assert runs[0]['profile']['pipeline'] != runs[1]['profile']['pipeline']
assert lines.index(pass_line) > max(lines.index(x) for x in lines if x.startswith('GPU_MAD_UNROLL_VALIDATED '))
summary_lines = [fields(x) for x in lines if x.startswith('GPU_MAD_UNROLL_MEASURED_SUMMARY ')]
assert [x['kind'] for x in summary_lines] == list(expected_native)
for summary in summary_lines:
    assert int(summary['runs']) == 4
    total = sum(x['record_submit_wait_seconds'] for x in metadata['runs'] if x['kind'] == summary['kind'] and x['phase'] == 'measured')
    assert abs(float(summary['record_submit_wait_total_seconds']) - total) < 1e-9
    assert abs(float(summary['mean_seconds']) - total / 4) < 1e-9


def guard(tag):
    return struct.pack('<4I', *[0x3e800000 | (tag * 131 + lane * 17 + 3) for lane in range(4)])


def seed(i):
    return ((i * 37 + (i // 32) * 13) % 257) - 128


steps = [1, -1, 3, -3]
expected_mad = struct.pack('<32768f', *[(seed(i) + 4096 * steps[i % 4]) / 4096 for i in range(32768)])
expected_seeds = struct.pack('<32768f', *[seed(i) / 4096 for i in range(32768)]) + guard(1)
expected_coeff = struct.pack('<8f', *([1.0] * 4 + [step / 4096 for step in steps]))
assert len(expected_mad) == 131072
sizes = {**{f'run-{i:02d}.f32':131088 for i in range(12)},
         'seeds-before.f32':131088, 'seeds-after.f32':131088, 'coeff-before.f32':32, 'coeff-after.f32':32}
assert len(sizes) == 16 and sum(sizes.values()) == 1835296
assert set(p.name for p in a.raw.iterdir()) == set(sizes) | {'result.json'}
raw_pins = {}
for name, size in sizes.items():
    path = a.raw / name
    assert path.is_file() and not path.is_symlink() and path.stat().st_size == size
    data = path.read_bytes()
    if name.startswith('run-'):
        index = int(name[4:6]); expected = expected_mad + guard(10 + index)
    elif name.startswith('seeds-'):
        expected = expected_seeds
    else:
        assert name.startswith('coeff-'); expected = expected_coeff
    assert data == expected, ('independent raw oracle', name)
    raw_pins[name] = dict(bytes=size, sha256=sha(path))

summaries = {}
for kind in expected_native:
    group = [r for r in runs if r['kind'] == kind and r['phase'] == 'measured']
    summary = {}
    for key, values in [('gpu_ns', [r['sample']['gpu_ns'] for r in group]),
                        ('host_submit_ns', [r['profile']['submit_ns'] for r in group]),
                        ('whole_call_s', [r['whole_call_s'] for r in group]),
                        ('achieved_GFLOP_s', [r['achieved_GFLOP_s'] for r in group])]:
        summary[key] = dict(values=values, mean=statistics.mean(values), median=statistics.median(values), minimum=min(values), maximum=max(values))
    summaries[kind] = summary
pairs = []
for first in [4, 6, 8, 10]:
    pair = {r['kind']: r for r in runs[first:first+2]}
    baseline_ns, candidate_ns = [pair[k]['sample']['gpu_ns'] for k in expected_native]
    pairs.append(dict(indices=[first, first+1], order=schedule[first:first+2], baseline_gpu_ns=baseline_ns,
                      candidate_gpu_ns=candidate_ns, candidate_faster=candidate_ns < baseline_ns,
                      gpu_reduction_percent=100 * (baseline_ns-candidate_ns) / baseline_ns))
base, candidate = [summaries[k] for k in expected_native]
gain = 100 * (base['gpu_ns']['mean'] - candidate['gpu_ns']['mean']) / base['gpu_ns']['mean']
comparison = dict(mean_gpu_reduction_percent=gain,
                  mean_host_submit_reduction_percent=100 * (base['host_submit_ns']['mean'] - candidate['host_submit_ns']['mean']) / base['host_submit_ns']['mean'],
                  mean_whole_call_reduction_percent=100 * (base['whole_call_s']['mean'] - candidate['whole_call_s']['mean']) / base['whole_call_s']['mean'],
                  balanced_pairs=pairs, prospective_gate='At least 5 percent mean GPU interval reduction and all four balanced adjacent pairs faster.',
                  material_calibration_gain=gain >= 5 and all(p['candidate_faster'] for p in pairs))
report = dict(status='PASS', timing_scope=metadata['timing_scope'], log=str(a.log), log_sha256=sha(a.log), analyzer_sha256=sha(Path(__file__)),
              expected_native_instrlen=expected_native,
              compiler_admission='Separate actual native/resource review required; profile lengths alone do not establish register/private-memory/dataflow safety.',
              result_json_sha256=sha(result_path), profile_totals=dict(dispatches=12, chunks=12, submits=12, samples=12),
              numerical=dict(independent_raw_oracles=True, mad_output_values=393216, output_guard_values=48,
                             all_output_payloads_bit_identical=True, source_bytes_unchanged=True, raw_files=16, raw_bytes=1835296),
              raw_pins=raw_pins, runs=runs, summaries=summaries, comparison=comparison,
              limitations=['Rates are achieved calibration lower bounds, not hardware ceilings, convolution speedups, or measured occupancy.',
                           'GPU intervals include retained command work and may include scheduling; host whole calls exclude all deferred CPU clone readback.',
                           'Four warmups and eight balanced measured values are all retained; no fastest-only or statistical confidence claim.',
                           'Source/binary pins, services, faults, temperature and whole-window clock records require separate runner audit.'])
with a.output.open('x') as f:
    json.dump(report, f, indent=2); f.write('\n')
print(json.dumps({k:report[k] for k in ['profile_totals','numerical','summaries','comparison']}, indent=2))
