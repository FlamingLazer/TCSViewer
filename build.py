"""Build with the locally installed MSVC toolchain; no global PATH changes."""
from pathlib import Path
import os, subprocess, json, urllib.request, tarfile, io, argparse

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--test-build',action='store_true',help='Build a separately named non-elevated QA executable; do not distribute it as the normal app.')
options=parser.parse_args()

root = Path(__file__).resolve().parent
os.chdir(root)
locator = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
instances = json.loads(subprocess.check_output([str(locator), '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-format', 'json'], text=True, encoding='utf-8-sig'))
if not instances:
    raise SystemExit('Install Desktop development with C++ in Visual Studio Build Tools.')
vs = Path(instances[0]['installationPath'])
tools = sorted((vs/'VC/Tools/MSVC').iterdir())[-1]
sdk = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'))/'Windows Kits/10'
sdk_version = sorted(p.name for p in (sdk/'Include').iterdir() if (p/'um/windows.h').exists())[-1]
headers = root/'deps/obs-source'
if not (headers/'libobs/obs.h').exists():
    print('Fetching matching OBS 32.1.2 source headers...')
    data = urllib.request.urlopen('https://codeload.github.com/obsproject/obs-studio/tar.gz/refs/tags/32.1.2', timeout=60).read()
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        for member in archive.getmembers():
            parts = Path(member.name).parts[1:]
            if not member.isfile() or not parts or '..' in parts:
                continue
            if (parts[0]=='libobs' and parts[-1].endswith(('.h', '.h.in'))) or parts==('COPYING',):
                dest=headers/Path(*parts); dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(archive.extractfile(member).read())
c99 = headers/'libobs/util/c99defs.h'
c99.write_text(c99.read_text().replace('#define EXPORT __declspec(dllexport)', '#define EXPORT /* declarations only: viewer resolves runtime exports */'))
(root/'generated').mkdir(exist_ok=True)
(root/'generated/obsconfig.h').write_text('''#pragma once
#define OBS_DATA_PATH "data"
#define OBS_PLUGIN_PATH "obs-plugins"
#define OBS_PLUGIN_DESTINATION "obs-plugins"
#define OBS_INSTALL_PREFIX ""
#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
''')
env = os.environ.copy()
env['INCLUDE'] = ';'.join(map(str, [tools/'include'] + [sdk/'Include'/sdk_version/p for p in ['ucrt','shared','um','winrt']]))
env['LIB'] = ';'.join(map(str, [tools/'lib/x64']+[sdk/'Lib'/sdk_version/p/'x64' for p in ['ucrt','um']]))
env['PATH'] = str(tools/'bin/Hostx64/x64')+';'+str(sdk/'bin'/sdk_version/'x64')+';'+env.get('PATH','')
out=root/'dist'; out.mkdir(exist_ok=True)
execution_level='asInvoker' if options.test_build else 'requireAdministrator'
executable_name='TCSViewerTest.exe' if options.test_build else 'TCSViewer.exe'
resource_name='viewer-test' if options.test_build else 'viewer'
manifest='''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
<assemblyIdentity version="0.6.0.0" processorArchitecture="amd64" name="TCSViewer" type="win32"/>
<dependency><dependentAssembly><assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls" version="6.0.0.0" processorArchitecture="amd64" publicKeyToken="6595b64144ccf1df" language="*"/></dependentAssembly></dependency>
<trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security><requestedPrivileges><requestedExecutionLevel level="EXECUTION_LEVEL" uiAccess="false"/></requestedPrivileges></security></trustInfo>
<compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1"><application><supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/></application></compatibility>
</assembly>'''
(root/'generated'/f'{resource_name}.manifest').write_text(manifest.replace('EXECUTION_LEVEL',execution_level))
(root/'generated'/f'{resource_name}.rc').write_text(f'1 24 "{resource_name}.manifest"\n')
subprocess.run([str(sdk/'bin'/sdk_version/'x64/rc.exe'), '/nologo', '/fo'+str(out/f'{resource_name}.res'), f'{resource_name}.rc'],cwd=root/'generated',env=env,check=True)
command=[str(tools/'bin/Hostx64/x64/cl.exe'), '/nologo','/std:c++17','/EHsc','/O2','/W4','/utf-8','/MT','/DUNICODE','/D_UNICODE', '/DNDEBUG', '/D_CRT_SECURE_NO_WARNINGS',
    '/I'+str(headers/'libobs'), '/I'+str(root/'generated'), str(root/'src/main.cpp'), '/Fo'+str(out/f'{resource_name}.obj'), '/Fe'+str(out/executable_name),
    str(out/f'{resource_name}.res'), '/link','/SUBSYSTEM:WINDOWS','/DYNAMICBASE','/NXCOMPAT', '/MANIFEST:NO',
    'user32.lib','gdi32.lib','shell32.lib','d3d11.lib','dxgi.lib','d3dcompiler.lib','avrt.lib','ole32.lib','uxtheme.lib']
subprocess.run(command, env=env, check=True)
print('Built',out/executable_name,'with',execution_level)

