/*
 * SPDX-License-Identifier: MIT
 *
 * The responsive sudo password prompt (task 014 P3d): the shared Dialog (title "Sudo Password", Cancel / OK at 9 mm,
 * one status line) with a masked TextField, built on the display's top layer so it sits above any page. The fallback
 * on-screen keyboard comes through the TextField exactly as on the Wi-Fi password dialog. No policy here: cp0_sudo_async
 * owns the request, the password and the key gate; this class only draws and reports what the user did.
 *
 * Widget callbacks never act inside the LVGL event that raised them: they post one async hop (a generation number, no
 * pointer) and the events below run from it, so the owner may delete the prompt in them. LVGL thread only.
 */
#pragma once

#include "cp0_ui_widgets.hpp"

#include <functional>
#include <memory>
#include <string>

namespace cp0_sudo {

struct PromptUiEvents {
    /* OK, or Enter in the field. `password` is the field's own text: copy it, do not keep the reference. */
    std::function<void(const std::string &password)> submit;
    std::function<void()> cancel; /* Cancel, Esc */
};

class PromptUi {
public:
    PromptUi() = default;
    ~PromptUi();
    PromptUi(const PromptUi &) = delete;
    PromptUi &operator=(const PromptUi &) = delete;

    /* Builds the dialog on `layer` (lv_layer_top()). False when it could not (nothing is left behind). */
    bool create(lv_obj_t *layer, PromptUiEvents events);
    /* Add this to the prompt's input group; begin_edit() then starts editing (the keyboard comes when allowed). */
    lv_obj_t *focus_object() const;
    void begin_edit();
    /* Authenticating: the field is wiped and stops editing (the keyboard goes), OK is disabled, the line says `text`. */
    void show_busy(const std::string &text);
    /* A refused password: the field is wiped and edits again, OK is enabled, the red line says `text`. */
    void show_error(const std::string &text);

private:
    static void hop_cb(void *user);
    void post(unsigned kind);
    void run(unsigned kind);

    PromptUiEvents events_;
    std::unique_ptr<cp0::ui::Dialog> dialog_;
    std::unique_ptr<cp0::ui::TextField> field_;
    unsigned generation_ = 0;
    bool busy_ = false;
};

} // namespace cp0_sudo
