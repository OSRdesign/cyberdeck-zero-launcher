/*
 * SPDX-License-Identifier: MIT
 *
 * The home grid's user order (main/ui/model/home_order.hpp, study 046 P1). The config calls go to the real config
 * service (ext_components/cp0_lvgl/src/cp0_config_service.cpp) on an in-memory file, so the 64-entry cap and
 * SetManyAndSave's refusal are the board's.
 */

#include "../main/ui/model/home_order.hpp"

#include "cp0_config_service.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace home_order;

namespace {

std::vector<std::uint32_t> keys(std::initializer_list<const char *> ids)
{
    std::vector<std::uint32_t> out;
    for (const char *id : ids) out.push_back(key_of(id));
    return out;
}

/* The tiles of `ids` (today's order) put in the saved order: the ids, in grid order. */
std::vector<std::string> grid(const std::vector<std::string> &ids, const std::vector<std::uint32_t> &saved)
{
    std::vector<std::uint32_t> tile_keys;
    for (const auto &id : ids) tile_keys.push_back(key_of(id));
    std::vector<std::string> out;
    for (const std::size_t index : arrange(tile_keys, saved)) out.push_back(ids[index]);
    return out;
}

/* A config service on an in-memory file of `preset` entries; `file` is what was saved last. */
struct Store {
    cp0cfg::Entries file;
    cp0cfg::ConfigService service;
    explicit Store(cp0cfg::Entries preset = {})
        : file(std::move(preset)),
          service([this](cp0cfg::Entries &out) { out = file; return true; },
                  [this](const cp0cfg::Entries &entries) { file = entries; return 0; })
    {
    }
    ConfigCall call()
    {
        return [this](std::list<std::string> args, Reply reply) { service.api_call(std::move(args), std::move(reply)); };
    }
    bool has(const std::string &key) const
    {
        for (const auto &entry : file)
            if (entry.first == key) return true;
        return false;
    }
    std::string get(const std::string &key) const
    {
        for (const auto &entry : file)
            if (entry.first == key) return entry.second;
        return "<absent>";
    }
};

cp0cfg::Entries filler(std::size_t count)
{
    cp0cfg::Entries entries;
    for (std::size_t i = 0; i < count; ++i) entries.emplace_back("filler" + std::to_string(i), "1");
    return entries;
}

const std::vector<std::string> kBuiltins = {"app_Setting", "app_Store", "app_CLI", "app_Python", "app_SSH",
                                            "app_IP_Panel", "app_Math", "app_Game", "app_Tank"};

} // namespace

