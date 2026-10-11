/*
 * SPDX-License-Identifier: MIT
 *
 * The compat path of the native Settings host (task 014 decision D3, development builds only): a page the host has
 * no widget for yet (Wi-Fi networks, Bluetooth pairing / scan / alias, Apps, the info pages...) opens exactly as the
 * legacy Settings shows it: built by its own page factory in a 320x150 container under the 20 px top bar, on the
 * compat display, scaled into the window above the stock-app toolbar, touch turned into keys (LIST mode). When the
 * page goes back (its own Esc / Left, or the toolbar's Esc) the native Settings screen comes back.
 *
 * LVGL thread only.
 */
#pragma once

#include "settings_tree_types.hpp"

#include <functional>
#include <memory>

class AppPageRoot;
namespace DComponens {
class LvglComponensBase;
}

class SettingsCompatPath {
public:
    using Post = std::function<void(std::function<void()>)>; /* run later on the LVGL thread */

    /* native_page: the native Settings page (its screen and input group come back on close). on_closed: after the
     * native screen is back (labels such as the Bluetooth alias may have changed). */
    SettingsCompatPath(AppPageRoot &native_page, Post post, std::function<void()> on_closed);
    ~SettingsCompatPath(); /* tears the page down without switching displays (the launcher goes home) */

    SettingsCompatPath(const SettingsCompatPath &) = delete;
    SettingsCompatPath &operator=(const SettingsCompatPath &) = delete;

    bool open(const NodeIter &node);
    bool active() const { return page_ != nullptr; }

private:
    class Window;
    void close();

    AppPageRoot &native_page_;
    Post post_;
    std::function<void()> on_closed_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<DComponens::LvglComponensBase> page_;
    bool closing_ = false;
};
