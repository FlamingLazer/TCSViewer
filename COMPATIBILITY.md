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

## Concurrent OBS capture fix

The viewer no longer opens a competing Game Capture source when OBS is running.
It reads the existing game's hook-info/data mappings with FILE_MAP_READ, opens the
shared GPU texture, and renders it into the existing viewer pipeline. It never
injects a hook, changes capture settings, signals capture events or holds the
hook's keepalive mutex in reader mode. The compatibility shared-memory path uses
the existing frame mutexes with zero-timeout acquisition for a bounded upload.

If OBS is opened while standalone capture is active, the viewer releases its
capture source and switches to the reader. OBS must have a visible Game Capture
source for the game. While waiting, the control panel explains that requirement.
The reader reconnects when capture is restarted or resized. Closing the viewer
releases only its reader resources. Closing OBS returns the viewer to standalone
capture. Changing modes can briefly wait for OBS's hook retry.

Concurrent libobs owner/reader tests passed on 32.0.4, 32.1.2, 32.2.2 and 33.0.1.
Each client completed three cycles with verified grey pixels; the reader also
passed framed-window resize and opaque-edge checks. Reader logs confirm that it
never loaded win-capture or created a Game Capture source. The 33.0.1 compatibility
(shared-memory) path also passed, and the 64-bit 33.0.1 test starts the reader
before its owner to check delayed attachment.

The pixel test now waits briefly for an initially unfilled capture buffer, and
requires a verified nonblack/grey pixel before accepting a test cycle. This avoids
treating asynchronous capture startup as a rendered test frame.

Run `python test_shared_capture.py --obs-dir <OBS-root>` for concurrent tests.
Add `--memory`, `--x64`, or `--delayed-owner` for the additional cases.
The optional `test_obs_recording.py` uses an isolated portable OBS configuration,
a fixture and a short recording; it requires websocket-client and ffprobe/ffmpeg.
No real gameplay or existing OBS configuration is driven by these tests.

Actual OBS Studio 33.0.1 recording was tested with a separate portable configuration
and a 64-bit D3D11 fixture. The reader attached automatically to OBS's existing
capture and completed three start/stop cycles, including framed resize checks.
OBS was still recording after the viewer finished. The finalized MKV contained
742 decoded frames at 640x360 over 24.833 seconds; centre samples during and after
the viewer cycles were 127,127,127. No existing OBS scenes/profiles were modified.
The full 12-check standalone matrix passed again across all four representatives.
Actual TCS gameplay and long-running recording still require a user comparison.
