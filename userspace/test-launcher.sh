#!/bin/sh
# SPDX-License-Identifier: MIT
# Replacement commands verify argument boundaries without starting services.
set -eu
source_directory=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
test_directory=$(mktemp -d)
trap 'rm -rf "$test_directory"' EXIT HUP INT TERM
export GPU_EXT_TEST_DIRECTORY="$test_directory"

cat > "$test_directory/id" <<'MOCK'
#!/bin/sh
printf '%s\n' "${GPU_EXT_TEST_UID:-1000}"
MOCK
cat > "$test_directory/systemd-run" <<'MOCK'
#!/bin/sh
printf '%s\n' "$GPU_EXT_PROFILE" > "$GPU_EXT_TEST_DIRECTORY/profile"
printf '%s\n' "$@" > "$GPU_EXT_TEST_DIRECTORY/arguments"
MOCK
cat > "$test_directory/systemctl" <<'MOCK'
#!/bin/sh
printf '%s\n' "$*" >> "$GPU_EXT_TEST_DIRECTORY/control"
case "$*" in
    '--user start app-gaming.slice') ;;
    '--user show --property=ControlGroup --value app-gaming.slice')
        printf '/user.slice/user-1000.slice/user@1000.service/app.slice/app-gaming.slice\n'
        ;;
    *) exit 99 ;;
esac
MOCK
chmod +x "$test_directory/id" "$test_directory/systemd-run" "$test_directory/systemctl"
PATH="$test_directory:$PATH"
export PATH

sh "$source_directory/run-game.sh" '/path with spaces/game' 'argument with spaces' "\$HOME" ''
printf '%s\n' --user --scope --quiet --slice=app-gaming.slice \
    --expand-environment=no -- '/path with spaces/game' 'argument with spaces' "\$HOME" '' \
    > "$test_directory/expected"
cmp "$test_directory/expected" "$test_directory/arguments"
test "$(cat "$test_directory/profile")" = game

if sh "$source_directory/run-game.sh" > /dev/null 2>&1; then
    printf 'FAIL: an empty game command was accepted.\n' >&2
    exit 1
fi
if GPU_EXT_TEST_UID=0 sh "$source_directory/run-game.sh" /bin/true > /dev/null 2>&1; then
    printf 'FAIL: a root game command was accepted.\n' >&2
    exit 1
fi
slice_path=$(sh "$source_directory/prepare-slice.sh")
test "$slice_path" = /sys/fs/cgroup/user.slice/user-1000.slice/user@1000.service/app.slice/app-gaming.slice
printf '%s\n' '--user start app-gaming.slice' \
    '--user show --property=ControlGroup --value app-gaming.slice' > "$test_directory/expected"
cmp "$test_directory/expected" "$test_directory/control"
printf 'PASS: launcher argument preservation, explicit game marker, root rejection, and slice preparation. No processes or services started.\n'
