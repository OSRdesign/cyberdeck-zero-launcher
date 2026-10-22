/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_system_model.hpp"

#include <cerrno>
#include <cstddef>
#include <sstream>
#include <vector>

namespace settings_system {
namespace {

constexpr const char *kUnavailable = "--";

std::string next_field(std::istringstream &lines)
{
    std::string value;
    if (!std::getline(lines, value)) return kUnavailable;
    if (!value.empty() && value.back() == '\r') value.pop_back();
    return value.empty() ? kUnavailable : value;
}

bool split_strict(const std::string &payload,
                  std::size_t expected_fields,
                  std::vector<std::string> &fields)
{
    fields.clear();
    if (!payload.empty() && payload.back() == '\n') return false;

    std::istringstream lines(payload);
    std::string field;
    while (std::getline(lines, field)) {
        if (!field.empty() && field.back() == '\r') field.pop_back();
        if (field.find('\r') != std::string::npos) return false;
        fields.push_back(std::move(field));
    }
    return fields.size() == expected_fields;
}

std::string normalized_field(const std::string &value)
{
    return value.empty() ? kUnavailable : value;
}

} // namespace

NetworkInfo parse_network_info(const std::string &payload)
{
    std::istringstream lines(payload);
    return {next_field(lines), next_field(lines), next_field(lines)};
}

AccountInfo parse_account_info(const std::string &payload)
{
    std::istringstream lines(payload);
    return {next_field(lines), next_field(lines)};
}

bool parse_network_info_strict(const std::string &payload, NetworkInfo &result)
{
    std::vector<std::string> fields;
    if (!split_strict(payload, 3, fields)) return false;
    result = {normalized_field(fields[0]), normalized_field(fields[1]), normalized_field(fields[2])};
    return true;
}

bool parse_account_info_strict(const std::string &payload, AccountInfo &result)
{
    std::vector<std::string> fields;
    if (!split_strict(payload, 2, fields)) return false;
    result = {normalized_field(fields[0]), normalized_field(fields[1])};
    return true;
}

std::string version_label(const std::string &version)
{
    return "Version: " + (version.empty() ? std::string(kUnavailable) : version);
}

std::string build_label(const std::string &date,
                        const std::string &channel,
                        const std::string &commit)
{
    return "Build: " + (date.empty() ? std::string(kUnavailable) : date) + " " +
           (channel.empty() ? std::string("unknown") : channel) + " (" +
           (commit.empty() ? std::string("unknown") : commit) + ")";
}

std::string backend_error_label(int result_code, const std::string &payload)
{
    if (!payload.empty()) return "Backend error: " + payload;
    if (result_code == -ETIMEDOUT) return "Backend request timed out";
    return "Backend unavailable (" + std::to_string(result_code) + ")";
}

std::string password_unsupported_label()
{
    return "Password changes are not supported";
}

} // namespace settings_system
