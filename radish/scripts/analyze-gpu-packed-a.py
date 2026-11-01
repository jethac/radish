#!/usr/bin/env python3
import argparse, hashlib, json, math, re, statistics, struct

# Generated-by: LLM; private 0042 offline analyzer.
if not __debug__:
    raise SystemExit("FAIL: assertions must be enabled; do not use python -O")
ISSUED_OPS = 1024 * 32 * 1408 * 2
USEFUL_OPS = 27264 * 1408 * 2
assert ISSUED_OPS == 92274688 and USEFUL_OPS == 76775424
from pathlib import Path

ap = argparse.ArgumentParser(description='Check one terminal real Convolution1D baseline/packed-A capture; no device or workload execution.')
ap.add_argument('log', type=Path)
ap.add_argument('raw', type=Path)
ap.add_argument('output', type=Path)
a = ap.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
initial_log_bytes = a.log.read_bytes()
t = initial_log_bytes.decode('utf-8')
lines = t.splitlines()
result_path = a.raw / 'result.json'
assert result_path.is_file() and not result_path.is_symlink()
metadata = json.loads(result_path.read_text())
expected_native = {'baseline': 27, 'packed-a': 28}
schedule = ['baseline', 'packed-a', 'packed-a', 'baseline'] * 3
assert metadata['format'] == 'radish-gpu-packed-a-v1' and metadata['status'] == 'passed'
expected_metadata = dict(warmup_runs=4, measured_runs=8, total_runs=12, expected_samples=12,
                         width=213, channels=128, reduction=1408, pushes=[268,213], grid=[4,2,1], local=[16,8,1],
                         raw_files=22, raw_bytes=4468960, total_validated_values=327168,
                         all_outputs_exact=True, all_output_payloads_bit_identical=True,
                         all_guards_unchanged=True, all_model_and_input_bytes_read_back_unchanged=True,
                         output_guard_words=48, input_guard_words=4, production_model_tail_guards=False,
                         distinct_gpu_destinations=True, raw_before_reference=True)
for key, value in expected_metadata.items():
    assert type(metadata[key]) is type(value) and metadata[key] == value, key
assert type(metadata['runs']) is list and len(metadata['runs']) == 12
pass_line = 'PASS: GPU packed-A paired probe warmup_runs=4 measured_runs=8 outputs_per_run=27264 all_outputs=327168; exact oracle and cross-variant payload identity, 52 guard words and all actual GPU model/input bytes preserved; native/timestamp/chunk/submit qualification separate'
assert lines.count(pass_line) == 1
complete_line = 'GPU_PACKED_A_COMPUTE_SEQUENCE_COMPLETE runs=12 all_gpu_readbacks=deferred'
raw_line = 'GPU_PACKED_A_RAW_CAPTURE_COMPLETE runs=12 files=22 bytes=4468960 validation=pending'
assert lines.count(complete_line) == lines.count(raw_line) == 1
assert metadata['timing_scope'] == 'record_pipeline, submit and wait only; all GPU reads, CPU oracle and file writes follow all twelve waits. Uploads and create_pipeline excluded.'
assert metadata['native_admission'] == 'pending independent exact resources/native/timer/chunk/submit qualification; no assumed native length'
assert metadata['create_pipeline_scope'] == 'wall intervals include packing and shader/pipeline creation; not isolated packing time'
assert type(metadata['create_pipeline_seconds']) is list and len(metadata['create_pipeline_seconds']) == 2
assert all(type(v) in (int, float) and math.isfinite(v) and v > 0 for v in metadata['create_pipeline_seconds'])
assert not re.search(r'vkCreateComputePipelines failed|new_pipeline failed|\bFAIL\b', t)
assert a.log.is_file() and not a.log.is_symlink()
assert a.raw.is_dir() and not a.raw.is_symlink()

