/*
 * SPDX-License-Identifier: MIT
 *
 * Rules of the native SSH connection form (see ssh_native_form_model.hpp).
 */

#include "ssh_native_form_model.hpp"

namespace ssh_native_form {

namespace {
constexpr std::size_t kFieldMaxBytes = 128; /* SshConnectionModel::MAX_FIELD_BYTES */
constexpr std::size_t kPortMaxBytes = 5;    /* 65535 */
constexpr std::size_t kPort = static_cast<std::size_t>(SshConnectionModel::Field::PORT);
} // namespace

SubmitAction submit_action(std::size_t field)
{
    return field + 1 >= SshConnectionModel::FIELD_COUNT ? SubmitAction::CONNECT : SubmitAction::NEXT_FIELD;
}

bool numeric_field(std::size_t field)
{
    return field == kPort;
}

std::size_t field_max_bytes(std::size_t field)
{
    return field == kPort ? kPortMaxBytes : kFieldMaxBytes;
}

bool sync(SshConnectionModel &model, const std::array<std::string, SshConnectionModel::FIELD_COUNT> &texts)
{
    while (model.select_previous()) {
    }
    bool ok = true;
    for (std::size_t i = 0; i < SshConnectionModel::FIELD_COUNT; ++i) {
        if (i > 0) model.select_next();
        while (model.erase_last()) {
        }
        if (!texts[i].empty() && !model.append(texts[i])) ok = false;
    }
    while (model.select_previous()) {
    }
    return ok;
}

std::string help_text(const char *help_hint)
{
    std::string text = "Connect to other devices via SSH (Secure Shell).\n\n"
                       "Up / Down or Tab: move input field\n"
                       "Enter: next field; on User: connect\n"
                       "Connect: connect (Host, Port and User are saved)\n";
    if (help_hint && help_hint[0]) {
        text += help_hint;
        text += "\n";
    }
    text += "Esc: back";
    return text;
}

} // namespace ssh_native_form
