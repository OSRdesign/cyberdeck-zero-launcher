/*
 * SPDX-License-Identifier: MIT
 *
 * The board's facts for board.state (report 035): what the launcher already works out for its own Settings
 * (cp0_capabilities.h, the board profile in the environment) gathered into one cp0_app_board_t. No LVGL, no service.
 * collect() may run programs (the sound-server probe: up to 2 x CP0_CAPS_SERVER_TIMEOUT_MS), so it belongs on a
 * worker thread.
 */
#pragma once

#include "cp0_app_contract.h"
#include "cp0_capabilities.h"

#include <string>

namespace board_facts {

/* Everything but pid and gen. `launcher_version` as the About page shows it. ask_server: also look for a sound
 * server's sink (pactl) when there is no local sound card; false keeps the last answer the caller has (sound then
 * reflects the card only). */
void collect(cp0_app_board_t &out, const char *launcher_version, bool ask_server);

/* The verbs launcher.sock answers on a board with these facts, comma separated: hello, caps, toast and beep always
 * (beep answers `err nosound` where there is no sound), volume with sound, brightness with a backlight, dim on a
 * battery board with brightness levels. The socket server's `hello` reply and board.state `requests=` use it. */
std::string requests_for(const cp0_app_board_t &board);

/* The kind of sink listing (`pactl list short sinks`): "bluetooth" when a usable sink is named bluez_output.* or
 * bluez_sink.* (guess = 1: it is the name only), else "server". "" when no usable sink (auto_null and HDMI ones do not
 * count). */
std::string server_sink_kind(const char *short_sinks, int &guess);

/* MemTotal of /proc/meminfo text in MB, -1 when not found. */
int meminfo_mb(const char *text);

} // namespace board_facts
