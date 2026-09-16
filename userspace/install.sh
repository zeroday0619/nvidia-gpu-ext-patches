#!/bin/sh
# SPDX-License-Identifier: MIT
# Installation is separate from compilation and never starts the policy.
set -eu

PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH

source_directory=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
if [ "$(id -u)" -ne 0 ]; then
    printf 'Run the installer with sudo after building gpu_sched_gaming.\n' >&2
    exit 1
fi
if [ ! -x "$source_directory/gpu_sched_gaming" ]; then
    printf 'Build gpu_sched_gaming before installation.\n' >&2
    exit 1
fi

install -d -m 0755 /usr/local/libexec/gpu-ext /usr/local/bin \
    /etc/gpu-ext /etc/gpu-ext/modes /etc/systemd/system
install -m 0755 "$source_directory/gpu_sched_gaming" /usr/local/libexec/gpu-ext/gpu_sched_gaming
install -m 0755 "$source_directory/gpu-ext-mode" /usr/local/bin/gpu-ext-mode
install -m 0755 "$source_directory/run-game.sh" /usr/local/bin/gpu-ext-run
install -m 0644 "$source_directory/systemd/gpu-ext@.service" /etc/systemd/system/gpu-ext@.service
if [ ! -e /etc/gpu-ext/applications.conf ]; then
    install -m 0644 "$source_directory/applications.conf" /etc/gpu-ext/applications.conf
fi
systemctl daemon-reload
printf 'Installed gpu_ext policy files. Existing application and mode rules were preserved.\n'
printf 'Enable gpu-ext@UID.service for the intended desktop or service user.\n'
