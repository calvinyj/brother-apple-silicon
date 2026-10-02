#!/bin/bash
# Remove the native HL-1210W queue, filter and PPD.
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  exec sudo "$0" "$@"
fi

lpadmin -x Brother_HL_1210W_native 2>/dev/null || true
rm -f "/Library/Printers/PPDs/Contents/Resources/Brother HL-1210W native.ppd"
rm -rf /Library/Printers/BrotherNative
echo "Removed."
