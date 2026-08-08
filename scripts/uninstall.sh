#!/usr/bin/env bash
set -euo pipefail

readonly APP_NAME="evgenium-waydroid-mapper"
readonly INSTALL_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/$APP_NAME"
readonly COMMAND_LINK="${XDG_BIN_HOME:-$HOME/.local/bin}/$APP_NAME"

rm -f -- "$COMMAND_LINK"
rm -rf -- "$INSTALL_ROOT"
printf 'Evgenium Waydroid Mapper was removed. User profiles were not created in this bootstrap version.\n'

