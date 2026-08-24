#!/usr/bin/env bash
set -euo pipefail

readonly APP_NAME="evgenium-waydroid-mapper"
readonly DESKTOP_FILE_NAME="EWM.desktop"
readonly REPOSITORY="velikiievgeniusultimate/Evgenium_Waydroid_Mapper"
readonly DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
readonly INSTALL_ROOT="$DATA_HOME/$APP_NAME"
readonly BIN_DIR="${XDG_BIN_HOME:-$HOME/.local/bin}"
readonly APPLICATIONS_DIR="$DATA_HOME/applications"
readonly ICON_DIR="$DATA_HOME/icons/hicolor/scalable/apps"
readonly API_URL="https://api.github.com/repos/$REPOSITORY/releases/latest"
readonly DIRECT_PROXY="socks5h://127.0.0.1:18443"

normalize_architecture() {
    case "$1" in
        x86_64|amd64)
            printf 'x86_64\n'
            ;;
        aarch64|arm64)
            printf 'aarch64\n'
            ;;
        *)
            printf 'Unsupported CPU architecture: %s (EWM supports x86_64 and aarch64).\n' "$1" >&2
            return 1
            ;;
    esac
}

readonly MACHINE="${EWM_MACHINE_OVERRIDE:-$(uname -m)}"
if ! RELEASE_ARCHITECTURE="$(normalize_architecture "$MACHINE")"; then
    exit 1
fi
readonly RELEASE_ARCHITECTURE

# Small network-free probes used by CI to prove that one bootstrap script
# selects the correct release on both supported machines.
if [[ "${1:-}" == "--print-architecture" ]]; then
    printf '%s\n' "$RELEASE_ARCHITECTURE"
    exit 0
fi
if [[ "${1:-}" == "--print-asset" ]]; then
    if [[ -z "${2:-}" ]]; then
        printf 'Usage: install.sh --print-asset VERSION\n' >&2
        exit 2
    fi
    printf '%s-%s-linux-%s.tar.gz\n' "$APP_NAME" "$2" "$RELEASE_ARCHITECTURE"
    exit 0
fi

run_privileged() {
    if (( EUID == 0 )); then
        "$@"
    elif command -v pkexec >/dev/null 2>&1; then
        pkexec "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        printf 'System packages are required, but neither pkexec nor sudo is available.\n' >&2
        return 1
    fi
}

ensure_runtime_dependencies() {
    local executable="$1"
    local missing_libraries=""
    local needs_packages=0

    if command -v ldd >/dev/null 2>&1; then
        missing_libraries="$(ldd "$executable" 2>&1 \
            | sed -n 's/^[[:space:]]*\([^[:space:]]\+\)[[:space:]]*=>[[:space:]]*not found.*/\1/p' \
            | sort -u)"
    fi
    if [[ -n "$missing_libraries" ]] || ! command -v pkexec >/dev/null 2>&1; then
        needs_packages=1
    fi
    if (( ! needs_packages )); then
        return 0
    fi

    if command -v dnf5 >/dev/null 2>&1 || command -v dnf >/dev/null 2>&1; then
        local dnf_command
        dnf_command="$(command -v dnf5 || command -v dnf)"
        printf 'Installing Fedora runtime dependencies for EWM (%s)...\n' "$RELEASE_ARCHITECTURE"
        run_privileged "$dnf_command" -y install \
            qt6-qtbase qt6-qtdeclarative qt6-qtwayland polkit
    elif command -v pacman >/dev/null 2>&1; then
        printf 'Installing Arch Linux runtime dependencies for EWM (%s)...\n' "$RELEASE_ARCHITECTURE"
        run_privileged "$(command -v pacman)" -S --needed --noconfirm \
            qt6-base qt6-declarative qt6-wayland polkit
    else
        printf 'Missing EWM runtime libraries:\n%s\n' \
            "${missing_libraries:-PolicyKit (pkexec)}" >&2
        printf 'Automatic dependency installation supports Fedora and Arch Linux.\n' >&2
        return 1
    fi

    if command -v ldd >/dev/null 2>&1 \
        && ldd "$executable" 2>&1 | grep -q '=>[[:space:]]*not found'; then
        printf 'Some EWM runtime libraries are still missing after package installation:\n' >&2
        ldd "$executable" 2>&1 | grep '=>[[:space:]]*not found' >&2 || true
        return 1
    fi
    if ! command -v pkexec >/dev/null 2>&1; then
        printf 'PolicyKit was installed but pkexec is still unavailable. Reopen the session and retry.\n' >&2
        return 1
    fi
}

