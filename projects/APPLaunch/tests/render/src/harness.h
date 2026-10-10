/* SPDX-License-Identifier: MIT
 *
 * Render harness: shared declarations of the harness-only pieces (fake devices, fake clock,
 * service fixtures). Nothing here is linked into the launcher.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Paths the display manager opens (APPLAUNCH_FB / APPLAUNCH_TOUCH_DEV point at them). */
#define HARNESS_FB_PATH "/harness/fb"
#define HARNESS_TOUCH_PATH "/harness/touch"

/* ---- fake framebuffer + touch device (harness_fb.c) ----
 * cp0_lvgl_dpi_scaled.c is compiled unchanged; its references to open/ioctl/mmap are renamed to
 * harness_fb_open/ioctl/mmap with objcopy, so it maps this memory instead of /dev/fbN. */
void harness_fb_configure(int pw, int ph, int bpp);
uint8_t *harness_fb_memory(size_t *size, int *stride);
/* Raw touch report in buffer axes (APPLAUNCH_TOUCH_ORIENT=buffer, range 0..pw-1 / 0..ph-1). */
void harness_touch_report(int pressed, int raw_x, int raw_y);

int harness_fb_open(const char *path, int flags, ...);
int harness_fb_ioctl(int fd, unsigned long request, ...);
void *harness_fb_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);

/* ---- fake keyboard backend (harness_keyboard.c): replaces the libinput/xkb reader thread ---- */
void harness_keyboard_init(void);
int harness_keyboard_inject_text(uint32_t key_code, int key_state, const char *utf8);

/* ---- fake wall clock (harness_services.cpp) ---- */
void harness_set_wall_clock(int64_t epoch_seconds);
int64_t harness_wall_clock(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <string>
#include <vector>

namespace harness {

/* Fixture state the stubbed services answer from (set by scene commands). */
struct Fixtures {
    std::string resource_root;         /* <repo>/projects/APPLaunch/APPLaunch (= /usr/share/APPLaunch) */
    std::string fixture_root;          /* tests/render/fixtures */
    bool wifi_connected = true;
    int wifi_signal = 70;              /* 0..100 */
    std::string wifi_ssid = "deck-lab";
    std::string wifi_ip = "192.168.1.42";
    bool wifi_radio = true;
    bool bt_powered = false;
    bool bt_connected = false;
    int backlight_max = 255;
    int backlight = 200;
};

Fixtures &fixtures();
void install_services();
void config_set(const std::string &key, const std::string &value);
std::string resolve_resource(const std::string &name);

} // namespace harness
#endif
