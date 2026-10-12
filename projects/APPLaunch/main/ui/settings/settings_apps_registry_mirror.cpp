/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps: the Store's shared source list (see settings_apps_registry_mirror.hpp).
 */

#include "settings_apps_registry_mirror.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace apps_registry_mirror {

namespace {

std::string escape_field(const std::string &value)
{
    std::string output;
    output.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': output += "\\\\"; break;
            case '\t': output += "\\t"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            default: output += ch; break;
        }
    }
    return output;
}

std::vector<std::string> split_entry(const std::string &value)
{
    std::vector<std::string> fields;
    std::string current;
    bool escaped = false;
    for (char ch : value) {
        if (escaped) {
            if (ch == 't') current += '\t';
            else if (ch == 'n') current += '\n';
            else if (ch == 'r') current += '\r';
            else current += ch;
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '\t') {
            fields.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    if (escaped) current += '\\';
    fields.push_back(current);
    return fields;
}

std::vector<std::string> split_tab(const std::string &line)
{
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == '\t') {
            fields.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current.push_back(c);
        }
    }
    fields.push_back(current);
    return fields;
}

std::string key(const std::string &suffix)
{
    return std::string(kPrefix) + suffix;
}

} // namespace

std::string pack_entry(const Entry &entry)
{
    return escape_field(entry.name) + "\t" + escape_field(entry.url) + "\t" + (entry.enabled ? "1" : "0") + "\t" +
           (entry.builtin ? "1" : "0") + "\t" + escape_field(entry.region);
}

bool unpack_entry(const std::string &packed, Entry &entry)
{
    const auto fields = split_entry(packed);
    if (fields.size() < 5 || fields[1].empty()) return false;
    entry.name = fields[0];
    entry.url = fields[1];
    entry.enabled = fields[2] != "0";
    entry.builtin = fields[3] == "1";
    entry.region = fields[4];
    return true;
}

bool parse_backend(const std::string &output, Config &config)
{
    config = Config{};
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        const auto fields = split_tab(line);
        if (fields.empty()) continue;
        if (fields[0] == "CONFIG" && fields.size() >= 4) {
            config.region = fields[1];
            config.active_region = fields[2];
        } else if (fields[0] == "CONFIG_REG" && fields.size() >= 7) {
            Entry entry;
            entry.name = fields[2];
            entry.url = fields[3];
            entry.enabled = fields[4] != "0";
            entry.builtin = fields[5] == "1";
            entry.region = fields[6];
            if (!entry.url.empty()) config.entries.push_back(std::move(entry));
        }
    }
    return !config.entries.empty();
}

bool pack(const Config &config, KeyValues &keys, int *skipped)
{
    keys.clear();
    if (skipped) *skipped = 0;
    std::vector<std::string> packed;
    for (const Entry &entry : config.entries) {
        if (static_cast<int>(packed.size()) >= kMaxEntries) break;
        std::string text = pack_entry(entry);
        if (entry.url.empty() || text.size() > kMaxValueLength) {
            if (skipped) ++*skipped;
            continue;
        }
        packed.push_back(std::move(text));
    }
    if (packed.empty()) return false;
    keys.emplace_back(key("region"), config.region.empty() ? "auto" : config.region);
    keys.emplace_back(key("active_region"), config.active_region.empty() ? "default" : config.active_region);
    keys.emplace_back(key("count"), std::to_string(packed.size()));
    for (int index = 0; index < kMaxEntries; ++index)
        keys.emplace_back(key(std::to_string(index) + ".entry"),
                          index < static_cast<int>(packed.size()) ? packed[static_cast<std::size_t>(index)] : "");
    return true;
}

bool parse(const std::function<std::string(const std::string &)> &get, Config &config)
{
    const std::string count_text = get(key("count"));
    if (count_text.empty()) return false;
    const int count = std::min(std::atoi(count_text.c_str()), kMaxEntries);
    if (count <= 0) return false;
    config = Config{};
    const std::string region = get(key("region"));
    const std::string active = get(key("active_region"));
    config.region = region.empty() ? "auto" : region;
    config.active_region = active.empty() ? "default" : active;
    for (int index = 0; index < count; ++index) {
        Entry entry;
        if (unpack_entry(get(key(std::to_string(index) + ".entry")), entry)) config.entries.push_back(std::move(entry));
    }
    return !config.entries.empty();
}

bool equivalent(const Config &a, const Config &b)
{
    KeyValues left, right;
    const bool has_left = pack(a, left);
    const bool has_right = pack(b, right);
    if (has_left != has_right) return false;
    return left == right;
}

Plan plan(const PlanInput &input)
{
    Plan result;
    if (!input.backend_read || input.backend.entries.empty()) return result;
    result.write_mirror = !input.mirror_present || !equivalent(input.backend, input.mirror);
    if (!input.flag_set) {
        const bool custom = std::any_of(input.backend.entries.begin(), input.backend.entries.end(),
                                        [](const Entry &entry) { return !entry.builtin; });
        if (custom) result.set_flag = true;
        else result.add_default = true;
    }
    return result;
}

} // namespace apps_registry_mirror
