/*
 * SPDX-License-Identifier: MIT
 *
 * Which Settings rows our boards show for their hardware (see settings_capabilities_model.hpp).
 */
#include "settings_capabilities_model.hpp"

namespace settings_caps {
namespace {

RowShow show(cp0_cap_state_t state)
{
    switch (state) {
    case CP0_CAP_PRESENT: return RowShow::Row;
    case CP0_CAP_NOT_SET_UP: return RowShow::Info;
    case CP0_CAP_ABSENT: break;
    }
    return RowShow::Hidden;
}

} // namespace

std::string info_label(const std::string &row, const std::string &note)
{
    return note.empty() ? row : row + ": " + note;
}

Rows rows_for(const cp0_caps_t &caps)
{
    Rows rows;
    rows.battery = show(caps.battery);
    rows.brightness = show(caps.backlight);
    rows.brightness_onoff = rows.brightness == RowShow::Row && caps.bl.kind == CP0_BL_ONOFF;
    rows.speaker = show(caps.sound);
    if (rows.battery == RowShow::Info) rows.battery_info = info_label("Battery", caps.note_battery);
    if (rows.brightness == RowShow::Info) rows.brightness_info = info_label("Brightness", caps.note_backlight);
    if (rows.speaker == RowShow::Info) rows.speaker_info = info_label("Speaker", caps.note_sound);
    return rows;
}

} // namespace settings_caps
