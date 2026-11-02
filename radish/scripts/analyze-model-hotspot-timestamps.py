import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--log', required=True, type=Path)
p.add_argument('--first-map', required=True, type=Path)
p.add_argument('--warm-map', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()

def pin(path):
    b = path.read_bytes()
    return {'path': str(path.resolve()), 'bytes': len(b), 'sha256': hashlib.sha256(b).hexdigest()}

def fields(line):
    return dict(item.split('=', 1) for item in line.split()[1:])

log = a.log.read_text()
log_pin = pin(a.log)
batches = [[] for _ in range(24)]
totals = {}
selected = {}
config = []
last_command = -1
last_submit = -1
for line in log.splitlines():
    if line.startswith('RADISH_GPU_HOTSPOT_CONFIG '):
        d = fields(line)
        assert d == dict(version='2', enabled='1', command_limit='24', binding_lookup='logical_set_binding_element'), d
        config.append(d)
    elif line.startswith('RADISH_GPU_HOTSPOT_SELECT '):
        d = fields(line)
        for key in ('version', 'command', 'selected_count', 'weight_slot', 'output_slot', 'bias_slot', 'weight_range', 'output_range', 'bias_range', 'input_w', 'output_w'):
            d[key] = int(d[key])
        assert d['version'] == 2 and d['command'] == len(selected) and d['selected_count'] == d['command'] + 1, d
        assert 0 <= d['command'] < 24 and len({d['weight_slot'], d['output_slot'], d['bias_slot']}) == 3, d
        assert all(0 <= d[key] < 4 for key in ('weight_slot', 'output_slot', 'bias_slot')), d
        assert d['weight_range'] == 720896 and d['output_range'] >= 25561 * 128 * 4 and d['bias_range'] >= 512 and d['output_w'] == 25561, d
        selected[d['command']] = d
    elif line.startswith('RADISH_GPU_HOTSPOT '):
        d = fields(line)
        numeric = ('version', 'command', 'batch', 'valid', 'instrlen', 'input_w', 'output_w',
                   'weight_range', 'chunks', 'submits', 'submit_id', 'start_ticks', 'stop_ticks',
                   'delta_ticks', 'gpu_ns', 'host_start_ns', 'host_done_ns', 'submit_ns', 'result')
        for key in numeric:
            d[key] = int(d[key])
        command = d['command']
        assert 0 <= command < 24 and command in (last_command, last_command + 1), d
        assert d['batch'] == len(batches[command]) and d['batch'] < 100, d
        assert d['version'] == d['valid'] == 1 and d['reason'] == 'ok' and d['result'] == 0, d
        assert d['grid'] == '400,2,1' and d['local'] == '16,8,1', d
        assert d['instrlen'] in (27, 28) and d['output_w'] == 25561 and d['weight_range'] == 720896, d
        assert d['chunks'] == 8 and d['submits'] == 1 and d['submit_id'] > last_submit, d
        if batches[command]:
            prev = batches[command][-1]
            assert d['submit_id'] == prev['submit_id'] + 1 and d['pipeline'] == prev['pipeline'], d
            assert d['start_ticks'] > prev['stop_ticks'], d
        assert 0 < d['delta_ticks'] == d['stop_ticks'] - d['start_ticks'] <= 96000000, d
        assert d['gpu_ns'] == d['delta_ticks'] * 10000 // 192, d
        assert d['submit_ns'] == d['host_done_ns'] - d['host_start_ns'] > 0, d
        assert d['gpu_ns'] <= d['submit_ns'] + 1000, d
        assert command in selected and all(d[k] == selected[command][k] for k in ('pipeline', 'weight_range', 'input_w', 'output_w')), d
        batches[command].append(d)
        last_command, last_submit = command, d['submit_id']
    elif line.startswith('RADISH_GPU_HOTSPOT_TOTAL '):
        d = fields(line)
        for key in ('version', 'command', 'batches', 'valid_batches', 'gpu_ns', 'submit_ns', 'instrumentation_ns'):
            d[key] = int(d[key])
        assert d['command'] not in totals and d['version'] == 1, d
        totals[d['command']] = d
assert len(config) == 1 and len(selected) == len(totals) == 24 and all(len(x) == 100 for x in batches)
expected = ['conv1d_28', 'conv1d_32', 'conv1d_36', 'conv1d_40', 'conv1d_43', 'conv1d_46',
            'conv1d_67', 'conv1d_70', 'conv1d_73', 'conv1d_76', 'conv1d_79', 'conv1d_82']
indices = [911, 938, 966, 993, 1021, 1048, 1375, 1402, 1430, 1457, 1485, 1512]
issued_flops = 128 * 25600 * 1408 * 2
useful_dot_flops = 128 * 25561 * 1408 * 2
iterations = []
for iteration, map_path in enumerate((a.first_map, a.warm_map)):
    mapping = json.loads(map_path.read_text())
    assert mapping['sha256'] == log_pin['sha256']
    assert mapping['totals']['commands'] == 1520
    rows = []
    for j, index in enumerate(indices):
        command = iteration * 12 + j
        actual = mapping['commands'][index]
        samples, total = batches[command], totals[command]
        assert actual['owner']['name'] == expected[j]
        assert actual['grid'] == [400, 2, 1] and actual['local'] == [16, 8, 1]
        assert actual['barrier'] == 1 and actual['loops'] == 4 and actual['result'] == 0
        assert actual['chunks'] == 800 and actual['submits'] == 100
        assert total['pipeline'] == actual['pipeline'] == samples[0]['pipeline']
        assert all(x['instrlen'] == actual['instrlen'] for x in samples)
        assert total['batches'] == total['valid_batches'] == 100
        assert total['gpu_ns'] == sum(x['gpu_ns'] for x in samples)
        assert total['submit_ns'] == sum(x['submit_ns'] for x in samples) == actual['submit_ns']
        assert max(x['submit_ns'] for x in samples) == actual['submit_max_ns']
        assert 0 < total['instrumentation_ns'] < actual['ns']
        assert total['submit_ns'] + total['instrumentation_ns'] <= actual['ns']
        rows.append({'owner': expected[j], 'command_index': index, 'sample_command': command,
                     'gpu_interval_ns': total['gpu_ns'], 'host_submit_ns': total['submit_ns'],
                     'command_wall_ns': actual['ns'], 'measured_host_instrumentation_ns': total['instrumentation_ns'],
                     'issued_dot_flops': issued_flops, 'useful_dot_flops': useful_dot_flops,
                     'issued_dot_GFLOPs_per_interval_second': issued_flops / total['gpu_ns'],
                     'max_batch_gpu_ns': max(x['gpu_ns'] for x in samples),
                     'max_batch_submit_ns': max(x['submit_ns'] for x in samples)})
    sums = {k: sum(r[k] for r in rows) for k in ('gpu_interval_ns', 'host_submit_ns', 'command_wall_ns',
                                               'measured_host_instrumentation_ns', 'issued_dot_flops', 'useful_dot_flops')}
    sums['issued_dot_GFLOPs_per_interval_second'] = sums['issued_dot_flops'] / sums['gpu_interval_ns']
    sums['gpu_interval_fraction_of_command_wall'] = sums['gpu_interval_ns'] / sums['command_wall_ns']
    iterations.append({'iteration': iteration + 1, 'totals': sums, 'commands': rows})
result = {'status': 'passed', 'run_log': log_pin, 'maps': [pin(a.first_map), pin(a.warm_map)],
          'batch_records': 2400, 'commands': 24, 'iterations': iterations,
          'limitations': ['GPU elapsed includes CP/setup/inter-chunk restore/flush/waits and diagnostic event overhead; not ALU busy or historical uninstrumented time.',
                         'Host instrumentation count excludes final summary/free/branch/packet-encoding overhead and GPU event/WFI overhead.',
                         'Full numerical/fault/service/payload/native and unchanged scheduling gates remain the original runner and independent pairing review responsibility.']}
a.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(pin(a.output)))
