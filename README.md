# TCS Viewer

A low-latency Windows game viewer for LEGO Star Wars: The Complete Saga.
Toggle capture onto a second monitor with a global hotkey, including while TCS
uses exclusive fullscreen. Choose Fullscreen, Borderless window, or Windowed.
Capture and rendering stop when the viewer is toggled off.

Experimental release: **0.6.0-alpha.2**.

[Download the Windows build](https://github.com/FlamingLazer/TCSViewer/releases/tag/v0.6.0-alpha.2).
Extract the ZIP before launching. This repository is private, so download access
requires permission to the repository.

## Requirements

- Windows 10/11, 64-bit, a Direct3D 11 capable GPU, and two monitors.
- OBS Studio **32.0.x, 32.1.x, 32.2.x, or 33.0.x** (stable releases).
- LEGO Star Wars: The Complete Saga on PC.

OBS is located automatically, and its actual version selects the capture module.
No OBS version or installation path needs to be entered. Discovery checks running
OBS, nearby portable copies, the Windows installation registry, standard install
locations, and OBS-named folders in Documents/Downloads. For a portable copy in an
unusual location, start OBS before toggling capture so the viewer can locate it.
If several copies exist, the first location in this order wins. The selected path
and loaded version appear in `viewer.log`. `obs_dir` remains an optional advanced
or QA override; leave it blank for automatic discovery.

Representatives **32.0.4, 32.1.2, 32.2.2, and 33.0.1** passed automated checks.
Other stable patches in those minor series are accepted but individually unverified.
Betas, release candidates and newer minor series are rejected.
Simultaneous capture of the same game by OBS and this viewer remains unvalidated.

The prebuilt executable does not need Python, C++ build tools, or NVENC.
Its C++ runtime is statically linked. Use the OBS installer if OBS itself reports
missing runtime dependencies. No OBS binaries are bundled.

## Use

1. Extract the entire release ZIP into a writable folder.
2. Open `TCSViewer.exe` and accept the normal Windows administrator prompt.
3. Select the game executable, second monitor, display mode, and shortcut; save settings.
4. Focus TCS and use the shortcut to start or stop the viewer.

The public default shortcut is **Ctrl+Alt+F10**. Settings are stored beside the
executable in `viewer.ini`. When updating, keep your existing `viewer.ini` to
preserve your shortcut and display settings. The viewer does not change the
game's refresh rate. The output monitor should retain its normal refresh rate.

Windowed has a title bar you can drag; size comes from the control panel presets.
Borderless window uses position presets. Closing an output window stops capture.
Saving settings also stops capture; toggle again to use the new settings.

Version 0.6 requests administrator access automatically, as elevated execution
improved consistency on the tested setup. This is not a guarantee against stutter.
The background shortcut remains available while the game is focused.

## GPU support and limits

The renderer uses Direct3D 11 and shared GPU textures, with no NVIDIA-only encoding
or SDK dependency. Testing has been on an NVIDIA RTX 4060 Ti; AMD and Intel GPUs
are intended to work but have not been verified. Hybrid laptops and multi-GPU
systems are untested. Capture currently selects adapter 0 and the presenter uses
that same adapter; there is no GPU selector or cross-adapter transfer path.

The capture canvas is fixed at 1920x1080. Sampling defaults to 120 Hz, configurable
from 30 to 240 through `capture_fps`; this does not increase unique game FPS.
There is no frame interpolation, recording, audio, or encoding. Latency and pacing
depend on the game, driver, Windows composition, GPU load, and output monitor.

## Troubleshooting

- If waiting for capture, check the executable name, OBS path/version, and that TCS is running.
- For an undiscovered portable OBS location, start OBS and toggle the viewer again.
- For a report, preserve `viewer.log` and `timing.csv` before reopening the app;
  they are overwritten on launch. Review logs before sharing because they can contain local paths.
- Include GPU, Windows/OBS versions, display connections, monitor refresh rates,
  and the sequence that caused a problem. Automated tests do not establish compatibility
  with every driver or actual physical refresh-rate transition.

## Build and test

Install Python 3.11 or newer, Visual Studio Build Tools with **Desktop development with C++**,
and a Windows SDK, then run:

```powershell
python build.py
```

Output is `dist/TCSViewer.exe`, with an embedded `requireAdministrator` manifest.
The build locates MSVC and the SDK automatically. Matching OBS 32.1.2 headers
are included; if missing, the script fetches the official source tag.
The one local header adjustment empties the MSVC `EXPORT` macro in
`libobs/util/c99defs.h`, because functions are resolved dynamically at runtime.

For development tests without UAC, build the separately named QA executable:

```powershell
python build.py --test-build
python test_capture.py --d3d11 --test-build
python test_capture.py --d3d11 --test-build --framed-test
```

Do not distribute `TCSViewerTest.exe` as the production application. These tests
use a separate fixture, temporarily show windows, and do not control the real game.
The framed test checks repeated start/stop, color, movement, resized backbuffers,
and opaque margins. `--x64` selects a 64-bit fixture; `--windowed-test` checks borderless mode.
Standalone hotkey and layout checks are in `src/hotkey_test.cpp` and `src/layout_test.cpp`.

## Architecture

Installed libobs and its win-capture module hook the game. The viewer renders into
an sRGB-capable GPU target and copies to a single shared keyed-mutex mailbox.
A separate Direct3D 11 presenter reads the latest available image and uses a
flip-discard swap chain with maximum frame latency one. Busy mailbox updates
are skipped rather than queued. Production rendering performs no CPU pixel readback.
Display-size changes recreate the backbuffer on the presentation thread, with
opaque black letterboxing when needed.

## License

Viewer source is GPL-2.0-or-later; see `LICENSE.txt`. OBS headers are from
[OBS Studio 32.1.2](https://github.com/obsproject/obs-studio/tree/32.1.2), retain
their original notices, and are covered by their upstream licenses. Complete
viewer source and matching headers accompany the downloadable build.
This is an unofficial project, not affiliated with LEGO, Lucasfilm, or OBS Studio.

## Repeatable OBS compatibility checks

```powershell
python setup_compatibility.py
python test_compatibility.py
```

Setup downloads official portable OBS 32.0.4 and 32.2.2 archives and verifies their
release-asset SHA256 digests. The runner reuses installed 32.1.2 and portable 33.0.1;
use `--runtime "VERSION=PATH"` to override a location, or `--only 32.0.4 32.2.2`
to select representatives. It checks version-policy boundaries and runs GPU,
32-bit borderless capture and 64-bit framed capture checks in isolated folders.
See [COMPATIBILITY.md](COMPATIBILITY.md) for results and limits.
