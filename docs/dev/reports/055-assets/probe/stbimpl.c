/* SPDX-License-Identifier: MIT
 * P0 spike: stb_image 2.30 built like RSS Reader (src/ui/image_codecs.c): PNG + JPEG only, no stdio, no float/HDR,
 * no SIMD (unless P0_STB_NEON: then STBI_NEON on arm64, to see what NEON would buy). */
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#ifdef P0_STB_NEON
#define STBI_NEON
#else
#define STBI_NO_SIMD
#endif
#define STBI_MAX_DIMENSIONS 16384
#ifdef P0_STB_STATIC
#define STB_IMAGE_STATIC
#endif
#define STB_IMAGE_IMPLEMENTATION
#pragma GCC optimize("wrapv")
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../third_party/stb/stb_image.h"

#ifdef P0_STB_STATIC
/* with STB_IMAGE_STATIC the library's functions are file-local: export only what the probe calls */
unsigned char *p0_stbi_load_from_memory(const unsigned char *b, int n, int *w, int *h, int *c, int want)
{ return stbi_load_from_memory(b, n, w, h, c, want); }
void p0_stbi_image_free(void *p) { stbi_image_free(p); }
int p0_stbi_zlib_noheader(char *o, int ol, const char *i, int il) { return stbi_zlib_decode_noheader_buffer(o, ol, i, il); }
#endif
