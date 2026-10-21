/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Date & Time > Time zone (native Settings only): the row shows the zone in use with a chevron and opens a
 * two-level picker on the host's scaffold, a Roller like the rest of Settings (rules: settings_timezone_model.hpp).
 *
 *   level 1  the regions (a chevron each; the region of the zone in use shows its city in green), then UTC (a check
 *            mark when in use): a region opens level 2, UTC applies at once
 *   level 2  the region's zones, the one in use checked and centred: Enter / a tap applies it at once
 * Esc (a key, Left, the header's Esc) goes up one level, then back to Date & Time. A change goes to timedated over
 * D-Bus on a worker (cp0_timedate_set_timezone; the PolicyKit rule 51-launcher-time-power.rules allows the sudo
 * group); when PolicyKit refuses, the sudo admin command TimezoneSet runs `timedatectl set-timezone ZONE` after the
 * shared sudo password prompt (a build without the D-Bus time path, APPLAUNCH_SETTINGS_TIME_NO_SUDO 0, goes there
 * directly). Done: the process follows the new zone at once (cp0_timedate_zone_changed: the status clock, Date &
 * Time > Info, Set Manually) and the picker closes on Date & Time, whose row shows the new zone. Not done: the reason
 * in the status line (red), the picker stays. Network Time is neither read nor changed here, and the Network Time
 * toggle never touches the zone.
 *
 * LVGL thread only.
 */
#pragma once

#include "settings_native_panel.hpp"
#include "settings_tree_types.hpp"

#include <functional>
#include <memory>
#include <string>

namespace DComponens {
class LvglComponensBase;
}

/* The tree entry's factory: identifies the native page (settings_native_page()); there is no legacy page (nullptr). */
std::unique_ptr<DComponens::LvglComponensBase> settings_timezone_page_factory(lv_obj_t *parent, const NodeIter &node,
                                                                              std::function<void()> back);

std::unique_ptr<SettingsNativePanel> settings_timezone_panel_create(const SettingsNativePanelContext &context);

/* The Date & Time row's value: the zone in use ("Europe/Paris"), "Unknown" when it cannot be read. Quick (a readlink). */
std::string settings_timezone_row_value();

namespace settings_timezone {
/* Date & Time > Time zone (after Set Manually); native Settings only */
void append_entry(Tree &tree, const NodeIter &date_time);
} // namespace settings_timezone