stdout_sequence = [line for line in lines if line.startswith(('GPU_PACKED_A_RUN_BEGIN ', 'GPU_PACKED_A_RUN_END ', 'GPU_PACKED_A_COMPUTE_SEQUENCE_COMPLETE ', 'GPU_PACKED_A_RAW_CAPTURE_COMPLETE '))]
assert len(stdout_sequence) == 26
assert all(stdout_sequence[2*i].startswith('GPU_PACKED_A_RUN_BEGIN ') and stdout_sequence[2*i+1].startswith('GPU_PACKED_A_RUN_END ') for i in range(12))
assert stdout_sequence[-2:] == [complete_line, raw_line]
assert all('record_submit_wait_download_seconds' not in row for row in metadata['runs'])
assert not (a.raw / 'pending.json').exists() and not (a.raw / 'pending.json').is_symlink()
poison_lines = [x for x in lines if x.startswith('GPU_PACKED_A_OUTPUT_POISON ')]
assert poison_lines == [f'GPU_PACKED_A_OUTPUT_POISON index={i} payload_values=27264 guard_values=4 changing_nan=1 uploaded_before_sequence=1' for i in range(12)]
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
begins = [fields(x) for x in lines if x.startswith('GPU_PACKED_A_RUN_BEGIN ')]
ends = [fields(x) for x in lines if x.startswith('GPU_PACKED_A_RUN_END ')]
assert [(x['kind'], int(x['index'])) for x in begins] == expected_order
assert [(x['kind'], int(x['index'])) for x in ends] == expected_order
runs = []
for index, kind in enumerate(schedule):
    phase = 'warmup' if index < 4 else 'measured'
    push0 = 268
    start, end = begins[index], ends[index]
    assert start['phase'] == end['phase'] == phase and end['status'] == 'SUBMITTED'
    assert int(start['push0']) == push0 and int(start['outw']) == 213 and int(start['updates_per_accumulator']) == 1408
    geometry(start)
    assert int(start['groups']) == 8
    assert start['actual_chunks'] == start['actual_submits'] == start['native_admission'] == 'pending'
    p = ints(fields(profile_lines[index]), ['barrier', 'loops', 'instrlen', 'chunks', 'submits', 'ns', 'submit_ns', 'submit_max_ns', 'result'])
    geometry(p)
    assert (p['barrier'], p['loops'], p['chunks'], p['submits'], p['result']) == (1, 4, 8, 1, 0)
    assert p['instrlen'] == expected_native[kind]
    assert 0 < p['submit_ns'] == p['submit_max_ns'] <= p['ns']
    s = ints(fields(sample_lines[index]), ['version', 'sample', 'valid', 'instrlen', 'chunks', 'submits', 'submit_id', 'start_ticks', 'stop_ticks', 'delta_ticks', 'gpu_ns', 'host_start_ns', 'host_done_ns', 'submit_ns', 'result'])
    geometry(s)
    assert s.get('calibration') == ('packed-a' if kind == 'packed-a' else None)
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
    assert row['output'] == f'run-{index:02d}.f32' and type(row['bytes']) is int and row['bytes'] == 109072
    assert type(row['record_submit_wait_seconds']) in (int, float) and row['record_submit_wait_seconds'] > 0
    assert math.isfinite(row['record_submit_wait_seconds']) and abs(row['record_submit_wait_seconds'] - duration) < 1e-9
    expected_validated = f'GPU_PACKED_A_VALIDATED kind={kind} index={index} payload_values=27264 guard_values=4 exact=1 cross_variant_payload_bit_identical=1'
    assert lines.count(expected_validated) == 1 and lines.index(expected_validated) > lines.index(raw_line)
    rate = ISSUED_OPS / s['gpu_ns']
    runs.append(dict(kind=kind, index=index, phase=phase, push0=push0, push1=213,
                     profile=p, sample=s, whole_call_s=duration, issued_GFLOP_s=rate, useful_GFLOP_s=USEFUL_OPS / s['gpu_ns'],
                     gpu_fraction_of_host_submit=s['gpu_ns'] / p['submit_ns'],
                     host_submit_minus_gpu_ns=p['submit_ns'] - s['gpu_ns']))
for kind in expected_native:
    assert len({x['profile']['pipeline'] for x in runs if x['kind'] == kind}) == 1
assert runs[0]['profile']['pipeline'] != runs[1]['profile']['pipeline']
assert lines.index(pass_line) > max(lines.index(x) for x in lines if x.startswith('GPU_PACKED_A_VALIDATED '))
# stdout and stderr travel through separate systemd-run pipes before being merged.
# Preserve each stream's ordering; do not infer cross-stream chronology from merge order.
# stdout_sequence/begins/ends above validate helper run order. stderr_events and the
# run loop validate sample/profile order, consecutive IDs, selector/native/pipeline,
# geometry and exact submit-interval correlation for the corresponding run index.
transport_scope = ('Separate stdout/stderr pipes may be merged out of chronological order; '
                   'per-stream ordering and sample/submit IDs plus pipeline, calibration, '
                   'geometry and submit-interval fields correlate runs. Merged line positions '
                   'do not establish timer bracketing by stdout BEGIN/END markers.')

