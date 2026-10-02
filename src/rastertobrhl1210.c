/*
 * rastertobrhl1210 - CUPS raster filter for the Brother HL-1210W series,
 * built natively for Apple silicon so it runs without Rosetta.
 *
 * Input:  1-bit black (cupsColorSpace 3) CUPS raster, as rendered by the
 *         macOS cgpdftoraster filter for the bundled PPD.
 * Output: PJL job + PCL raster using Brother's "1030" line compression.
 *
 * Copyright 2013 Peter De Wachter (brlaser)
 * Copyright 2026 calvinyj
 *
 * The line encoder and the PJL/PCL job framing are ported from brlaser's
 * src/line.cc and src/job.cc (https://github.com/Owl-Maintain/brlaser at
 * commit edf2770, originally https://github.com/pdewacht/brlaser).
 * Changes made in October 2026: rewritten as a single C file; clears
 * padding bits past the raster width; always requests one copy (CUPS
 * makes copies); fixed tray, media type and resolution settings for the
 * HL-1210W.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <cups/raster.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FILTER_NAME "rastertobrhl1210"
#define LINES_PER_BLOCK 64     /* blocks restart from a literal line */
#define MAX_BLOCK_BYTES 16350  /* payload limit of one "w" block */
#define MAX_EDITS 254          /* an edit count of 255 means "blank line" */
#define MAX_LINE_BYTES 10000

static volatile sig_atomic_t cancelled = 0;

static void on_sigterm(int sig) {
  (void)sig;
  cancelled = 1;
}

/* ---- growable byte buffer ---------------------------------------------- */

typedef struct {
  unsigned char *data;
  size_t len, cap;
} buf_t;

static void buf_reserve(buf_t *b, size_t extra) {
  if (b->len + extra <= b->cap)
    return;
  size_t cap = b->cap ? b->cap * 2 : 4096;
  while (cap < b->len + extra)
    cap *= 2;
  b->data = realloc(b->data, cap);
  if (!b->data) {
    fputs("ERROR: " FILTER_NAME ": out of memory\n", stderr);
    exit(1);
  }
  b->cap = cap;
}

static void buf_put(buf_t *b, unsigned char c) {
  buf_reserve(b, 1);
  b->data[b->len++] = c;
}

static void buf_append(buf_t *b, const unsigned char *p, size_t n) {
  buf_reserve(b, n);
  memcpy(b->data + b->len, p, n);
  b->len += n;
}

/* ---- line compression ---------------------------------------------------
 *
 * Each line is sent as a list of edits against the previous line:
 *   substitute  0ooooccc  skip o bytes, then copy c+1 literal bytes
 *   repeat      1ooccccc  skip o bytes, then write one byte c+2 times
 * Offsets/counts too big for their bit field continue in overflow bytes.
 * A line starts with its edit count; 0xFF instead means an all-white line.
 */

static void put_overflow(buf_t *b, int value) {
  if (value < 0)
    return;
  while (value >= 255) {
    buf_put(b, 255);
    value -= 255;
  }
  buf_put(b, (unsigned char)value);
}

static void put_substitute(buf_t *b, int offset, const unsigned char *p, int n) {
  int count = n - 1;
  buf_put(b, (unsigned char)(((offset < 15 ? offset : 15) << 3) |
                             (count < 7 ? count : 7)));
  put_overflow(b, offset - 15);
  put_overflow(b, count - 7);
  buf_append(b, p, (size_t)n);
}

static void put_repeat(buf_t *b, int offset, int n, unsigned char value) {
  int count = n - 2;
  buf_put(b, (unsigned char)(0x80 | ((offset < 3 ? offset : 3) << 5) |
                             (count < 31 ? count : 31)));
  put_overflow(b, offset - 3);
  put_overflow(b, count - 31);
  buf_put(b, value);
}

