# Compatibility verification ? v0.6.0-alpha.2

Windows local checks completed on 2026-11-16.

| Minor series | Representative | GPU | 32-bit D3D11 borderless | 64-bit D3D11 framed |
|---|---|---|---|---|
| 32.0.x | 32.0.4 | PASS | PASS | PASS |
| 32.1.x | 32.1.2 | PASS | PASS | PASS |
| 32.2.x | 32.2.2 | PASS | PASS | PASS |
| 33.0.x | 33.0.1 | PASS | PASS | PASS |

All 12 representative checks passed before the discovery update. Each runs three
start/stop cycles. Capture checks exercise the actual OBS hook, GPU pixel colour,
sufficient captured/presented frames and window geometry. Framed checks also cover
caption hit testing, non-activating movement, resize recovery, matching surface
dimensions and opaque edge pixels. The runner checks actual loaded versions.

After automatic discovery was added, no-INI GPU checks passed with installed
32.1.2 and nearby portable 32.2.2. Automatic portable 33.0.1 discovery also passed
64-bit framed hook capture and resize checks. Version-policy boundary tests pass.
The release-label change only changes the startup log text.

Other stable patches in these minor series are accepted, not individually tested.
Tests use a non-elevated QA executable and a D3D11 fixture. Actual TCS gameplay,
D3D9, physical refresh-rate switching, elevated performance and long-duration
stability remain untested by these checks. GPU testing was on an RTX 4060 Ti.

Run `python setup_compatibility.py` then `python test_compatibility.py` to repeat.
Raw logs remain local because they include machine-specific paths and identifiers.
No OBS binaries are included in the release package.
