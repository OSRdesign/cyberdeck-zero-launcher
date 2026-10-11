/*
 * SPDX-License-Identifier: MIT
 *
 * Native SSH form rules (task 014 P4): Enter per field, field types and limits, the fields copied into the model so
 * that validation sees what is shown, the help text.
 */

#include "../main/ui/model/ssh_native_form_model.hpp"

#include <cassert>
#include <iterator>
#include <string>

using Texts = std::array<std::string, SshConnectionModel::FIELD_COUNT>;

int main()
{
    using ssh_native_form::SubmitAction;
    assert(ssh_native_form::submit_action(0) == SubmitAction::NEXT_FIELD);
    assert(ssh_native_form::submit_action(1) == SubmitAction::NEXT_FIELD);
    assert(ssh_native_form::submit_action(2) == SubmitAction::CONNECT);

    assert(!ssh_native_form::numeric_field(0));
    assert(ssh_native_form::numeric_field(1));
    assert(!ssh_native_form::numeric_field(2));
    assert(ssh_native_form::field_max_bytes(0) == 128);
    assert(ssh_native_form::field_max_bytes(1) == 5);
    assert(ssh_native_form::field_max_bytes(2) == 128);

    // the texts reach the model, the active field goes back to Host
    SshConnectionModel model;
    assert(ssh_native_form::sync(model, Texts{"10.0.0.5", "2222", "admin"}));
    assert(model.value(0) == "10.0.0.5");
    assert(model.value(1) == "2222");
    assert(model.value(2) == "admin");
    assert(model.active_index() == 0);
    auto arguments = model.arguments();
    assert(arguments.size() == 7);
    assert(*std::next(arguments.begin(), 5) == "2222");
    assert(arguments.back() == "admin@10.0.0.5");

    // an empty host is shown to validation as it is (set_values would refuse it and keep the old host)
    assert(ssh_native_form::sync(model, Texts{"", "22", "pi"}));
    assert(model.value(0).empty());
    auto validation = model.validate();
    assert(validation.error == SshConnectionModel::Error::EMPTY_HOST);
    assert(std::string(validation.message) == "Host is required");
    assert(model.arguments().empty());

    assert(ssh_native_form::sync(model, Texts{"host", "0", "pi"}));
    validation = model.validate();
    assert(validation.error == SshConnectionModel::Error::INVALID_PORT);
    assert(std::string(validation.message) == "Port must be 1-65535");

    assert(ssh_native_form::sync(model, Texts{"host", "", "a b"}));
    assert(model.validate().error == SshConnectionModel::Error::INVALID_USER);
    assert(ssh_native_form::sync(model, Texts{"-oProxy", "22", ""}));
    assert(model.validate().error == SshConnectionModel::Error::INVALID_HOST);

    // empty port and user: ssh defaults (the compat form's rules)
    assert(ssh_native_form::sync(model, Texts{"host", "", ""}));
    assert(model.valid());
    assert(model.arguments().back() == "host");

    // a text the model cannot take (control character) is reported; the field is left empty
    assert(!ssh_native_form::sync(model, Texts{"ho\tst", "22", "pi"}));
    assert(model.value(0).empty());
    assert(model.value(2) == "pi");

    const std::string help = ssh_native_form::help_text("F1:Help");
    assert(help.find("Connect to other devices via SSH (Secure Shell).") == 0);
    assert(help.find("F1:Help\n") != std::string::npos);
    assert(help.find("Esc: back") != std::string::npos);
    assert(ssh_native_form::help_text(nullptr).find("Help\n") == std::string::npos);
    return 0;
}
