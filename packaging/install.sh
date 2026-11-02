#!/usr/bin/env bash
# Installs the prebuilt WallpaperColor from a release tarball for the current user. Nothing is
# compiled. Ships inside wallpapercolor-<version>-<arch>.tar.gz as install.sh.
#   ~/.local/bin/wallpapercolor   terminal client
#   ~/.local/bin/wallpapercolord  service (systemd user unit wallpapercolord.service)
# Usage: ./install.sh              install and (re)start the service
#        ./install.sh --no-start   install only
#        ./install.sh --udev       also install the udev rule for Gigabyte RGB Fusion 2 (sudo)
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

start=1 udev=0
for arg in "$@"; do
  case "$arg" in
    --no-start) start=0 ;;
    --udev) udev=1 ;;
    *) echo "Unknown option: $arg (use --no-start, --udev)" >&2; exit 2 ;;
  esac
done

install -Dm755 wallpapercolor  "$HOME/.local/bin/wallpapercolor"
install -Dm755 wallpapercolord "$HOME/.local/bin/wallpapercolord"
install -Dm644 wallpapercolord.service "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/wallpapercolord.service"
systemctl --user daemon-reload

if (( udev )); then
  sudo install -Dm644 70-wallpapercolor.rules /etc/udev/rules.d/70-wallpapercolor.rules
  sudo udevadm control --reload
  sudo udevadm trigger --subsystem-match=hidraw
fi

if (( start )); then
  systemctl --user enable wallpapercolord.service
  systemctl --user restart wallpapercolord.service
  echo "Service running. Logs: journalctl --user -u wallpapercolord -f"
fi
case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) echo "Add ~/.local/bin to your PATH." ;; esac
echo "In OpenLinkHub: enableOpenRGBTargetServer = true in /var/lib/openlinkhub/config.json,"
echo "then turn on \"OpenRGB Integration\" for each device in the WebUI."