curl_download() {
    local -a proxy_arguments=()
    if [[ "${EWM_DISABLE_DIRECT_PROXY:-0}" != "1" ]] \
        && curl --fail --silent --max-time 2 \
            --proxy "$DIRECT_PROXY" https://api.github.com/zen \
            >/dev/null 2>&1; then
        proxy_arguments=(--proxy "$DIRECT_PROXY")
    fi
    curl "${proxy_arguments[@]}" "$@"
}

find_desktop_directory() {
    local desktop_directory=""
    local candidate=""

    if command -v xdg-user-dir >/dev/null 2>&1; then
        desktop_directory="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
    fi

    if [[ -z "$desktop_directory" || "$desktop_directory" == "$HOME" ]]; then
        desktop_directory=""
        for candidate in "$HOME/Desktop" "$HOME/Рабочий стол"; do
            if [[ -d "$candidate" ]]; then
                desktop_directory="$candidate"
                break
            fi
        done
    fi

    if [[ -n "$desktop_directory" && "$desktop_directory" != "$HOME" ]]; then
        printf '%s\n' "$desktop_directory"
    fi
}

desktop_exec_value() {
    local value="$1"
    value="${value//\\/\\\\}"
    value="${value//\"/\\\"}"
    printf '"%s"' "$value"
}

for required_command in curl sha256sum tar install; do
    if ! command -v "$required_command" >/dev/null 2>&1; then
        printf 'Required command is missing: %s\n' "$required_command" >&2
        exit 1
    fi
done

release_json="$(curl_download --fail --silent --show-error --location \
    --header 'Accept: application/vnd.github+json' "$API_URL")"
version="$(sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\{0,1\}\([^"]*\)".*/\1/p' <<< "$release_json" | head -n 1)"

if [[ -z "$version" ]]; then
    printf 'The latest GitHub Release has no readable version.\n' >&2
    exit 1
fi

asset="${APP_NAME}-${version}-linux-${RELEASE_ARCHITECTURE}.tar.gz"
download_url="https://github.com/$REPOSITORY/releases/download/v${version}/${asset}"
checksum_url="${download_url}.sha256"
temporary_directory="$(mktemp -d)"
staging_root=""
cleanup() {
    rm -rf -- "$temporary_directory"
    if [[ -n "$staging_root" && -e "$staging_root" ]]; then
        rm -rf -- "$staging_root"
    fi
}
trap cleanup EXIT

printf 'Downloading Evgenium Waydroid Mapper %s...\n' "$version"
curl_download --fail --show-error --location --output "$temporary_directory/$asset" "$download_url"
curl_download --fail --show-error --location --output "$temporary_directory/$asset.sha256" "$checksum_url"

(
    cd "$temporary_directory"
    sha256sum --check "$asset.sha256"
    mkdir extracted
    # Release archives are produced on GitHub-hosted runners whose numeric
    # uid/gid are unrelated to the installing account.  Never restore that
    # ownership: this also keeps the bootstrap reliable when it is invoked
    # from an administrator shell or a user-namespace environment.
    tar --extract --gzip --no-same-owner --file "$asset" --directory extracted
)

if [[ ! -x "$temporary_directory/extracted/bin/$APP_NAME" ]]; then
    printf 'The %s release does not contain an executable EWM binary.\n' \
        "$RELEASE_ARCHITECTURE" >&2
    exit 1
