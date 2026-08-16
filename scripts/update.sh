#!/usr/bin/env bash
set -euo pipefail

readonly INSTALLER_URL="https://raw.githubusercontent.com/velikiievgeniusultimate/Evgenium_Waydroid_Mapper/main/scripts/install.sh"
readonly DIRECT_PROXY="socks5h://127.0.0.1:18443"

if ! command -v curl >/dev/null 2>&1; then
    printf 'Required command is missing: curl\n' >&2
    exit 1
fi

proxy_arguments=()
if [[ "${EWM_DISABLE_DIRECT_PROXY:-0}" != "1" ]] \
    && curl --fail --silent --max-time 2 \
        --proxy "$DIRECT_PROXY" https://api.github.com/zen \
        >/dev/null 2>&1; then
    proxy_arguments=(--proxy "$DIRECT_PROXY")
    printf 'Using Evgenium VPN DIRECT update channel.\n'
fi

curl "${proxy_arguments[@]}" --fail --silent --show-error --location \
    "$INSTALLER_URL" | bash
