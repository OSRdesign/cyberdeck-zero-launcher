/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/settings/settings_about_help_model.hpp"
#include "../main/ui/settings/settings_boot_action_policy.hpp"
#include "../main/ui/settings/settings_launcher_model.hpp"

#include <cassert>
#include <string>

namespace {

struct Descriptor
{
    const char *label;
    const char *config_key;
    bool configurable;
    bool always_on;
    LauncherAppOrigin origin;
};

} // namespace

int main()
{
    const Descriptor descriptors[] = {
        {"Hidden", "hidden", false, true, LauncherAppOrigin::Builtin},
        {"Calculator", "calculator", true, false, LauncherAppOrigin::Builtin},
        {"Python", "app_Python", true, false, LauncherAppOrigin::Builtin},
        {"Snake", "app_Game", true, false, LauncherAppOrigin::Builtin},
        {"Settings", "app_Setting", true, false, LauncherAppOrigin::Builtin},
        {"Store", "app_Store", true, false, LauncherAppOrigin::Builtin},
        {"CLI", "app_CLI", true, false, LauncherAppOrigin::Builtin},
        {"ZClaw", "app_desktop_zclaw", true, false, LauncherAppOrigin::Preinstalled},
        {"Downloaded", "app_desktop_downloaded", true, false, LauncherAppOrigin::StoreInstalled},
        {nullptr, "missing-label", true, false, LauncherAppOrigin::Builtin},
        {"Missing key", nullptr, true, false, LauncherAppOrigin::Builtin},
    };
    const auto entries = settings_t12b::launcher::configurable_entries(
        descriptors,
        11,
        [](const Descriptor &descriptor) { return descriptor.always_on; });
    // Settings and CLI are protected even when configurable; a configurable Store (the Pi profile,
    // APPLAUNCH_LAUNCHER_STORE_TOGGLE) is listed like any built-in app.
    assert(entries.size() == 5);
    assert(entries[0].label == "Calculator");
    assert(entries[0].config_key == "calculator");
    assert(!entries[0].enabled);
    assert(entries[1].label == "Python" && entries[1].config_key == "app_Python");
    assert(entries[2].label == "Snake" && entries[2].config_key == "app_Game");
    assert(entries[3].label == "Store" && entries[3].config_key == "app_Store");
    assert(entries[4].label == "ZClaw" && entries[4].config_key == "app_desktop_zclaw");
    assert(!settings_t12b::launcher::is_protected_launcher_app("app_Store"));
    assert(settings_t12b::launcher::is_protected_launcher_app("app_Setting"));
    assert(settings_t12b::launcher::is_protected_launcher_app("app_CLI"));
    // The stock CardputerZero descriptor (not configurable, always on): no toggle, the tile always shows.
    const Descriptor stock_store[] = {{"Store", "app_Store", false, true, LauncherAppOrigin::Builtin}};
    assert(settings_t12b::launcher::configurable_entries(
               stock_store, 1, [](const Descriptor &descriptor) { return descriptor.always_on; })
               .empty());
    // The Store credit row follows the Store toggle only (it exists only where the Store is listed).
    assert(std::string(settings_t12b::launcher::kStoreCredit) == "CardputerZero Store by M5Stack");
    assert(settings_t12b::launcher::store_credit_follows("app_Store"));
    assert(!settings_t12b::launcher::store_credit_follows("app_Python"));
    using settings_t12b::boot_actions::Action;
    using settings_t12b::boot_actions::Operation;
    assert(settings_t12b::boot_actions::presentation(Action::Reboot).confirmation_title == "Reboot?");
    assert(settings_t12b::boot_actions::presentation(Action::Shutdown).confirmation_title == "Shutdown?");
    const std::vector<Operation> factory_plan = {
        Operation::RemoveLauncherSettings,
        Operation::Reboot,
    };
    const std::vector<Operation> oobe_plan = {
        Operation::TouchOobeMarker,
        Operation::Reboot,
    };
    assert(settings_t12b::boot_actions::operation_plan(Action::FactoryReset) == factory_plan);
    assert(settings_t12b::boot_actions::operation_plan(Action::RearmOobe) == oobe_plan);
    assert(settings_t12b::boot_actions::may_continue(Operation::RemoveLauncherSettings, true));
    assert(!settings_t12b::boot_actions::may_continue(Operation::RemoveLauncherSettings, false));
    assert(!settings_t12b::boot_actions::may_continue(Operation::Reboot, true));
    assert(std::string(settings_t12b::boot_actions::process_command(Operation::Reboot)) == "Reboot");
    assert(std::string(settings_t12b::boot_actions::filesystem_alias(Operation::TouchOobeMarker)) ==
           "oobe_marker");

    const auto about = settings_t12b::about_help::about("1.2.3", "2026-08-24", "stable", "abc123");
    const auto credit = settings_t12b::about_help::credit();
    assert(!about.title.empty() && about.lines.size() >= 4);
    for (const auto &line : about.lines) assert(line.rfind("Board:", 0) != 0); // no profile: no row
    const auto about_board =
        settings_t12b::about_help::about("1.2.3", "2026-08-24", "stable", "abc123", "pi3a-luckfox35");
    assert(about_board.lines.size() == about.lines.size() + 1);
    assert(about_board.lines[1] == "Board: pi3a-luckfox35");
    assert(credit.title == "Third-Party Licenses & Credits");
    assert(about.lines.front() == "CyberdeckZero Launcher");
    assert(about.lines.back() == "Not affiliated with M5Stack"); // the disclaimer, short form, last
    assert(credit.lines.size() > 80);
    assert(credit.lines.front() == "CyberdeckZero Launcher");
    // our notice, then the disclaimer, then the credit of what it derives from and of the bundled Store
    assert(credit.lines[6].rfind("CyberdeckZero Launcher is an independent hobby project", 0) == 0);
    assert(credit.lines[6].find("not affiliated with, sponsored or endorsed by M5Stack") != std::string::npos);
    assert(credit.lines[8] == "Derived from");
    assert(credit.lines[9] == "The CardputerZero launcher by M5Stack Technology CO LTD");
    assert(credit.lines[13] == "Bundled");
    assert(credit.lines[14] == "CardputerZero Store by M5Stack, unchanged");
    assert(credit.lines[18] == "Upstream");
    assert(credit.lines[19].find("Kickstarter backers") != std::string::npos);
    for (const auto &line : credit.lines) {
        assert(line.find("CardputerZero is built with") == std::string::npos); // upstream's product sentence
        assert(line.find("Proxmark") == std::string::npos);                    // upstream OS image's list
    }
    assert(credit.lines.back() == "CC0-1.0");
}
