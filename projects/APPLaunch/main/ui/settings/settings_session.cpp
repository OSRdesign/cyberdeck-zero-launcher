/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > System > Quit to desktop (see settings_session.hpp).
 */

#include "settings_session.hpp"

#include "settings_hw_profile.hpp"
#include "settings_native_mode.hpp"
#include "settings_session_model.hpp"

#include <cstdlib>

namespace settings_session {

void append_quit_to_desktop(Tree &tree, const NodeIter &system, const SettingPageFactory &confirmation_factory)
{
    if (!confirmation_factory) return;
    if (!launcher_session::quit_to_desktop_shown(APPLAUNCH_SETTINGS_QUIT_TO_DESKTOP != 0, settings_ui::native_selected(),
                                                 std::getenv("APPLAUNCH_SESSION")))
        return;
    const NodeIter quit = tree.append_child(system, SettingEntry{"Quit to desktop", confirmation_factory});
    tree.append_child(quit, SettingEntry{"Yes",
                                         SettingApiCallBackFunc{[](int command, void *) {
                                             if (command == SettingApiActivate) launcher_session::request_quit();
                                         }},
                                         false});
    tree.append_child(quit, SettingEntry{"No", SettingApiCallBackFunc{[](int, void *) {}}, false});
}

} // namespace settings_session
