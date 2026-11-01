#!/usr/bin/env python3
import argparse, hashlib, json, math, re, statistics, struct
from pathlib import Path

p = argparse.ArgumentParser(description='Validate one fixed 12-forward immutable-state-reuse comparison; no device actions.')
p.add_argument('log', type=Path)
p.add_argument('raw_directory', type=Path)
p.add_argument('report', type=Path)
p.add_argument('--pins', type=Path, help='Optional JSON object mapping frozen local file paths to expected SHA256.')
a = p.parse_args()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
pins = json.loads(a.pins.read_text()) if a.pins else {}
for path, expected in pins.items():
    assert sha(Path(path)) == expected, ('pin', path)
text = a.log.read_text()
lines = text.splitlines()
result = json.loads((a.raw_directory / 'result.json').read_text())
assert not (a.raw_directory / 'pending.json').exists()
assert result['format'] == 'radish-state-reuse-probe-v1' and result['status'] == 'passed'
for key, expected in dict(width=213, channels=128, kernel=11, dilation=1, padding=5,
                          warmup_runs=4, measured_runs=8, total_runs=12, expected_timestamp_samples=12, expected_reuse_samples=6,
                          output_values_per_run=27264, output_bytes_per_run=109056,
                          total_validated_values=327168, absolute_tolerance=2e-5,
                          relative_tolerance=2e-5, all_outputs_bit_identical=True,
                          input_preserved=True, per_run_output_poison=True, raw_capture_before_reference=True).items():
    assert result[key] == expected, key
assert result['per_run_output_poison'] is True
assert len(result['runs']) == 12
assert result['timing_scope'] == 'Forward construction, changing output poison/flush, submit, wait and download; excludes CPU reference, raw file writes, uploads and pipeline creation. GPU timestamps exclude the host poison; driver math/GPU sample validation is separate.'
assert 'PASS: GPU state reuse balanced probe warmup_runs=4 measured_runs=8 outputs_per_run=27264 all_outputs=327168;' in text
assert 'every run bit-identical to OFF run0 and passes double reference atol=2e-05 rtol=2e-05; all input bytes preserved; timestamp/profile qualification is separate' in text
assert text.count('GPU_STATE_REUSE_RAW_CAPTURE_COMPLETE runs=12 values=327168 validation=pending') == 1
stdout_runs = [line for line in lines if line.startswith(('GPU_STATE_REUSE_RUN_BEGIN ', 'GPU_STATE_REUSE_OUTPUT_POISON ', 'GPU_STATE_REUSE_RUN_END '))]
assert len(stdout_runs) == 36
for index in range(12):
    assert stdout_runs[3 * index].startswith(f'GPU_STATE_REUSE_RUN_BEGIN index={index} ')
    assert stdout_runs[3 * index + 1] == f'GPU_STATE_REUSE_OUTPUT_POISON index={index} values=27264 changing_nan=1 flushed=1 before_submit=1'
    assert stdout_runs[3 * index + 2].startswith(f'GPU_STATE_REUSE_RUN_END index={index} ')


def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line))


def profile(line):
    value = fields(line)
    for key in ['barrier', 'loops', 'instrlen', 'chunks', 'submits', 'ns', 'submit_ns', 'submit_max_ns', 'result']:
        value[key] = int(value[key])
    for key in ['grid', 'local']:
        value[key] = list(map(int, value[key].split(',')))
    assert value['result'] == 0
    assert 0 <= value['submit_max_ns'] <= value['submit_ns'] <= value['ns']
    assert value['submit_ns'] <= value['submits'] * value['submit_max_ns']
    if value['submits'] == 1:
        assert value['submit_ns'] == value['submit_max_ns']
    return value


def signature(value):
    return {key: value[key] for key in ['pipeline', 'grid', 'local', 'barrier', 'loops', 'instrlen', 'chunks', 'submits', 'result']}


