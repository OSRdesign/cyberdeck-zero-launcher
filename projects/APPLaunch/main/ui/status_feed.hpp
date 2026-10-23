/*
 * The launcher's status feed for apps (report 032): Wi-Fi, Ethernet, Bluetooth and battery as the home grid shows them,
 * published as $XDG_RUNTIME_DIR/applaunch/status.state (cp0_status_state.h) so the status strip of a fullscreen app can
 * show the same values. Written from a thread of its own (the legacy fullscreen path turns the LVGL timers off), only
 * when a value changes. Device builds only; elsewhere these calls do nothing.
 */
#pragma once

#include "cp0_app_contract.h"

namespace status_feed {

/* Start / stop the publisher thread. After the Wi-Fi and Bluetooth services exist; stop() before they are torn down.
 * start() twice, or stop() without start(), does nothing. */
void start();
void stop();

/* Next feed text state from a sample: gen and pid are not compared. True when the file must be rewritten. */
bool differs(const cp0_app_status_t &a, const cp0_app_status_t &b);

} // namespace status_feed
