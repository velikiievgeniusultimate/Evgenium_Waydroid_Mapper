#!/usr/bin/env bash
set -euo pipefail

readonly APP_NAME="evgenium-waydroid-mapper"
readonly DESKTOP_FILE_NAME="Evgenium Waydroid Mapper.desktop"
readonly DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
readonly INSTALL_ROOT="$DATA_HOME/$APP_NAME"
readonly COMMAND_LINK="${XDG_BIN_HOME:-$HOME/.local/bin}/$APP_NAME"
readonly UPDATE_LINK="${XDG_BIN_HOME:-$HOME/.local/bin}/${APP_NAME}-update"
readonly APPLICATIONS_DIR="$DATA_HOME/applications"
readonly ICON_FILE="$DATA_HOME/icons/hicolor/scalable/apps/$APP_NAME.svg"

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

rm -f -- "$COMMAND_LINK"
rm -f -- "$UPDATE_LINK"
rm -f -- "$APPLICATIONS_DIR/$APP_NAME.desktop"
rm -f -- "$ICON_FILE"

desktop_directory="$(find_desktop_directory || true)"
if [[ -n "$desktop_directory" ]]; then
    rm -f -- "$desktop_directory/$DESKTOP_FILE_NAME"
fi

# Remove shortcuts from the two common fallback locations too. This also
# handles a desktop-directory rename after the application was installed.
rm -f -- "$HOME/Desktop/$DESKTOP_FILE_NAME"
rm -f -- "$HOME/Рабочий стол/$DESKTOP_FILE_NAME"
rm -rf -- "$INSTALL_ROOT"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
fi

printf 'Evgenium Waydroid Mapper and its launchers were removed. User profiles and settings were preserved.\n'