profiles = [profile(line) for line in lines if line.startswith('RADISH_PROFILE dispatch ')]
assert len(profiles) == 38
assert not any(line.startswith('RADISH_PROFILE transfer ') for line in lines)
all_sample_lines = [line for line in lines if line.startswith('RADISH_GPU_ELAPSED ')]
assert len(all_sample_lines) == 12
assert all(len(line.encode()) + 1 <= 1024 for line in all_sample_lines)
raw_profiles = [line for line in lines if line.startswith('RADISH_PROFILE dispatch ')]
stderr_events = [line for line in lines if line.startswith(('RADISH_PROFILE ', 'RADISH_GPU_ELAPSED '))]
expected_stderr = [raw_profiles[0]]
for index in range(12):
    expected_stderr.extend([raw_profiles[1 + 3 * index], all_sample_lines[index],
                            raw_profiles[2 + 3 * index], raw_profiles[3 + 3 * index]])
expected_stderr.append(raw_profiles[-1])
assert stderr_events == expected_stderr, 'unexpected sample/profile stderr order'
assert all('state_reuse_chunks' not in value for value in profiles), 'profile format changed'
assert sum(x['chunks'] for x in profiles) == 188
assert sum(x['submits'] for x in profiles) == 38
schedule = [0, 1, 1, 0] * 3
runs, samples, boundaries = [], [], []
raw_pins = {}
run0 = (a.raw_directory / 'run-00.f32').read_bytes()
assert len(run0) == 109056 and all(math.isfinite(x) for x in struct.unpack('<27264f', run0))
for index, mode in enumerate(schedule):
    phase = 'warmup' if index < 4 else 'measured'
    expected_begin = (f'GPU_STATE_REUSE_RUN_BEGIN index={index} phase={phase} state_reuse={mode} '
                      'expected_native_length=27 grid=4,2,1 local=16,8,1 math_groups=8 '
                      'expected_math_chunks=8 expected_math_submits=1 padding_groups=144 pitch=268')
    assert lines.count(expected_begin) == 1
    begin = lines.index(expected_begin)
    endings = [(i, line) for i, line in enumerate(lines) if line.startswith(f'GPU_STATE_REUSE_RUN_END index={index} ')]
    assert len(endings) == 1
    end, end_line = endings[0]
    assert begin < end and (not boundaries or boundaries[-1][1] < begin)
    boundaries.append([begin, end])
    ending = fields(end_line)
    assert ending['phase'] == phase and int(ending['state_reuse']) == mode and ending['status'] == 'SUBMITTED'
    whole = float(ending['construct_submit_wait_download_seconds'])
    assert math.isfinite(whole) and whole > 0
    inside = lines[begin + 1:end]
    current = [profile(line) for line in inside if line.startswith('RADISH_PROFILE dispatch ')]
    if index == 0 and len(current) == 4:
        assert current[0] == profiles[0]
        current = current[1:]
    assert current == profiles[1 + 3 * index:4 + 3 * index] and len(current) == 3
    pad, compute, download = current
    assert (pad['grid'], pad['local'], pad['barrier'], pad['loops'], pad['instrlen'], pad['chunks'], pad['submits']) == ([9, 16, 1], [8, 8, 1], 0, 0, 9, 3, 1)
    assert (compute['grid'], compute['local'], compute['barrier'], compute['loops'], compute['instrlen'], compute['chunks'], compute['submits']) == ([4, 2, 1], [16, 8, 1], 1, 4, 27, 8, 1)
    assert (download['grid'], download['local'], download['barrier'], download['loops'], download['instrlen'], download['chunks'], download['submits']) == ([7, 32, 1], [32, 1, 1], 0, 0, 4, 4, 1)
    selected = [fields(line) for line in inside if line.startswith('RADISH_GPU_ELAPSED ')]
    assert len(selected) == 1
    timer = None
    if selected:
        timer = selected[0]
        for key in ['version', 'sample', 'valid', 'instrlen', 'chunks', 'submits', 'submit_id', 'start_ticks', 'stop_ticks', 'delta_ticks', 'gpu_ns', 'host_start_ns', 'host_done_ns', 'submit_ns', 'result']:
            timer[key] = int(timer[key])
        for key in ['grid', 'local']:
            timer[key] = list(map(int, timer[key].split(',')))
        assert timer['version'] == 1 and timer['sample'] == len(samples) and timer['sample'] < 16
        assert timer['valid'] == 1 and timer['reason'] == 'ok' and timer['result'] == 0
        assert 'calibration' not in timer
        if mode:
            timer['state_reuse_chunks'] = int(timer['state_reuse_chunks'])
            assert timer['state_reuse_chunks'] == 7, ('actual skipped chunk count', index)
        else:
            assert 'state_reuse_chunks' not in timer, ('OFF emitted reuse count', index)
        for key in ['pipeline', 'grid', 'local', 'instrlen', 'chunks', 'submits', 'submit_ns']:
            assert timer[key] == compute[key], ('sample/profile', index, key)
        start, stop = timer['start_ticks'], timer['stop_ticks']
        assert 0 < start < stop < 2**64
        assert all(((x >> shift) & 0xffffffff) != 0xffffffff for x in [start, stop] for shift in [0, 32])
        delta = stop - start
        assert 0 < delta <= 96000000 and timer['delta_ticks'] == delta
        quotient, remainder = divmod(delta, 19200000)
        assert timer['gpu_ns'] == quotient * 1000000000 + remainder * 1000000000 // 19200000
        assert 0 <= timer['host_start_ns'] < timer['host_done_ns']
        assert timer['host_done_ns'] - timer['host_start_ns'] == timer['submit_ns'] > 0
        assert timer['gpu_ns'] <= timer['submit_ns'] + 1000
        if samples:
            assert timer['submit_id'] > samples[-1]['submit_id']
        samples.append(timer)
    validated = [fields(line) for line in lines if line.startswith(f'GPU_STATE_REUSE_VALIDATED index={index} ')]
    assert len(validated) == 1
    validation = validated[0]
    assert validation['phase'] == phase and int(validation['state_reuse']) == mode
    assert int(validation['outputs']) == 27264 and validation['bit_identical_to_run0'] == '1'
    recorded = result['runs'][index]
    assert recorded['index'] == index and recorded['phase'] == phase and recorded['state_reuse'] == mode
    assert recorded['output'] == f'run-{index:02d}.f32'
    assert abs(recorded['construct_submit_wait_download_seconds'] - whole) < 1e-9
    assert abs(recorded['max_abs_error'] - float(validation['max_abs'])) < 1e-12
    raw = a.raw_directory / recorded['output']
    assert raw.read_bytes() == run0
    raw_pins[raw.name] = sha(raw)
    runs.append(dict(index=index, phase=phase, state_reuse=mode, whole_forward_s=whole,
                     padding=pad, math=compute, download=download, sample=timer,
                     profiled_ns=sum(x['ns'] for x in current),
                     unprofiled_forward_residual_s=whole - sum(x['ns'] for x in current) / 1e9))
