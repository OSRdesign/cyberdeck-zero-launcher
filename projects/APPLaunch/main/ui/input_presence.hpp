/*
 * Keyboard presence in the launcher (F1 phase 0): the launcher reads every real keyboard, USB or Bluetooth,
 * and publishes the input policy file for apps ($XDG_RUNTIME_DIR/applaunch/input.state, see
 * cp0_input_state.h). Device builds only; elsewhere these calls do nothing.
 *
 * APPLAUNCH_KEYBOARD_SCAN=0 in the service environment turns the multi-keyboard reading off (the launcher
 * then reads LV_LINUX_KEYBOARD_DEVICE only, as before 0.5.0); the policy file is published either way.
 */
#pragma once

#include <cstdint>

namespace input_presence {

/* What the on-screen keyboard policy needs (task 014 P2b, cp0_ui_osk.h): the real keyboards present now and when
 * keyboards went away, in monotonic seconds (0 = never since the launcher started). A Bluetooth keyboard (bus 0x0005)
 * that went away is asleep for the policy (counts as present for N minutes). known = false when there is no presence
 * watcher (not a device build, or it could not start): no keyboard is assumed. */
struct KeyboardSnapshot {
    bool known = false;
    unsigned keyboards = 0;
    std::int64_t now_s = 0;
    std::int64_t any_gone_s = 0; /* the last time the keyboard count dropped to 0 */
    std::int64_t bt_gone_s = 0;  /* the last time the Bluetooth keyboard count dropped to 0 */
};

/* Any thread. */
KeyboardSnapshot keyboards();

/* Before cp0_lvgl_run(): lets the keyboard thread read every keyboard (unless APPLAUNCH_KEYBOARD_SCAN=0). */
void configure();

/* Start / stop publishing the policy file from the presence watcher. */
void start();
void stop();

} // namespace input_presence
