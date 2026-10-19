#!/bin/zsh
set -eu
ROOT="${0:A:h}"
BINARY="$ROOT/dist/soRound Media.app/Contents/MacOS/soRoundMedia"
if [[ ! -x "$BINARY" ]]; then
    print -u2 -- "Build first: $ROOT/build.sh"
    exit 1
fi
# 手动前台运行，Ctrl-C 会清理子进程；不注册登录项。/ Manual foreground run; Ctrl-C cleans the child, no login item.
exec "$BINARY" "$@"
