/*
 * SPDX-License-Identifier: MIT
 *
 * Which Settings rows our boards show for their hardware (report 030, decisions C1, C2, C4, C8): Battery, Screen >
 * Brightness and Speaker follow the board's capabilities (cp0_capabilities.h), never a build switch or a board name.
 *   - present: the row (Brightness: On / Off when the backlight is on/off only, else levels; Speaker: the volume);
 *   - not set up (a board profile hint names the hardware): an Info row "<Row>: <reason>" instead;
 *   - absent: no row at all (no rows that do nothing).
 * Pure: no LVGL, no service.
 */
#pragma once

#include "cp0_capabilities.h"

#include <string>

namespace settings_caps {

enum class RowShow {
    Hidden, /* absent: no row */
    Info,   /* not set up: a read-only row with the reason */
    Row,    /* present: the row itself */
};

struct Rows {
    RowShow battery = RowShow::Hidden;
    RowShow brightness = RowShow::Hidden;
    bool brightness_onoff = false; /* Brightness offers On / Off instead of levels */
    RowShow speaker = RowShow::Hidden;
    std::string battery_info;    /* the Info rows' labels: "Battery: ...", "Brightness: ...", "Speaker: ..." */
    std::string brightness_info;
    std::string speaker_info;
};

/* The rows for these capabilities. */
Rows rows_for(const cp0_caps_t &caps);

/* An Info row's label: "Speaker: pair XWF-M18-M28-M38 in Bluetooth" ("Speaker" alone without a reason). */
std::string info_label(const std::string &row, const std::string &note);

} // namespace settings_caps
