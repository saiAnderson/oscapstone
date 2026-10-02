#!/usr/bin/env bash

PTY_FILE="$1"

while IFS= read -r line; do
    # QEMU 原本的 stderr 照樣顯示
    printf '%s\n' "$line" >&2

    # 找出 /dev/pts/N
    if [[ "$line" =~ (/dev/pts/[0-9]+) ]]; then
        printf '%s\n' "${BASH_REMATCH[1]}" > "$PTY_FILE"
    fi
done