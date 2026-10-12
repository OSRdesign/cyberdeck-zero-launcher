/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > System > Quit to desktop (desktop boards; the rules are in settings_session_model.hpp).
 *
 * The entry is built like Reboot and Shutdown: a confirmation page with Yes / No. Yes asks the launcher to exit
 * (launcher_session::request_quit()); applaunch-desktop.service then gives the screen back to the desktop.
 */
#pragma once

#include "settings_tree_types.hpp"

namespace settings_session {

/* Appends "Quit to desktop" (Yes / No) to the System section when launcher_session::quit_to_desktop_shown() says so
 * for this build and this process; otherwise appends nothing. confirmation_factory: the confirmation page factory of
 * the Reboot / Shutdown entries. */
void append_quit_to_desktop(Tree &tree, const NodeIter &system, const SettingPageFactory &confirmation_factory);

} // namespace settings_session
