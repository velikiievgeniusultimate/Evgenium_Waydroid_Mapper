#!/usr/bin/env bash
set -euo pipefail

readonly APP_NAME="evgenium-waydroid-mapper"
readonly REPOSITORY="velikiievgeniusultimate/Evgenium_Waydroid_Mapper"
readonly INSTALL_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/$APP_NAME"
readonly BIN_DIR="${XDG_BIN_HOME:-$HOME/.local/bin}"
readonly API_URL="https://api.github.com/repos/$REPOSITORY/releases/latest"

for required_command in curl sha256sum tar; do
    if ! command -v "$required_command" >/dev/null 2>&1; then
        printf 'Required command is missing: %s\n' "$required_command" >&2
        exit 1
    fi
done

release_json="$(curl --fail --silent --show-error --location \
    --header 'Accept: application/vnd.github+json' "$API_URL")"
version="$(sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\{0,1\}\([^"]*\)".*/\1/p' <<< "$release_json" | head -n 1)"

if [[ -z "$version" ]]; then
    printf 'The latest GitHub Release has no readable version.\n' >&2
    exit 1
fi

asset="${APP_NAME}-${version}-linux-x86_64.tar.gz"
download_url="https://github.com/$REPOSITORY/releases/download/v${version}/${asset}"
checksum_url="${download_url}.sha256"
temporary_directory="$(mktemp -d)"
trap 'rm -rf -- "$temporary_directory"' EXIT

printf 'Downloading Evgenium Waydroid Mapper %s...\n' "$version"
curl --fail --show-error --location --output "$temporary_directory/$asset" "$download_url"
curl --fail --show-error --location --output "$temporary_directory/$asset.sha256" "$checksum_url"

(
    cd "$temporary_directory"
    sha256sum --check "$asset.sha256"
    mkdir extracted
    tar --extract --gzip --file "$asset" --directory extracted
)

mkdir -p "$INSTALL_ROOT" "$BIN_DIR"
cp -a "$temporary_directory/extracted/." "$INSTALL_ROOT/"
ln -sfn "$INSTALL_ROOT/bin/evgenium-waydroid-mapper" "$BIN_DIR/evgenium-waydroid-mapper"

printf 'Installed version %s.\n' "$version"
if [[ ":${PATH}:" != *":${BIN_DIR}:"* ]]; then
    printf 'Add %s to PATH, then reopen the terminal.\n' "$BIN_DIR"
fi
printf 'Run: evgenium-waydroid-mapper doctor\n'

