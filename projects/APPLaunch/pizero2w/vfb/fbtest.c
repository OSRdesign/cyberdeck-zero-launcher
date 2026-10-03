/* Minimal stock-style framebuffer app: opens /dev/fb0, draws colour bars and a moving square with
 * double buffering (FBIOPUT_VSCREENINFO + FBIOPAN_DISPLAY), exits after ~25 s. */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void)
{
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("open /dev/fb0"); return 1; }
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) || ioctl(fd, FBIOGET_FSCREENINFO, &fix)) { perror("ioctl"); return 1; }
    fprintf(stderr, "fb: %ux%u virt %ux%u bpp=%u stride=%u smem=%u\n", var.xres, var.yres, var.xres_virtual,
            var.yres_virtual, var.bits_per_pixel, fix.line_length, fix.smem_len);
    if (var.bits_per_pixel != 32) { fprintf(stderr, "need 32bpp\n"); return 1; }

    var.yres_virtual = var.yres * 2; /* ask for two pages */
    ioctl(fd, FBIOPUT_VSCREENINFO, &var);
    ioctl(fd, FBIOGET_FSCREENINFO, &fix);
    const unsigned w = var.xres, h = var.yres, stride = fix.line_length / 4;
    uint32_t *fb = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); return 1; }

    for (int frame = 0; frame < 25 * 30; frame++) {
        const unsigned page = frame & 1;
        uint32_t *buf = fb + (size_t)page * h * stride;
        /* colour bars + a thin 1-pixel border so the 2x scaling is easy to see */
        static const uint32_t bars[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0xFFFFFF, 0x303030};
        for (unsigned y = 0; y < h; y++)
            for (unsigned x = 0; x < w; x++) {
                uint32_t c = y < h / 2 ? bars[x * 8 / w] : ((x / 10 + y / 10) & 1 ? 0x202020 : 0x101010);
                if (x == 0 || y == 0 || x == w - 1 || y == h - 1) c = 0xFFFFFF;
                buf[y * stride + x] = c;
            }
        /* moving square */
        const unsigned sx = (frame * 3) % (w - 30), sy = h / 2 + 15 + ((frame / 2) % (h / 2 - 50));
        for (unsigned y = sy; y < sy + 20 && y < h; y++)
            for (unsigned x = sx; x < sx + 20; x++) buf[y * stride + x] = 0xFF8000;

        var.yoffset = page * h;
        ioctl(fd, FBIOPAN_DISPLAY, &var);
        usleep(33000);
    }
    return 0;
}
