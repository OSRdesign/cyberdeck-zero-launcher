/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <list>
#include <string_view>
#include <string>

namespace cp0::settings {

using Arguments = std::list<std::string>;

enum class Command {
    Unknown,
    BacklightRead,
    BacklightMax,
    BacklightWrite,
    Log,
    TimeStr,
};

Command command_from(const Arguments &arguments);
bool valid_arguments(const Arguments &arguments);
std::string argument_at(const Arguments &arguments, std::size_t index);
bool integer_argument(const Arguments &arguments,
                      std::size_t index,
                      int minimum,
                      int maximum,
                      int &value);
bool parse_integer_response(std::string_view text,
                            int minimum,
                            int maximum,
                            int &value);
bool parse_integer_file_line(std::string_view text,
                             int minimum,
                             int maximum,
                             int &value);
bool parse_switch_response(std::string_view text, int &value);
int switch_value(int value);

} // namespace cp0::settings
