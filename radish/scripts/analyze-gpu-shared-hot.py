#!/usr/bin/env python3
import argparse, hashlib, json, math, re, statistics, struct
from pathlib import Path

ap = argparse.ArgumentParser(description='Check one terminal balanced shared-hot capture; no device or workload execution.')
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
expected_native = {'planar-baseline': 27, 'shared-hot': 25}
schedule = ['planar-baseline', 'shared-hot', 'shared-hot', 'planar-baseline'] * 3
assert metadata['format'] == 'radish-gpu-shared-hot-v1' and metadata['status'] == 'passed'
expected_metadata = dict(warmup_runs=4, measured_runs=8, total_runs=12, expected_samples=12,
                         operations_per_run=92274688, useful_operations_per_run=76775424, payload_bytes=109056, tail_guard_bytes=16,
                         total_validated_values=327168, all_outputs_exact=True,
                         same_kind_output_payloads_bit_identical=True, all_guards_unchanged=True,
                         all_inputs_read_back_unchanged=True, per_run_poison=True,
                         distinct_gpu_destinations=True, raw_before_reference=True)
for key, value in expected_metadata.items():
    assert type(metadata[key]) is type(value) and metadata[key] == value, key
assert len(metadata['runs']) == 12
pass_line = 'PASS: GPU shared-hot balanced probe warmup_runs=4 measured_runs=8 outputs_per_run=27264 all_outputs=327168; each output exact, same-kind payloads bit-identical, per-run guards unchanged, five GPU inputs read back unchanged; native/timestamp qualification separate'
assert lines.count(pass_line) == 1
complete_line = 'GPU_SHARED_HOT_COMPUTE_SEQUENCE_COMPLETE runs=12 output_readbacks=deferred'
raw_line = 'GPU_SHARED_HOT_RAW_CAPTURE_COMPLETE runs=12 files=22 bytes=3124576 validation=pending'
assert lines.count(complete_line) == lines.count(raw_line) == 1
assert metadata['timing_scope'] == 'Record, submit and wait only; all twelve output downloads are deferred until after the timed sequence. CPU uploads, poison, reference and file writes excluded. Actual native admission and timestamp/profile qualification are separate.'
stdout_sequence = [line for line in lines if line.startswith(('GPU_SHARED_HOT_RUN_BEGIN ', 'GPU_SHARED_HOT_RUN_END ', 'GPU_SHARED_HOT_COMPUTE_SEQUENCE_COMPLETE ', 'GPU_SHARED_HOT_RAW_CAPTURE_COMPLETE '))]
assert len(stdout_sequence) == 26
assert all(stdout_sequence[2*i].startswith('GPU_SHARED_HOT_RUN_BEGIN ') and stdout_sequence[2*i+1].startswith('GPU_SHARED_HOT_RUN_END ') for i in range(12))
assert stdout_sequence[-2:] == [complete_line, raw_line]
assert all('record_submit_wait_download_seconds' not in row for row in metadata['runs'])
assert not (a.raw / 'pending.json').exists()
poison_lines = [x for x in lines if x.startswith('GPU_SHARED_HOT_OUTPUT_POISON ')]
assert poison_lines == [f'GPU_SHARED_HOT_OUTPUT_POISON index={i} payload_values=27264 guard_values=4 changing_nan=1 uploaded_before_sequence=1' for i in range(12)]
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
begins = [fields(x) for x in lines if x.startswith('GPU_SHARED_HOT_RUN_BEGIN ')]
ends = [fields(x) for x in lines if x.startswith('GPU_SHARED_HOT_RUN_END ')]
assert [(x['kind'], int(x['index'])) for x in begins] == expected_order
assert [(x['kind'], int(x['index'])) for x in ends] == expected_order
runs = []
for index, kind in enumerate(schedule):
    phase = 'warmup' if index < 4 else 'measured'
    push0 = 268 if kind == 'planar-baseline' else 44
    start, end = begins[index], ends[index]
    assert start['phase'] == end['phase'] == phase and end['status'] == 'SUBMITTED'
    assert int(start['push0']) == push0 and int(start['outw']) == 213 and int(start['updates_per_accumulator']) == 1408
    geometry(start)
    assert (int(start['groups']), int(start['expected_chunks']), int(start['expected_submits'])) == (8, 8, 1)
    p = ints(fields(profile_lines[index]), ['barrier', 'loops', 'instrlen', 'chunks', 'submits', 'ns', 'submit_ns', 'submit_max_ns', 'result'])
    geometry(p)
    assert (p['barrier'], p['loops'], p['chunks'], p['submits'], p['result']) == (1, 4, 8, 1, 0)
    assert p['instrlen'] == expected_native[kind]
    assert 0 < p['submit_ns'] == p['submit_max_ns'] <= p['ns']
    s = ints(fields(sample_lines[index]), ['version', 'sample', 'valid', 'instrlen', 'chunks', 'submits', 'submit_id', 'start_ticks', 'stop_ticks', 'delta_ticks', 'gpu_ns', 'host_start_ns', 'host_done_ns', 'submit_ns', 'result'])
    geometry(s)
    assert s.get('calibration') == ('shared-hot' if kind == 'shared-hot' else None)
    assert s['version'] == 1 and s['sample'] == index
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
    assert type(row['push0']) is int and row['push0'] == push0
    assert type(row['push1']) is int and row['push1'] == 213
    assert row['output'] == f'run-{index:02d}.f32' and row['bytes'] == 109072
    assert math.isfinite(row['record_submit_wait_seconds']) and abs(row['record_submit_wait_seconds'] - duration) < 1e-9
    expected_validated = f'GPU_SHARED_HOT_VALIDATED kind={kind} index={index} payload_values=27264 guard_values=4 exact=1 same_kind_payload_bit_identical=1'
    assert lines.count(expected_validated) == 1 and lines.index(expected_validated) > lines.index(raw_line)
    rate = 92274688 / s['gpu_ns']
    runs.append(dict(kind=kind, index=index, phase=phase, push0=push0, push1=213,
                     profile=p, sample=s, whole_call_s=duration, achieved_GFLOP_s=rate,
                     gpu_fraction_of_host_submit=s['gpu_ns'] / p['submit_ns'],
                     host_submit_minus_gpu_ns=p['submit_ns'] - s['gpu_ns']))
