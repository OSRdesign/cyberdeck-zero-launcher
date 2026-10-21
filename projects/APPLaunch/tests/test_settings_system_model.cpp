/*
 * SPDX-License-Identifier: MIT
 *
 * The Settings system model: the info pages' parsers and labels. (Its software-update half went with the upstream
 * updater, report 029.)
 */

#include "../main/ui/settings/settings_system_model.hpp"

#include <cassert>
#include <cerrno>
#include <string>

int main()
{
    using namespace settings_system;

    NetworkInfo network;
    assert(parse_network_info_strict("192.168.1.20\n192.168.1.1\naa:bb:cc:dd:ee:ff", network));
    assert(network.ip == "192.168.1.20" && network.gateway == "192.168.1.1" && network.mac == "aa:bb:cc:dd:ee:ff");
    assert(parse_network_info_strict("\n\naa:bb", network) && network.ip == "--" && network.gateway == "--");
    assert(!parse_network_info_strict("only\ntwo", network));
    assert(!parse_network_info_strict("a\nb\nc\n", network));   // a trailing newline is refused

    const NetworkInfo loose = parse_network_info("10.0.0.2\r\n");
    assert(loose.ip == "10.0.0.2" && loose.gateway == "--" && loose.mac == "--");

    AccountInfo account;
    assert(parse_account_info_strict("pi\ndeck", account) && account.username == "pi" && account.hostname == "deck");
    assert(!parse_account_info_strict("pi", account));
    assert(parse_account_info("pi\n").hostname == "--");

    assert(version_label("0.8.1") == "Version: 0.8.1");
    assert(version_label("") == "Version: --");
    assert(build_label("2026-10-22", "development", "abc1234") == "Build: 2026-10-22 development (abc1234)");
    assert(build_label("", "", "") == "Build: -- unknown (unknown)");
    assert(backend_error_label(-1, "no service") == "Backend error: no service");
    assert(backend_error_label(-ETIMEDOUT) == "Backend request timed out");
    assert(backend_error_label(-5) == "Backend unavailable (-5)");
    assert(!password_unsupported_label().empty());

    int current = 1;
    assert(!commit_if_success(false, 2, current) && current == 1);
    assert(commit_if_success(true, 3, current) && current == 3);
    return 0;
}
