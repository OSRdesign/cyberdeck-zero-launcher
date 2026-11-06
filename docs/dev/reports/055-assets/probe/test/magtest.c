/* SPDX-License-Identifier: MIT */
/* P0: dump one tile magnified 1.5x (fast path) and 2x nearest as PPM, to eyeball the probe's resamplers. */
#define main p0probe_main
#include "../p0probe.c"
#undef main
static void ppm565(const char *f, const uint16_t *d, int w, int h)
{
    FILE *o = fopen(f, "wb"); fprintf(o, "P6 %d %d 255\n", w, h);
    for (int i = 0; i < w * h; i++) { uint16_t c = d[i]; fputc((c >> 8) & 0xf8, o); fputc((c >> 3) & 0xfc, o); fputc((c << 3) & 0xf8, o); }
    fclose(o);
}
int main(int argc, char **argv)
{
    pmt_t p; pmt_open(&p, argv[1], 0);
    uint8_t *b = malloc(4 << 20); long n = pmt_get(&p, 14, 8292, 5684, b, 4 << 20);
    int w, h, c; uint8_t *rgb = stbi_load_from_memory(b, (int)n, &w, &h, &c, 3);
    uint16_t *o = malloc(1024 * 1024 * 2), *s = malloc(512 * 512 * 2);
    mag15_rgb_565(rgb, w, h, o); ppm565(argv[2], o, w * 3 / 2, h * 3 / 2);
    magbil_rgb_565(rgb, w, h, o, 384, 384); ppm565(argv[3], o, 384, 384);
    (void)argc; return 0;
}