static int is_blank(const unsigned char *p, int n) {
  for (int i = 0; i < n; i++)
    if (p[i])
      return 0;
  return 1;
}

/* Length of the literal run starting at `start`. It stops where the line
 * matches the reference again for two bytes, or where three equal bytes
 * begin (cheaper as a repeat). Zero means "start with a repeat". */
static int literal_length(const unsigned char *line, const unsigned char *ref,
                          int start, int end) {
  int prev = start;
  for (int i = start; i + 1 < end; i++) {
    if (line[i] == ref[i] && line[i + 1] == ref[i + 1])
      return i - start;
    if (line[i] == line[i + 1] && line[i] == line[prev])
      return prev - start;
    prev = i;
  }
  return end - start;
}

/* Encode one line. Without a reference the whole line is sent as one
 * literal (required for the first line of every block). */
static void encode_line(buf_t *out, const unsigned char *line,
                        const unsigned char *ref, int n) {
  if (is_blank(line, n)) {
    buf_put(out, 0xFF);
    return;
  }
  if (!ref) {
    buf_put(out, 1);
    put_substitute(out, 0, line, n);
    return;
  }

  int end = n;
  while (end > 0 && line[end - 1] == ref[end - 1])
    end--;

  size_t count_at = out->len;
  buf_put(out, 0);
  int edits = 0, pos = 0, i = 0;
  for (;;) {
    while (i < end && line[i] == ref[i])
      i++;
    if (i == end)
      break;
    int offset = i - pos;
    if (++edits == MAX_EDITS) {
      put_substitute(out, offset, line + i, end - i);
      break;
    }
    int len = literal_length(line, ref, i, end);
    if (len > 0) {
      put_substitute(out, offset, line + i, len);
    } else {
      len = 2;
      while (i + len < end && line[i + len] == line[i])
        len++;
      put_repeat(out, offset, len, line[i]);
    }
    i += len;
    pos = i;
  }
  out->data[count_at] = (unsigned char)edits;
}

/* ---- blocks ---------------------------------------------------------- */

typedef struct {
  buf_t data;
  int lines;
} block_t;

static void block_flush(FILE *out, block_t *b) {
  if (!b->lines)
    return;
  fprintf(out, "%dw", (int)b->data.len + 2);
  putc(b->lines >> 8, out);
  putc(b->lines & 0xFF, out);
  fwrite(b->data.data, 1, b->data.len, out);
  b->data.len = 0;
  b->lines = 0;
}

/* ---- job / page framing -------------------------------------------- */

typedef struct {
  int resolution;
  int economode;
  int density;
  const char *paper;
} page_params_t;

static const char *pjl_paper(const cups_page_header2_t *h) {
  static const struct {
    const char *cups, *pjl;
    unsigned w, l;
  } sizes[] = {
    {"A4", "A4", 595, 842},
    {"Letter", "LETTER", 612, 792},
    {"Legal", "LEGAL", 612, 1008},
    {"Executive", "EXECUTIVE", 522, 756},
    {"A5", "A5", 420, 595},
    {"ISOB5", "B5", 499, 709},
  };
  const int n = sizeof(sizes) / sizeof(sizes[0]);
  for (int i = 0; i < n; i++)
    if (!strcmp(h->cupsPageSizeName, sizes[i].cups))
      return sizes[i].pjl;
  for (int i = 0; i < n; i++)
    if (abs((int)h->PageSize[0] - (int)sizes[i].w) <= 2 &&
        abs((int)h->PageSize[1] - (int)sizes[i].l) <= 2)
      return sizes[i].pjl;
  return "A4";
}

static void begin_job(FILE *out, const char *name) {
  for (int i = 0; i < 128; i++)
    putc(0, out);
  fprintf(out, "\033%%-12345X@PJL\n");
  fprintf(out, "@PJL JOB NAME=\"%s\"\n", name);
}

