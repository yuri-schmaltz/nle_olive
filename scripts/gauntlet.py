#!/usr/bin/env python3
"""Build/test/install gate. Stops on failure; never labels a skipped gate passed."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preset', choices=['linux-release', 'linux-debug', 'linux-asan', 'linux-tsan'], default='linux-release')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--gpu', action='store_true', help='Require a working display/OpenGL, fail if unavailable')
    parser.add_argument('--fresh', action='store_true', help='Use a new empty build directory, preserve existing builds')
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--soak-seconds', type=int, default=0)
    parser.add_argument('--install', action='store_true', help='Stage installation and check installed binary')
    args = parser.parse_args()
    if args.jobs < 1 or args.repeat < 1 or args.soak_seconds < 0:
        parser.error('jobs/repeat must be positive and soak-seconds nonnegative')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    build = ROOT / (f'build-gauntlet-{stamp}' if args.fresh else f'build-{args.preset}')
    output = ROOT / 'qa-results' / stamp
    output.mkdir(parents=True)
    report = {'started_utc': stamp, 'preset': args.preset, 'build': str(build),
              'platform': platform.platform(), 'gpu_requested': args.gpu,
              'steps': [], 'status': 'running'}
    env = os.environ.copy()
    # Isolate user configuration and caches without changing HOME.
    for name in ('XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'XDG_DATA_HOME'):
        path = output / name.lower()
        path.mkdir()
        env[name] = str(path)

    def save():
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(label, argv, timeout=3600, capture=False):
        print(f'[{label}] {" ".join(map(str, argv))}', flush=True)
        log = output / f'{label}.log'
        start = time.monotonic()
        with log.open('wb') as stream:
            try:
                result = subprocess.run(list(map(str, argv)), cwd=ROOT, env=env,
                                        stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
                code = result.returncode
            except subprocess.TimeoutExpired:
                code = 124
        report['steps'].append({'name': label, 'command': list(map(str, argv)),
                                'exit_code': code, 'seconds': round(time.monotonic()-start, 3),
                                'log': log.name})
        save()
        if code:
            print(log.read_text(errors='replace')[-12000:], file=sys.stderr)
            raise RuntimeError(f'{label} failed ({code}); see {log}')
        return log.read_text() if capture else None

    save()
    try:
        run('git-head', ['git', 'rev-parse', 'HEAD'])
        run('git-status', ['git', 'status', '--short'])
        run('submodules', ['git', 'submodule', 'status'])
        run('diff', ['git', 'diff', '--binary'])
        run('cmake-version', ['cmake', '--version'])
        run('configure', ['cmake', '--preset', args.preset, '-B', build,
                         f'-DBUILD_GPU_TESTS={"ON" if args.gpu else "OFF"}'])
        run('build', ['cmake', '--build', build, '-j', args.jobs])
        inventory = json.loads(run('test-inventory', ['ctest', '--test-dir', build,
                                                    '--show-only=json-v1'], capture=True))
        report['test_executables'] = len(inventory.get('tests', []))
        if not report['test_executables']:
            raise RuntimeError('No tests registered')
        start = time.monotonic()
        iteration = 0
        while iteration < args.repeat or time.monotonic() - start < args.soak_seconds:
            iteration += 1
            run(f'test-{iteration:04}', ['ctest', '--test-dir', build, '--output-on-failure',
                                        '--no-tests=error', '--timeout', '120',
                                        '--output-junit', output / f'junit-{iteration:04}.xml'])
        report['iterations'] = iteration
        report['test_seconds'] = round(time.monotonic()-start, 3)
        if args.install:
            stage = output / 'install'
            run('install', ['cmake', '--install', build / 'app', '--prefix', stage])
            binary = stage / 'bin' / 'olive-editor'
            run('installed-version', [binary, '--version'], timeout=30)
            report['binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
        report['status'] = 'passed'
    except (OSError, RuntimeError, ValueError) as error:
        report['status'] = 'failed'
        report['error'] = str(error)
        print(error, file=sys.stderr)
    finally:
        save()
        print(f'Report: {output / "report.json"}', flush=True)
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    sys.exit(main())
