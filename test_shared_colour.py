"""Regress typed BGRA/BGRX shared-texture gamma with a controlled producer."""
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
    parser.add_argument('--levels', type=int, nargs='+', default=[32, 128, 224])
    parser.add_argument('--format', choices=['bgra', 'bgrx', 'both'], default='both')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    if any(not 1 <= value <= 254 for value in args.levels):
        parser.error('Test levels must be between 1 and 254')
    if not args.skip_build:
        subprocess.run([sys.executable, str(ROOT/'build.py'), '--test-build'], check=True)
    run = ROOT/'qa-results'/('colour-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
    run.mkdir(parents=True)
    subprocess.run([sys.executable, str(ROOT/'test_capture.py'), '--build-only', '--x64',
                    '--viewer-dir', str(run)], check=True)
    results = []
    for format in (['bgra', 'bgrx'] if args.format == 'both' else [args.format]):
        for level in args.levels:
            folder = run/(format+'-'+str(level))
            folder.mkdir()
            shutil.copy2(ROOT/'dist/TCSViewerTest.exe', folder/'TCSViewerTest.exe')
            (folder/'viewer.ini').write_text('[viewer]\nobs_dir='+str(args.obs_dir.resolve())+'\n')
            fixture = subprocess.Popen([str(run/'TCSCaptureFixture.exe'), '--d3d11', '--typed-share',
                                        '--gray-level='+str(level)] + (['--bgrx'] if format=='bgrx' else []))
            try:
                time.sleep(.5)
                result = subprocess.run([str(folder/'TCSViewerTest.exe'), '--capture-test', '--passive-test',
                                         '--expect-gray='+str(level), '--framed-test'], timeout=40)
                log = (folder/'viewer.log').read_text(errors='replace')
                passed = (result.returncode==0 and 'SELFTEST PASS' in log and
                          log.count('SELFTEST GPU pixel check passed')==3 and
                          log.count('using sRGB-capable sampling copy for typed texture')==3)
                results.append(dict(format=format, level=level, exit=result.returncode, passed=passed))
                (run/'results.json').write_text(json.dumps(results, indent=2))
                print(results[-1], flush=True)
            finally:
                if fixture.poll() is None:
                    fixture.terminate()
                    fixture.wait(timeout=5)
    print('Logs:',run,flush=True)
    return 0 if all(row['passed'] for row in results) else 1


if __name__=='__main__':
    sys.exit(main())
