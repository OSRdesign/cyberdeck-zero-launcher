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
/* Physical size the fake driver reports in var.width / var.height (mm, buffer orientation; 0 = unknown). */
void harness_fb_set_mm(int mm_w, int mm_h);
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
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace harness {

/* A network the fake Wi-Fi scan finds (task 014 P3b scenes). No real network names or passwords. */
struct FixtureNetwork {
    std::string ssid;
    int signal;           /* 0..100 */
    std::string security; /* "" = open */
    bool saved;
};

/* A Bluetooth device the fake BlueZ knows (task 014 P3c scenes). Fake names, locally administered addresses. */
struct FixtureBtDevice {
    std::string address;
    std::string name;    /* "" = no name (hidden by Named Only) */
    int rssi;
    bool paired;
    bool connected;
    bool nearby;         /* found by a discovery (paired devices are known anyway) */
    std::string pairing; /* what Pair asks the agent: "" (nothing), "confirm", "pin", "passkey", "display" */
};

/* Fixture state the stubbed services answer from (set by scene commands). */
struct Fixtures {
    std::string resource_root;         /* <repo>/projects/APPLaunch/APPLaunch (= /usr/share/APPLaunch) */
    std::string fixture_root;          /* tests/render/fixtures */
    bool wifi_connected = true;
    int wifi_signal = 70;              /* 0..100 */
    std::string wifi_ssid = "deck-lab";
    std::string wifi_ip = "192.168.1.42";
    bool wifi_radio = true;
    /* the scan: the connected network (deck-lab, saved), a saved one, open ones, a new secured one, a weak one */
    std::vector<FixtureNetwork> wifi_networks = {
        {"deck-lab", 70, "WPA2", true},     {"Workshop", 84, "WPA2", true}, {"Cafe Libre", 62, "", false},
        {"Neighbour-5G", 55, "WPA2", false}, {"Garage", 12, "WPA2", false},  {"Guest", 33, "", false},
    };
    std::string wifi_password = "harness-pass"; /* a new secured network accepts only this (fake) password */
    std::string wifi_hidden_ssid = "Hidden-Lab"; /* Join Hidden Network finds only this one */
    bool bt_powered = false;
    bool bt_connected = false;         /* the status strip's connected icon (BtConnectedList) */
    std::string bt_alias = "zero7";    /* the adapter's name (BtStatus, BtAlias) */
    bool bt_discovered = false;        /* a discovery ran: BtList shows the nearby devices too */
    /* paired: a keyboard (connected), headphones with a long name, a car kit; nearby: a phone (numeric comparison),
     * a keyboard (passkey shown here, typed there: "btkeytyped" completes it), an old headset (PIN), a tablet (passkey
     * shown there, typed here), two unnamed ones (Named Only hides them), a speaker with a very long name (no prompt) */
    std::vector<FixtureBtDevice> bt_devices = {
        {"02:5A:00:00:00:01", "M4 keyboard", -52, true, true, true, ""},
        {"02:5A:00:00:00:02", "Studio headphones with a rather long name", -61, true, false, true, ""},
        {"02:5A:00:00:00:03", "Car kit", -77, true, false, false, ""},
        {"02:5A:00:00:00:10", "Field phone", -48, false, false, true, "confirm"},
        {"02:5A:00:00:00:11", "Pocket keyboard", -55, false, false, true, "display"},
        {"02:5A:00:00:00:12", "Old headset", -66, false, false, true, "pin"},
        {"02:5A:00:00:00:13", "Tablet", -70, false, false, true, "passkey"},
        {"02:5A:00:00:00:14", "", -81, false, false, true, ""},
        {"02:5A:00:00:00:15", "02-5A-00-00-00-15", -84, false, false, true, ""},
        {"02:5A:00:00:00:16", "Garden speaker with an extremely long Bluetooth name", -73, false, false, true, ""},
    };
    std::string bt_confirm_code = "482913"; /* the phone's numeric comparison */
    std::string bt_display_code = "731904"; /* the passkey shown for the keyboard */
    std::string bt_pin = "0000";            /* the only PIN the old headset takes (fake) */
    std::string bt_passkey = "246810";      /* the passkey the tablet shows (fake) */
    int backlight_max = 255;
    int backlight = 200;
    int volume = 50;                   /* the audio service's VolumeRead / VolumeWrite (Settings > Speaker) */
    unsigned keyboards = 0;            /* physical keyboards present (on-screen keyboard policy) */
    /* Network Time (NtpGet answers 1 when on, 0 off); scene command "ntp on|off"; the Network Time toggle sets it
     * (cp0_timedate_set_ntp, a worker thread) */
    std::atomic<bool> ntp_on{false};
    /* Date & Time > Time zone: the zone in use (cp0_timedate_get_timezone; scene command "tz ZONE", "tz none": it
     * cannot be read), what setting one answers ("tzset ok|fail|denied": 0 and the zone changes, -1, -13 PolicyKit
     * refused), the list (cp0_timedate_list_timezones: fixtures/timezones.txt, a real `timedatectl list-timezones`;
     * "tzlist off": none, as without timedated and zoneinfo files). A zone set becomes the process's TZ when the page
     * reports the change (cp0_timedate_zone_changed), so the clocks follow it. */
    std::mutex tz_mutex;
    std::string timezone = "Etc/UTC";
    int timezone_set_result = 0;
    bool timezone_list = true;
    bool time_settable = false;        /* cp0_timedate_set_time (Date & Time > Set Manually > Save?) succeeds; "timeset ok|fail" */
    std::atomic<int64_t> time_set_epoch{-1}; /* the time it was last set to (LocalTime answers it from then on), -1: none */
};

Fixtures &fixtures();
void install_services();
/* Bluetooth scene commands: a BlueZ agent request as the backend sends it (reply logged), and the remote keyboard
 * having typed the passkey shown for it (a pending "display" pairing completes). */
void bt_agent_request(const std::string &method, const std::string &address, const std::string &value);
void bt_key_typed();
/* The fake PTY (task 014 P4b: the terminal pages, cp0_signal_pty_api): no process. What the scene prints is what the
 * terminal reads next ("\n" = CR LF, "\e" = Esc); what the terminal writes is echoed as a cooked tty and a shell would
 * (text, Backspace, Enter = a new prompt, ^C = "^C" and a new prompt, ^D = "exit" and the end; escape sequences, Tab:
 * nothing) and logged with every Open / Resize / Close. pty_exit(): the child has exited (Read reports the end,
 * CheckChild 1). */
void pty_output(const std::string &text);
void pty_set_prompt(const std::string &prompt);
void pty_exit();
std::vector<std::string> pty_log();
void config_set(const std::string &key, const std::string &value);
std::string resolve_resource(const std::string &name);

} // namespace harness
#endif
