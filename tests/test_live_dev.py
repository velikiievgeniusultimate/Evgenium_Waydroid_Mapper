"""Integration checks against an isolated mapper, never the user's Android session."""
import json
import hashlib
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]

class LiveDevTest(unittest.TestCase):
    def test_reload_and_control(self):
        with tempfile.TemporaryDirectory(prefix='ewm-dev-test-') as directory:
            base = Path(directory)
            runtime = base / 'runtime'
            runtime.mkdir(mode=0o700)
            qml = base / 'qml'
            qml.mkdir()
            overlay = qml / 'MapperOverlay.qml'
            shutil.copy(ROOT / 'app/qml/MapperOverlay.qml', overlay)
            original = overlay.read_text()
            env = dict(os.environ, EWM_DEV_MODE='1', EWM_DEV_QML_DIR=str(qml),
                       XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(base / 'config'),
                       XDG_DATA_HOME=str(base / 'data'), XDG_STATE_HOME=str(base / 'state'),
                       QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                       QT_WAYLAND_CLIENT_BUFFER_INTEGRATION='shm')
            configuration = base / 'config/Evgenium'
            configuration.mkdir(parents=True)
            (configuration / 'ewm-development.ini').write_text(
                '[development]\nenabled = true\nqmlDirectory = ' + str(qml) + '\n')
            env.pop('EWM_DEV_MODE', None)
            env.pop('EWM_DEV_QML_DIR', None)
            with (base / 'process.log').open('w+') as log:
                process = subprocess.Popen([str(ROOT / 'build/evgenium-waydroid-mapper')], env=env,
                                           stdout=log, stderr=log)
                try:
                    for _ in range(100):
                        if (runtime / 'ewm-dev.sock').exists(): break
                        if process.poll() is not None: break
                        time.sleep(.05)
                    def request(command, **fields):
                        with socket.socket(socket.AF_UNIX) as client:
                            client.settimeout(5)
                            client.connect(str(runtime / 'ewm-dev.sock'))
                            client.sendall(json.dumps(dict(command=command, **fields)).encode()+b'\n')
                            response = b''
                            while b'\n' not in response:
                                chunk = client.recv(65536)
                                if not chunk: raise RuntimeError('empty response')
                                response += chunk
                            return json.loads(response)
                    self.assertTrue(request('status')['ok'])
                    self.assertTrue(request('preview-ui')['ok'])
                    self.assertTrue(request('reload-ui')['ok'])
                    status = request('status')
                    self.assertEqual(status['loadedQmlUrl'], overlay.as_uri())
                    self.assertEqual(status['diskQmlSha256'], hashlib.sha256(overlay.read_bytes()).hexdigest())
                    self.assertIn(str(overlay), status['devWatchFiles'])
                    self.assertIn(str(qml), status['devWatchDirectories'])
                    self.assertEqual(status['pid'], process.pid)
                    self.assertTrue(request('ui', action='tree')['ok'])
                    self.assertFalse(request('ui', action='click', options={'target': 'tapBadge-0'})['ok'])
                    fake_qml = base / 'Game.qml'
                    fake_qml.write_text('import QtQuick\nimport QtQuick.Window\nWindow { visible: true; width: 800; height: 600; Rectangle { anchors.fill: parent; color: "green" } }')
                    client_env = dict(env, QT_QPA_PLATFORM='wayland', WAYLAND_DISPLAY='evgenium-wayland-0')
                    game = subprocess.Popen(['/usr/lib/qt6/bin/qml', str(fake_qml)], env=client_env, stdout=log, stderr=log)
                    try:
                        for _ in range(80):
                            status = request('status')
                            if status['surfaceId'] != '0': break
                            time.sleep(.05)
                        self.assertNotEqual(status['surfaceId'], '0')
                        for _ in range(3):
                            self.assertTrue(request('reload-ui')['ok'])
                            after = request('status')
                            self.assertEqual(after['surfaceId'], status['surfaceId'])
                            self.assertEqual(after['compositorId'], status['compositorId'])
                            self.assertIsNone(game.poll())
                    finally:
                        game.terminate()
                        game.wait(timeout=5)
                    self.assertTrue(request('call', method='setCursorStyle', args=[1])['ok'])
                    self.assertEqual(request('status')['cursorStyle'], 1)
                    self.assertFalse(request('call', method='megaStopWaydroid', args=[])['ok'])
                    self.assertFalse(request('call', method='setCursorStyle', args=[])['ok'])
                    revision = request('status')['uiRevision']
                    overlay.write_text('import QtQuick\nItem { invalid syntax !!! }')
                    self.assertFalse(request('reload-ui')['ok'])
                    self.assertEqual(request('status')['uiRevision'], revision)
                    overlay.write_text(original)
                    self.assertTrue(request('reload-ui')['ok'])
                    # Atomic editor saves must also trigger the watcher.
                    revision = request('status')['uiRevision']
                    next_file = qml / 'next.qml'
                    next_file.write_text(original)
                    next_file.replace(overlay)
                    time.sleep(.7)
                    self.assertGreater(request('status')['uiRevision'], revision)
                    self.assertIsNone(process.poll())
                    log.flush()
                    log.seek(0)
                    output = log.read()
                    self.assertIn('mapper overlay reloaded; compositor and Android surfaces retained', output)
                    self.assertNotIn('ReferenceError', output)
                    self.assertNotIn('Cannot assign', output)
                    self.assertNotIn('TypeError', output)
                except Exception:
                    log.flush()
                    log.seek(0)
                    print(log.read())
                    raise
                finally:
                    process.terminate()
                    process.wait(timeout=10)

if __name__ == '__main__':
    unittest.main()
