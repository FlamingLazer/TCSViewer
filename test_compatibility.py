"""Run sequential, isolated GPU/capture checks for one OBS patch per minor series."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent
REPRESENTATIVES = ("32.0.4", "32.1.2", "32.2.2", "33.0.1")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", nargs="+", choices=REPRESENTATIVES)
    parser.add_argument("--runtime", action="append", default=[], metavar="VERSION=PATH",
                        help="Override a representative's installation root")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    runtimes = {v: ROOT / "qa-runtimes" / v for v in REPRESENTATIVES}
    runtimes["32.1.2"] = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "obs-studio"
    runtimes["33.0.1"] = Path.home() / "Documents" / "OBS V33.0.1"
    for override in args.runtime:
        version, separator, path = override.partition("=")
        if not separator or version not in runtimes:
            parser.error("--runtime must be a representative VERSION=PATH")
        runtimes[version] = Path(path).resolve()
    versions = args.only or REPRESENTATIVES
    for version in versions:
        if not (runtimes[version] / "bin/64bit/obs.dll").is_file():
            parser.error(f"Missing OBS {version}: {runtimes[version]}. Run setup_compatibility.py or use --runtime.")
    subprocess.run([sys.executable, str(ROOT / "build.py"), "--policy-test"], check=True)
    if not args.skip_build:
        subprocess.run([sys.executable, str(ROOT / "build.py"), "--test-build"], check=True)
    viewer = ROOT / "dist/TCSViewerTest.exe"
    digest = hashlib.sha256(viewer.read_bytes()).hexdigest()
    # A separate run folder preserves historical logs and avoids overwriting user settings.
    run = ROOT / "qa-results" / ("matrix-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ"))
    run.mkdir(parents=True)
    results = []
    tests = [("gpu", ["--self-test"]),
             ("x86-borderless", ["--windowed-test"]),
             ("x64-framed", ["--x64", "--framed-test"])]
    for version in versions:
        folder = run / version
        folder.mkdir()
        shutil.copy2(viewer, folder / viewer.name)
        (folder / "viewer.ini").write_text("[viewer]\nobs_dir=" + str(runtimes[version]) + "\n", encoding="utf-8")
        for name, flags in tests:
            command = ([str(folder / viewer.name)] + flags if name == "gpu" else
                       [sys.executable, str(ROOT / "test_capture.py"), "--test-build", "--d3d11",
                        "--viewer-dir", str(folder)] + flags)
            log = folder / "viewer.log"
            if log.exists():
                log.unlink()
            error = None
            try:
                with (folder / (name + "-console.txt")).open("w") as console:
                    process = subprocess.run(command, timeout=50, stdout=console, stderr=subprocess.STDOUT)
                code = process.returncode
            except subprocess.TimeoutExpired:
                code, error = -1, "Timed out"
            content = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
            if log.exists():
                shutil.copy2(log, folder / (name + ".log"))
            passed = (code == 0 and f"Capture engine ready: {version}\n" in content and
                      "SELFTEST PASS: three start/stop cycles completed" in content)
            if name != "gpu":
                passed = passed and content.count("SELFTEST GPU pixel check passed") >= 3
            record = dict(runtime=version, test=name, passed=passed, exit=code, error=error,
                          obs_dir=str(runtimes[version]), executable_sha256=digest)
            results.append(record)
            (run / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
            print(version, name, "PASS" if passed else "FAIL", flush=True)
    lines = ["# OBS representative compatibility results", "", "| Runtime | Check | Result |", "|---|---|---|"]
    lines += [f"| {r['runtime']} | {r['test']} | {'PASS' if r['passed'] else 'FAIL'} |" for r in results]
    lines += ["", "Viewer SHA256: `" + digest + "`", "",
              "One patch per minor series was tested. Other accepted patches are unverified.",
              "Tests use a non-elevated QA build and a D3D11 fixture; actual gameplay,",
              "D3D9, physical refresh-rate switching and long-duration stability remain untested."]
    (run / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("Results:", run, flush=True)
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
