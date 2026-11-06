# stb_image (vendored)

`stb_image.h`: the single-header image loader of Sean Barrett and the stb contributors, **v2.30**, unmodified; the
same file the apps repo's RSS Reader ships (`apps/rss-reader/third_party/stb/`).

| | |
|---|---|
| Upstream | https://github.com/nothings/stb |
| Commit | `013ac3beddff3dbffafd5177e7972067cd2b5083` (2024-05-31; the header says v2.30) |
| Fetched from | `https://raw.githubusercontent.com/nothings/stb/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h` |
| SHA-256 | `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3` (283,010 bytes) |
| Licence | public domain (Unlicense) **or** MIT, at your choice: the text is at the end of the header and in `LICENSES/stb_image-MIT-or-Unlicense.txt` |

Who uses it: only the map engine (`src/map/cp0_map_stb.c`, compiled with `CONFIG_CP0_LVGL_MAP`), with `STBI_ONLY_PNG`,
`STBI_ONLY_JPEG`, `STBI_NO_STDIO`, `STBI_NO_LINEAR`, `STBI_NO_HDR`, `STBI_NO_SIMD`, `STBI_MAX_DIMENSIONS 4096`,
`STB_IMAGE_STATIC` (no `stbi_*` symbol leaves that file) and a per-thread counting allocator with a 24 MiB ceiling.
Its zlib inflater also opens the gzip directories of PMTiles archives. Do not edit the header: fixes go around it in
`cp0_map_stb.c` (as the `wrapv` pragma does). To update: replace the file, update this table, run
`ext_components/cp0_lvgl/tests/run_tests.sh` (the map tests decode and fuzz with ASan + UBSan).
