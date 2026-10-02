/*
 * rasterdump - print CUPS raster page headers and write each page as PBM/PGM.
 * Test helper only. Usage: rasterdump file.ras outprefix
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <cups/raster.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "usage: rasterdump file.ras outprefix\n");
    return 1;
  }
  int fd = open(argv[1], O_RDONLY);
  if (fd < 0) { perror(argv[1]); return 1; }
  cups_raster_t *ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
  cups_page_header2_t h;
  int page = 0;
  while (cupsRasterReadHeader2(ras, &h)) {
    page++;
    printf("page %d: %ux%u bpl=%u bpc=%u bpp=%u cspace=%u order=%u res=%ux%u "
           "PageSize=%ux%u name='%s' media='%s' copies=%u int10=%u int11=%u\n",
           page, h.cupsWidth, h.cupsHeight, h.cupsBytesPerLine, h.cupsBitsPerColor,
           h.cupsBitsPerPixel, h.cupsColorSpace, h.cupsColorOrder,
           h.HWResolution[0], h.HWResolution[1], h.PageSize[0], h.PageSize[1],
           h.cupsPageSizeName, h.MediaType, h.NumCopies,
           h.cupsInteger[10], h.cupsInteger[11]);
    char name[1024];
    int pbm = h.cupsBitsPerPixel == 1;
    snprintf(name, sizeof(name), "%s-%d.%s", argv[2], page, pbm ? "pbm" : "pgm");
    FILE *f = fopen(name, "wb");
    if (pbm)
      fprintf(f, "P4 %u %u\n", h.cupsWidth, h.cupsHeight);
    else
      fprintf(f, "P5 %u %u 255\n", h.cupsWidth, h.cupsHeight);
    unsigned char *buf = malloc(h.cupsBytesPerLine);
    for (unsigned y = 0; y < h.cupsHeight; y++) {
      cupsRasterReadPixels(ras, buf, h.cupsBytesPerLine);
      if (!pbm && h.cupsColorSpace == CUPS_CSPACE_K)   /* K: 0 = white */
        for (unsigned x = 0; x < h.cupsBytesPerLine; x++) buf[x] = 255 - buf[x];
      fwrite(buf, 1, pbm ? (h.cupsWidth + 7) / 8 : h.cupsWidth, f);
    }
    free(buf);
    fclose(f);
  }
  cupsRasterClose(ras);
  return page ? 0 : 1;
}
