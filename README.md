# Unofficial Brother HL-1210W driver for Apple silicon Macs

Brother's macOS driver for the HL-1210W runs its print filter
(`rastertobrother1210`) as an Intel-only binary. On macOS without Rosetta,
every job stops with "Stopped due to filter errors", and
`/var/log/cups/error_log` shows:

```
execv of .../rastertobrother1210 failed. err:86, Bad CPU type in executable
```

The HL-1210W has no AirPrint, PCL or PostScript, so it can't print without a
filter on the Mac. This project is a small replacement filter compiled
natively for Apple silicon, plus a printer description (PPD), so you can keep
printing until Brother ships an updated driver.

> Not affiliated with or endorsed by Brother. See [Credits and license](#credits-and-license).

## Requirements

- An Apple silicon Mac. Intel Macs can keep using Brother's driver.
- An HL-1210W on the same Wi-Fi/Ethernet network. USB should also work but
  hasn't been tested.
- Xcode Command Line Tools, for `clang`, `make`, `swift` and `python3`:
  `xcode-select --install`
- An administrator password, for installing.

Brother's own driver is not needed. If it's installed, its printer icon is
reused.

Tested on an M-series Mac running macOS 27.0.1, with an HL-1210W on Wi-Fi.

## Quick start

```sh
git clone https://github.com/calvinyj/brother-apple-silicon.git
cd brother-apple-silicon
make test            # optional: checks the driver without printing anything
sudo ./install.sh    # builds, installs and adds the printer
```

Then print from any app (File › Print) and choose
**Brother HL-1210W (native)**. To print from the terminal:

```sh
lp -d Brother_HL_1210W_native document.pdf
```

To make it your default printer, use System Settings › Printers & Scanners.

### How the printer is found

`install.sh` uses the first of these that works:

1. The address of an existing Brother HL-1210W printer entry on this Mac.
2. Bonjour (network) or USB discovery.
3. An address you pass in yourself. Find the printer's IP address in your
   router's device list, then run:
   ```sh
   sudo ./install.sh socket://192.168.1.50:9100
   ```

With a fixed IP address, give the printer a DHCP reservation in your router
so the address doesn't change. If it does change, update the queue:
`sudo lpadmin -p Brother_HL_1210W_native -v socket://NEW-IP:9100`.

## Options

These appear in the print dialog under Printer Options.

| Option          | Values                                         |
|-----------------|------------------------------------------------|
| Paper size      | A4 (default), Letter, Legal, Executive, A5, B5 |
| Toner Save Mode | Off / On                                       |
| Toner Density   | -3 … +3                                        |

Resolution is fixed at 600 dpi. macOS handles copies and collation itself.

## Troubleshooting

**Job stops with "filter errors".** Turn on detailed logging, print again,
then look for the filter's messages:

```sh
cupsctl --debug-logging
grep -E "rastertobrhl1210|Job [0-9]+.*(ERROR|stopped)" /var/log/cups/error_log | tail -20
cupsctl --no-debug-logging
```

**Pages of strange text or PostScript code come out.** CUPS skipped the
filter. This usually means the installed file has the wrong owner or
permissions. Re-run `sudo ./install.sh`.

**Nothing prints.** Check the queue's address with
`lpstat -v Brother_HL_1210W_native`. Then check the printer is reachable with
`nc -z <printer-ip> 9100`, which should report "succeeded".

**Print is shifted or clipped.** Open an issue with a photo of the test page.
`make test` creates the page at `build/test/testpage.pdf`. Its corner marks
should be about 4 mm from each edge of the paper.

## Update or uninstall

```sh
git pull && sudo ./install.sh   # update: replaces the filter and PPD in place
sudo ./uninstall.sh             # remove this driver and its printer entry
```

Brother's old printer entry still uses the Intel-only filter. Once this one
works, you can remove the old entry:

```sh
sudo lpadmin -x Brother_HL_1210W_series
```

## What gets installed

- `/Library/Printers/BrotherNative/Filter/rastertobrhl1210`: the filter.
- `/Library/Printers/PPDs/Contents/Resources/Brother HL-1210W native.ppd`.
  You can also pick this PPD in System Settings › Printers & Scanners › Add
  Printer › Use › Select Software.
- A print queue named `Brother_HL_1210W_native`, not shared on the network.

## How it works

```
PDF ──cgpdftoraster (macOS)──▶ 1-bit raster ──rastertobrhl1210──▶ PJL + PCL raster ──▶ printer
```

- macOS renders each page to 1-bit black-and-white raster at 600 dpi.
- `src/rastertobrhl1210.c` wraps the raster in a PJL job and compresses each
  line with Brother's "1030" delta encoding.
- The raster covers only the printable area. The printer starts printing
  about 4 mm (12pt) in from the paper edges, which matches the PPD's margins.

## Limitations

- Only the HL-1210W has been tried. Other Brother monochrome lasers may speak
  the same language; see [brlaser](https://github.com/Owl-Maintain/brlaser)
  for a broader driver.
- No status monitor, toner levels, or HQ1200 mode.
- Gray areas and photos use Apple's error-diffusion dither, so they look
  grainier than with Brother's driver. Text and line art are crisp.

## Development

`make test` checks everything without a printer:

1. It renders `test/make-testpage.swift` through the real macOS rasterizer.
2. It runs the filter on that raster, then decodes the printer data with
   brlaser's `brdecode`.
3. Every decoded page must match the source raster pixel for pixel.

A synthetic stress raster also covers the encoder's edge cases: lines with
more than 254 edits, overflow counts, the block size limit, and padding
bits past the edge of the image.

## Credits and license

The printer language was reverse-engineered by the brlaser project
(Copyright 2013 Peter De Wachter and contributors;
[original](https://github.com/pdewacht/brlaser),
[maintained fork](https://github.com/Owl-Maintain/brlaser)):

- The line encoder and job framing in `src/rastertobrhl1210.c` are ported
  from brlaser's `src/line.cc` and `src/job.cc`. The file header lists the
  changes.
- `test/brdecode.cc` is copied unmodified from brlaser (commit `edf2770`).

This project is licensed under the GNU General Public License v2.0 or later
(see `LICENSE`), and comes with no warranty.

No Brother software, firmware or other files are included. The PPD only
refers to the printer icon at the path where Brother's driver installs it.

Brother and HL-1210W are trademarks of Brother Industries, Ltd. They are used
here only to identify the printer this driver works with. This project is not
affiliated with, endorsed by, or supported by Brother.
