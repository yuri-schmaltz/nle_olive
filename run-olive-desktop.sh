#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

# Use the native build validated by README-ci.md. Arguments go to the editor.
editor=./build-host/app/olive-editor
if [[ ! -x "$editor" ]]; then
  echo "Build ausente: execute cmake --build build-host -j2 (veja README-ci.md)." >&2
  exit 1
fi
exec "$editor" "$@"
