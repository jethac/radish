#!/usr/bin/env python3
"""Freeze synthesis inputs and validate one-shot or bounded request-loop captures."""
import argparse
import array
import ctypes
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shlex
import sys

sys.dont_write_bytecode = True
SOURCE_PIN = 'b13da99ce02f03fb6b605720c46d09b746063ddb651bfaf5156a770204ffc4e9'
GRAPHS = {
    'frontend-only.onnx': (111933749, '56ba27008352a2191b631ebbf269bf4506d690a1b83d8ff8ff5524b11291ddd3'),
    'harmonic-source.onnx': (13810, '4bdcb9f2f54d6efbfdcefce384300352933cc71fa562930f23eaeb3443528fd2'),
}
CORE_GRAPHS = {
    'instancenorm': ('6499e4996cea79d28e9c72caab1e2f0ca7d9fab4420a6f198c7e197fb06d347f', 1098, 1307),
    'snake': ('e01c56a4f23d6d3c8f92dbecb0c3fc37f6f74e9f305f58b82d5196278a450d38', 906, 1067),
    'affine-snake': ('dbd9fbfe67e6053382f1e1734b6480c3a42f0bc59e57b85627fb16a6e9783d5a', 714, 875),
    'snake-deconv2d': ('5ba4b3fd791904b4c7560846072919027fd9d0c7d764f38028a79862e09e4980', 912, 1073),
    'affine-snake-deconv2d': ('e211a573578f5ad45000b487f6e36ac1376ae4bbdcfa6a6e32a95070ffbc807d', 720, 881),
}
ORT = '/usr/local/lib/straylight-listen/libonnxruntime.so'
ENV_NAMES = {
    'RADISH_NCNN_TILED', 'RADISH_NCNN_TILED32', 'RADISH_NCNN_PLANAR', 'RADISH_NCNN_NORM_SUM4',
    'RADISH_NCNN_SHARED32', 'RADISH_NCNN_MIXED_STORAGE', 'RADISH_NCNN_RESHAPE_ALIAS',
    'RADISH_NCNN_PIPELINE_CACHE', 'RADISH_NCNN_TRACE', 'RADISH_PROFILE', 'RADISH_BATCH_CHUNKS',
    'RADISH_TILED_BATCH_CHUNKS', 'RADISH_TILED_GROUPS_X', 'RADISH_SHARED_GROUPS',
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def asset(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), 'invalid regular file: ' + str(path))
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return {'bytes': path.stat().st_size, 'sha256': digest.hexdigest()}


def read_json(path):
    asset(path)
    return json.loads(Path(path).read_text())


def write_json(path, value):
    with Path(path).open('x') as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write('\n')


def pinned(path):
    path = Path(path).resolve(strict=True)
    value = asset(path)
    return {'path': str(path.resolve()), **value}


