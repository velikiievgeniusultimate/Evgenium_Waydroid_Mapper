#!/usr/bin/env bash
set -euo pipefail
EWM_REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if pgrep -u "$(id -u)" -f '^(/[^ ]*/)?evgenium-waydroid-mapper( |$)' >/dev/null; then
    echo 'Mapper is already running. Close it once before starting the development build.' >&2
    exit 1
fi
cmake -S "$EWM_REPO_ROOT" -B "$EWM_REPO_ROOT/build"
cmake --build "$EWM_REPO_ROOT/build" -j "${EWM_BUILD_JOBS:-4}"
export EWM_DEV_MODE=1
export EWM_DEV_QML_DIR="$EWM_REPO_ROOT/app/qml"
exec "$EWM_REPO_ROOT/build/evgenium-waydroid-mapper" "$@"