assert len(samples) == 12 and sum(r['sample'] is not None for r in runs[:4]) == 4
assert sum('state_reuse_chunks' in sample for sample in samples) == 6
assert all(len({json.dumps(signature(r[k]), sort_keys=True) for r in runs}) == 1 for k in ['padding', 'math', 'download'])
for conversion in [profiles[0], profiles[-1]]:
    assert (conversion['grid'], conversion['local'], conversion['chunks'], conversion['submits']) == ([7, 32, 1], [32, 1, 1], 4, 1)
expected_input = struct.pack('<27264f', *[((x * 13 + c * 7) % 127 - 63) / 64 for c in range(128) for x in range(213)])
for name in ['input-before.f32', 'input-after.f32']:
    raw = a.raw_directory / name
    assert raw.read_bytes() == expected_input
    raw_pins[name] = sha(raw)

summaries = {}
for mode in [0, 1]:
    selected = [r for r in runs[4:] if r['state_reuse'] == mode]
    assert len(selected) == 4
    summary = {}
    for key, get in [('math_submit_ns', lambda r: r['math']['submit_ns']),
                     ('math_command_ns', lambda r: r['math']['ns']),
                     ('whole_forward_s', lambda r: r['whole_forward_s'])]:
        values = [get(r) for r in selected]
        summary[key] = dict(values=values, mean=statistics.mean(values), median=statistics.median(values), minimum=min(values), maximum=max(values))
    printed = [fields(line) for line in lines if line.startswith(f'GPU_STATE_REUSE_MEASURED_SUMMARY state_reuse={mode} ')]
    assert len(printed) == 1 and printed[0]['runs'] == '4'
    assert abs(float(printed[0]['mean_seconds']) - summary['whole_forward_s']['mean']) < 1e-9
    assert abs(float(printed[0]['construct_submit_wait_download_total_seconds']) - sum(summary['whole_forward_s']['values'])) < 1e-8
    values = [r['sample']['gpu_ns'] for r in selected]
    summary['gpu_queued_ns'] = dict(values=values, mean=statistics.mean(values), median=statistics.median(values), minimum=min(values), maximum=max(values))
    summaries[str(mode)] = summary
