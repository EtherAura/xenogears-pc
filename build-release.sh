#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 -I -B "$root/packaging/build_release.py" "$@"
