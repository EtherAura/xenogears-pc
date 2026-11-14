#!/bin/bash
# Runs a command holding the Intel GPU lock that every headless game run on the Intel GPU shares (tools/intel.sh, the
# art chapters' run.sh, the real-clock port tests), whichever account runs it: carl or the art lanes' hermes (xg-3tf).
#   tools/gpu_lock.sh <command...>
# The lock file belongs to the account that made it first. With fs.protected_regular, flock's own open of another
# account's file in /tmp (O_CREAT) is refused, so the file is opened read-only and its descriptor is locked.
L=/tmp/xg-intel-gpu.lock
[ -e "$L" ] || { : > "$L" && chmod 644 "$L"; } 2>/dev/null
exec 9<"$L" || { echo "tools/gpu_lock.sh: cannot open $L" >&2; exit 1; }
flock 9 || exit 1
exec "$@"
