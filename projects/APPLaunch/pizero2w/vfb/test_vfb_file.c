/*
 * Host helper of test_vfb.sh: writes a virtual framebuffer file the way the launcher does (native_ui.cpp
 * create_vfb_file), and checks what fbtest drew into it through the shim.
 *
 *   test_vfb_file create2|create1|createbad <path>   VFB2 (64 KiB header), VFB1 (4096) or a bad magic
 *   test_vfb_file check <path> <frames>               fbtest's pages are at the file's data offset
 * Exit 0 = ok, 1 = failed (the reason on stderr).
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "applaunch_vfb.h"

static int fail(const char *what)
{
    fprintf(stderr, "test_vfb_file: FAIL %s\n", what);
    return 1;
}

static int create(const char *path, int kind)
{
    const uint32_t w = APPLAUNCH_VFB_DEFAULT_WIDTH, h = APPLAUNCH_VFB_DEFAULT_HEIGHT, stride = w * 4;
    const size_t header = kind == 1 ? APPLAUNCH_VFB_HEADER_BYTES_V1 : APPLAUNCH_VFB_HEADER_BYTES;
    const size_t size = header + (size_t)stride * h * 2;
    const int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0) return fail("create");
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) return fail("mmap");
    memset(map, 0, size);
    applaunch_vfb_header_t *hd = map;
    hd->width = w;
    hd->height = h;
    hd->bpp = 32;
    hd->stride = stride;
    hd->yres_virtual = h;
    if (kind == 2) {
        hd->version = APPLAUNCH_VFB_VERSION;
        hd->header_bytes = APPLAUNCH_VFB_HEADER_BYTES;
        hd->host_w = 640;
        hd->host_h = 480;
        hd->win_x = 0;
        hd->win_y = 0;
        hd->win_w = 640;
        hd->win_h = 340;
        hd->scale = 2;
        hd->ppmm_x100 = 1130;
        hd->canvas_mode = APPLAUNCH_VFB_CANVAS_NATIVE;
        hd->gen = 3;
    }
    hd->magic = kind == 2 ? APPLAUNCH_VFB_MAGIC : kind == 1 ? APPLAUNCH_VFB_MAGIC_V1 : 0x12345678u;
    munmap(map, size);
    close(fd);
    return 0;
}

static uint32_t pixel(const uint8_t *base, size_t data, uint32_t stride, uint32_t x, uint32_t y)
{
    uint32_t value;
    memcpy(&value, base + data + (size_t)y * stride + (size_t)x * 4, 4);
    return value & 0xFFFFFFu;
}

static int check(const char *path, int frames)
{
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return fail("open");
    const off_t size = lseek(fd, 0, SEEK_END);
    const uint8_t *base = mmap(NULL, (size_t)size, PROT_READ, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) return fail("mmap");
    const applaunch_vfb_header_t *hd = (const applaunch_vfb_header_t *)base;
    const size_t data = hd->magic == APPLAUNCH_VFB_MAGIC ? hd->header_bytes : APPLAUNCH_VFB_HEADER_BYTES_V1;
    const uint32_t h = hd->height, stride = hd->stride;
    if (hd->attached != 1) return fail("attached");
    if (hd->yres_virtual != h * 2) return fail("page flip: yres_virtual");
    if ((int)hd->frame != frames) return fail("page flip: frame count");
    if (hd->yoffset != (uint32_t)((frames - 1) & 1) * h) return fail("page flip: yoffset");
    if (hd->magic == APPLAUNCH_VFB_MAGIC &&
        (hd->version != APPLAUNCH_VFB_VERSION || hd->host_w != 640 || hd->win_h != 340 || hd->scale != 2 ||
         hd->ppmm_x100 != 1130 || hd->gen != 3 || hd->kb_request != 0 || hd->kb_inset != 0))
        return fail("VFB2 fields");
    /* both pages: white border at (0,0), first colour bar (red) at (20,1) */
    for (uint32_t page = 0; page < 2; page++) {
        if (pixel(base, data, stride, 0, page * h) != 0xFFFFFFu) return fail("border pixel at the data offset");
        if (pixel(base, data, stride, 20, page * h + 1) != 0xFF0000u) return fail("bar pixel at the data offset");
    }
    /* nothing drawn between the header struct and the data offset (VFB2: the old +4096 place stays empty) */
    for (size_t i = sizeof(applaunch_vfb_header_t); i < data; i++)
        if (base[i] != 0) return fail("bytes written inside the header area");
    printf("test_vfb_file: ok, magic %s, pixel data at +%zu, %d frames, yoffset %u\n",
           hd->magic == APPLAUNCH_VFB_MAGIC ? "VFB2" : "VFB1", data, frames, hd->yoffset);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "create2") == 0) return create(argv[2], 2);
    if (argc == 3 && strcmp(argv[1], "create1") == 0) return create(argv[2], 1);
    if (argc == 3 && strcmp(argv[1], "createbad") == 0) return create(argv[2], 0);
    if (argc == 4 && strcmp(argv[1], "check") == 0) return check(argv[2], atoi(argv[3]));
    fprintf(stderr, "usage: test_vfb_file create2|create1|createbad <path> | check <path> <frames>\n");
    return 2;
}
