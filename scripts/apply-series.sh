#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
script_directory=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
# shellcheck source=series-common.sh
. "$script_directory/series-common.sh"
initialize_series "$@"

sh "$script_directory/check-series.sh" "$target_directory"
# Repeat the guard after validation in case the caller changed the checkout.
initialize_series "$target_directory"
temp_file=$(mktemp)
trap 'rm -f "$temp_file"' EXIT HUP INT TERM
patch_paths > "$temp_file"
set --
while IFS= read -r patch; do
    set -- "$@" "$patch"
done < "$temp_file"
git -C "$target_directory" apply --check --index --whitespace=error-all "$@"
git -C "$target_directory" apply --index --whitespace=error-all "$@"
printf 'Applied and staged the series in %s. No build or installation performed.\n' "$target_directory"
