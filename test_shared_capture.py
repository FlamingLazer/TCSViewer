"""Verify two simultaneous libobs clients without touching real OBS scenes or gameplay."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--obs-dir', type=Path, required=True)
    parser.add_argument('--memory', action='store_true', help='Exercise OBS compatibility shared-memory capture')
    parser.add_argument('--x64', action='store_true')
    parser.add_argument('--delayed-owner', action='store_true')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    runtime = args.obs_dir.resolve()
    if not (runtime / 'bin/64bit/obs.dll').exists():
        parser.error('OBS runtime not found')
    if not args.skip_build:
        subprocess.run([sys.executable, str(ROOT / 'build.py'), '--test-build'], check=True)
    run = ROOT / 'qa-results' / ('shared-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
    run.mkdir(parents=True)
    for role in ['owner', 'reader']:
        folder = run / role
        folder.mkdir()
        shutil.copy2(ROOT / 'dist/TCSViewerTest.exe', folder / 'TCSViewerTest.exe')
        (folder / 'viewer.ini').write_text('[viewer]\nobs_dir=' + str(runtime) + '\n', encoding='utf-8')
    subprocess.run([sys.executable, str(ROOT / 'test_capture.py'), '--build-only', '--viewer-dir',
                    str(run / 'owner')] + (['--x64'] if args.x64 else []), check=True)
    processes = []

    def start(role):
        flags = ['--capture-test', '--expect-gray']
        if role == 'reader':
            flags += ['--passive-test', '--framed-test']
        elif args.memory:
            flags += ['--memory-test']
        process = subprocess.Popen([str(run / role / 'TCSViewerTest.exe')] + flags)
        processes.append(process)
        return process

    try:
        fixture = subprocess.Popen([str(run / 'owner/TCSCaptureFixture.exe'), '--d3d11', '--gray'])
        processes.append(fixture)
        time.sleep(0.5)
        if args.delayed_owner:
            reader = start('reader')
            time.sleep(2)
            owner = start('owner')
        else:
            owner = start('owner')
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                log = run / 'owner/viewer.log'
                if log.exists() and 'SELFTEST GPU pixel check passed' in log.read_text(errors='replace'):
                    break
                if owner.poll() is not None:
                    raise RuntimeError('Owner failed before reader started')
                time.sleep(0.1)
            else:
                raise RuntimeError('Owner capture did not become ready')
            reader = start('reader')
        exits = {'reader': reader.wait(timeout=45), 'owner': owner.wait(timeout=45)}
        logs = {role: (run / role / 'viewer.log').read_text(errors='replace') for role in exits}
        checks = {role: code == 0 and 'SELFTEST PASS: three start/stop cycles completed' in logs[role]
                  and logs[role].count('SELFTEST GPU pixel check passed') >= 3 for role, code in exits.items()}
        checks['read_only'] = ('Capture mode: OBS shared reader' in logs['reader']
                               and 'OBS shared capture attached (read-only)' in logs['reader']
                               and 'Game Capture module:' not in logs['reader']
                               and "[game-capture: 'TCS Viewer Capture']" not in logs['reader'])
        checks['owner_continued'] = ('attempting to hook process:' in logs['owner']
                                     and logs['owner'].count('Self-test cycle frames published=') == 3)
        results = dict(runtime=str(runtime), memory=args.memory, x64=args.x64,
                       delayed_owner=args.delayed_owner, exits=exits, checks=checks,
                       passed=all(checks.values()))
        (run / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
        print(json.dumps(results, indent=2), flush=True)
        print('Logs:', run, flush=True)
        return 0 if results['passed'] else 1
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)


if __name__ == '__main__':
    sys.exit(main())
