#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

# Use the same Release build as the native validation preset.
editor=./build-linux-release/app/olive-editor
if [[ ! -x "$editor" ]]; then
  echo "Build ausente: execute cmake --preset linux-release e cmake --build --preset linux-release (veja README-ci.md)." >&2
  exit 1
fi
exec "$editor" "$@"
