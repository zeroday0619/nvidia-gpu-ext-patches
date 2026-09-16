#!/bin/sh
# SPDX-License-Identifier: MIT
# A scope preserves Steam's environment and groups Proton descendants together.
set -eu

if [ "$#" -eq 0 ]; then
    printf 'Usage: run-game.sh COMMAND [ARGUMENT ...]\n' >&2
    exit 2
fi
if [ "$(id -u)" -eq 0 ]; then
    printf 'Run games as the Steam desktop user, without sudo.\n' >&2
    exit 1
fi

# The marker also identifies unknown native games to the automatic detector.
export GPU_EXT_PROFILE=game
exec systemd-run --user --scope --quiet --slice=app-gaming.slice \
    --expand-environment=no -- "$@"
