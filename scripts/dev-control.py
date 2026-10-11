#!/usr/bin/env python3
"""Control the opt-in mapper dev endpoint without restarting Android."""
import argparse
import json
import os
from pathlib import Path
import socket

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('command', choices=['status', 'reload-ui', 'preview-ui', 'call', 'logs', 'ui'])
parser.add_argument('method', nargs='?')
parser.add_argument('args', nargs='*', help='JSON values, e.g. 0 960 540')
options = parser.parse_args()
if options.command == 'logs':
    path = Path(os.environ.get('XDG_STATE_HOME', Path.home() / '.local/state')) / 'evgenium-waydroid-mapper/runtime.log'
    print('\n'.join(path.read_text().splitlines()[-100:]))
    raise SystemExit()
request = {'command': options.command}
if options.command == 'ui':
    request.update(action=options.method, options=json.loads(options.args[0]) if options.args else {})
if options.command == 'call':
    if not options.method:
        parser.error('call requires a method')
    request.update(method=options.method, args=[json.loads(arg) for arg in options.args])
try:
    with socket.socket(socket.AF_UNIX) as client:
        client.settimeout(4)
        client.connect(str(Path(os.environ.get('XDG_RUNTIME_DIR', f'/run/user/{os.getuid()}')) / 'ewm-dev.sock'))
        client.sendall(json.dumps(request).encode() + b'\n')
        response = b''
        while b'\n' not in response:
            chunk = client.recv(65536)
            if not chunk:
                raise RuntimeError('Mapper closed the connection without a response')
            response += chunk
        result = json.loads(response)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        raise SystemExit(0 if result.get('ok') else 1)
except (OSError, RuntimeError) as error:
    parser.exit(1, f'{error}\nRestart EWM using the usual desktop shortcut with development enabled, or use scripts/dev-run.sh.\n')
