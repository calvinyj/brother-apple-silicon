#!/bin/bash
# Install the native HL-1210W filter and PPD, and add a print queue.
#
# Usage: sudo ./install.sh [device-uri]
#
# The device URI defaults to the one used by the existing Brother queue
# (e.g. dnssd://Brother%20HL-1210W%20series._pdl-datastream._tcp.local./?bidi).
# Without one, pass the printer's raw port yourself: socket://192.168.1.50:9100
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
cd "$(dirname "$0")"

QUEUE=Brother_HL_1210W_native
DEST=/Library/Printers/BrotherNative
PPD_DEST="/Library/Printers/PPDs/Contents/Resources/Brother HL-1210W native.ppd"

if [ "$(id -u)" -ne 0 ]; then
  exec sudo "$0" "$@"
fi

# Build as the invoking user so build/ is not left owned by root.
if [ ! -x build/rastertobrhl1210 ] || [ src/rastertobrhl1210.c -nt build/rastertobrhl1210 ]; then
  sudo -u "${SUDO_USER:-$(stat -f%Su .)}" make
fi

URI=${1:-}
if [ -z "$URI" ]; then
  URI=$(lpstat -v 2>/dev/null | awk -F': ' '/HL_1210W/ && !/'"$QUEUE"'/ {print $2; exit}')
fi
if [ -z "$URI" ]; then
  echo "Looking for the printer on the network and USB..."
  URI=$(lpinfo --include-schemes dnssd,usb -v 2>/dev/null | awk '/1210W/ {print $2; exit}')
fi
if [ -z "$URI" ]; then
  echo "Could not find the printer. Re-run with its address, e.g.:"
  echo "  sudo ./install.sh socket://192.168.1.50:9100"
  exit 1
fi

# CUPS only runs filters that are owned by root and not group/world writable.
install -d -o root -g wheel -m 755 "$DEST" "$DEST/Filter"
install -o root -g wheel -m 755 build/rastertobrhl1210 "$DEST/Filter/rastertobrhl1210"
install -o root -g wheel -m 644 ppd/Brother-HL-1210W-native.ppd "$PPD_DEST"

if ! out=$(lpadmin -p "$QUEUE" -E -v "$URI" -P "$PPD_DEST" \
    -D "Brother HL-1210W (native)" -o printer-is-shared=false 2>&1); then
  echo "$out"
  exit 1
fi
echo "$out" | grep -v "deprecated" || true   # CUPS warns about PPDs in general

echo
echo "Installed queue '$QUEUE' -> $URI"
echo "Print a test page with:"
echo "  lp -d $QUEUE build/test/testpage.pdf      (after 'make test'), or any PDF"
if lpstat -p Brother_HL_1210W_series >/dev/null 2>&1; then
  echo
  echo "The old queue 'Brother_HL_1210W_series' still points at Brother's Intel-only"
  echo "driver. Once this one works you can remove it with:"
  echo "  sudo lpadmin -x Brother_HL_1210W_series"
fi
