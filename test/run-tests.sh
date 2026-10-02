#!/bin/bash
# Printer-free end-to-end test:
#   PDF -> macOS cgpdftoraster (via cupsfilter) -> rastertobrhl1210
#       -> brlaser's brdecode -> PBM
# Every decoded page must match the rendered raster pixel for pixel, and the
# PJL header must carry the chosen options.
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."

FILTER=build/rastertobrhl1210
PPD=ppd/Brother-HL-1210W-native.ppd
T=build/test
rm -rf "$T"
mkdir -p "$T"
fail=0

swift test/make-testpage.swift "$T/testpage.pdf" >/dev/null

# run_case NAME "cupsfilter options" EXPECTED_PAGES LEFT TOP PJL_PATTERNS...
run_case() {
  local name=$1 opts=$2 pages=$3 left=$4 top=$5
  shift 5
  # shellcheck disable=SC2086
  cupsfilter -p "$PPD" -m application/vnd.cups-raster $opts "$T/testpage.pdf" \
    >"$T/$name.ras" 2>"$T/$name.cupsfilter.log"
  check_raster "$name" "$pages" "$left" "$top" "$@"
}

check_raster() {
  local name=$1 pages=$2 left=$3 top=$4
  shift 4
  "$FILTER" 1 tester "$name" 1 "" "$T/$name.ras" >"$T/$name.prn" 2>"$T/$name.filter.log"
  build/rasterdump "$T/$name.ras" "$T/$name-src" >"$T/$name.headers"
  build/brdecode "$T/$name.prn" "$T/$name-out" 2>/dev/null
  local got
  got=$(ls "$T/$name"-out-*.pbm 2>/dev/null | wc -l | tr -d ' ')
  if [ "$got" != "$pages" ]; then
    echo "FAIL $name: decoded $got pages, expected $pages"; fail=1; return
  fi
  local p=1
  for f in "$T/$name"-out-*.pbm; do
    # per-page offsets may be given as "l1,l2" and "t1,t2"
    local l t
    l=$(echo "$left" | cut -d, -f"$p" -s); l=${l:-$left}
    t=$(echo "$top" | cut -d, -f"$p" -s); t=${t:-$top}
    python3 test/compare.py "$f" "$T/$name-src-$p.pbm" "$l" "$t" || fail=1
    p=$((p + 1))
  done
  for pat in "$@"; do
    if ! grep -aq -- "$pat" "$T/$name.prn"; then
      echo "FAIL $name: printer data lacks '$pat'"; fail=1
    fi
  done
}

run_case a4      ""                                   1 0 0 "PAPER = A4" "ECONOMODE = OFF" "DENSITY=0"
run_case letter  "-o PageSize=Letter -o TonerSave=True -o Density=2" \
                                                      1 0 0 "PAPER = LETTER" "ECONOMODE = ON" "DENSITY=2"
run_case copies  "-o copies=2"                        2 0 0 "PAPER = A4"

# Copies are produced by CUPS; the printer must always be told 1 copy.
if [ "$(grep -ac $'\033&l1X' "$T/copies.prn")" -lt 1 ] || grep -aq $'\033&l2X' "$T/copies.prn"; then
  echo "FAIL copies: unexpected copy count command"; fail=1
fi

# Whole chain as CUPS runs it, driven by the PPD's *cupsFilter line
# (-e makes cupsfilter use PPD filters; the path points at this build).
sed "s|/Library/Printers/BrotherNative/Filter/rastertobrhl1210|$PWD/$FILTER|" "$PPD" >"$T/local.ppd"
cupsfilter -e -p "$T/local.ppd" -m printer/test "$T/testpage.pdf" >"$T/chain.prn" 2>"$T/chain.log"
if grep -aq "rastertobrhl1210 .* exited with no errors" "$T/chain.log" &&
   build/brdecode "$T/chain.prn" "$T/chain-out" 2>/dev/null; then
  python3 test/compare.py "$T/chain-out-1.pbm" "$T/a4-src-1.pbm" 0 0 || fail=1
else
  echo "FAIL chain: CUPS did not run the filter from the PPD (see $T/chain.log)"; fail=1
fi

# Encoder stress test on synthetic raster.
build/mkraster "$T/stress.ras"
check_raster stress 2 0 0

size=$(stat -f%z "$T/a4.prn")
echo "A4 test page: $size bytes of printer data"
if [ $fail -ne 0 ]; then
  echo "TESTS FAILED"
  exit 1
fi
echo "ALL TESTS PASSED"