def validator_result(path, directory):
    spec = importlib.util.spec_from_file_location('radish_runtime_fixture_validation', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.validate(Path(directory))


def frontend_inputs(source, models, binary, graphs):
    capture = read_json(source / 'runtime.json')
    require(capture['format'] == 'radish-kokoro-runtime-fixture-v1'
            and capture['source_sha256'] == SOURCE_PIN, 'unexpected input capture provenance')
    inputs = {name: asset(source / name) for name in ('ids.i64', 'style.f32', 'speed.f32')}
    require(all(value == capture['files'][name] for name, value in inputs.items()), 'raw input differs from capture')
    require(inputs['style.f32']['bytes'] == 1024 and inputs['speed.f32']['bytes'] == 4
            and 24 <= inputs['ids.i64']['bytes'] <= 4096 and inputs['ids.i64']['bytes'] % 8 == 0,
            'invalid raw frontend input lengths')
    return {'format': 'radish-kokoro-device-frontend-inputs-v1', 'source_sha256': SOURCE_PIN,
            'input_directory': str(source.resolve()), 'runtime_manifest': asset(source / 'runtime.json'),
            'model_directory': str(models.resolve()), 'graphs': graphs, 'inputs': inputs,
            'binary_path': str(binary.resolve()), 'binary': asset(binary.resolve())}


def request_records(plan):
    if not plan.get('request_loop', False):
        return [plan]
    records = []
    for index, request in enumerate(plan['requests']):
        record = {key: value for key, value in plan.items() if key not in ('request_loop', 'requests')}
        record.update(request)
        record['request_index'] = index
        records.append(record)
    return records


def prepare(args):
    require(re.fullmatch(r'/var/tmp/radish/[A-Za-z0-9_-]+', args.remote), 'unsafe remote directory')
    require(1 <= args.threads <= 6 and 1 <= args.ort_threads <= 6 and 1 <= args.max_frames <= 4096
            and 1 <= args.repeat <= 10, 'invalid runtime limits')
    require((args.request_loop and 1 <= len(args.next_source) <= 3 and args.repeat == 1
             and args.core_graph == 'affine-snake-deconv2d')
            or (not args.request_loop and not args.next_source), 'invalid request-loop selection')
    graph_manifest = read_json(args.models / 'frontend-models.json')
    require(graph_manifest['format'] == 'radish-kokoro-frontend-models-v1'
            and graph_manifest['source_sha256'] == SOURCE_PIN, 'unexpected frontend model provenance')
    graphs = {}
    for name, (size, digest) in GRAPHS.items():
        graphs[name] = asset(args.models / name)
        require(graphs[name] == {'bytes': size, 'sha256': digest}, 'changed graph: ' + name)
        require(all(graph_manifest['graphs'][name][key] == value for key, value in graphs[name].items()),
                'graph manifest mismatch')
    assets = {name: pinned(path) for name, path in (
        ('binary', args.binary), ('driver', args.driver), ('loader', args.loader),
        ('param', args.template / 'decoder.ncnn.param'), ('weights', args.template / 'decoder.ncnn.bin'),
        ('tail', args.tail))}
    require(assets['loader']['bytes'] == 703696 and assets['loader']['sha256'] ==
            '75699a38b1f26c1e8bfa2062e6593177bb07d4e3e346486d117fab8a0de0104a', 'unexpected private Vulkan loader')
    digest, layers, blobs = CORE_GRAPHS[args.core_graph]
    require(assets['param']['sha256'] == digest, 'unexpected selected parameter template')
    require((args.template / 'decoder.ncnn.param').read_text().splitlines()[:2] == ['7767517', f'{layers} {blobs}'],
            'selected graph inventory mismatch')
    core = {'variant': args.core_graph, 'layers': layers, 'blobs': blobs, 'param_sha256': digest}
    if args.core_graph != 'instancenorm':
        core['validator'] = pinned(args.validator)
        core['host_validation'] = validator_result(args.validator, args.template)
        if args.core_graph.endswith('-deconv2d'):
            require(core['host_validation']['variant'] == args.core_graph, 'selected validator graph differs')
    require(assets['weights']['bytes'] == 213042160 and assets['weights']['sha256'] ==
            '0f77cf77ff79a7724662f2e79a4d50284eb498abb2e519469a188e33f6c051ae', 'unexpected core weights')
    require(assets['tail']['bytes'] == 1936 and assets['tail']['sha256'] ==
            'e04b5c7b07c51b98b8883ba4feb753dbcb5bad9260fc93d009aa7fd577e80749', 'unexpected waveform tail')
    environment = {}
    for item in args.env:
        key, value = item.split('=', 1)
        require(key in ENV_NAMES and key not in environment, 'unexpected or duplicate environment key')
        allowed = {'RADISH_BATCH_CHUNKS': {'1', '2', '3', '4', '8'},
                   'RADISH_TILED_BATCH_CHUNKS': {'1', '2', '4', '8'},
                   'RADISH_TILED_GROUPS_X': {'1', '2'}, 'RADISH_SHARED_GROUPS': set('12345678')}
        require(value in allowed.get(key, {'0', '1'}), 'invalid environment value: ' + key)
        environment[key] = value
    require(set(environment) == ENV_NAMES, 'incomplete environment inventory')
    environment['VK_DRIVER_FILES'] = args.remote + '/radish_icd.json'
    # SIMPLEVK's line scanner must open our loader; the loader reads this private ICD.
    with args.icd.open('x') as stream:
        json.dump({'file_format_version': '1.0.0', 'ICD': {
            'library_path': args.remote + '/libvulkan_radish.so', 'api_version': '1.0.0'}}, stream)
        stream.write('\n')
    assets['icd'] = pinned(args.icd)
    original = frontend_inputs(args.source, args.models, args.binary, graphs)
    record = {'format': 'radish-kokoro-device-runtime-inputs-v1', 'frontend_inputs': original,
              'assets': assets, 'core_graph': core, 'environment': environment,
              'loader_environment': {'LD_LIBRARY_PATH': args.remote}, 'backend': args.backend,
              'threads': args.threads, 'ort_threads': args.ort_threads, 'max_frames': args.max_frames,
              'repeat': args.repeat, 'remote_directory': args.remote,
              'scope': 'Pinned single-process synthesis inputs; generation itself does not establish accuracy.'}
    record['implementation'] = {str(path.resolve()): asset(path) for path in
                                (args.wrapper, Path(__file__), args.wrapper.parent / 'check-gpu-log.py')}
    record['source_identity'] = {str(path.resolve()): asset(path) for path in
        (args.source_root / name for name in ('radish/tests/ncnn/kokoro_runtime.cpp',
         'radish/tests/ncnn/runtime_helpers.h', 'radish/tests/kokoro_frontend.cpp',
         'radish/tests/kokoro_waveform.cpp', 'radish/tests/kokoro_shapes.cpp',
         'radish/tests/ncnn/CMakeLists.txt')) if path.is_file()}
    record['source_identity_scope'] = 'Source files observed at launch; the actual executable hash identifies the binary. No source-to-binary attestation is inferred.'
    if args.request_loop:
        record['request_loop'] = True
        record['requests'] = []
        for source in [args.source] + args.next_source:
            require(source.name in ('kokoro-runtime-short', 'kokoro-runtime-full'), 'unsupported loop input directory')
            kind = source.name.removeprefix('kokoro-')
            record['requests'].append({'request_kind': kind,
                'remote_input_directory': args.remote + '/input-' + kind,
                'frontend_inputs': frontend_inputs(source, args.models, args.binary, graphs)})
    write_json(args.output, record)
    print(f'Pinned graphs, inputs, {args.core_graph} fixture and actual runtime assets; {len(request_records(record))} request(s).')


def recheck(record):
    for group in ('implementation', 'source_identity'):
        for path, expected in record.get(group, {}).items():
            require(asset(path) == expected, 'launch source changed: ' + path)
    for name, expected in record['assets'].items():
        require(asset(expected['path']) == {key: expected[key] for key in ('bytes', 'sha256')}, 'asset changed: ' + name)
    for request in request_records(record):
        original = request['frontend_inputs']
        for name, expected in original['inputs'].items():
            require(asset(Path(original['input_directory']) / name) == expected, 'input changed: ' + name)
        require(asset(Path(original['input_directory']) / 'runtime.json') == original['runtime_manifest'], 'input provenance changed')
        for name, expected in original['graphs'].items():
            require(asset(Path(original['model_directory']) / name) == expected, 'graph changed: ' + name)
    core = record['core_graph']
    if core['variant'] != 'instancenorm':
        validator = core['validator']
        require(asset(validator['path']) == {k: validator[k] for k in ('bytes', 'sha256')}, 'selected validator changed')
        require(validator_result(validator['path'], Path(record['assets']['param']['path']).parent) == core['host_validation'],
                'selected fixture validation changed')


def remote_assets(record, shared_weights):
    require(re.fullmatch(r'/var/tmp/radish/[A-Za-z0-9_./-]+', shared_weights) and '..' not in shared_weights,
            'unsafe shared weights path')
    remote = record['remote_directory']
    names = {'binary': 'radish-kokoro-runtime', 'driver': 'libvulkan_radish.so', 'loader': 'libvulkan.so.1',
             'param': 'decoder.ncnn.param', 'tail': 'kokoro-tail.bin', 'icd': 'radish_icd.json'}
    result = {remote + '/' + name: {k: record['assets'][key][k] for k in ('bytes', 'sha256')}
              for key, name in names.items()}
    result[shared_weights] = {k: record['assets']['weights'][k] for k in ('bytes', 'sha256')}
    for name, value in record['frontend_inputs']['graphs'].items():
        result[remote + '/models/' + name] = value
    for request in request_records(record):
        directory = request.get('remote_input_directory', remote + '/input')
        for name, value in request['frontend_inputs']['inputs'].items():
            path = directory + '/' + name
            require(path not in result or result[path] == value, 'conflicting repeated input asset')
            result[path] = value
    return result


def remote_script(record, shared_weights):
    lines = ['set -eu']
    for path in [*remote_assets(record, shared_weights), ORT]:
        quoted = shlex.quote(path)
        lines.append(f'test -f {quoted}')
        if path != ORT:
            lines.append(f'test ! -L {quoted}')
        lines.extend((f'row=$(sha256sum {quoted})', f'size=$(stat -Lc %s {quoted})',
                      f'printf "%s %s %s\\n" "${{row%% *}}" "$size" {quoted}'))
    return '\n'.join(lines)


def read_remote(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        digest, size, name = line.split(maxsplit=2)
        require(re.fullmatch('[0-9a-f]{64}', digest) and size.isdecimal() and name not in result, 'invalid remote hash inventory')
        result[name] = {'bytes': int(size), 'sha256': digest}
    return result


def verify_remote(record, shared_weights, path):
    observed = read_remote(path)
    require(set(observed) == set(remote_assets(record, shared_weights)) | {ORT}, 'remote asset inventory differs')
    for name, expected in remote_assets(record, shared_weights).items():
        require(observed[name] == expected, 'remote asset mismatch: ' + name)
    require(observed[ORT]['bytes'] > 0, 'empty installed ORT library')
    return observed


def floats(path, count):
    require(asset(path)['bytes'] == 4 * count, 'wrong float count: ' + str(path))
    values = array.array('f')
    values.frombytes(Path(path).read_bytes())
    if sys.byteorder != 'little':
        values.byteswap()
    require(all(math.isfinite(value) for value in values), 'nonfinite values: ' + str(path))
    return values


def finite_times(values):
    return all(type(value) in (int, float) and math.isfinite(value) and value >= 0 for value in values)


def check_request(stage, record, evidence, shared_graphs, shared_weights):
    require(stage.is_dir() and not stage.is_symlink(), 'invalid request directory')
    require({p.name for p in stage.iterdir()} == {'boundary', 'core.f32', 'waveform.f32', 'runtime.json'}, 'unexpected runtime output inventory')
    boundary = stage / 'boundary'
    require(boundary.is_dir() and not boundary.is_symlink(), 'invalid boundary directory')
    front, run = read_json(boundary / 'frontend.json'), read_json(stage / 'runtime.json')
    original, remote = record['frontend_inputs'], record['remote_directory']
    require(front['format'] == 'radish-kokoro-frontend-boundary-v1' and front['ort_api'] == 24
            and front['optimization'] == 'disabled', 'unexpected frontend metadata')
    require(front['ort_library'] == ORT and front['threads'] == record['ort_threads'], 'unexpected frontend runtime configuration')
    require(front['frontend_graph'] == remote + '/models/frontend-only.onnx'
            and front['source_graph'] == remote + '/models/harmonic-source.onnx'
            and front['input_directory'] == record.get('remote_input_directory', remote + '/input'),
            'unexpected frontend path provenance')
    t = front['T']
    require(type(t) is int and 1 <= t <= record['max_frames'], 'invalid frontend T')
    frames, samples, tokens = 120 * t + 1, 600 * t, original['inputs']['ids.i64']['bytes'] // 8
    require(front['frames'] == frames and front['samples'] == samples and front['sample_rate'] == 24000
            and front['tokens_with_pads'] == tokens, 'inconsistent frontend dimensions')
    sizes = {'ids.i64': tokens * 8, 'style.f32': 1024, 'speed.f32': 4, 'asr.f32': 512*t*4,
             'f0.f32': 2*t*4, 'n.f32': 2*t*4, 'har.f32': 22*frames*4, 'durations.f32': tokens*4}
    require({p.name for p in boundary.iterdir()} == set(sizes) | {'frontend.json'}, 'unexpected boundary inventory')
    raw = {}
    for name, size in sizes.items():
        raw[name] = asset(boundary / name)
        require(raw[name]['bytes'] == size, 'wrong raw length: ' + name)
        if name.endswith('.f32'):
            values = floats(boundary / name, size // 4)
            if name == 'durations.f32':
                require(all(v >= 1 and v == math.floor(v) for v in values) and sum(values) == t, 'invalid durations')
    for name, expected in original['inputs'].items():
        require(raw[name] == expected, 'captured input differs: ' + name)
    core = record['core_graph']
    require(run['format'] == 'radish-kokoro-runtime-v1' and run['backend'] == record['backend'], 'unexpected runtime schema/backend')
    require(run['core_graph'] == core['variant'], 'runtime core graph selection mismatch')
    require(run['T'] == t and run['frames'] == frames and run['samples'] == samples
            and run['sample_rate'] == 24000 and run['shape_blobs'] == core['blobs'], 'runtime shape mismatch')
    require(run['threads'] == record['threads'] and run['ort_threads'] == record['ort_threads']
            and run['repeat'] == record['repeat'] and run['ort_version'] == front['ort_version'], 'runtime configuration mismatch')
    require(run['environment'] == record['environment'], 'runtime environment mismatch')
    require(len(run['extraction_seconds']) == record['repeat'], 'repeat timing count mismatch')
    times = [run[key] for key in ('frontend_wall_seconds', 'shape_seconds', 'core_load_seconds', 'core_scope_seconds',
                                  'tail_seconds', 'synthesis_seconds')] + run['extraction_seconds']
    if 'request_index' in record:
        require(run['request_index'] == record['request_index'] and type(run['core_cache_hit']) is bool, 'missing resident request metadata')
        require(len(run['workspace_trim_seconds']) == 1, 'wrong trim timing count')
        times += run['workspace_trim_seconds']
    require(finite_times(times), 'invalid runtime timing')
    floats(stage / 'core.f32', 22 * frames)
    floats(stage / 'waveform.f32', samples)
    capture = {'format': 'radish-kokoro-device-frontend-v1', 'inputs': original, 'frontend': front, 'files': raw,
               'frontend_manifest': asset(boundary / 'frontend.json'), 'evidence': evidence,
               'device_checks': 'Both services active; no new GPU fault/recovery messages; memory/swap and stage timings recorded in evidence.',
               'scope': 'Fresh frontend/source from one private synthesis process. Its generated core/waveform require separate same-boundary accuracy validation.'}
    write_json(boundary / 'device-capture.json', capture)
    result = {'format': 'radish-kokoro-device-runtime-v1', 'inputs': record, 'runtime': run,
              'shared_device_weights': shared_weights, 'shared_device_graphs': shared_graphs,
              'files': {name: asset(stage / name) for name in ('core.f32', 'waveform.f32', 'runtime.json')},
              'boundary_capture_manifest': asset(boundary / 'device-capture.json'), 'evidence': evidence,
              'accuracy': 'Pending separate comparison against CPU references generated from this exact captured boundary. No accuracy gate ran during synthesis.'}
    write_json(stage / 'device-runtime.json', result)
    return run


def check_session(stage, plan, evidence, shared_graphs, shared_weights):
    records = request_records(plan)
    n = len(records)
    require(2 <= n <= 4 and plan['repeat'] == 1, 'invalid bounded session plan')
    session = read_json(stage / 'session.json')
    require(session['format'] == 'radish-kokoro-runtime-session-v1' and session['status'] == 'complete'
            and session['phase'] == 'complete', 'resident session did not complete')
    require(session['requested_requests'] == session['completed_requests'] == session['active_request_index'] == n,
            'incomplete resident request count')
    require({p.name for p in stage.iterdir()} == {'session.json'} | {f'request-{i:03d}' for i in range(n)}, 'unexpected session inventory')
    require(len(session['requests']) == n and finite_times([session['session_seconds'], session['final_core_cleanup_seconds']]),
            'invalid session requests/timings')
    require(isinstance(session['memory_samples'], list) and session['memory_samples'], 'missing phase memory evidence')
    previous_t, ort_version, requests = None, None, []
    for index, (record, item) in enumerate(zip(records, session['requests'])):
        name = f'request-{index:03d}'
        require(item['index'] == index and item['directory'] == name
                and item['input_directory'] == record['remote_input_directory'], 'request ordering/provenance differs')
        run = check_request(stage / name, record, evidence, shared_graphs, shared_weights)
        hit = previous_t == run['T']
        require(item['T'] == run['T'] and type(item['core_cache_hit']) is bool
                and item['core_cache_hit'] == run['core_cache_hit'] == hit, 'unexpected core cache pattern')
        require(item['core_load_seconds'] == run['core_load_seconds']
                and (run['core_load_seconds'] == 0 if hit else run['core_load_seconds'] > 0), 'cache/load timing mismatch')
        require(item['synthesis_seconds'] == run['synthesis_seconds']
                and finite_times([item['through_publication_seconds'], item['previous_core_eviction_seconds']]),
                'request/session timing mismatch')
        require(ort_version is None or ort_version == run['ort_version'], 'ORT version changed between requests')
        previous_t, ort_version = run['T'], run['ort_version']
        requests.append({'index': index, 'directory': name, 'T': run['T'], 'core_cache_hit': hit,
                         'device_manifest': asset(stage / name / 'device-runtime.json')})
    return {'format': 'radish-kokoro-device-runtime-session-v1', 'status': 'complete-accuracy-pending',
            'session': session, 'session_manifest': asset(stage / 'session.json'), 'requests': requests,
            'evidence': evidence, 'accuracy': 'Every request requires unchanged same-boundary core/waveform validation; synthesis ran no reference/calibration.'}


def publish(stage, output):
    rename = ctypes.CDLL(None, use_errno=True).renameat2
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(stage), -100, os.fsencode(output), 1) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(output))


def finalize(args):
    record = read_json(args.record)
    recheck(record)
    shared_graphs = read_json(args.shared_graphs)
    require(set(shared_graphs) == set(record['frontend_inputs']['graphs']), 'unexpected reused graph inventory')
    for name, entry in shared_graphs.items():
        require({key: entry[key] for key in ('bytes', 'sha256')} == record['frontend_inputs']['graphs'][name]
                and entry['remote_path'] == record['remote_directory'] + '/models/' + name
                and entry['method'] in ('hardlink', 'upload'), 'remote graph provenance differs: ' + name)
    before = verify_remote(record, args.shared_weights, args.remote_before)
    require(verify_remote(record, args.shared_weights, args.remote_after) == before, 'remote assets/ORT changed during run')
    require(args.stage.is_dir() and not args.stage.is_symlink()
            and not any(path.is_symlink() for path in args.stage.rglob('*')), 'invalid pulled directory/symlink')
    hashes = {}
    prefix = record['remote_directory'] + '/output/'
    for line in args.output_hashes.read_text().splitlines():
        digest, path = line.split(maxsplit=1)
        require(re.fullmatch('[0-9a-f]{64}', digest) and path.startswith(prefix), 'invalid remote output hash')
        name = path[len(prefix):]
        require(name not in hashes and '..' not in Path(name).parts, 'duplicate/unsafe output path')
        hashes[name] = digest
    require({str(path.relative_to(args.stage)): asset(path)['sha256'] for path in args.stage.rglob('*') if path.is_file()} == hashes,
            'pulled outputs differ from final remote hashes')
    evidence_paths = [args.record, args.shared_graphs, args.remote_before, args.remote_after, args.output_hashes] + args.evidence
    evidence = {str(path.resolve()): asset(path) for path in evidence_paths}
    if record.get('request_loop', False):
        result = check_session(args.stage, record, evidence, shared_graphs, args.shared_weights)
        write_json(args.stage / 'device-session.json', result)
    else:
        check_request(args.stage, record, evidence, shared_graphs, args.shared_weights)
    recheck(record)
    publish(args.stage, args.output)
    print(f'Published unreferenced {record["backend"]} synthesis ({len(request_records(record))} request(s)): {args.output}')
    print('Accuracy remains pending; every boundary/ retains the unchanged importer/validator contract.')


def disk_needed(args):
    record = read_json(args.record)
    graphs = read_json(args.shared_graphs)
    payload = sum(value['bytes'] for name, value in record['assets'].items() if name != 'weights')
    payload += sum(value['bytes'] for value in graphs.values() if value['method'] == 'upload')
    seen = set()
    for request in request_records(record):
        original = request['frontend_inputs']
        if original['input_directory'] not in seen:
            payload += sum(value['bytes'] for value in original['inputs'].values())
            seen.add(original['input_directory'])
    if args.upload_weights:
        payload += record['assets']['weights']['bytes']
    t = record['max_frames']
    f = 120*t + 1
    output = 4*(512*t + 4*t + 256 + 1 + 22*f + 512 + 22*f + 600*t) + 8*512 + 1024*1024
    print(max(256*1024, (payload + output*len(request_records(record)) + 64*1024*1024 + 1023)//1024))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('prepare')
    for name in ('source', 'models', 'template', 'binary', 'driver', 'loader', 'tail', 'output', 'icd', 'validator', 'wrapper', 'source-root'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--next-source', type=Path, action='append', default=[])
    p.add_argument('--request-loop', action='store_true')
    p.add_argument('--remote', required=True)
    p.add_argument('--backend', choices=['vulkan', 'cpu-packed'], required=True)
    p.add_argument('--core-graph', choices=CORE_GRAPHS, required=True)
    for name in ('threads', 'ort-threads', 'max-frames', 'repeat'):
        p.add_argument('--' + name, type=int, required=True)
    p.add_argument('--env', action='append', default=[])
    for name in ('recheck', 'remote-script', 'verify-remote', 'disk-needed', 'finalize'):
        q = commands.add_parser(name)
        q.add_argument('--record', type=Path, required=True)
        if name in ('remote-script', 'verify-remote', 'finalize'):
            q.add_argument('--shared-weights', required=True)
        if name == 'verify-remote':
            q.add_argument('--hashes', type=Path, required=True)
        if name in ('disk-needed', 'finalize'):
            q.add_argument('--shared-graphs', type=Path, required=True)
        if name == 'disk-needed':
            q.add_argument('--upload-weights', action='store_true')
        if name == 'finalize':
            for field in ('stage', 'output', 'remote-before', 'remote-after', 'output-hashes'):
                q.add_argument('--' + field, type=Path, required=True)
            q.add_argument('--evidence', type=Path, action='append', default=[])
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args)
    elif args.command == 'recheck':
        recheck(read_json(args.record))
    elif args.command == 'remote-script':
        print(remote_script(read_json(args.record), args.shared_weights))
    elif args.command == 'verify-remote':
        verify_remote(read_json(args.record), args.shared_weights, args.hashes)
    elif args.command == 'disk-needed':
        disk_needed(args)
    else:
        finalize(args)


if __name__ == '__main__':
    main()
