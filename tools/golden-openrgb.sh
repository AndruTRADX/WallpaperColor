#!/usr/bin/env bash
# Runs `devices` and several `set` commands of a wallpapercolor binary against the fake
# OpenRGB server and saves what it printed and every packet it sent, one file per scenario.
# Usage: tools/golden-openrgb.sh BINARY OUTDIR
#   Rust reference: tools/golden-openrgb.sh target/release/wallpapercolor tests/golden/openrgb
#   then the C++ build into another folder and `diff -r` the two.
set -euo pipefail
bin=$(readlink -f "$1")
out=$2
here=$(dirname "$(readlink -f "$0")")
image="$here/../tests/golden/gradient.png"
port=16743
mkdir -p "$out"
out=$(readlink -f "$out")
work=$(mktemp -d)
trap 'kill $server 2>/dev/null || true; rm -rf "$work"' EXIT

printf '[openrgb]\naddress = "127.0.0.1:%s"\n' $port > "$work/plain.toml"
cat > "$work/calibrated.toml" <<EOF
[openrgb]
address = "127.0.0.1:$port"

[[calibration]]
device = "commander"
zone = "fan"
gain = [0.72, 0.78, 1.0]

[[calibration]]
device = "VENGEANCE"
gain = [1.0, 0.9, 0.9]
EOF

run() { # name config args...
  local name=$1 config=$2
  shift 2
  : > "$work/log"
  python3 "$here/fake-openrgb.py" $port "$work/log" & server=$!
  sleep 0.3
  set +e
  "$bin" --config "$work/$config.toml" "$@" > "$out/$name.out" 2>&1
  echo "exit $?" >> "$out/$name.out"
  set -e
  sleep 0.2
  kill $server; wait $server 2>/dev/null || true
  cp "$work/log" "$out/$name.packets"
}

run devices          plain devices
run set-one          plain set ff0000
run set-three-arc    plain set ff0000 00ff00 0000ff --arc
run set-three-single plain set ff0000 00ff00 0000ff --single
run set-image        plain set --image "$image"
run set-calibrated   calibrated set ff0000 00ff00 0000ff
run set-white-cal    calibrated set ffffff --single
echo "saved to $out"
