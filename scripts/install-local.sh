#!/usr/bin/env bash
# Install a local build at the same location used by the desktop/release updater.
set -euo pipefail
EWM_REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
EWM_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
EWM_INSTALL_ROOT="$EWM_DATA_HOME/evgenium-waydroid-mapper"
cmake -S "$EWM_REPO_ROOT" -B "$EWM_REPO_ROOT/build"
cmake --build "$EWM_REPO_ROOT/build" -j "${EWM_BUILD_JOBS:-4}"
mkdir -p "$EWM_DATA_HOME"
EWM_STAGE="$(mktemp -d "$EWM_DATA_HOME/.ewm-local-stage.XXXXXX")"
trap 'if [[ -n "$EWM_STAGE" ]]; then rm -rf -- "$EWM_STAGE"; fi' EXIT
if [[ -d "$EWM_INSTALL_ROOT" ]]; then cp -a "$EWM_INSTALL_ROOT/." "$EWM_STAGE/"; fi
cmake --install "$EWM_REPO_ROOT/build" --prefix "$EWM_STAGE"
cp "$EWM_REPO_ROOT/VERSION" "$EWM_STAGE/VERSION"
cp "$EWM_REPO_ROOT/scripts/"{install,update,uninstall}.sh "$EWM_STAGE/scripts/"
EWM_BACKUP=""
if [[ -e "$EWM_INSTALL_ROOT" ]]; then
    EWM_BACKUP="$(mktemp -d "$EWM_DATA_HOME/.ewm-local-backup.XXXXXX")"
    rmdir "$EWM_BACKUP"
    mv -- "$EWM_INSTALL_ROOT" "$EWM_BACKUP"
fi
if ! mv -- "$EWM_STAGE" "$EWM_INSTALL_ROOT"; then
    if [[ -n "$EWM_BACKUP" ]]; then mv -- "$EWM_BACKUP" "$EWM_INSTALL_ROOT"; fi
    exit 1
fi
EWM_STAGE=""
if [[ "${1:-}" == "--enable-live" ]]; then
    python3 - "$EWM_REPO_ROOT/app/qml" <<'PY'
import configparser
import os
from pathlib import Path
import sys
path = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')) / 'Evgenium/ewm-development.ini'
path.parent.mkdir(parents=True, exist_ok=True)
config = configparser.ConfigParser()
config.optionxform = str
config.read(path)
if not config.has_section('development'): config.add_section('development')
config.remove_option('development', 'qmldirectory')
config.set('development', 'enabled', 'true')
config.set('development', 'qmlDirectory', sys.argv[1])
staged = path.with_suffix('.ini.tmp')
with staged.open('w') as stream: config.write(stream)
staged.replace(path)
PY
fi
printf 'Installed: %s\nBackup: %s\nUse the usual EWM desktop shortcut.\n' "$EWM_INSTALL_ROOT" "$EWM_BACKUP"
