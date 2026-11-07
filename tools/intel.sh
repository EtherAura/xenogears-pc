#!/bin/bash
# One headless game run at a time on the Intel GPU, with the safety environment every such run needs:
#   tools/intel.sh <command...>
# Serialised by a lock (other tools/intel.sh runs wait), never the user's settings or memory cards: XG_SETTINGS and
# XG_CARD_DIR must be given by the caller (refused otherwise), XG_VK_DEVICE=Intel, headless, offscreen video, dummy
# audio, the stepped clock unless XG_CLOCK is set. Runs longer than 8 minutes: start the whole thing with setsid nohup.
[ -n "$XG_SETTINGS" ] || { echo "tools/intel.sh: set XG_SETTINGS (none, or a scratch copy): never the user's settings.ini" >&2; exit 2; }
[ -n "$XG_CARD_DIR" ] || { echo "tools/intel.sh: set XG_CARD_DIR to a scratch directory: never the user's memory cards" >&2; exit 2; }
export XG_VK_DEVICE=Intel XG_HEADLESS=1 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy XG_CLOCK="${XG_CLOCK:-step}"
exec flock /tmp/xg-intel-gpu.lock "$@"
