#!/bin/sh
# SPDX-License-Identifier: MIT
# A stable slice lets the BPF policy attach before Proton creates GPU contexts.
set -eu

if [ "$(id -u)" -eq 0 ]; then
    printf 'Run this command as the Steam desktop user, without sudo.\n' >&2
    exit 1
fi

systemctl --user start app-gaming.slice
control_group=$(systemctl --user show --property=ControlGroup --value app-gaming.slice)
case "$control_group" in
    /user.slice/*/app-gaming.slice) ;;
    *)
        printf 'Unexpected gaming slice cgroup: %s\n' "$control_group" >&2
        exit 1
        ;;
esac

printf '/sys/fs/cgroup%s\n' "$control_group"
