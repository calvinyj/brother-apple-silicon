/*
 * mkraster - write synthetic 1-bit CUPS raster pages that stress the
 * line encoder (edit-count limit, overflow bytes, block size limit,
 * junk padding bits). Test helper only.
 *
 * Page 1: 8000 px wide, starts with 100 noise lines.
 * Page 2: 4701 px wide (not a multiple of 8) with junk padding bits.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <cups/raster.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_header(cups_page_header2_t *h, unsigned w, unsigned height,
                       unsigned pw, unsigned ph, unsigned l, unsigned b,
                       unsigned r, unsigned t) {
  memset(h, 0, sizeof(*h));
  h->HWResolution[0] = h->HWResolution[1] = 600;
  h->cupsWidth = w;
  h->cupsHeight = height;
  h->cupsBitsPerColor = h->cupsBitsPerPixel = 1;
  h->cupsBytesPerLine = (w + 7) / 8;
  h->cupsColorSpace = CUPS_CSPACE_K;
  h->cupsColorOrder = CUPS_ORDER_CHUNKED;
  h->cupsNumColors = 1;
  h->NumCopies = 1;
  h->PageSize[0] = pw;
  h->PageSize[1] = ph;
  h->ImagingBoundingBox[0] = l;
  h->ImagingBoundingBox[1] = b;
  h->ImagingBoundingBox[2] = r;
  h->ImagingBoundingBox[3] = t;
  strcpy(h->cupsPageSizeName, "Custom");
}

/* Fill one line according to a pattern chosen by the line number. */
static void make_line(unsigned char *line, const unsigned char *prev, int n, int y) {
  switch (y % 9) {
  case 0:  /* noise */
    for (int i = 0; i < n; i++) line[i] = (unsigned char)rand();
    break;
  case 1:  /* previous line with a few scattered changes */
    memcpy(line, prev, (size_t)n);
    for (int k = 0; k < 6; k++) line[rand() % n] ^= (unsigned char)(1 + rand() % 255);
    break;
  case 2:  /* identical to previous: zero edits */
    memcpy(line, prev, (size_t)n);
    break;
  case 3:  /* blank */
    memset(line, 0, (size_t)n);
    break;
  case 4:  /* X 0 0 X 0 0 ... against a blank line: > 254 edits */
    memset(line, 0, (size_t)n);
    for (int i = 0; i < n; i += 3) line[i] = (unsigned char)(1 + rand() % 255);
    break;
  case 5:  /* solid black: long repeat, count overflow */
    memset(line, 0xFF, (size_t)n);
    break;
  case 6:  /* random runs and literals with long gaps */
    memcpy(line, prev, (size_t)n);
    for (int i = rand() % 400; i < n; i += 1 + rand() % 300) {
      int len = 1 + rand() % 300;
      unsigned char v = (unsigned char)rand();
      for (int j = 0; j < len && i < n; j++, i++) line[i] = (rand() % 4) ? v : (unsigned char)rand();
    }
    break;
  case 7:  /* single pixel at the right edge only */
    memset(line, 0, (size_t)n);
    line[n - 1] = 0x01;
    break;
  default: /* left half black, right half previous */
    memcpy(line, prev, (size_t)n);
    memset(line, 0xFF, (size_t)n / 2);
    break;
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: mkraster out.ras\n");
    return 1;
  }
  FILE *f = fopen(argv[1], "wb");
  if (!f) { perror(argv[1]); return 1; }
  cups_raster_t *ras = cupsRasterOpen(fileno(f), CUPS_RASTER_WRITE);
  cups_page_header2_t h;
  srand(1);

  /* Page 1: wide, no margins; 100 noise lines first to hit the block limit. */
  set_header(&h, 8000, 700, 960, 84, 0, 0, 960, 84);
  cupsRasterWriteHeader2(ras, &h);
  int n = (int)h.cupsBytesPerLine;
  unsigned char *line = calloc((size_t)n, 1), *prev = calloc((size_t)n, 1);
  for (unsigned y = 0; y < h.cupsHeight; y++) {
    if (y < 100)
      for (int i = 0; i < n; i++) line[i] = (unsigned char)rand();
    else
      make_line(line, prev, n, (int)y);
    cupsRasterWritePixels(ras, line, (unsigned)n);
    memcpy(prev, line, (size_t)n);
  }
  free(line);
  free(prev);

  /* Page 2: A4 imageable area only, 4701 px wide, junk in the padding bits. */
  set_header(&h, 4701, 600, 595, 842, 15, 12, 583, 830);
  cupsRasterWriteHeader2(ras, &h);
  n = (int)h.cupsBytesPerLine;
  line = calloc((size_t)n, 1);
  prev = calloc((size_t)n, 1);
  for (unsigned y = 0; y < h.cupsHeight; y++) {
    make_line(line, prev, n, (int)y);
    memcpy(prev, line, (size_t)n);
    line[n - 1] |= 0x07;  /* bits past pixel 4700: must not be printed */
    cupsRasterWritePixels(ras, line, (unsigned)n);
  }
  free(line);
  free(prev);

  cupsRasterClose(ras);
  fclose(f);
  return 0;
}