static void end_job(FILE *out, const char *name) {
  fprintf(out, "\033%%-12345X@PJL\n");
  fprintf(out, "@PJL EOJ NAME=\"%s\"\n", name);
  fprintf(out, "\033%%-12345X\n");
}

static void write_page_header(FILE *out, const page_params_t *p) {
  fprintf(out, "\033%%-12345X@PJL\n");
  fprintf(out, "@PJL SET RAS1200MODE = FALSE\n");
  fprintf(out, "@PJL SET RESOLUTION = %d\n", p->resolution);
  fprintf(out, "@PJL SET ECONOMODE = %s\n", p->economode ? "ON" : "OFF");
  fprintf(out, "@PJL SET SOURCETRAY = AUTO\n");
  fprintf(out, "@PJL SET MEDIATYPE = PLAIN\n");
  fprintf(out, "@PJL SET DENSITY=%d\n", p->density);
  fprintf(out, "@PJL SET DEVBIASADJUST=%d\n", p->density);
  fprintf(out, "@PJL SET PAPER = %s\n", p->paper);
  fprintf(out, "@PJL SET PAGEPROTECT = AUTO\n");
  fprintf(out, "@PJL SET ORIENTATION = PORTRAIT\n");
  fprintf(out, "@PJL ENTER LANGUAGE = PCL\n");
  fputs("\033E", out);                          /* reset */
  fputs("\033&l1X", out);                       /* 1 copy: CUPS makes copies */
  fprintf(out, "\033&u%dD", p->resolution);     /* unit of measure */
  fprintf(out, "\033*t%dR", p->resolution);     /* raster resolution */
  fputs("\033&l0S", out);                       /* simplex */
}

/* Encode one raster page. The raster covers only the imageable area
 * (as macOS renders it) and is sent unchanged: the printer itself starts
 * printing at its hardware margin, which the PPD's ImageableArea matches. */
static int encode_page(FILE *out, cups_raster_t *ras, const cups_page_header2_t *h) {
  const int bpl = (int)h->cupsBytesPerLine;

  /* Padding bits past the right edge of the raster are cleared. */
  const int used = (int)((h->cupsWidth + 7) / 8) < bpl ? (int)((h->cupsWidth + 7) / 8) : bpl;
  const unsigned char edge_mask =
      h->cupsWidth % 8 ? (unsigned char)(0xFF << (8 - h->cupsWidth % 8)) : 0xFF;

  unsigned char *line = calloc((size_t)bpl, 1);
  unsigned char *ref = calloc((size_t)bpl, 1);
  buf_t enc = {0};
  block_t blk = {{0}, 0};
  int ok = 1;
  if (!line || !ref) {
    fputs("ERROR: " FILTER_NAME ": out of memory\n", stderr);
    exit(1);
  }

  fputs("\033*b1030m", out);
  for (int y = 0; y < (int)h->cupsHeight && !cancelled; y++) {
    if (cupsRasterReadPixels(ras, line, (unsigned)bpl) != (unsigned)bpl) {
      fputs("ERROR: " FILTER_NAME ": raster data ended early\n", stderr);
      ok = 0;
      break;
    }
    line[used - 1] &= edge_mask;
    memset(line + used, 0, (size_t)(bpl - used));

    enc.len = 0;
    if (y % LINES_PER_BLOCK == 0) {
      block_flush(out, &blk);
      encode_line(&enc, line, NULL, bpl);
    } else {
      encode_line(&enc, line, ref, bpl);
      if (blk.data.len + enc.len >= MAX_BLOCK_BYTES) {
        block_flush(out, &blk);
        enc.len = 0;
        encode_line(&enc, line, NULL, bpl);
      }
    }
    buf_append(&blk.data, enc.data, enc.len);
    blk.lines++;

    unsigned char *t = line;
    line = ref;
    ref = t;
  }
  block_flush(out, &blk);
  fputs("1030M\f", out);
  fflush(out);

  free(line);
  free(ref);
  free(enc.data);
  free(blk.data.data);
  return ok;
}