summary_lines = [fields(x) for x in lines if x.startswith('GPU_PACKED_A_MEASURED_SUMMARY ')]
assert [x['kind'] for x in summary_lines] == list(expected_native)
for summary in summary_lines:
    assert int(summary['runs']) == 4
    total = sum(x['record_submit_wait_seconds'] for x in metadata['runs'] if x['kind'] == summary['kind'] and x['phase'] == 'measured')
    assert abs(float(summary['record_submit_wait_total_seconds']) - total) < 1e-9
    assert abs(float(summary['mean_seconds']) - total / 4) < 1e-9


def guard(tag):
    return struct.pack('<4I', *[0x3e800000 | (tag * 131 + lane * 17 + 3) for lane in range(4)])


# Independent integer fixtures, rather than values decoded from helper output.
a_values = [((13*co+7*(k%32)+3)%31)-15 for co in range(128) for k in range(1408)]
b_values = [(((11*ci+5*(xpad-5)+1)%29)-14) if 5 <= xpad < 218 else 0
            for ci in range(128) for xpad in range(268)]
bias = [((17*co+9)%255)-127 for co in range(128)]
expected = [0.0] * 27264
max_abs_partial = 0
for co in range(128):
    row = a_values[co*1408:(co+1)*1408]
    for x in range(213):
        numerator = bias[co]
        for ci in range(128):
            for tap in range(11):
                numerator += row[ci*11+tap] * b_values[ci*268+x+tap]
                assert abs(numerator) <= 295807 < 2**24
                max_abs_partial = max(max_abs_partial, abs(numerator))
        index = ((co//4)*213+x)*4+co%4
        expected[index] = numerator / 4096
expected_output = struct.pack('<27264f', *expected)
packed_values = [a_values[co*1408+k] for k in range(1408) for co in range(128)]
input_expected = {
    'baseline-weights': struct.pack('<180224f', *[v/64 for v in a_values]),
    'packed-weights': struct.pack('<180224f', *[v/64 for v in packed_values]),
    'baseline-bias': struct.pack('<128f', *[v/4096 for v in bias]),
    'packed-bias': struct.pack('<128f', *[v/4096 for v in bias]),
    'input': struct.pack('<34304f', *[v/64 for v in b_values]) + guard(1),
}
assert input_expected['baseline-weights'] != input_expected['packed-weights']
sizes = {**{f'run-{i:02d}.f32':109072 for i in range(12)},
         **{f'{name}-{when}.f32':len(payload) for name,payload in input_expected.items() for when in ['before','after']}}
assert len(sizes) == 22 and sum(sizes.values()) == 4468960
assert set(p.name for p in a.raw.iterdir()) == set(sizes) | {'result.json'}
raw_pins = {}
for name, size in sizes.items():
    path = a.raw / name
    assert path.is_file() and not path.is_symlink() and path.stat().st_size == size
    data = path.read_bytes()
    if name.startswith('run-'):
        index = int(name[4:6]); oracle = expected_output + guard(10+index)
        poison = 0x7fc00100 + index
        assert all(word[0] != poison for word in struct.iter_unpack('<I', data[:109056]))
    else:
        input_name, when = name[:-4].rsplit('-',1)
        assert when in ['before','after']; oracle = input_expected[input_name]
    assert data == oracle, ('independent raw oracle', name)
    raw_pins[name] = dict(bytes=size, sha256=sha(path))
# Both expected-layout matches above cover every source/upload and GPU-readback byte.
# Also spell out actual before/after and actual uploaded scalar transposition relations.
for name in input_expected:
    assert (a.raw / (name+'-before.f32')).read_bytes() == (a.raw / (name+'-after.f32')).read_bytes()
baseline_words = struct.unpack('<180224I', (a.raw / 'baseline-weights-after.f32').read_bytes())
packed_words = struct.unpack('<180224I', (a.raw / 'packed-weights-after.f32').read_bytes())
assert all(packed_words[k*128+co] == baseline_words[co*1408+k] for co in range(128) for k in range(1408))

# Bind offline raw identity to the runner's successful terminal collection and remote hashes.
evidence = a.raw.parent
receipt_path = evidence / 'runner-result.json'
assert receipt_path.is_file() and not receipt_path.is_symlink()
receipt = json.loads(receipt_path.read_text())
assert receipt['status'] == 'passed' and receipt['mode'] == 'probe' and receipt['exit_code'] == 0
assert receipt['run_log'] == dict(bytes=a.log.stat().st_size, sha256=sha(a.log))
all_raw_pins = dict(raw_pins, **{'result.json':dict(bytes=result_path.stat().st_size, sha256=sha(result_path))})
assert receipt['raw_files'] == all_raw_pins
launch_path = evidence / 'launch.json'
launch = json.loads(launch_path.read_text())
assert launch['mode'] == 'probe' and launch['payload'] == receipt['payload']
remote_output = launch['remote'] + '/probe-output/'
remote_hashes = {}
remote_before, remote_after = evidence / 'outputs-before.sha256', evidence / 'outputs-after.sha256'
assert remote_before.read_bytes() == remote_after.read_bytes()
for line in remote_before.read_text().splitlines():
    digest, path = line.split(None, 1)
    assert re.fullmatch('[0-9a-f]{64}', digest) and path.startswith(remote_output)
    name = path[len(remote_output):]
    assert name in all_raw_pins and name not in remote_hashes
    remote_hashes[name] = digest
assert remote_hashes == {name:pin['sha256'] for name,pin in all_raw_pins.items()}

summaries = {}
for kind in expected_native:
    group = [r for r in runs if r['kind'] == kind and r['phase'] == 'measured']
    summary = {}
    for key, values in [('gpu_ns', [r['sample']['gpu_ns'] for r in group]),
                        ('host_submit_ns', [r['profile']['submit_ns'] for r in group]),
                        ('whole_call_s', [r['whole_call_s'] for r in group]),
                        ('issued_GFLOP_s', [r['issued_GFLOP_s'] for r in group]),
                        ('useful_GFLOP_s', [r['useful_GFLOP_s'] for r in group])]:
        summary[key] = dict(values=values, mean=statistics.mean(values), median=statistics.median(values), minimum=min(values), maximum=max(values), sample_stdev=statistics.stdev(values))
    summary['issued_work_over_mean_gpu_GFLOP_s'] = ISSUED_OPS / summary['gpu_ns']['mean']
    summary['useful_work_over_mean_gpu_GFLOP_s'] = USEFUL_OPS / summary['gpu_ns']['mean']
    summaries[kind] = summary
pairs = []
for first in [4, 6, 8, 10]:
    pair = {r['kind']: r for r in runs[first:first+2]}
    baseline_ns, candidate_ns = [pair[k]['sample']['gpu_ns'] for k in expected_native]
    pairs.append(dict(indices=[first, first+1], order=schedule[first:first+2], baseline_gpu_ns=baseline_ns,
                      candidate_gpu_ns=candidate_ns, candidate_faster=candidate_ns < baseline_ns,
                      gpu_reduction_percent=100 * (baseline_ns-candidate_ns) / baseline_ns,
                      host_submit_ns={kind:pair[kind]['profile']['submit_ns'] for kind in expected_native},
                      whole_call_s={kind:pair[kind]['whole_call_s'] for kind in expected_native}))
base, candidate = [summaries[k] for k in expected_native]
gain = 100 * (base['gpu_ns']['mean'] - candidate['gpu_ns']['mean']) / base['gpu_ns']['mean']
base_sum = sum(base['gpu_ns']['values'])
candidate_sum = sum(candidate['gpu_ns']['values'])
difference = base_sum - candidate_sum
direction = 'packed-a-faster' if difference > 0 else 'packed-a-slower' if difference < 0 else 'equal'
consistent = all((p['baseline_gpu_ns'] - p['candidate_gpu_ns']) * difference > 0 for p in pairs)
comparison = dict(mean_gpu_reduction_percent=gain, mean_gap_direction=direction,
                  baseline_measured_gpu_total_ns=base_sum, candidate_measured_gpu_total_ns=candidate_sum,
                  mean_host_submit_reduction_percent=100 * (base['host_submit_ns']['mean'] - candidate['host_submit_ns']['mean']) / base['host_submit_ns']['mean'],
                  mean_whole_call_reduction_percent=100 * (base['whole_call_s']['mean'] - candidate['whole_call_s']['mean']) / base['whole_call_s']['mean'],
                  balanced_pairs=pairs, prospective_gate='Advance focused production candidate only if mean GPU interval falls at least 5 percent and all four balanced adjacent pairs favor packed-A; native/resource qualification remains separate.',
                  all_pairs_same_direction_as_mean=consistent,
                  all_pairs_favor_candidate=all(p['candidate_faster'] for p in pairs),
                  focused_performance_gate_passed=20 * difference >= base_sum and all(p['candidate_faster'] for p in pairs))
report = dict(status='PASS', generated_by='LLM', timing_scope=metadata['timing_scope'],
              log_transport_scope=transport_scope,
              log=str(a.log), log_sha256=sha(a.log), analyzer_sha256=sha(Path(__file__)),
              expected_timer_profile_instrlen=expected_native,
              native_resource_qualification='PENDING separate exact native/resource/dataflow review; 27/28 timer/profile fields alone do not prove it.',
              result_json_sha256=sha(result_path), runner_receipt_sha256=sha(receipt_path),
              remote_hashes_before_sha256=sha(remote_before), remote_hashes_after_sha256=sha(remote_after),
              profile_totals=dict(dispatches=12, chunks=96, submits=12, samples=12),
              numerical=dict(independent_integer_oracle=True, oracle_scalar_count=27264, output_values=327168,
                             output_guard_values=48, input_guard_values=4, production_model_guard_values=0,
                             all_output_payloads_bit_identical=True, all_gpu_model_and_input_bytes_unchanged=True,
                             actual_gpu_weight_full_transposition=True, max_abs_oracle_integer_partial=max_abs_partial,
                             changing_nan_poison_absent_from_all_outputs=True, raw_files=22, raw_bytes=4468960),
              fixture_sha256={**{name:hashlib.sha256(data).hexdigest() for name,data in input_expected.items()},
                              'output_payload':hashlib.sha256(expected_output).hexdigest()},
              work=dict(issued_fp32_ops_per_run=ISSUED_OPS, useful_fp32_ops_per_run=USEFUL_OPS,
                        counting='Each scalar MAD counts as two FP32 operations; issued work includes inactive output-column arithmetic. Bias/address/control operations excluded.',
                        rates='Operations divided by GPU nanoseconds numerically gives GFLOP/s. Per-sample rate means and work divided by mean GPU time are reported separately.'),
              create_pipeline_seconds=metadata['create_pipeline_seconds'],
              create_pipeline_scope=metadata['create_pipeline_scope'],
              raw_pins=raw_pins, runs=runs, summaries=summaries, comparison=comparison,
              scope='Actual two Convolution1D create_pipeline/upload paths, identical convolution fixture and math workload, OFF versus packed-A weights/preload; no padding operator inside timed math calls.',
              limitations=['Native/resource/dataflow admission remains pending separate root review, including candidate contiguous A shared-store code generation and register/shared/private limits.',
                           'Poison upload, distinct live GPU destination allocations and deferred-readback execution are supported by pinned helper source, markers and helper checks; raw files contain final readbacks rather than pre-dispatch allocation-address evidence.',
                           'This focused gain does not establish padding/forward/full-graph integration, end-to-end model performance or acceptance.',
                           'Create-pipeline intervals include packing plus shader/pipeline creation; they are not isolated packing cost.',
                           'Achieved rates are implementation observations, not hardware ceilings, bandwidth, occupancy or measured ALU utilization.',
                           'GPU intervals retain command work and may include scheduling; host whole calls exclude every deferred readback.',
                           'Four warmups and eight measured calls are retained without fastest-only selection or statistical confidence claims.',
                           'Sources/binaries, live services, new faults, temperature and whole-window clocks require separate runner/conditions review.'])
assert a.log.read_bytes() == initial_log_bytes, 'log changed during offline analysis'
for name,pin in all_raw_pins.items():
    assert (a.raw / name).stat().st_size == pin['bytes'] and sha(a.raw / name) == pin['sha256'], ('capture changed during analysis', name)
with a.output.open('x') as f:
    json.dump(report, f, indent=2, allow_nan=False); f.write('\n')
print(json.dumps({k:report[k] for k in ['profile_totals','numerical','summaries','comparison']}, indent=2))
