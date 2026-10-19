#!/bin/zsh
set -eu
ROOT="${0:A:h}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/soround-media-tests.XXXXXX")"
trap 'rm -rf -- "$WORK"' EXIT
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
    -I "$ROOT/stubs" -I "$ROOT/../../main" "$ROOT/remote_media_tests.c" -o "$WORK/test"
ASAN_OPTIONS=detect_leaks=0 "$WORK/test"
