#!/usr/bin/env bash
set -euo pipefail

readonly INSTALLER_URL="https://raw.githubusercontent.com/velikiievgeniusultimate/Evgenium_Waydroid_Mapper/main/scripts/install.sh"

if ! command -v curl >/dev/null 2>&1; then
    printf 'Required command is missing: curl\n' >&2
    exit 1
fi

curl --fail --silent --show-error --location "$INSTALLER_URL" | bash
