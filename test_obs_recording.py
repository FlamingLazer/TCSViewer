"""Smoke-test actual portable OBS recording in an isolated configuration.

Requires websocket-client and ffprobe. Never loads the user's scenes or profile.
"""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import os
import secrets
import shutil
import subprocess
import sys
import time
import uuid
import websocket

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--obs-dir', type=Path, required=True)
    args = parser.parse_args()
    source = args.obs_dir.resolve()
    run = ROOT / 'qa-results' / ('recording-' + uuid.uuid4().hex[:12])
    isolated = run / 'obs'
    isolated.mkdir(parents=True)
    # Hardlink official binaries/resources; configurations and outputs are new files.
    for name in ['bin', 'core', 'data']:
        shutil.copytree(source / name, isolated / name, copy_function=os.link)
    (isolated / 'portable_mode.txt').touch()
    config = isolated / 'config/obs-studio'
    profile = config / 'basic/profiles/TCSViewerQA'
    profile.mkdir(parents=True)
    scenes = config / 'basic/scenes'
    scenes.mkdir()
    (config / 'global.ini').write_text('[General]\nEnableAutoUpdates=false\n[Locations]\nConfiguration=../../config\nSceneCollections=../../config\nProfiles=../../config\nPluginManagerSettings=../../config\n')
    (config / 'user.ini').write_text('[General]\nFirstRun=true\n[Basic]\nProfile=TCSViewerQA\nProfileDir=TCSViewerQA\nSceneCollection=TCSViewerQA\nSceneCollectionFile=TCSViewerQA.json\n[BasicWindow]\nSysTrayEnabled=true\nSysTrayWhenStarted=true\n')
    (scenes / 'TCSViewerQA.json').write_text(json.dumps(dict(name='TCSViewerQA', current_scene='QA',
        current_program_scene='QA', sources=[dict(name='QA', id='scene', versioned_id='scene',
        uuid=str(uuid.uuid4()), settings=dict(items=[], id_counter=0))], groups=[],
        scene_order=[dict(name='QA')], canvases=[])))
    recordings = run / 'recordings'
    recordings.mkdir()
    (profile / 'basic.ini').write_text(f'''[General]
Name=TCSViewerQA
[Output]
Mode=Simple
[SimpleOutput]
FilePath={recordings.as_posix()}
RecFormat2=mkv
RecQuality=Small
RecEncoder=x264
Preset=ultrafast
RecTracks=1
[Video]
BaseCX=640
BaseCY=360
OutputCX=640
OutputCY=360
FPSType=0
FPSCommon=30
ColorFormat=NV12
ColorSpace=709
ColorRange=Partial
[Audio]
SampleRate=48000
ChannelSetup=Stereo
''')
    password = secrets.token_urlsafe(32)
    ws_config = config / 'plugin_config/obs-websocket'
    ws_config.mkdir(parents=True)
    (ws_config / 'config.json').write_text(json.dumps(dict(server_enabled=True, server_port=4457,
        auth_required=True, server_password=password, alerts_enabled=False, first_load=False)))
    subprocess.run([sys.executable, str(ROOT / 'test_capture.py'), '--build-only', '--x64',
                    '--viewer-dir', str(run)], check=True)
    reader = run / 'reader'
    reader.mkdir()
    shutil.copy2(ROOT / 'dist/TCSViewerTest.exe', reader / 'TCSViewerTest.exe')
    (reader / 'viewer.ini').write_text('[viewer]\nobs_dir=' + str(source) + '\n')
    processes = []
    connection = None
    recording = False

    def request(kind, data=None):
        identifier = uuid.uuid4().hex
        deadline = time.monotonic() + 25
        connection.send(json.dumps(dict(op=6, d=dict(requestType=kind, requestId=identifier, requestData=data or {}))))
        while True:
            reply = json.loads(connection.recv())
            if reply['op'] == 7 and reply['d']['requestId'] == identifier:
                if not reply['d']['requestStatus']['result']:
                    if reply['d']['requestStatus']['code'] == 207 and time.monotonic() < deadline:
                        time.sleep(0.25)
                        identifier = uuid.uuid4().hex
                        connection.send(json.dumps(dict(op=6, d=dict(requestType=kind, requestId=identifier, requestData=data or {}))))
                        continue
                    raise RuntimeError(kind + ': ' + str(reply['d']['requestStatus']))
                return reply['d'].get('responseData', {})

    try:
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        obs = subprocess.Popen([str(isolated / 'bin/64bit/obs64.exe'), '--portable', '--multi',
                               '--disable-updater', '--minimize-to-tray', '--profile', 'TCSViewerQA',
                               '--collection', 'TCSViewerQA'], cwd=isolated / 'bin/64bit', startupinfo=startup)
        processes.append(obs)
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            try:
                connection = websocket.create_connection('ws://127.0.0.1:4457', timeout=10)
                break
            except OSError:
                if obs.poll() is not None:
                    raise RuntimeError('Isolated OBS failed to start')
                time.sleep(0.25)
        else:
            raise RuntimeError('Isolated OBS WebSocket not ready')
        hello = json.loads(connection.recv())['d']
        identity = dict(rpcVersion=1, eventSubscriptions=0)
        if 'authentication' in hello:
            auth = hello['authentication']
            secret = base64.b64encode(hashlib.sha256((password + auth['salt']).encode()).digest()).decode()
            identity['authentication'] = base64.b64encode(hashlib.sha256((secret + auth['challenge']).encode()).digest()).decode()
        connection.send(json.dumps(dict(op=1, d=identity)))
        assert json.loads(connection.recv())['op'] == 2
        fixture = subprocess.Popen([str(run / 'TCSCaptureFixture.exe'), '--d3d11', '--gray'])
        processes.append(fixture)
        request('CreateInput', dict(sceneName='QA', inputName='QA Game', inputKind='game_capture',
            inputSettings=dict(capture_mode='window', window='TCS Capture Fixture:TCSCaptureFixture:TCSCaptureFixture.exe',
                               priority=2, capture_cursor=False, anti_cheat_hook=False, capture_audio=False), sceneItemEnabled=True))
        request('SetCurrentProgramScene', dict(sceneName='QA'))
        request('StartRecord')
        recording = True
        time.sleep(5)
        viewer = subprocess.Popen([str(reader / 'TCSViewerTest.exe'), '--capture-test',
                                   '--expect-gray', '--framed-test'])
        processes.append(viewer)
        exit_code = viewer.wait(timeout=40)
        before_stop = request('GetRecordStatus')
        stopped = request('StopRecord')
        deadline = time.monotonic() + 20
        while request('GetRecordStatus')['outputActive']:
            if time.monotonic() > deadline:
                raise RuntimeError('Recording did not finish finalizing')
            time.sleep(0.1)
        recording = False
        output = Path(stopped['outputPath'])
        # Wait for the muxer to flush its header/index after the stop acknowledgement.
        deadline = time.monotonic() + 10
        while True:
            with output.open('rb') as stream:
                header = stream.read(4)
            if header == bytes.fromhex('1a45dfa3'):
                break
            if time.monotonic() > deadline:
                raise RuntimeError('Recording container did not finalize')
            time.sleep(0.1)
        probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
            '-count_frames', '-show_entries', 'stream=nb_read_frames,width,height:format=duration', '-of', 'json', str(output)], text=True))
        pixels = []
        duration = float(probe['format']['duration'])
        for second in [7, duration / 2, duration - 1]:
            raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-ss', str(second), '-i', str(output),
                '-frames:v', '1', '-vf', 'crop=64:64:288:148,scale=1:1', '-pix_fmt', 'rgb24', '-f', 'rawvideo', 'pipe:1'])
            pixels.append(list(raw))
        log = (reader / 'viewer.log').read_text(errors='replace')
        result = dict(reader_exit=exit_code, obs_recording_while_reader_finished=before_stop['outputActive'],
                      record_seconds=before_stop['outputDuration'] / 1000,
                      file=str(output), probe=probe, recorded_pixels=pixels,
                      passed=exit_code == 0 and before_stop['outputActive'] and
                      'SELFTEST PASS' in log and 'OBS shared capture attached (read-only)' in log and
                      int(probe['streams'][0]['nb_read_frames']) > 300 and
                      all(len(pixel)==3 and all(abs(value-128)<=8 for value in pixel) for pixel in pixels))
        (run / 'results.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2), flush=True)
        return 0 if result['passed'] else 1
    finally:
        if connection:
            if recording:
                try:
                    request('StopRecord')
                except Exception:
                    pass
            connection.close()
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)


if __name__ == '__main__':
    sys.exit(main())
