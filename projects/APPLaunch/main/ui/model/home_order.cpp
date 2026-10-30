/*
 * SPDX-License-Identifier: MIT
 */

#include "home_order.hpp"

#include "desktop_app_order.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace home_order {

static_assert(kChunks == desktop_app_order::kChunks && kChunkKeys == desktop_app_order::kChunkKeys &&
                  kMaxSaved == desktop_app_order::kMaxSaved,
              "home_order reuses the app_order chunk format");

namespace {

/* GetStr answers its fallback for a missing key: a value no chunk can hold tells "absent" from "empty". */
const char kAbsent[] = "\x1f";

std::vector<std::uint32_t> unique(const std::vector<std::uint32_t> &keys)
{
    std::vector<std::uint32_t> out;
    std::unordered_set<std::uint32_t> seen;
    for (const std::uint32_t key : keys)
        if (seen.insert(key).second) out.push_back(key);
    return out;
}

struct Chunk {
    bool exists = false;
    std::string value;
};

Chunk read_chunk(const ConfigCall &config, std::size_t index)
{
    Chunk chunk;
    if (!config) return chunk;
    try {
        config({"GetStr", chunk_name(index), kAbsent}, [&](int code, std::string data) {
            if (code != 0 || data == kAbsent) return;
            chunk.exists = true;
            chunk.value = std::move(data);
        });
    } catch (...) {
        return Chunk{};
    }
    return chunk;
}

bool set_many(const ConfigCall &config, std::list<std::string> request)
{
    if (request.size() < 3) return true; /* nothing to write */
    if (!config) return false;
    bool ok = false;
    try {
        config(std::move(request), [&](int code, std::string) { ok = code == 0; });
    } catch (...) {
        return false;
    }
    return ok;
}

} // namespace

std::uint32_t key_of(const std::string &tile_id)
{
    return desktop_app_order::key_of(tile_id);
}

std::vector<std::uint32_t> parse(const std::string &text)
{
    return desktop_app_order::parse(text);
}

std::string format(const std::vector<std::uint32_t> &keys)
{
    return desktop_app_order::format(keys);
}

std::string chunk_name(std::size_t index)
{
    return index == 0 ? "home_order" : "home_order" + std::to_string(index + 1);
}

std::vector<std::string> split_chunks(const std::vector<std::uint32_t> &keys)
{
    return desktop_app_order::split_chunks(keys);
}

std::vector<std::size_t> arrange(const std::vector<std::uint32_t> &tiles, const std::vector<std::uint32_t> &saved)
{
    return desktop_app_order::arrange(tiles, saved).order;
}

std::vector<std::uint32_t> merge(const std::vector<std::uint32_t> &stored,
                                 const std::vector<std::uint32_t> &registered,
                                 const std::vector<std::uint32_t> &visible)
{
    const std::vector<std::uint32_t> shown = unique(visible);
    std::vector<std::uint32_t> list = unique(stored);
    std::unordered_set<std::uint32_t> in_list(list.begin(), list.end());
    // the first save starts from every registered tile; later saves add the registered tiles they lack at the end
    for (const std::uint32_t key : registered)
        if (in_list.insert(key).second) list.push_back(key);
    // a shown tile the list lacks (registered under another key, a collision) gets a slot at the end
    for (const std::uint32_t key : shown)
        if (in_list.insert(key).second) list.push_back(key);

    // the shown tiles fill the slots of shown tiles, in their new order; the others keep theirs
    const std::unordered_set<std::uint32_t> shown_set(shown.begin(), shown.end());
    std::size_t next = 0;
    for (std::uint32_t &slot : list)
        if (shown_set.count(slot)) slot = shown[next++];

    // bounded: forget the oldest ids of tiles that are gone first (neither registered nor shown)
    if (list.size() > kMaxSaved) {
        std::unordered_set<std::uint32_t> known(registered.begin(), registered.end());
        known.insert(shown.begin(), shown.end());
        for (std::size_t i = 0; list.size() > kMaxSaved && i < list.size();) {
            if (!known.count(list[i])) list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            else ++i;
        }
        if (list.size() > kMaxSaved) list.resize(kMaxSaved);
    }
    return list;
}

std::vector<std::uint32_t> load(const ConfigCall &config)
{
    std::string text;
    for (std::size_t chunk = 0; chunk < kChunks; ++chunk) {
        const Chunk read = read_chunk(config, chunk);
        if (read.value.empty()) break;
        if (!text.empty()) text.push_back(',');
        text += read.value;
    }
    return unique(parse(text));
}

bool store(const ConfigCall &config, const std::vector<std::uint32_t> &list)
{
    std::vector<std::uint32_t> bounded = unique(list);
    if (bounded.size() > kMaxSaved) bounded.resize(kMaxSaved);
    const std::vector<std::string> chunks = split_chunks(bounded);
    std::list<std::string> request = {"SetManyAndSave"};
    for (std::size_t chunk = 0; chunk < kChunks; ++chunk) {
        const std::string value = chunk < chunks.size() ? chunks[chunk] : std::string();
        // an empty chunk is written only to clear one that exists: it must not take a new store entry
        if (value.empty() && !read_chunk(config, chunk).exists) continue;
        request.push_back(chunk_name(chunk));
        request.push_back(value);
    }
    return set_many(config, std::move(request));
}

bool save(const ConfigCall &config, const std::vector<std::string> &registered_ids,
          const std::vector<std::string> &visible_ids)
{
    std::vector<std::uint32_t> registered;
    registered.reserve(registered_ids.size());
    for (const std::string &id : registered_ids)
        if (!id.empty()) registered.push_back(key_of(id));
    std::vector<std::uint32_t> visible;
    visible.reserve(visible_ids.size());
    for (const std::string &id : visible_ids)
        if (!id.empty()) visible.push_back(key_of(id));
    return store(config, merge(load(config), registered, visible));
}

bool reset(const ConfigCall &config)
{
    std::list<std::string> request = {"SetManyAndSave"};
    for (std::size_t chunk = 0; chunk < kChunks; ++chunk) {
        const Chunk read = read_chunk(config, chunk);
        if (!read.exists || read.value.empty()) continue;
        request.push_back(chunk_name(chunk));
        request.push_back(std::string());
    }
    return set_many(config, std::move(request));
}

} // namespace home_order
