/*
 * SPDX-License-Identifier: MIT
 */

#include "desktop_app_order.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>

namespace desktop_app_order {

std::uint32_t key_of(const std::string &desktop_filename)
{
    std::uint32_t hash = 2166136261u;   // FNV-1a, the same as the per-app config key suffix
    for (const unsigned char character : desktop_filename) {
        hash ^= character;
        hash *= 16777619u;
    }
    return hash;
}

std::vector<std::uint32_t> parse(const std::string &text)
{
    std::vector<std::uint32_t> keys;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        std::size_t end = text.find(',', begin);
        if (end == std::string::npos) end = text.size();
        const std::string item = text.substr(begin, end - begin);
        if (item.size() == 8 && item.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
            keys.push_back(static_cast<std::uint32_t>(std::strtoul(item.c_str(), nullptr, 16)));
        begin = end + 1;
    }
    return keys;
}

std::string format(const std::vector<std::uint32_t> &keys)
{
    std::string text;
    char item[16];
    for (const std::uint32_t key : keys) {
        std::snprintf(item, sizeof(item), "%08x", static_cast<unsigned>(key));
        if (!text.empty()) text.push_back(',');
        text += item;
    }
    return text;
}

std::string chunk_name(std::size_t index)
{
    return index == 0 ? "app_order" : "app_order" + std::to_string(index + 1);
}

std::vector<std::string> split_chunks(const std::vector<std::uint32_t> &keys)
{
    std::vector<std::string> chunks(kChunks);
    for (std::size_t chunk = 0; chunk < kChunks; ++chunk) {
        const std::size_t begin = chunk * kChunkKeys;
        if (begin >= keys.size()) break;
        const std::size_t end = std::min(keys.size(), begin + kChunkKeys);
        chunks[chunk] = format(std::vector<std::uint32_t>(keys.begin() + static_cast<std::ptrdiff_t>(begin),
                                                          keys.begin() + static_cast<std::ptrdiff_t>(end)));
    }
    return chunks;
}

Result arrange(const std::vector<std::uint32_t> &candidates, const std::vector<std::uint32_t> &saved)
{
    Result result;
    std::unordered_set<std::uint32_t> present(candidates.begin(), candidates.end());

    // Saved order without duplicates.
    std::vector<std::uint32_t> list;
    std::unordered_set<std::uint32_t> seen;
    for (const std::uint32_t key : saved)
        if (seen.insert(key).second) list.push_back(key);

    // Known candidates first, in the saved order.
    for (const std::uint32_t key : list) {
        if (!present.count(key)) continue;
        const auto found = std::find(candidates.begin(), candidates.end(), key);
        result.order.push_back(static_cast<std::size_t>(found - candidates.begin()));
    }
    // Then the new ones, in the fallback order.
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (seen.insert(candidates[index]).second) {
            list.push_back(candidates[index]);
            result.order.push_back(index);
        }
    }
    // A key collision must never hide an app: anything not placed yet goes last.
    std::vector<bool> placed(candidates.size(), false);
    for (const std::size_t index : result.order) placed[index] = true;
    for (std::size_t index = 0; index < candidates.size(); ++index)
        if (!placed[index]) result.order.push_back(index);
    // Bound the list: forget the oldest entries of apps that are gone first.
    for (std::size_t i = 0; list.size() > kMaxSaved && i < list.size();) {
        if (!present.count(list[i])) list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
    if (list.size() > kMaxSaved) list.resize(kMaxSaved);
    result.saved = std::move(list);
    return result;
}

} // namespace desktop_app_order
