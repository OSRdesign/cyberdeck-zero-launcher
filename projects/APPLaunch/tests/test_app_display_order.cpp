/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/app_display_order.hpp"

#include <array>
#include <cassert>
#include <string_view>

int main()
{
    constexpr std::array<std::string_view, 20> expected = {
        "Settings", "CLI", "Python", "ZClaw", "SSH", "Files",
        "Camera", "Rec", "Music", "Piano", "Compass", "IR Remote", "IR Chat",
        "Calculator", "Keyboard Guide", "FactoryTest", "GPS", "LoRa",
        "NFC", "SubG Chat",
    };

    for (std::size_t index = 0; index < expected.size(); ++index)
        assert(launcher_builtin_app_display_order(expected[index]) == static_cast<int>(index));

    // Existing images may use the hardware-prefixed SubG desktop name.
    assert(launcher_builtin_app_display_order("Cap-CC1101-SubG-Chat") == 19);
    assert(launcher_builtin_app_display_order("Vim") == -1);

    assert(!launcher_builtin_desktop_app_is_managed("Settings"));
    assert(!launcher_builtin_desktop_app_is_managed("Store")); // no longer a built-in: not in the order list
    assert(!launcher_builtin_desktop_app_is_managed("CLI"));
    for (std::size_t index = 2; index < expected.size(); ++index)
        assert(launcher_builtin_desktop_app_is_managed(expected[index]));
    assert(launcher_builtin_desktop_app_is_managed("Cap-CC1101-SubG-Chat"));
    assert(!launcher_builtin_desktop_app_is_managed("Vim"));

    assert(launcher_builtin_desktop_filename_is_managed("files.desktop"));
    assert(launcher_builtin_desktop_filename_is_managed("camera_app.desktop"));
    assert(launcher_builtin_desktop_filename_is_managed("recorder.desktop"));
    assert(launcher_builtin_desktop_filename_is_managed("cap-lora-1262.desktop"));
    assert(launcher_builtin_desktop_filename_is_managed("cap-cc1101-nfc.desktop"));
    assert(launcher_builtin_desktop_filename_is_managed("cap-cc1101-subg-chat.desktop"));
    assert(!launcher_builtin_desktop_filename_is_managed("downloaded.desktop"));
    return 0;
}
