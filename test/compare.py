#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that a page decoded from printer data equals the source raster.

Usage: compare.py decoded.pbm source.pbm left top

The source (as rendered by CUPS) must appear at pixel offset (left, top)
in the decoded page, and every other pixel must be white.
"""
import sys


def read_pbm(path):
    with open(path, "rb") as f:
        data = f.read()
    nl = data.index(b"\n")
    magic, w, h = data[:nl].split()
    if magic != b"P4":
        sys.exit(f"{path}: not a P4 PBM")
    w, h = int(w), int(h)
    bpl = (w + 7) // 8
    pix = data[nl + 1:]
    rows = [int.from_bytes(pix[y * bpl:(y + 1) * bpl], "big") for y in range(h)]
    return w, h, bpl * 8, rows


def main():
    decoded, source, left, top = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    dw, dh, dbits, drows = read_pbm(decoded)
    sw, sh, sbits, srows = read_pbm(source)

    if dbits < left + sw:
        sys.exit(f"FAIL {decoded}: decoded width {dbits} < {left}+{sw}")
    if dh != top + sh:
        sys.exit(f"FAIL {decoded}: decoded height {dh}, expected {top}+{sh}")

    width_mask = ((1 << sw) - 1) << (sbits - sw)   # drop padding bits
    shift = dbits - left - sbits
    bad = []
    for y in range(dh):
        if y < top:
            want = 0
        else:
            r = srows[y - top] & width_mask
            want = r << shift if shift >= 0 else r >> -shift
        if drows[y] != want:
            bad.append(y)
    if bad:
        sys.exit(f"FAIL {decoded}: {len(bad)} rows differ, first at y={bad[0]}")
    ink = sum(bin(r & width_mask).count("1") for r in srows)
    print(f"ok   {decoded}: {dw}x{dh}, source {sw}x{sh} at +{left}+{top}, {ink} black px")


if __name__ == "__main__":
    main()