/* "id/user/title" with anything that could break the PJL string removed. */
static void make_job_name(char *dst, size_t size, char *argv[]) {
  dst[0] = 0;
  for (int i = 1; i <= 3; i++) {
    const char *part = argv[i];
    if (!*part)
      continue;
    size_t len = strlen(dst);
    if (len && len + 1 < size) {
      dst[len++] = '/';
      dst[len] = 0;
    }
    for (; *part && len + 1 < size; part++)
      dst[len++] = (*part < 32 || *part > 126 || *part == '"' || *part == '\\') ? ' ' : *part;
    dst[len] = 0;
  }
  if (!dst[0])
    snprintf(dst, size, "%s", FILTER_NAME);
}

int main(int argc, char *argv[]) {
  if (argc != 6 && argc != 7) {
    fputs("Usage: " FILTER_NAME " job-id user title copies options [file]\n", stderr);
    fputs("This is a CUPS filter; it is normally run by the print system.\n", stderr);
    return 1;
  }

  signal(SIGTERM, on_sigterm);
  signal(SIGPIPE, SIG_IGN);

  int fd = 0;
  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
    perror("ERROR: " FILTER_NAME ": unable to open raster file");
    return 1;
  }
  cups_raster_t *ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
  if (!ras) {
    fputs("ERROR: " FILTER_NAME ": cannot read raster data\n", stderr);
    return 1;
  }

  char job_name[80];
  make_job_name(job_name, sizeof(job_name), argv);

  static char obuf[1 << 16];
  setvbuf(stdout, obuf, _IOFBF, sizeof(obuf));

  cups_page_header2_t h;
  page_params_t last = {0};
  int pages = 0, status = 0;
  while (!cancelled && cupsRasterReadHeader2(ras, &h)) {
    if (h.cupsBitsPerPixel != 1 || h.cupsColorSpace != CUPS_CSPACE_K ||
        h.cupsBytesPerLine == 0 || h.cupsBytesPerLine > MAX_LINE_BYTES ||
        h.HWResolution[0] == 0 || h.HWResolution[1] == 0) {
      fprintf(stderr, "ERROR: " FILTER_NAME ": unsupported raster (bpp=%u colorspace=%u "
              "bytes/line=%u); expected 1-bit black\n",
              h.cupsBitsPerPixel, h.cupsColorSpace, h.cupsBytesPerLine);
      status = 1;
      break;
    }

    page_params_t p = {
      .resolution = (int)h.HWResolution[0],
      .economode = h.cupsInteger[10] != 0,
      .density = h.cupsInteger[11] ? (int)h.cupsInteger[11] - 100 : 0,
      .paper = pjl_paper(&h),
    };
    if (pages == 0) {
      fprintf(stderr, "DEBUG: " FILTER_NAME ": %ux%u @ %ux%u dpi, paper %s\n",
              h.cupsWidth, h.cupsHeight, h.HWResolution[0], h.HWResolution[1], p.paper);
      begin_job(stdout, job_name);
    }
    if (pages == 0 || p.resolution != last.resolution || p.economode != last.economode ||
        p.density != last.density || strcmp(p.paper, last.paper) != 0)
      write_page_header(stdout, &p);
    last = p;
    pages++;

    if (!encode_page(stdout, ras, &h))
      status = 1;
    fprintf(stderr, "PAGE: %d 1\n", pages);
  }

  if (pages)
    end_job(stdout, job_name);
  else if (!status)
    fputs("ERROR: " FILTER_NAME ": no pages found\n", stderr);

  cupsRasterClose(ras);
  if (fd)
    close(fd);

  fflush(stdout);
  if (ferror(stdout)) {
    fputs("ERROR: " FILTER_NAME ": could not write print data\n", stderr);
    return 1;
  }
  return pages ? status : 1;
}
