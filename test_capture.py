"""Integration test using a separate 32-bit Direct3D 9 source, never the game."""
from pathlib import Path
import os, subprocess, json, time, sys
root=Path(__file__).resolve().parent
locator=Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=Path(json.loads(subprocess.check_output([str(locator),'-products','*','-format','json'],text=True,encoding='utf-8-sig'))[0]['installationPath'])
tools=sorted((vs/'VC/Tools/MSVC').iterdir())[-1]
sdk=Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Windows Kits/10'
version=sorted(p.name for p in (sdk/'Include').iterdir() if (p/'um/windows.h').exists())[-1]
env=os.environ.copy()
env['INCLUDE']=';'.join(map(str,[tools/'include']+[sdk/'Include'/version/p for p in ['ucrt','shared','um','winrt']]))
arch='x64' if '--x64' in sys.argv else 'x86'
env['LIB']=';'.join(map(str,[tools/'lib'/arch]+[sdk/'Lib'/version/p/arch for p in ['ucrt','um']]))
env['PATH']=str(tools/'bin/Hostx64'/arch)+';'+env.get('PATH','')
out=root/'dist'
subprocess.run([str(tools/'bin/Hostx64'/arch/'cl.exe'),'/nologo','/O2','/MT','/EHsc','/DUNICODE','/D_UNICODE',str(root/'src/capture_fixture.cpp'),'/Fo'+str(out/'fixture.obj'),'/Fe'+str(out/'TCSCaptureFixture.exe'),'/link','/SUBSYSTEM:WINDOWS','user32.lib','d3d9.lib','d3d11.lib','dxgi.lib'],env=env,check=True)
fixture=subprocess.Popen([str(out/'TCSCaptureFixture.exe')]+(['--d3d11','--gray'] if '--d3d11' in sys.argv else []))
try:
    time.sleep(0.5)
    viewer_name='TCSViewerTest.exe' if '--test-build' in sys.argv else 'TCSViewer.exe'
    p=subprocess.run([str(out/viewer_name),'--capture-test']+(['--expect-gray'] if '--d3d11' in sys.argv else [])+(['--windowed-test'] if '--windowed-test' in sys.argv else [])+(['--framed-test'] if '--framed-test' in sys.argv else []),timeout=35)
    print('Capture integration test exit:',p.returncode)
    if p.returncode: raise SystemExit(p.returncode)
finally:
    if fixture.poll() is None:
        fixture.terminate()
        fixture.wait(timeout=5)