for kind in expected_native:
    assert len({x['profile']['pipeline'] for x in runs if x['kind'] == kind}) == 1
assert runs[0]['profile']['pipeline'] != runs[1]['profile']['pipeline']
assert lines.index(pass_line) > max(lines.index(x) for x in lines if x.startswith('GPU_SHARED_HOT_VALIDATED '))
summary_lines = [fields(x) for x in lines if x.startswith('GPU_SHARED_HOT_MEASURED_SUMMARY ')]
assert [x['kind'] for x in summary_lines] == list(expected_native)
for summary in summary_lines:
    assert int(summary['runs']) == 4
    total = sum(x['record_submit_wait_seconds'] for x in metadata['runs'] if x['kind'] == summary['kind'] and x['phase'] == 'measured')
    assert abs(float(summary['record_submit_wait_total_seconds']) - total) < 1e-9
    assert abs(float(summary['mean_seconds']) - total / 4) < 1e-9


def guard(tag):
    return struct.pack('<4I', *[0x3e800000 | (tag * 131 + lane * 17 + 3) for lane in range(4)])


a_values = [((13*(i//1408)+7*((i%1408)%32)+3)%31)-15 for i in range(128*1408)]
b_values = [(((11*(i//268)+5*((i%268)-5)+1)%29)-14) if 5 <= i%268 < 218 else 0 for i in range(128*268)]
hot_a = [((13*(i//32)+7*(i%32)+3)%31)-15 for i in range(128*32)]
hot_b = [((11*(i//256)+5*(i%256)+1)%29)-14 for i in range(32*256)]
bias = [((17*c+9)%255)-127 for c in range(128)]
expected = {'planar-baseline':[0]*27264, 'shared-hot':[0]*27264}
for c in range(128):
    row = a_values[c*1408:(c+1)*1408]
    for x in range(213):
        base_num = bias[c] + sum(row[k]*b_values[(k//11)*268+x+k%11] for k in range(1408))
        hot_num = bias[c] + 44*sum(hot_a[c*32+k]*hot_b[k*256+x] for k in range(32))
        assert abs(base_num) <= 295807 and abs(hot_num) <= 295807
        packed = ((c//4)*213+x)*4+c%4
        expected['planar-baseline'][packed] = base_num/4096
        expected['shared-hot'][packed] = hot_num/4096
expected = {kind:struct.pack('<27264f', *values) for kind, values in expected.items()}
assert expected['planar-baseline'] != expected['shared-hot']
input_vectors = [('baseline-a', a_values, 64), ('baseline-b', b_values, 64),
                 ('hot-a', hot_a, 64), ('hot-b', hot_b, 64), ('bias', bias, 4096)]
input_expected = {name:struct.pack('<%df' % len(values), *[v/divisor for v in values])+guard(index+1)
                  for index,(name,values,divisor) in enumerate(input_vectors)}
sizes = {**{f'run-{i:02d}.f32':109072 for i in range(12)},
         **{f'{name}-{when}.f32':len(payload) for name,payload in input_expected.items() for when in ['before','after']}}
assert len(sizes) == 22 and sum(sizes.values()) == 3124576
assert set(p.name for p in a.raw.iterdir()) == set(sizes) | {'result.json'}
raw_pins = {}
for name, size in sizes.items():
    path = a.raw / name
    assert path.is_file() and not path.is_symlink() and path.stat().st_size == size
    data = path.read_bytes()
    if name.startswith('run-'):
        index = int(name[4:6]); oracle = expected[schedule[index]] + guard(10+index)
    else:
        input_name, when = name[:-4].rsplit('-',1)
        assert when in ['before','after']; oracle = input_expected[input_name]
    assert data == oracle, ('independent raw oracle', name)
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
    summary['work_over_mean_gpu_GFLOP_s'] = 92274688 / summary['gpu_ns']['mean']
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
base_sum = sum(base['gpu_ns']['values'])
candidate_sum = sum(candidate['gpu_ns']['values'])
difference = base_sum - candidate_sum
direction = 'shared-hot-faster' if difference > 0 else 'shared-hot-slower' if difference < 0 else 'equal'
consistent = all((p['baseline_gpu_ns'] - p['candidate_gpu_ns']) * difference > 0 for p in pairs)
comparison = dict(mean_gpu_reduction_percent=gain, mean_gap_direction=direction,
                  baseline_measured_gpu_total_ns=base_sum, candidate_measured_gpu_total_ns=candidate_sum,
                  mean_host_submit_reduction_percent=100 * (base['host_submit_ns']['mean'] - candidate['host_submit_ns']['mean']) / base['host_submit_ns']['mean'],
                  mean_whole_call_reduction_percent=100 * (base['whole_call_s']['mean'] - candidate['whole_call_s']['mean']) / base['whole_call_s']['mean'],
                  balanced_pairs=pairs, prospective_gate='Exploratory combined-implementation gap: absolute mean GPU interval difference at least 5 percent, with all four balanced adjacent pairs in the same direction.',
                  all_pairs_same_direction_as_mean=consistent,
                  useful_exploratory_gap=20 * abs(difference) >= base_sum and consistent)
report = dict(status='PASS', timing_scope=metadata['timing_scope'], log=str(a.log), log_sha256=sha(a.log), analyzer_sha256=sha(Path(__file__)),
              expected_native_instrlen=expected_native,
              compiler_admission='Separate actual native/resource review required; profile lengths alone do not establish register/private-memory/dataflow safety.',
              result_json_sha256=sha(result_path), profile_totals=dict(dispatches=12, chunks=96, submits=12, samples=12),
              numerical=dict(independent_raw_oracles=True, output_values=327168, output_guard_values=48,
                             same_kind_output_payloads_bit_identical=True, source_bytes_unchanged=True, input_guard_values=20, raw_files=22, raw_bytes=3124576),
              raw_pins=raw_pins, runs=runs, summaries=summaries, comparison=comparison,
              strict_original_compiler_gate='FAILED: both original and coordinate prototypes added two hot MOVs per K update; this capture uses the original implementation, including 2816 extra MOVs per invocation. This is not original-gate admission.',
              revised_scope='Root authorized exploratory timing of the combined shared-hot implementation. No pure outer-work latency, physical-traffic conclusion, guaranteed conservative bound, or production adoption follows.',
              limitations=['The kernels have different inputs, control structure and register copies; the gap cannot be uniquely attributed to tile refill, addressing, barriers or LDS latency.',
                           'Rates are achieved implementation measurements, not hardware ceilings, convolution/model speedups, or measured occupancy.',
                           'GPU intervals include retained command work and may include scheduling; host whole calls exclude all deferred CPU clone readback.',
                           'Four warmups and eight balanced measured values are all retained; no fastest-only or statistical confidence claim.',
                           'Source/binary pins, services, faults, temperature and whole-window clock records require separate runner audit.'])
with a.output.open('x') as f:
    json.dump(report, f, indent=2); f.write('\n')
print(json.dumps({k:report[k] for k in ['profile_totals','numerical','summaries','comparison']}, indent=2))
