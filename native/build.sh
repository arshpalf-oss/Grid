#!/usr/bin/env bash
# Builds Grid.exe for Windows from this Linux/macOS checkout.
#
# The engine and the Win32 UI are plain C++17 with no third-party
# dependencies, so any C++17 compiler targeting Windows works. This script uses
# zig's bundled clang + mingw-w64, which cross-compiles a self-contained PE32+
# executable with no runtime DLLs to install.
set -euo pipefail
cd "$(dirname "$0")"

ZIG="${ZIG:-zig}"
if ! command -v "$ZIG" >/dev/null 2>&1; then
  for candidate in \
    /usr/local/lib/python3.11/dist-packages/ziglang/zig \
    "$HOME/.local/bin/zig" \
    /usr/local/bin/zig; do
    if [ -x "$candidate" ]; then
      ZIG="$candidate"
      break
    fi
  done
fi

if ! command -v "$ZIG" >/dev/null 2>&1 && [ ! -x "$ZIG" ]; then
  echo "error: zig not found. Install it (pip install ziglang) or pass ZIG=/path/to/zig." >&2
  exit 1
fi

mkdir -p release

SOURCES=(
  engine/engine.cpp
  ui/util.cpp
  ui/sheet.cpp
  ui/chrome.cpp
  ui/commands.cpp
  ui/dialogs.cpp
  ui/selftest.cpp
  ui/main.cpp
)

COMMON=(
  -target x86_64-windows-gnu
  -std=c++17
  -DUNICODE
  -D_UNICODE
)

case "${1:-release}" in
  debug)
    OPT=(-O0 -g)
    OUT=release/Grid-debug.exe
    ;;
  *)
    OPT=(-O2)
    OUT=release/Grid.exe
    ;;
esac

"$ZIG" c++ "${COMMON[@]}" "${OPT[@]}" -static \
  "${SOURCES[@]}" \
  -o "$OUT" \
  -Wl,--subsystem,windows \
  -lgdi32 -lcomdlg32 -luser32 -lkernel32 -lshell32

rm -f release/*.pdb
printf 'built %s (%s bytes)\n' "$OUT" "$(stat -c%s "$OUT" 2>/dev/null || echo '?')"