int main()
{
    // ---------------------------------------------------------------- format
    assert(key_of("") == 2166136261u);   // FNV-1a, the same as app_order and the desktop config key suffix
    assert(key_of("a") == 0xe40c292cu);
    const auto list = keys({"app_Setting", "app_Store", "app_desktop_files_desktop_12345678"});
    assert(parse(format(list)) == list);
    assert(parse("").empty());
    assert(parse("zz,1234,,0000000G,0000000a") == std::vector<std::uint32_t>{0xau});
    assert(chunk_name(0) == "home_order");
    assert(chunk_name(1) == "home_order2");
    assert(chunk_name(4) == "home_order5");

    // chunks: at most 27 keys each, every value fits the store's 255 characters, the pieces rebuild the list
    std::vector<std::uint32_t> big;
    for (std::uint32_t i = 0; i < kMaxSaved; ++i) big.push_back(0x10000000u + i * 7919u);
    const auto chunks = split_chunks(big);
    assert(chunks.size() == kChunks);
    std::string joined;
    for (const auto &chunk : chunks) {
        assert(chunk.size() <= 255);
        assert(parse(chunk).size() <= kChunkKeys);
        if (chunk.empty()) continue;
        if (!joined.empty()) joined.push_back(',');
        joined += chunk;
    }
    assert(parse(joined) == big);

    // ---------------------------------------------------------------- arrange
    const std::vector<std::string> today = {"app_Setting", "app_Store", "app_CLI", "app_desktop_a", "app_desktop_b"};
    // known ids in the user's order
    assert((grid(today, keys({"app_desktop_b", "app_Setting", "app_desktop_a", "app_CLI", "app_Store"})) ==
            std::vector<std::string>{"app_desktop_b", "app_Setting", "app_desktop_a", "app_CLI", "app_Store"}));
    // unknown ids (new apps) after the known ones, in today's order
    assert((grid(today, keys({"app_desktop_a", "app_Setting"})) ==
            std::vector<std::string>{"app_desktop_a", "app_Setting", "app_Store", "app_CLI", "app_desktop_b"}));
    // removed and hidden ids in the saved list take no place on the grid
    assert((grid(today, keys({"app_gone", "app_desktop_b", "app_Python", "app_Setting", "app_Store", "app_CLI",
                              "app_desktop_a"})) ==
            std::vector<std::string>{"app_desktop_b", "app_Setting", "app_Store", "app_CLI", "app_desktop_a"}));
    // no user order: today's grid exactly
    assert(grid(today, {}) == today);
    // a hash collision never hides a tile: both tiles shown, the second one last
    {
        const std::vector<std::uint32_t> colliding = {7, 9, 7};
        const auto order = arrange(colliding, {9, 7});
        assert((order == std::vector<std::size_t>{1, 0, 2}));
    }

    // ---------------------------------------------------------------- merge (slot filling)
    // first save: every registered tile is stored, hidden ones (Python, SSH) included, at their registry slots
    {
        const auto registered = keys({"app_Setting", "app_Store", "app_CLI", "app_Python", "app_SSH", "app_desktop_a"});
        const auto moved = keys({"app_desktop_a", "app_Setting", "app_Store", "app_CLI"}); // a moved to the front
        const auto saved = merge({}, registered, moved);
        assert((saved == keys({"app_desktop_a", "app_Setting", "app_Store", "app_Python", "app_SSH", "app_CLI"})));
        // Python enabled later comes back at its slot: after Store, before CLI
        const std::vector<std::string> later = {"app_Setting", "app_Store", "app_CLI", "app_Python", "app_desktop_a"};
        assert((grid(later, saved) ==
                std::vector<std::string>{"app_desktop_a", "app_Setting", "app_Store", "app_Python", "app_CLI"}));
    }
    // hidden and removed ids keep their slots; the visible ids fill the visible slots in the new order
    {
        const auto stored = keys({"app_A", "app_hidden", "app_B", "app_removed", "app_C"});
        const auto registered = keys({"app_A", "app_hidden", "app_B", "app_C"});
        const auto saved = merge(stored, registered, keys({"app_C", "app_A", "app_B"}));
        assert((saved == keys({"app_C", "app_hidden", "app_A", "app_removed", "app_B"})));
    }
    // a new app shown since the last save (last on the grid until moved) gets a slot at the end
    {
        const auto stored = keys({"app_A", "app_B"});
        const auto saved = merge(stored, keys({"app_A", "app_B", "app_new"}), keys({"app_B", "app_A", "app_new"}));
        assert((saved == keys({"app_B", "app_A", "app_new"})));
    }
    // duplicates in the stored list are dropped
    assert((merge(keys({"app_A", "app_A", "app_B"}), {}, keys({"app_A", "app_B"})) == keys({"app_A", "app_B"})));
    // bounded: the oldest ids of gone tiles are forgotten first, the shown and registered ones stay
    {
        std::vector<std::uint32_t> stored;
        for (std::uint32_t i = 0; i < kMaxSaved; ++i) stored.push_back(0x20000000u + i);
        const auto shown = keys({"app_A", "app_B"});
        const auto saved = merge(stored, shown, shown);
        assert(saved.size() == kMaxSaved);
        assert(saved[0] == 0x20000002u);
        assert(saved[kMaxSaved - 2] == key_of("app_A") && saved[kMaxSaved - 1] == key_of("app_B"));
    }

    // ---------------------------------------------------------------- store, load, save, reset (real config service)
    {
        Store store;
        // nothing stored: no user order, no key written by a load
        assert(load(store.call()).empty());
        assert(!store.has("home_order"));
        // reset with nothing stored: done, nothing written
        assert(reset(store.call()));
        assert(store.file.empty());

        // first save after a move: every registered tile, hidden built-ins included
        std::vector<std::string> visible = {"app_desktop_x", "app_Setting", "app_Store", "app_CLI", "app_Math"};
        assert(save(store.call(), kBuiltins, visible));
        assert(store.has("home_order") && !store.has("home_order2"));
        const auto first = load(store.call());
        assert(first.size() == kBuiltins.size() + 1);
        assert(first[0] == key_of("app_desktop_x"));
        assert(parse(store.get("home_order")) == first);

        // restart: the same order comes back
        const std::vector<std::string> today_grid = {"app_Setting", "app_Store", "app_CLI", "app_Math",
                                                     "app_desktop_x"};
        assert(grid(today_grid, load(store.call())) == visible);

        // a new app lands last
        std::vector<std::string> with_new = today_grid;
        with_new.insert(with_new.begin() + 4, "app_desktop_new");   // today's order puts it before x
        auto shown = grid(with_new, load(store.call()));
        assert(shown.back() == "app_desktop_new");
        assert(shown.front() == "app_desktop_x");

        // move the new app to the second place and save; then remove it and reinstall it: it returns to its place
        shown = {"app_desktop_x", "app_desktop_new", "app_Setting", "app_Store", "app_CLI", "app_Math"};
        assert(save(store.call(), kBuiltins, shown));
        assert(grid(with_new, load(store.call())) == shown);
        assert((grid(today_grid, load(store.call())) ==
                std::vector<std::string>{"app_desktop_x", "app_Setting", "app_Store", "app_CLI", "app_Math"}));
        // a save while it is removed keeps its slot
        assert(save(store.call(), kBuiltins,
                    {"app_desktop_x", "app_Setting", "app_Store", "app_CLI", "app_Math"}));
        assert(grid(with_new, load(store.call())) == shown);

        // reset: every home_order key cleared (empty values: the store has no delete), today's grid again,
        // app_order untouched
        store.service.api_call({"SetManyAndSave", "app_order", "deadbeef"}, nullptr);
        assert(reset(store.call()));
        assert(store.get("home_order").empty());
        assert(load(store.call()).empty());
        assert(grid(today_grid, load(store.call())) == today_grid);
        assert(store.get("app_order") == "deadbeef");
        // a second reset writes nothing
        const auto before = store.file;
        assert(reset(store.call()));
        assert(store.file == before);
    }
    // a long order takes several chunks; a shorter one later clears the chunks it no longer needs
    {
        Store store;
        std::vector<std::string> many;
        for (int i = 0; i < 60; ++i) many.push_back("app_desktop_" + std::to_string(i));
        assert(save(store.call(), many, many));
        assert(store.has("home_order") && store.has("home_order2") && store.has("home_order3"));
        assert(!store.has("home_order4"));
        assert(load(store.call()).size() == 60);
        assert(home_order::store(store.call(), keys({"app_A"})));
        assert(load(store.call()) == keys({"app_A"}));
        assert(store.get("home_order2").empty() && store.get("home_order3").empty());
        assert(reset(store.call()));
        for (std::size_t chunk = 0; chunk < kChunks; ++chunk) {
            const std::string value = store.get(chunk_name(chunk));
            assert(value.empty() || value == "<absent>");
        }
    }
    // full store: the save fails (no silent drop), nothing is written, the caller can say "cannot save"
    {
        Store store(filler(64));
        assert(!save(store.call(), kBuiltins, {"app_Store", "app_Setting"}));
        assert(!store.has("home_order"));
        assert(load(store.call()).empty());
        // one free entry: a short order fits
        Store almost(filler(63));
        assert(save(almost.call(), kBuiltins, {"app_Store", "app_Setting"}));
        assert(almost.has("home_order"));
        // a store already holding the key accepts a new order even when full
        assert(almost.file.size() == 64);
        assert(save(almost.call(), kBuiltins, {"app_Setting", "app_Store"}));
        // but an order needing a second chunk is refused as a whole
        std::vector<std::string> many;
        for (int i = 0; i < 40; ++i) many.push_back("app_desktop_" + std::to_string(i));
        const auto before = almost.file;
        assert(!save(almost.call(), many, many));
        assert(almost.file == before);
    }
    // no config service at all: nothing to read, a save fails
    assert(load(ConfigCall{}).empty());
    assert(!save(ConfigCall{}, kBuiltins, {"app_Setting"}));

    std::puts("test_home_order: ok");
    return 0;
}
