"""Exercise the production backend with a separate, touch-responsive Wayland client.
Requires a CMake Makefiles build in build/, as used by install-local.sh.
"""
import os
import platform
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class ExperimentalCalibrationTest(unittest.TestCase):
    def test_shared_calibration_and_range(self):
        flags=shlex.split(subprocess.check_output(['pkg-config','--cflags',
            'Qt6Widgets','Qt6WaylandCompositor','Qt6Quick'],text=True))
        with tempfile.TemporaryDirectory(prefix='ewm-experimental-') as directory:
            base=Path(directory);runtime=base/'runtime';runtime.mkdir(mode=0o700)
            obj=base/'test.o';exe=base/'test'
            subprocess.run(['c++','-std=c++20','-O2','-fPIC',*(['-mno-direct-extern-access'] if platform.machine().lower() in ('x86_64', 'amd64') else []),
                '-I'+str(ROOT/'app'),*flags,'-c',str(ROOT/'tests/experimental_calibration_integration.cpp'),
                '-o',str(obj)],check=True,timeout=60)
            link=shlex.split((ROOT/'build/CMakeFiles/evgenium-waydroid-mapper.dir/link.txt').read_text())
            link=[str(obj) if item.endswith('/app/main.cpp.o') else item for item in link
                  if not item.startswith('-Wl,--dependency-file=')]
            link[link.index('-o')+1]=str(exe)
            subprocess.run(link,cwd=ROOT/'build',check=True,timeout=60)
            env=dict(os.environ,XDG_RUNTIME_DIR=str(runtime),XDG_CONFIG_HOME=str(base/'config'),
                XDG_DATA_HOME=str(base/'data'),XDG_STATE_HOME=str(base/'state'),
                QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',
                QT_WAYLAND_CLIENT_BUFFER_INTEGRATION='shm',EWM_DEV_MODE='1')
            env.pop('EWM_DEV_QML_DIR',None)
            subprocess.run([str(exe),str(ROOT/'tests/fake_skill_game.qml')],env=env,check=True,timeout=45)
if __name__=='__main__':unittest.main()
