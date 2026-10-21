/*
 * SPDX-License-Identifier: MIT
 *
 * Time zone names and lists (see cp0_timezone.hpp).
 */

#include "cp0_timezone.hpp"

#include <algorithm>

namespace cp0 {
namespace tz {
namespace {

bool letter(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool name_char(char c) noexcept
{
    return letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '+' || c == '-' || c == '/';
}

bool blank(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

/* Calls each(line) for every line of text (without its '\n'). */
template <typename Each>
void for_each_line(std::string_view text, Each each)
{
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::size_t stop = end == std::string_view::npos ? text.size() : end;
        each(text.substr(start, stop - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
}

/* The whitespace-separated fields of a line. */
std::vector<std::string_view> fields(std::string_view line)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && blank(line[i])) ++i;
        const std::size_t start = i;
        while (i < line.size() && !blank(line[i])) ++i;
        if (i > start) out.push_back(line.substr(start, i - start));
    }
    return out;
}

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && blank(text.front())) text.remove_prefix(1);
    while (!text.empty() && blank(text.back())) text.remove_suffix(1);
    return text;
}

void keep(std::vector<std::string> &names, std::string_view name)
{
    if (valid_name(name)) names.emplace_back(name);
}

std::vector<std::string> sorted_unique(std::vector<std::string> names)
{
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

} // namespace

bool valid_name(std::string_view name) noexcept
{
    if (name.empty() || name.size() > kMaxNameBytes) return false;
    bool component_start = true;
    for (const char c : name) {
        if (!name_char(c)) return false;
        if (component_start) {
            if (!letter(c)) return false; /* also refuses a leading '/', "//", a leading '-' or '+' or digit */
            component_start = false;
        } else if (c == '/') {
            component_start = true;
        }
    }
    return !component_start; /* no trailing '/' */
}

std::vector<std::string> parse_list(std::string_view text)
{
    std::vector<std::string> names;
    for_each_line(text, [&](std::string_view line) { keep(names, trimmed(line)); });
    return sorted_unique(std::move(names));
}

std::vector<std::string> parse_tzdata_zi(std::string_view text)
{
    std::vector<std::string> names;
    for_each_line(text, [&](std::string_view line) {
        const std::vector<std::string_view> f = fields(line);
        if (f.size() >= 2 && (f[0] == "Z" || f[0] == "Zone")) keep(names, f[1]);
        else if (f.size() >= 3 && (f[0] == "L" || f[0] == "Link")) keep(names, f[2]);
    });
    return sorted_unique(std::move(names));
}

std::vector<std::string> parse_zone_tab(std::string_view text)
{
    std::vector<std::string> names;
    for_each_line(text, [&](std::string_view line) {
        if (line.empty() || line.front() == '#') return;
        std::size_t start = 0;
        for (int field = 0; field < 2; ++field) {
            const std::size_t tab = line.find('\t', start);
            if (tab == std::string_view::npos) return;
            start = tab + 1;
        }
        const std::size_t end = line.find('\t', start);
        keep(names, trimmed(line.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start)));
    });
    return sorted_unique(std::move(names));
}

std::string zone_from_link(std::string_view target)
{
    constexpr std::string_view marker = "zoneinfo/";
    const std::size_t at = target.find(marker);
    if (at == std::string_view::npos) return {};
    const std::string_view name = target.substr(at + marker.size());
    return valid_name(name) ? std::string(name) : std::string();
}

} // namespace tz
} // namespace cp0
