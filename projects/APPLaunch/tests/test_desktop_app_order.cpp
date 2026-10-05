/*
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/model/desktop_app_order.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace desktop_app_order;

static std::vector<std::uint32_t> keys(std::initializer_list<const char *> names)
{
    std::vector<std::uint32_t> out;
    for (const char *name : names) out.push_back(key_of(name));
    return out;
}

int main()
{
    // FNV-1a reference values.
    assert(key_of("") == 2166136261u);
    assert(key_of("a") == 0xe40c292cu);

    // text round trip, junk is ignored
    const auto list = keys({"a.desktop", "b.desktop", "c.desktop"});
    assert(parse(format(list)) == list);
    assert(parse("").empty());
    assert(parse("zz,1234,,0000000G,0000000a") == std::vector<std::uint32_t>{0xau});

    // first run: nothing saved, the fallback (file time) order is kept and saved
    const auto fallback = keys({"files.desktop", "lanscan.desktop", "viz1090.desktop"});
    Result first = arrange(fallback, {});
    assert((first.order == std::vector<std::size_t>{0, 1, 2}));
    assert(first.saved == fallback);

    // upgrade: lanscan is rebuilt, so its file time is now the newest; it keeps its place
    const auto after_upgrade = keys({"files.desktop", "viz1090.desktop", "lanscan.desktop"});
    Result upgraded = arrange(after_upgrade, first.saved);
    assert((upgraded.order == std::vector<std::size_t>{0, 2, 1}));   // files, lanscan, viz1090
    assert(upgraded.saved == first.saved);

    // first install of a new app goes to the end, even with an old file time
    const auto with_new = keys({"newapp.desktop", "files.desktop", "lanscan.desktop", "viz1090.desktop"});
    Result added = arrange(with_new, first.saved);
    assert((added.order == std::vector<std::size_t>{1, 2, 3, 0}));
    assert(added.saved.size() == 4 && added.saved.back() == key_of("newapp.desktop"));

    // two new apps keep their relative (file time) order
    const auto two_new = keys({"x.desktop", "files.desktop", "y.desktop"});
    Result both = arrange(two_new, keys({"files.desktop"}));
    assert((both.order == std::vector<std::size_t>{1, 0, 2}));

    // removal: the app is gone, the others keep their order; the key stays so a reinstall returns to its place
    const auto removed = keys({"files.desktop", "viz1090.desktop"});
    Result gone = arrange(removed, first.saved);
    assert((gone.order == std::vector<std::size_t>{0, 1}));
    assert(gone.saved == first.saved);
    const auto back = keys({"files.desktop", "viz1090.desktop", "lanscan.desktop"});
    Result again = arrange(back, gone.saved);
    assert((again.order == std::vector<std::size_t>{0, 2, 1}));

    // the saved list is bounded and forgets absent apps first
    std::vector<std::uint32_t> many;
    for (std::uint32_t i = 0; i < kMaxSaved + 10; ++i) many.push_back(1000 + i);
    const std::vector<std::uint32_t> present = {1000 + static_cast<std::uint32_t>(kMaxSaved) + 9, 7};
    Result bounded = arrange(present, many);
    assert(bounded.saved.size() == kMaxSaved);
    assert(bounded.saved.back() == 7);
    assert(bounded.order.size() == 2);

    // duplicates in the saved list and in the candidates never hide an app
    Result dup = arrange({5, 5, 6}, {6, 6, 5});
    assert(dup.order.size() == 3);

    // chunks: at most 27 keys each, the pieces rebuild the list
    std::vector<std::uint32_t> sixty;
    for (std::uint32_t i = 0; i < 60; ++i) sixty.push_back(i * 7919u);
    const auto chunks = split_chunks(sixty);
    assert(chunks.size() == kChunks);
    std::string joined;
    for (const auto &chunk : chunks) {
        assert(chunk.size() <= 255);
        if (chunk.empty()) continue;
        if (!joined.empty()) joined += ",";
        joined += chunk;
    }
    assert(parse(joined) == sixty);
    assert(chunk_name(0) == "app_order" && chunk_name(1) == "app_order2" && chunk_name(4) == "app_order5");
    return 0;
}
