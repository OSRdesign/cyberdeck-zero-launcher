/*
 * SPDX-License-Identifier: MIT
 *
 * Rules of the native (responsive) SSH connection form (task 014 P4): the page itself is ui_app_ssh_native.cpp.
 * Pure C++ (no LVGL), unit tested in tests/test_ssh_native_form_model.cpp.
 */

#pragma once

#include "ssh_connection_model.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace ssh_native_form {

/* What Enter in a field does: the next field, or connect on the last one (User). */
enum class SubmitAction { NEXT_FIELD, CONNECT };
SubmitAction submit_action(std::size_t field);

/* Port takes digits only (the number pad); Host and User are text. */
bool numeric_field(std::size_t field);

/* Bytes a field takes: the model's limit (128), Port 5 (65535). */
std::size_t field_max_bytes(std::size_t field);

/* Copies the fields' texts into the model through the same edits the compat form makes while typing (erase, append),
 * so validate() sees exactly what is shown (set_values() would refuse an invalid set and keep the old values). Returns
 * false when a text cannot be taken (control characters, too long); the model then holds what could be taken. */
bool sync(SshConnectionModel &model, const std::array<std::string, SshConnectionModel::FIELD_COUNT> &texts);

/* The help page's text (the compat help view's content, for the native keys); help_hint is the hardware profile's
 * help key text (APPLAUNCH_TXT_SSH_HELP_HINT: "F1:Help" / "KEY_HELP:Help"). */
std::string help_text(const char *help_hint);

} // namespace ssh_native_form
