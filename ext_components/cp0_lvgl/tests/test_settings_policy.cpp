/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_settings_policy.hpp"

#include <cassert>
#include <climits>

using cp0::settings::Arguments;
using cp0::settings::Command;

int main()
{
    assert(cp0::settings::command_from({}) == Command::Unknown);
    assert(cp0::settings::command_from({"BacklightRead"}) == Command::BacklightRead);
    assert(cp0::settings::command_from({"BacklightMax"}) == Command::BacklightMax);
    assert(cp0::settings::command_from({"BacklightWrite", "50"}) == Command::BacklightWrite);
    assert(cp0::settings::command_from({"Log"}) == Command::Log);
    assert(cp0::settings::command_from({"TimeStr"}) == Command::TimeStr);
    assert(cp0::settings::command_from({"GpioSet"}) == Command::Unknown);  // the ExtPort GPIO commands are gone
    assert(cp0::settings::command_from({"backlightread"}) == Command::Unknown);
    assert(cp0::settings::valid_arguments({"BacklightRead"}));
    assert(!cp0::settings::valid_arguments({"BacklightRead", "junk"}));
    assert(cp0::settings::valid_arguments({"BacklightWrite", "0"}));
    assert(cp0::settings::valid_arguments({"BacklightWrite", "100"}));
    assert(!cp0::settings::valid_arguments({"BacklightWrite"}));
    assert(!cp0::settings::valid_arguments({"BacklightWrite", "12junk"}));
    assert(!cp0::settings::valid_arguments({"BacklightWrite", "-1"}));
    assert(!cp0::settings::valid_arguments({"BacklightWrite", "1", "2"}));
    assert(!cp0::settings::valid_arguments({"GpioSet", "EXT5V", "0"}));
    assert(cp0::settings::valid_arguments({"Log", "topic", "message"}));
    assert(!cp0::settings::valid_arguments({"Log", "topic"}));

    const Arguments arguments{"Log", "topic", "1"};
    assert(cp0::settings::argument_at(arguments, 0) == "Log");
    assert(cp0::settings::argument_at(arguments, 1) == "topic");
    assert(cp0::settings::argument_at(arguments, 3).empty());
    int parsed = -1;
    assert(cp0::settings::integer_argument(arguments, 2, 0, 1, parsed) && parsed == 1);
    assert(!cp0::settings::integer_argument(arguments, 3, 0, 1, parsed));
    assert(!cp0::settings::integer_argument({"BacklightWrite", "invalid"}, 1, 0, 100, parsed));
    assert(cp0::settings::parse_integer_response("2147483647", 0, INT_MAX, parsed) &&
           parsed == INT_MAX);
    assert(!cp0::settings::parse_integer_response("12junk", 0, INT_MAX, parsed));
    assert(!cp0::settings::parse_integer_response(" 12", 0, INT_MAX, parsed));
    assert(!cp0::settings::parse_integer_response("-1", 0, INT_MAX, parsed));
    assert(cp0::settings::parse_integer_file_line("12\n", 0, 100, parsed) && parsed == 12);
    assert(cp0::settings::parse_integer_file_line("12\r\n", 0, 100, parsed) && parsed == 12);
    assert(cp0::settings::parse_integer_file_line("255\n", 0, INT_MAX, parsed) &&
           cp0::settings::switch_value(parsed) == 1);
    assert(!cp0::settings::parse_integer_file_line("12junk\n", 0, 100, parsed));
    assert(cp0::settings::parse_switch_response("0", parsed) && parsed == 0);
    assert(cp0::settings::parse_switch_response("1", parsed) && parsed == 1);
    assert(!cp0::settings::parse_switch_response("2", parsed));
    assert(cp0::settings::switch_value(0) == 0);
    assert(cp0::settings::switch_value(1) == 1);
    assert(cp0::settings::switch_value(-1) == 1);
    return 0;
}
