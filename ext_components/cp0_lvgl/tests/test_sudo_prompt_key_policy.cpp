/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_sudo_prompt_key_policy.hpp"

#include <cassert>
#include <string>

int main()
{
    cp0_sudo::PromptConfirmKeyGate gate;

    // The release belonging to the key press that opened the prompt must not submit it.
    assert(!gate.release());

    gate.press();
    assert(gate.release());
    assert(!gate.release());

    gate.press();
    gate.reset();
    assert(!gate.release());

    // the responsive prompt only on the native display the layout service describes (task 014 P3d)
    assert(cp0_sudo::prompt_is_responsive(true, 640, 480, 640, 480));
    assert(cp0_sudo::prompt_is_responsive(true, 480, 320, 480, 320));
    assert(!cp0_sudo::prompt_is_responsive(true, 640, 480, 320, 170)); // compat display: stock apps
    assert(!cp0_sudo::prompt_is_responsive(false, 0, 0, 640, 480));    // SDL / no metrics
    assert(!cp0_sudo::prompt_is_responsive(true, 0, 0, 0, 0));
    assert(!cp0_sudo::prompt_is_responsive(true, 640, 480, 0, 0));     // no default display

    assert(cp0_sudo::auth_error_text(1) == "Wrong password (1/3). Try again.");
    assert(cp0_sudo::auth_error_text(2) == "Wrong password (2/3). Try again.");
    return 0;
}
