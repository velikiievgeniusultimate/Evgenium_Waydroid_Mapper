"""Native detector regressions using the user's examples and synthetic negatives."""
import os
import platform
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class SkillCircleTest(unittest.TestCase):
    def test_native_detector(self):
        flags = shlex.split(subprocess.check_output(
            ['pkg-config', '--cflags', '--libs', 'Qt6Gui', 'Qt6Core'], text=True))
        with tempfile.TemporaryDirectory(prefix='ewm-circle-test-') as path:
            executable = str(Path(path) / 'detector')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-fPIC',
                *(['-mno-direct-extern-access'] if platform.machine().lower() in ('x86_64', 'amd64') else []), '-I'+str(ROOT/'app'),
                str(ROOT/'tests/skill_circle_probe.cpp'), str(ROOT/'app/SkillCircleDetector.cpp'),
                *flags, '-o', executable], check=True, timeout=60)
            subprocess.run([executable, str(ROOT/'tests/fixtures/skill-circles')], check=True, timeout=30)
if __name__ == '__main__': unittest.main()