fi
ensure_runtime_dependencies "$temporary_directory/extracted/bin/$APP_NAME"

mkdir -p "$DATA_HOME" "$BIN_DIR"
staging_root="$(mktemp -d "$DATA_HOME/.${APP_NAME}.stage.XXXXXX")"
previous_root="$DATA_HOME/.${APP_NAME}.previous"
cp -a "$temporary_directory/extracted/." "$staging_root/"

# Never overwrite a running ELF in place: Linux correctly rejects that with
# ETXTBSY ("Text file busy"). Both directories live on the same filesystem, so
# two renames replace the complete application atomically while the old EWM
# process safely keeps its already-open inode until it exits.
rm -rf -- "$previous_root"
if [[ -e "$INSTALL_ROOT" ]]; then
    mv -- "$INSTALL_ROOT" "$previous_root"
fi
if ! mv -- "$staging_root" "$INSTALL_ROOT"; then
    if [[ -e "$previous_root" && ! -e "$INSTALL_ROOT" ]]; then
        mv -- "$previous_root" "$INSTALL_ROOT"
    fi
    printf 'Could not activate the staged EWM release. The previous installation was restored.\n' >&2
    exit 1
fi
staging_root=""
rm -rf -- "$previous_root"
ln -sfn "$INSTALL_ROOT/bin/evgenium-waydroid-mapper" "$BIN_DIR/evgenium-waydroid-mapper"
ln -sfn "$INSTALL_ROOT/scripts/update.sh" "$BIN_DIR/evgenium-waydroid-mapper-update"

desktop_template="$INSTALL_ROOT/share/applications/$APP_NAME.desktop.in"
icon_source="$INSTALL_ROOT/share/icons/hicolor/scalable/apps/$APP_NAME.svg"
if [[ ! -f "$desktop_template" || ! -f "$icon_source" ]]; then
    printf 'The release is missing its desktop launcher resources.\n' >&2
    exit 1
fi

desktop_exec="$(desktop_exec_value "$INSTALL_ROOT/bin/$APP_NAME")"
generated_launcher="$temporary_directory/$APP_NAME.desktop"
while IFS= read -r desktop_line || [[ -n "$desktop_line" ]]; do
    if [[ "$desktop_line" == 'Exec=@EXECUTABLE@' ]]; then
        printf 'Exec=%s\n' "$desktop_exec"
    else
        printf '%s\n' "$desktop_line"
    fi
done < "$desktop_template" > "$generated_launcher"

install -Dm644 "$generated_launcher" "$APPLICATIONS_DIR/$APP_NAME.desktop"
install -Dm644 "$icon_source" "$ICON_DIR/$APP_NAME.svg"

desktop_directory="$(find_desktop_directory || true)"
if [[ -n "$desktop_directory" ]]; then
    rm -f -- "$desktop_directory/Evgenium Waydroid Mapper.desktop"
    install -Dm755 "$generated_launcher" "$desktop_directory/$DESKTOP_FILE_NAME"
    printf 'Desktop shortcut: %s\n' "$desktop_directory/$DESKTOP_FILE_NAME"
else
    printf 'Application-menu shortcut installed; no XDG desktop directory was found.\n'
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
fi

printf 'Installed version %s for %s.\n' "$version" "$RELEASE_ARCHITECTURE"
if ! command -v waydroid >/dev/null 2>&1 \
    || [[ ! -f /var/lib/waydroid/waydroid.cfg ]]; then
    printf '\nWaydroid setup is incomplete. EWM will offer to install the package\n'
    printf 'and initialize Android with Google Play on first launch.\n'
fi
if [[ ":${PATH}:" != *":${BIN_DIR}:"* ]]; then
    printf 'Add %s to PATH, then reopen the terminal.\n' "$BIN_DIR"
fi
printf 'Run from KDE Plasma or with: evgenium-waydroid-mapper\n'
