#!/usr/bin/env python3
import argparse, hashlib, json, math, re, statistics, struct
from pathlib import Path

p = argparse.ArgumentParser(description='Validate one fixed 12-forward GPU-elapsed qualification capture; no device actions.')
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
assert result['format'] == 'radish-gpu-elapsed-probe-v1' and result['status'] == 'passed'
for key, expected in dict(width=213, channels=128, kernel=11, dilation=1, padding=5,
                          warmup_runs=4, measured_runs=8, total_runs=12, expected_on_samples=6,
                          output_values_per_run=27264, output_bytes_per_run=109056,
                          total_validated_values=327168, absolute_tolerance=2e-5,
                          relative_tolerance=2e-5, all_outputs_bit_identical=True,
                          input_preserved=True, raw_capture_before_reference=True).items():
    assert result[key] == expected, key
assert 'PASS: GPU elapsed balanced probe warmup_runs=4 measured_runs=8 outputs_per_run=27264 all_outputs=327168;' in text
assert 'every run bit-identical to OFF run0 and passes double reference atol=2e-05 rtol=2e-05; all input bytes preserved; timestamp/profile qualification is separate' in text
assert text.count('GPU_ELAPSED_RAW_CAPTURE_COMPLETE runs=12 values=327168 validation=pending') == 1


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
assert len(all_sample_lines) == 6
assert all(len(line.encode()) + 1 <= 1024 for line in all_sample_lines)
assert sum(x['chunks'] for x in profiles) == 188
assert sum(x['submits'] for x in profiles) == 38
schedule = [0, 1, 1, 0] * 3
runs, samples, boundaries = [], [], []
raw_pins = {}
run0 = (a.raw_directory / 'run-00.f32').read_bytes()
assert len(run0) == 109056 and all(math.isfinite(x) for x in struct.unpack('<27264f', run0))
for index, mode in enumerate(schedule):
    phase = 'warmup' if index < 4 else 'measured'
    expected_begin = (f'GPU_ELAPSED_RUN_BEGIN index={index} phase={phase} diagnostic={mode} '
                      'expected_native_length=27 grid=4,2,1 local=16,8,1 math_groups=8 '
                      'expected_math_chunks=8 expected_math_submits=1 padding_groups=144 pitch=268')
    assert lines.count(expected_begin) == 1
    begin = lines.index(expected_begin)
    endings = [(i, line) for i, line in enumerate(lines) if line.startswith(f'GPU_ELAPSED_RUN_END index={index} ')]
    assert len(endings) == 1
    end, end_line = endings[0]
    assert begin < end and (not boundaries or boundaries[-1][1] < begin)
    boundaries.append([begin, end])
    ending = fields(end_line)
    assert ending['phase'] == phase and int(ending['diagnostic']) == mode and ending['status'] == 'SUBMITTED'
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
    assert len(selected) == mode
    timer = None
    if selected:
        timer = selected[0]
        for key in ['version', 'sample', 'valid', 'instrlen', 'chunks', 'submits', 'submit_id', 'start_ticks', 'stop_ticks', 'delta_ticks', 'gpu_ns', 'host_start_ns', 'host_done_ns', 'submit_ns', 'result']:
            timer[key] = int(timer[key])
        for key in ['grid', 'local']:
            timer[key] = list(map(int, timer[key].split(',')))
        assert timer['version'] == 1 and timer['sample'] == len(samples) and timer['sample'] < 16
        assert timer['valid'] == 1 and timer['reason'] == 'ok' and timer['result'] == 0
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
    validated = [fields(line) for line in lines if line.startswith(f'GPU_ELAPSED_VALIDATED index={index} ')]
    assert len(validated) == 1
    validation = validated[0]
    assert validation['phase'] == phase and int(validation['diagnostic']) == mode
    assert int(validation['outputs']) == 27264 and validation['bit_identical_to_run0'] == '1'
    recorded = result['runs'][index]
    assert recorded['index'] == index and recorded['phase'] == phase and recorded['diagnostic'] == mode
    assert recorded['output'] == f'run-{index:02d}.f32'
    assert abs(recorded['construct_submit_wait_download_seconds'] - whole) < 1e-9
    assert abs(recorded['max_abs_error'] - float(validation['max_abs'])) < 1e-12
    raw = a.raw_directory / recorded['output']
    assert raw.read_bytes() == run0
    raw_pins[raw.name] = sha(raw)
    runs.append(dict(index=index, phase=phase, diagnostic=mode, whole_forward_s=whole,
                     padding=pad, math=compute, download=download, sample=timer,
                     profiled_ns=sum(x['ns'] for x in current),
                     unprofiled_forward_residual_s=whole - sum(x['ns'] for x in current) / 1e9))
