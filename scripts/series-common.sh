#!/bin/sh
# SPDX-License-Identifier: MIT

series_error()
{
    printf 'gpu-ext patches: %s\n' "$*" >&2
    exit 1
}

initialize_series()
{
    script_directory=${script_directory:?The caller must define script_directory.}
    [ "$#" -eq 1 ] || series_error 'Supply one pristine upstream Git checkout.'
    target_directory=$(CDPATH='' cd -- "$1" && pwd -P) || exit 1
    repository_directory=$(CDPATH='' cd -- "$script_directory/.." && pwd -P) || exit 1
    series_directory=$repository_directory/patches/615.71.09
    expected_base=$(sed -n "s/^UPSTREAM_COMMIT='\([0-9a-f]*\)'$/\1/p" "$series_directory/base.env")
    [ "${#expected_base}" -eq 40 ] || series_error 'Invalid upstream commit metadata.'
    top_directory=$(git -C "$target_directory" rev-parse --show-toplevel) || exit 1
    [ "$top_directory" = "$target_directory" ] || series_error 'Supply the upstream repository root.'
    actual_base=$(git -C "$target_directory" rev-parse HEAD) || exit 1
    [ "$actual_base" = "$expected_base" ] || series_error "Expected upstream commit $expected_base; found $actual_base."
    status=$(git -C "$target_directory" status --porcelain --untracked-files=all) || exit 1
    [ -z "$status" ] || series_error 'The upstream worktree and index must be clean, including untracked files.'
}

patch_paths()
{
    while IFS= read -r entry || [ -n "$entry" ]; do
        case "$entry" in
            ''|'#'*) continue ;;
            */*|*..*) series_error 'The series must contain plain patch filenames.' ;;
            [0-9]*.patch) ;;
            *) series_error "Invalid series entry: $entry" ;;
        esac
        [ -f "$series_directory/$entry" ] || series_error "Missing patch: $entry"
        printf '%s\n' "$series_directory/$entry"
    done < "$series_directory/series"
}
