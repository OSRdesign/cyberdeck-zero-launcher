/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdio>
#include <string>

namespace cp0_sudo {

/* Which prompt is drawn (task 014 P3d): the responsive one (shared Dialog + masked TextField) only on the native
 * display of the dpi-scaled backend, i.e. when the layout service published metrics and the default display is the
 * native canvas they describe. The 320x170 compat display (stock apps, SDL, no metrics) keeps the fixed box. */
inline bool prompt_is_responsive(bool have_metrics, int metrics_w, int metrics_h, int display_w, int display_h) noexcept
{
    return have_metrics && metrics_w > 0 && metrics_h > 0 && display_w == metrics_w && display_h == metrics_h;
}

/* The error line after a refused password (both prompts): attempts so far of the 3 allowed. */
inline std::string auth_error_text(int attempts)
{
    char text[64];
    std::snprintf(text, sizeof(text), "Wrong password (%d/3). Try again.", attempts);
    return text;
}

class PromptConfirmKeyGate {
public:
    void reset() noexcept { pressed_in_prompt_ = false; }

    void press() noexcept { pressed_in_prompt_ = true; }

    bool release() noexcept
    {
        const bool confirmed = pressed_in_prompt_;
        pressed_in_prompt_ = false;
        return confirmed;
    }

private:
    bool pressed_in_prompt_ = false;
};

} // namespace cp0_sudo
