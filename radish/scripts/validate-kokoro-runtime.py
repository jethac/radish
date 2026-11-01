#!/usr/bin/env python3
"""Compare a complete runtime capture with CPU references from its exact boundary."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np


def require(condition, message):
    if not condition:
        raise ValueError(message)


def asset(path):
    require(path.is_file() and not path.is_symlink(), 'invalid capture file: '+str(path))
    data = path.read_bytes()
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def compare(actual_path, reference_path, count, atol, rtol, minimum_snr=None):
    require(actual_path.stat().st_size == reference_path.stat().st_size == 4*count,
            'incorrect full output length')
    actual = np.fromfile(actual_path, dtype='<f4').astype(np.float64)
    reference = np.fromfile(reference_path, dtype='<f4').astype(np.float64)
    require(np.isfinite(actual).all() and np.isfinite(reference).all(), 'nonfinite output/reference')
    error = actual-reference
    signal_power = float(reference @ reference)
    error_power = float(error @ error)
    require(signal_power > 0, 'zero reference signal power')
    mismatches = int(np.count_nonzero(np.abs(error) > atol+rtol*np.abs(reference)))
    snr = float(10*np.log10(signal_power/max(error_power, 1e-30)))
    return {'values': count, 'actual': asset(actual_path), 'reference': asset(reference_path),
            'max_abs_error': float(np.max(np.abs(error))),
            'relative_rms': float(np.sqrt(error_power/signal_power)), 'snr_db': snr,
            'atol': atol, 'rtol': rtol, 'minimum_snr_db': minimum_snr,
            'mismatches': mismatches, 'passed': mismatches == 0 and (minimum_snr is None or snr >= minimum_snr)}


def validate(capture, reference, report_path):
    require(not report_path.exists() and not report_path.is_symlink(), 'report must be a new file')
    run = json.loads((capture/'runtime.json').read_text())
    device = json.loads((capture/'device-runtime.json').read_text())
    ref = json.loads((reference/'runtime.json').read_text())
    require(run['format'] == 'radish-kokoro-runtime-v1', 'unexpected runtime format')
    require(device['format'] == 'radish-kokoro-device-runtime-v1' and device['runtime'] == run,
            'runtime differs from its device capture')
    require(set(device['files']) == {'core.f32', 'waveform.f32', 'runtime.json'},
            'unexpected frozen output inventory')
    for name, expected in device['files'].items():
        require(asset(capture/name) == expected, 'generated output changed: '+name)
    require(asset(capture/'boundary'/'device-capture.json') == device['boundary_capture_manifest'],
            'boundary capture changed after synthesis')
    for name, expected in device['evidence'].items():
        require(asset(Path(name)) == expected, 'device evidence changed: '+name)
    require(ref['format'] == 'radish-kokoro-runtime-fixture-v1'
            and ref['source_sha256'] == 'b13da99ce02f03fb6b605720c46d09b746063ddb651bfaf5156a770204ffc4e9'
            and ref['runtime']['source_crosscheck'] == 'device-boundary-preserved', 'reference is not from a preserved device boundary')
    require(all(run[key] == ref[key] for key in ('T', 'frames', 'samples', 'sample_rate')), 'runtime/reference dimensions differ')
    require(type(run['T']) is int and 1 <= run['T'] <= 4096
            and run['frames'] == 120*run['T']+1 and run['samples'] == 600*run['T']
            and run['sample_rate'] == 24000, 'invalid runtime dimensions')
    require(asset(capture/'boundary'/'device-capture.json') == ref['device_capture']['manifest']
            and asset(capture/'boundary'/'frontend.json') == ref['device_capture']['frontend_manifest'],
            'reference came from a different frontend capture')
    names = ('ids.i64', 'style.f32', 'speed.f32', 'asr.f32', 'f0.f32', 'n.f32', 'har.f32', 'durations.f32')
    for name in names:
        require(asset(capture/'boundary'/name) == asset(reference/name)
                == ref['files'][name] == ref['device_capture']['raw_files'][name], 'boundary changed: '+name)
    for name in ('reference.f32', 'reference-waveform.f32'):
        require(asset(reference/name) == ref['files'][name], 'reference changed: '+name)
    core = compare(capture/'core.f32', reference/'reference.f32', 22*run['frames'], 0.002, 0.002)
    waveform = compare(capture/'waveform.f32', reference/'reference-waveform.f32', run['samples'], 2e-4, 2e-3, 50.0)
    report = {'format': 'radish-kokoro-runtime-validation-v1',
              'capture': str(capture.resolve()), 'reference_directory': str(reference.resolve()),
              'device_manifest': asset(capture/'device-runtime.json'),
              'runtime_manifest': asset(capture/'runtime.json'), 'reference_manifest': asset(reference/'runtime.json'),
              'boundary_identity': 'all eight raw files byte-identical to the reference inputs',
              'core': core, 'waveform': waveform, 'passed': core['passed'] and waveform['passed'],
              'scope': 'Complete generated core and waveform compared to original ORT decoder/tail on the exact tablet frontend/source boundary. Frontend/source are not recomputed. Timing remains in the device runtime and cgroup evidence.'}
    with report_path.open('x') as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps(report, indent=2))
    require(report['passed'], 'full runtime accuracy gate failed')
    print('PASS: complete runtime core and waveform, unchanged accuracy gates')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('reference', type=Path, help='output of import-kokoro-device-boundary.py')
    parser.add_argument('report', type=Path, help='new validation JSON file')
    args = parser.parse_args()
    try:
        validate(args.capture, args.reference, args.report)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
