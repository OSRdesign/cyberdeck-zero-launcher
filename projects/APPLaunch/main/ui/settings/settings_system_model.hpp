/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <utility>

namespace settings_system {

template <typename Value>
bool commit_if_success(bool success, Value candidate, Value &current)
{
    if (!success) return false;
    current = std::move(candidate);
    return true;
}

struct NetworkInfo
{
    std::string ip;
    std::string gateway;
    std::string mac;
};

struct AccountInfo
{
    std::string username;
    std::string hostname;
};

NetworkInfo parse_network_info(const std::string &payload);
AccountInfo parse_account_info(const std::string &payload);
bool parse_network_info_strict(const std::string &payload, NetworkInfo &result);
bool parse_account_info_strict(const std::string &payload, AccountInfo &result);

std::string version_label(const std::string &version);
std::string build_label(const std::string &date,
                        const std::string &channel,
                        const std::string &commit);
std::string backend_error_label(int result_code, const std::string &payload = {});
std::string password_unsupported_label();

} // namespace settings_system

namespace system_page {

using NetworkInfo = settings_system::NetworkInfo;
using AccountInfo = settings_system::AccountInfo;

template <typename Value>
bool commit_if_success(bool success, Value candidate, Value &current)
{
    return settings_system::commit_if_success(success, std::move(candidate), current);
}

inline NetworkInfo parse_network_info(const std::string &payload)
{
    return settings_system::parse_network_info(payload);
}

inline AccountInfo parse_account_info(const std::string &payload)
{
    return settings_system::parse_account_info(payload);
}

inline bool parse_network_info_strict(const std::string &payload, NetworkInfo &result)
{
    return settings_system::parse_network_info_strict(payload, result);
}

inline bool parse_account_info_strict(const std::string &payload, AccountInfo &result)
{
    return settings_system::parse_account_info_strict(payload, result);
}

inline std::string version_label(const std::string &version)
{
    return settings_system::version_label(version);
}

inline std::string build_label(const std::string &date,
                               const std::string &channel,
                               const std::string &commit)
{
    return settings_system::build_label(date, channel, commit);
}

inline std::string backend_error_label(int result_code, const std::string &payload = {})
{
    return settings_system::backend_error_label(result_code, payload);
}

inline std::string password_unsupported_label()
{
    return settings_system::password_unsupported_label();
}

} // namespace system_page
