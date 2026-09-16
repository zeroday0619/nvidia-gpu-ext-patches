#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
script_directory=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
# shellcheck source=series-common.sh
. "$script_directory/series-common.sh"
initialize_series "$@"

temp_directory=$(mktemp -d)
trap 'rm -rf "$temp_directory"' EXIT HUP INT TERM
patch_paths > "$temp_directory/patches"
[ -s "$temp_directory/patches" ] || series_error 'The patch series is empty.'

# An isolated local clone keeps the caller's index and worktree unchanged.
git clone --quiet --shared --no-checkout -- "$target_directory" "$temp_directory/tree"
git -C "$temp_directory/tree" checkout --quiet --detach "$expected_base"
while IFS= read -r patch; do
    git -C "$temp_directory/tree" apply --check --index --whitespace=error-all "$patch"
    git -C "$temp_directory/tree" apply --index --whitespace=error-all "$patch"
done < "$temp_directory/patches"

awk '{ paths[NR] = $0 } END { for (line_number = NR; line_number > 0; line_number--) print paths[line_number] }' \
    "$temp_directory/patches" > "$temp_directory/reverse"
while IFS= read -r patch; do
    git -C "$temp_directory/tree" apply --reverse --check --index --whitespace=error-all "$patch"
    git -C "$temp_directory/tree" apply --reverse --index --whitespace=error-all "$patch"
done < "$temp_directory/reverse"
status=$(git -C "$temp_directory/tree" status --porcelain --untracked-files=all)
[ -z "$status" ] || series_error 'Reverse application did not restore the baseline.'
printf 'PASS: exact base, sequential application, whitespace, and reverse restoration. No build performed.\n'
