#!/usr/bin/env bash
# macOS/Linux-аналог tools/setup_lua.ps1: скачивает закреплённые исходники Lua 5.4.8 и sol2 3.3.1,
# проверяет SHA-256 и распаковывает в external/. Lua потом компилируется статически в движок.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"

fetch() {
    local url="$1" archive="$root/$2" destination="$root/$3" expected="$4"
    mkdir -p "$destination"
    [ -f "$archive" ] || curl -fL --retry 3 -o "$archive" "$url"
    local actual
    actual="$(shasum -a 256 "$archive" | cut -d' ' -f1 | tr '[:lower:]' '[:upper:]')"
    if [ "$actual" != "$expected" ]; then
        echo "Checksum mismatch: $archive" >&2
        exit 1
    fi
    case "$archive" in
        *.zip) unzip -qo "$archive" -d "$destination" ;;
        *) tar -xzf "$archive" -C "$destination" ;;
    esac
}

fetch https://www.lua.org/ftp/lua-5.4.8.tar.gz external/lua/source.tar.gz external/lua \
    4F18DDAE154E793E46EEAB727C59EF1C0C0C2B744E7B94219710D76F530629AE
fetch https://github.com/ThePhD/sol2/archive/refs/tags/v3.3.1.zip external/sol2/source.zip external/sol2 \
    8976A3F5302AB6328E0C6B61FA8ECE3DF877D709EE0CD73FC1D2766245AF753E
echo 'Lua 5.4.8 and sol2 3.3.1 are ready. Lua is linked statically.'