assert len(samples) == 6 and sum(r['sample'] is not None for r in runs[:4]) == 2
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
    selected = [r for r in runs[4:] if r['diagnostic'] == mode]
    assert len(selected) == 4
    summary = {}
    for key, get in [('math_submit_ns', lambda r: r['math']['submit_ns']),
                     ('math_command_ns', lambda r: r['math']['ns']),
                     ('whole_forward_s', lambda r: r['whole_forward_s'])]:
        values = [get(r) for r in selected]
        summary[key] = dict(values=values, mean=statistics.mean(values), median=statistics.median(values), minimum=min(values), maximum=max(values))
    printed = [fields(line) for line in lines if line.startswith(f'GPU_ELAPSED_MEASURED_SUMMARY diagnostic={mode} ')]
    assert len(printed) == 1 and printed[0]['runs'] == '4'
    assert abs(float(printed[0]['mean_seconds']) - summary['whole_forward_s']['mean']) < 1e-9
    assert abs(float(printed[0]['construct_submit_wait_download_total_seconds']) - sum(summary['whole_forward_s']['values'])) < 1e-8
    if mode:
        values = [r['sample']['gpu_ns'] for r in selected]
        summary['gpu_queued_ns'] = dict(values=values, mean=statistics.mean(values), minimum=min(values), maximum=max(values))
    summaries[str(mode)] = summary
pairs = []
for i in range(4, 12, 2):
    pair = runs[i:i + 2]
    off = next(r for r in pair if r['diagnostic'] == 0)
    on = next(r for r in pair if r['diagnostic'] == 1)
    pairs.append(dict(indices=[i, i + 1], order=['ON' if r['diagnostic'] else 'OFF' for r in pair],
                      math_submit_off_ns=off['math']['submit_ns'], math_submit_on_ns=on['math']['submit_ns'],
                      math_submit_on_penalty_percent=100 * (on['math']['submit_ns'] / off['math']['submit_ns'] - 1),
                      whole_forward_off_s=off['whole_forward_s'], whole_forward_on_s=on['whole_forward_s'],
                      whole_forward_on_penalty_percent=100 * (on['whole_forward_s'] / off['whole_forward_s'] - 1)))
interpretation = {}
for metric, pair_key in [('math_submit_ns', 'math_submit_on_penalty_percent'), ('whole_forward_s', 'whole_forward_on_penalty_percent')]:
    ratio = summaries['1'][metric]['mean'] / summaries['0'][metric]['mean']
    every_pair = all(pair[pair_key] > 5 for pair in pairs)
    interpretation[metric] = dict(on_mean_penalty_percent=100 * (ratio - 1), mean_within_5_percent=ratio <= 1.05,
                                  all_four_pairs_more_than_5_percent=every_pair, practical_budget_pass=ratio <= 1.05 and not every_pair)
qualified = all(x['practical_budget_pass'] for x in interpretation.values())
report = dict(log=str(a.log), log_sha256=sha(a.log), result_json_sha256=sha(a.raw_directory / 'result.json'), pins=pins,
              numerical=dict(passed=True, values=327168, finite=True, all_raw_outputs_bit_identical=True, input_bytes_preserved=True,
                             double_reference='Full helper gate, unchanged 2e-5 + 2e-5*abs(ref); not rerun by this parser'),
              profile_totals=dict(dispatch_commands=38, chunks=188, submits=38), profiles=profiles, runs=runs,
              samples=samples, summaries=summaries, pairs=pairs, raw_sha256=raw_pins,
              timestamp_protocol_pass=True, interpretation_budget=interpretation, interpretation_budget_pass=qualified,
              interpretation=('Eligible for further bounded timer calibration; no saturation or confidence claim.' if qualified else
                              'Functional timestamp result preserved; perturbation/noise budget fails, so do not project model headroom.'),
              scope='GPU queued elapsed for eight program/descriptor/EXEC/WFI/flush sequences plus seven later restores; not pure shader/ALU time. Host submit ends before diagnostic CPU read/log. Pin/service/fault/clock evidence requires separate runner audit.')
with a.report.open('x') as out:
    json.dump(report, out, indent=2)
    out.write('\n')
print(json.dumps({key: report[key] for key in ['profile_totals', 'summaries', 'pairs', 'interpretation_budget', 'interpretation_budget_pass', 'interpretation']}, indent=2))