pairs = []
for off_index, on_index in [(4, 5), (7, 6), (8, 9), (11, 10)]:
    off, on = runs[off_index], runs[on_index]
    assert off['state_reuse'] == 0 and on['state_reuse'] == 1
    chronological = sorted([off_index, on_index])
    pairs.append(dict(off_index=off_index, on_index=on_index,
                      chronological_indices=chronological,
                      chronological_order=['ON' if runs[i]['state_reuse'] else 'OFF' for i in chronological],
                      gpu_off_ns=off['sample']['gpu_ns'], gpu_on_ns=on['sample']['gpu_ns'],
                      gpu_gain_percent=100 * (1 - on['sample']['gpu_ns'] / off['sample']['gpu_ns']),
                      math_submit_off_ns=off['math']['submit_ns'], math_submit_on_ns=on['math']['submit_ns'],
                      math_submit_gain_percent=100 * (1 - on['math']['submit_ns'] / off['math']['submit_ns']),
                      whole_forward_off_s=off['whole_forward_s'], whole_forward_on_s=on['whole_forward_s'],
                      whole_forward_gain_percent=100 * (1 - on['whole_forward_s'] / off['whole_forward_s'])))
mean_gains = {metric: 100 * (1 - summaries['1'][metric]['mean'] / summaries['0'][metric]['mean'])
              for metric in ['gpu_queued_ns', 'math_submit_ns', 'math_command_ns', 'whole_forward_s']}
off_total = sum(summaries['0']['gpu_queued_ns']['values'])
on_total = sum(summaries['1']['gpu_queued_ns']['values'])
mean_pass = on_total * 100 <= off_total * 95
pairs_pass = all(pair['gpu_on_ns'] < pair['gpu_off_ns'] for pair in pairs)
criterion = dict(prospective_minimum_mean_gpu_gain_percent=5,
                 mean_gpu_gain_percent=mean_gains['gpu_queued_ns'],
                 mean_gpu_gain_at_least_5_percent=mean_pass,
                 all_four_gpu_pairs_improve=pairs_pass,
                 focused_candidate_pass=mean_pass and pairs_pass,
                 scope='Prospective engineering stop criterion; not statistical confidence, a diagnostic perturbation budget or a model speedup.')
qualified = criterion['focused_candidate_pass']
report = dict(log=str(a.log), log_sha256=sha(a.log), result_json_sha256=sha(a.raw_directory / 'result.json'), pins=pins,
              numerical=dict(passed=True, values=327168, finite=True, all_raw_outputs_bit_identical=True, input_bytes_preserved=True, per_run_output_poison=True,
                             double_reference='Full helper gate, unchanged 2e-5 + 2e-5*abs(ref); not rerun by this parser'),
              profile_totals=dict(dispatch_commands=38, chunks=188, submits=38), profiles=profiles, runs=runs,
              samples=samples, summaries=summaries, pairs=pairs, raw_sha256=raw_pins,
              timestamp_protocol_pass=True, actual_reuse_count_pass=True,
              mean_gains_percent=mean_gains, performance_criterion=criterion,
              interpretation=('Focused performance criterion passes; broader shader/model/reference qualification is still required.' if qualified else
                              'Functional result preserved; reject this performance candidate without relaxing waits or flushes.'),
              scope='Timestamping is ON for all12 calls. OFF retains eight full state setups; ON retains first setup and skips seven immutable setups while preserving WFI/constants/EXEC/flush/waits. GPU queued elapsed is not pure shader/ALU time. Host submit ends before diagnostic CPU read/log. Whole forward includes changing output poison/flush and download; GPU timestamps exclude host poison. Pin/service/fault/clock evidence requires separate runner audit.')
with a.report.open('x') as out:
    json.dump(report, out, indent=2)
    out.write('\n')
print(json.dumps({key: report[key] for key in ['profile_totals', 'summaries', 'pairs', 'mean_gains_percent', 'performance_criterion', 'interpretation']}, indent=2))
