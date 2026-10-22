/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_settings_policy.hpp"

#include <charconv>
#include <limits>

namespace cp0::settings {

Command command_from(const Arguments &arguments)
{
    if (arguments.empty())
        return Command::Unknown;

    const std::string &command = arguments.front();
    if (command == "BacklightRead")
        return Command::BacklightRead;
    if (command == "BacklightMax")
        return Command::BacklightMax;
    if (command == "BacklightWrite")
        return Command::BacklightWrite;
    if (command == "Log")
        return Command::Log;
    if (command == "TimeStr")
        return Command::TimeStr;
    return Command::Unknown;
}

bool valid_arguments(const Arguments &arguments)
{
    const Command command = command_from(arguments);
    if (command == Command::Unknown) return false;
    if (command == Command::BacklightRead || command == Command::BacklightMax ||
        command == Command::TimeStr)
        return arguments.size() == 1;
    if (command == Command::Log)
        return arguments.size() == 3;

    // BacklightWrite value
    if (arguments.size() != 2u)
        return false;
    int parsed = 0;
    return integer_argument(arguments, 1, 0, std::numeric_limits<int>::max(), parsed);
}

std::string argument_at(const Arguments &arguments, std::size_t index)
{
    auto iterator = arguments.begin();
    for (std::size_t current = 0; current < index && iterator != arguments.end(); ++current)
        ++iterator;
    return iterator == arguments.end() ? std::string() : *iterator;
}

bool parse_integer_response(std::string_view text,
                            int minimum,
                            int maximum,
                            int &value)
{
    if (text.empty()) return false;
    int parsed = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < minimum || parsed > maximum)
        return false;
    value = parsed;
    return true;
}

bool integer_argument(const Arguments &arguments,
                      std::size_t index,
                      int minimum,
                      int maximum,
                      int &value)
{
    return parse_integer_response(argument_at(arguments, index), minimum, maximum, value);
}

bool parse_integer_file_line(std::string_view text,
                             int minimum,
                             int maximum,
                             int &value)
{
    if (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
    return parse_integer_response(text, minimum, maximum, value);
}

bool parse_switch_response(std::string_view text, int &value)
{
    return parse_integer_response(text, 0, 1, value);
}

int switch_value(int value)
{
    return value == 0 ? 0 : 1;
}

} // namespace cp0::settings
