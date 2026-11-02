#!/usr/bin/env bash
# Builds WallpaperColor and installs it for the current user:
#   ~/.local/bin/wallpapercolor   terminal client
#   ~/.local/bin/wallpapercolord  service (systemd user unit wallpapercolord.service)
# Usage: ./install.sh            build, test, install and (re)start the service
#        ./install.sh --no-start build, test and install only
# Needs: cmake ninja stb tomlplusplus nlohmann-json cli11 sdbus-cpp doctest libwebp
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
install -Dm755 build/src/wallpapercolor  "$HOME/.local/bin/wallpapercolor"
install -Dm755 build/src/wallpapercolord "$HOME/.local/bin/wallpapercolord"
install -Dm644 systemd/wallpapercolord.service "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/wallpapercolord.service"
systemctl --user daemon-reload

if [[ "${1:-}" != --no-start ]]; then
  systemctl --user enable wallpapercolord.service
  systemctl --user restart wallpapercolord.service
  echo "Service running. Logs: journalctl --user -u wallpapercolord -f"
fi
echo "In OpenLinkHub: enableOpenRGBTargetServer = true in /var/lib/openlinkhub/config.json,"
echo "then turn on \"OpenRGB Integration\" for each device in the WebUI."
