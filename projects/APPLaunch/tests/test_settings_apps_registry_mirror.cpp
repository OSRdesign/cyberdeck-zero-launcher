/*
 * SPDX-License-Identifier: MIT
 *
 * The Store's shared source list kept by the launcher (settings_apps_registry_mirror): pack / parse / round trip with
 * the Store's escaping, the 16 limit, the empty-list guard, the reconcile decision, and the format compatibility with
 * the Store's own code in both directions (projects/AppStore/main/interface/shared_registry_store.cpp, compiled
 * read-only into this test only), including the Store's start: import the shared list over the backend file.
 */

#include "settings_apps_registry_mirror.hpp"

#include "shared_registry_store.hpp"

#include <cstdio>
#include <map>
#include <string>

namespace am = apps_registry_mirror;

static int g_failed = 0;
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failed; \
        } \
    } while (0)

namespace {

const char *kHubUrl = "https://cardputerzero.github.io/generated/registry.json";
const char *kOurUrl = "https://raw.githubusercontent.com/OSRdesign/cyberdeck-zero-apps/main/registry.json";

am::Entry hub()
{
    am::Entry e;
    e.name = "CardputerZero Hub";
    e.url = kHubUrl;
    e.builtin = true;
    e.region = "default";
    return e;
}

am::Entry ours()
{
    am::Entry e;
    e.name = "OSRdesign/cyberdeck-zero-apps";
    e.url = kOurUrl;
    return e;
}

am::Config config_of(std::vector<am::Entry> entries)
{
    am::Config c;
    c.entries = std::move(entries);
    return c;
}

using Store = std::map<std::string, std::string>;

void store_keys(Store &store, const am::KeyValues &keys)
{
    for (const auto &pair : keys) store[pair.first] = pair.second;
}

am::Config read_back(const Store &store, bool *ok = nullptr)
{
    am::Config c;
    const bool found = am::parse([&](const std::string &k) {
        auto it = store.find(k);
        return it == store.end() ? std::string() : it->second;
    }, c);
    if (ok) *ok = found;
    return c;
}

const char *kBackendOutput =
    "CONFIG\tauto\tdefault\t2\n"
    "CONFIG_REG\t0\tCardputerZero Hub\thttps://cardputerzero.github.io/generated/registry.json\t1\t1\tdefault\n"
    "CONFIG_REG\t1\tOSRdesign/cyberdeck-zero-apps\t"
    "https://raw.githubusercontent.com/OSRdesign/cyberdeck-zero-apps/main/registry.json\t0\t0\t\n";

void test_parse_backend()
{
    am::Config c;
    CHECK(am::parse_backend(kBackendOutput, c));
    CHECK(c.region == "auto" && c.active_region == "default");
    CHECK(c.entries.size() == 2);
    CHECK(c.entries[0].builtin && c.entries[0].enabled && c.entries[0].region == "default");
    CHECK(!c.entries[1].builtin && !c.entries[1].enabled && c.entries[1].url == kOurUrl && c.entries[1].region.empty());
    CHECK(!am::parse_backend("", c));
    CHECK(!am::parse_backend("INFO\tnoise\nERROR\tx\n", c));
    CHECK(am::parse_backend(std::string("12:00 log line\r\n") + kBackendOutput, c) && c.entries.size() == 2);
}

void test_pack_layout()
{
    am::KeyValues keys;
    CHECK(am::pack(config_of({hub(), ours()}), keys));
    CHECK(keys.size() == 3 + 16);
    CHECK(keys[0].first == "appstore.registry.region" && keys[0].second == "auto");
    CHECK(keys[1].first == "appstore.registry.active_region" && keys[1].second == "default");
    CHECK(keys[2].first == "appstore.registry.count" && keys[2].second == "2");
    CHECK(keys[3].first == "appstore.registry.0.entry");
    CHECK(keys[3].second == std::string("CardputerZero Hub\t") + kHubUrl + "\t1\t1\tdefault");
    CHECK(keys[4].second == std::string("OSRdesign/cyberdeck-zero-apps\t") + kOurUrl + "\t1\t0\t");
    CHECK(keys[5].first == "appstore.registry.2.entry" && keys[5].second.empty());
    CHECK(keys.back().first == "appstore.registry.15.entry" && keys.back().second.empty());
    am::Config c = config_of({ours()});
    c.region.clear();
    c.active_region.clear();
    CHECK(am::pack(c, keys) && keys[0].second == "auto" && keys[1].second == "default");
}

void test_round_trip_and_escaping()
{
    am::Entry odd;
    odd.name = "a\tb\\c\nd\re";
    odd.url = "https://x.example/p?q=1\\2";
    odd.enabled = false;
    odd.region = "CN";
    am::Entry back;
    CHECK(am::unpack_entry(am::pack_entry(odd), back));
    CHECK(back.name == odd.name && back.url == odd.url && !back.enabled && !back.builtin && back.region == "CN");
    CHECK(am::pack_entry(odd).find('\n') == std::string::npos);

    Store store;
    am::KeyValues keys;
    CHECK(am::pack(config_of({hub(), odd, ours()}), keys));
    store_keys(store, keys);
    bool ok = false;
    const am::Config c = read_back(store, &ok);
    CHECK(ok && c.entries.size() == 3 && c.entries[1].name == odd.name && c.entries[2].url == kOurUrl);
}

void test_limits_and_guards()
{
    std::vector<am::Entry> many;
    for (int i = 0; i < 20; ++i) {
        am::Entry e = ours();
        e.url += std::to_string(i);
        many.push_back(e);
    }
    am::KeyValues keys;
    CHECK(am::pack(config_of(many), keys));
    CHECK(keys[2].second == "16");
    CHECK(!keys[3 + 15].second.empty() && keys.size() == 19);

    // unused slots are cleared like the Store's save(): shrink 3 -> 1
    Store store;
    am::KeyValues three, one;
    CHECK(am::pack(config_of({hub(), ours(), ours()}), three));
    store_keys(store, three);
    CHECK(am::pack(config_of({hub()}), one));
    store_keys(store, one);
    CHECK(store["appstore.registry.count"] == "1" && store["appstore.registry.1.entry"].empty() &&
          store["appstore.registry.2.entry"].empty());

    // an empty list is never written
    CHECK(!am::pack(config_of({}), keys) && keys.empty());
    // an entry too long for a config value is left out, not truncated
    am::Entry big = ours();
    big.name = std::string(300, 'n');
    int skipped = 0;
    CHECK(am::pack(config_of({hub(), big}), keys, &skipped) && skipped == 1 && keys[2].second == "1");
    CHECK(!am::pack(config_of({big}), keys, &skipped) && skipped == 1);
    // the Store ignores an invalid slot and a missing count
    Store bad;
    bool ok = true;
    read_back(bad, &ok);
    CHECK(!ok);
    bad["appstore.registry.count"] = "0";
    read_back(bad, &ok);
    CHECK(!ok);
    bad["appstore.registry.count"] = "2";
    bad["appstore.registry.0.entry"] = "garbage";
    bad["appstore.registry.1.entry"] = am::pack_entry(ours());
    const am::Config c = read_back(bad, &ok);
    CHECK(ok && c.entries.size() == 1);
}

void test_equivalent()
{
    CHECK(am::equivalent(config_of({hub(), ours()}), config_of({hub(), ours()})));
    CHECK(!am::equivalent(config_of({hub(), ours()}), config_of({hub()})));
    am::Entry off = ours();
    off.enabled = false;
    CHECK(!am::equivalent(config_of({hub(), ours()}), config_of({hub(), off})));
    am::Config a = config_of({hub()});
    am::Config b = a;
    b.region.clear();
    CHECK(am::equivalent(config_of({hub()}), config_of({hub()})) && !am::equivalent(a, config_of({})));
    CHECK(am::equivalent(config_of({}), config_of({})));
}

am::PlanInput input(const am::Config &backend, const am::Config *mirror, bool flag)
{
    am::PlanInput in;
    in.backend_read = true;
    in.backend = backend;
    if (mirror) {
        in.mirror_present = true;
        in.mirror = *mirror;
    }
    in.flag_set = flag;
    return in;
}

void test_plan()
{
    const am::Config only_hub = config_of({hub()});
    const am::Config with_ours = config_of({hub(), ours()});

    // backend unknown or empty: nothing, whatever the mirror holds
    am::PlanInput none;
    am::Plan p = am::plan(none);
    CHECK(!p.write_mirror && !p.add_default && !p.set_flag);
    p = am::plan(input(config_of({}), &with_ours, false));
    CHECK(!p.write_mirror && !p.add_default && !p.set_flag);

    // the deck: backend wiped to the hub (and the Store's mirror too), flag not set -> add the default
    p = am::plan(input(only_hub, &only_hub, false));
    CHECK(!p.write_mirror && p.add_default && !p.set_flag);
    // fresh board, no mirror at all
    p = am::plan(input(only_hub, nullptr, false));
    CHECK(p.write_mirror && p.add_default && !p.set_flag);
    // a source of the user's: never add, record the decision; stale mirror follows the backend
    p = am::plan(input(with_ours, &only_hub, false));
    CHECK(p.write_mirror && !p.add_default && p.set_flag);
    // the user removed it later: flag stays, nothing re-added, the mirror follows
    p = am::plan(input(only_hub, &with_ours, true));
    CHECK(p.write_mirror && !p.add_default && !p.set_flag);
    // all in step
    p = am::plan(input(with_ours, &with_ours, true));
    CHECK(!p.write_mirror && !p.add_default && !p.set_flag);
    // a disabled custom source still counts as a source of the user's
    am::Entry off = ours();
    off.enabled = false;
    p = am::plan(input(config_of({hub(), off}), nullptr, false));
    CHECK(!p.add_default && p.set_flag);
    // the launcher start (flag already decided): a seeded source reaches the mirror the Store imports, nothing is added
    p = am::plan(input(with_ours, &only_hub, true));
    CHECK(p.write_mirror && !p.add_default && !p.set_flag);
    // already equal: a no-op (no config write)
    p = am::plan(input(with_ours, &with_ours, true));
    CHECK(!p.write_mirror);
    // not read: nothing
    am::PlanInput unread = input(with_ours, &only_hub, false);
    unread.backend_read = false;
    p = am::plan(unread);
    CHECK(!p.write_mirror && !p.add_default && !p.set_flag);
}

/* ---- the Store's own code ------------------------------------------------------------------------------------ */

appstore::SharedRegistryStore store_over(Store &data, int *saves)
{
    return appstore::SharedRegistryStore(
        [&data](const std::string &k, const std::string &fallback) {
            auto it = data.find(k);
            return it == data.end() ? fallback : it->second;
        },
        [&data](const std::string &k, const std::string &v) { data[k] = v; },
        [saves] { if (saves) ++*saves; });
}

void test_store_compat_mirror_to_store()
{
    am::Entry odd = ours();
    odd.name = "tab\there\\and\nnewline";
    odd.enabled = false;
    Store data;
    am::KeyValues keys;
    CHECK(am::pack(config_of({hub(), odd, ours()}), keys));
    store_keys(data, keys);

    // the Store's start: import the shared list (what RegistryPersistenceService::initialize does)
    appstore::RegistryConfig loaded;
    CHECK(store_over(data, nullptr).load(loaded));
    CHECK(loaded.region == "auto" && loaded.active_region == "default");
    CHECK(loaded.entries.size() == 3);
    CHECK(loaded.entries[0].builtin && loaded.entries[0].name == "CardputerZero Hub" && loaded.entries[0].region == "default");
    CHECK(loaded.entries[1].name == odd.name && !loaded.entries[1].enabled && !loaded.entries[1].builtin);
    CHECK(loaded.entries[2].url == kOurUrl && loaded.entries[2].enabled);
}

void test_store_compat_store_to_mirror()
{
    appstore::RegistryConfig config;
    config.region = "CN";
    config.active_region = "CN";
    appstore::RegistryEntry b;
    b.name = "Hub CN";
    b.url = "https://cn.example/registry.json";
    b.builtin = true;
    b.region = "CN";
    appstore::RegistryEntry c;
    c.name = "we\\ird\tname\n";
    c.url = kOurUrl;
    c.enabled = false;
    config.entries = {b, c};
    Store data;
    int saves = 0;
    CHECK(store_over(data, &saves).save(config) && saves == 1);

    bool ok = false;
    const am::Config read = read_back(data, &ok);
    CHECK(ok && read.region == "CN" && read.active_region == "CN" && read.entries.size() == 2);
    CHECK(read.entries[0].builtin && read.entries[0].region == "CN");
    CHECK(read.entries[1].name == c.name && !read.entries[1].enabled && read.entries[1].url == kOurUrl);

    // the same list packed by both sides is byte-identical, slot clearing included
    Store mine;
    am::KeyValues keys;
    CHECK(am::pack(read, keys));
    store_keys(mine, keys);
    CHECK(mine == data);
}

/* The deck's failure and its fix, end to end: the Store imports its shared list over the backend file. */
void test_store_start_does_not_wipe_a_mirrored_source()
{
    // the backend file as Settings > Apps leaves it
    am::Config backend;
    CHECK(am::parse_backend(kBackendOutput, backend));
    // before the fix: the Store's own copy held only the hub
    Store data;
    am::KeyValues keys;
    CHECK(am::pack(config_of({hub()}), keys));
    store_keys(data, keys);
    appstore::RegistryConfig imported;
    CHECK(store_over(data, nullptr).load(imported) && imported.entries.size() == 1);   // wipes the source
    // the launcher's reconcile: the backend list becomes the mirror
    const am::Config mirror = read_back(data);
    const am::Plan plan = am::plan(input(backend, &mirror, true));
    CHECK(plan.write_mirror);
    CHECK(am::pack(backend, keys));
    store_keys(data, keys);
    CHECK(store_over(data, nullptr).load(imported) && imported.entries.size() == 2 && imported.entries[1].url == kOurUrl);
}

} // namespace

int main()
{
    test_parse_backend();
    test_pack_layout();
    test_round_trip_and_escaping();
    test_limits_and_guards();
    test_equivalent();
    test_plan();
    test_store_compat_mirror_to_store();
    test_store_compat_store_to_mirror();
    test_store_start_does_not_wipe_a_mirrored_source();
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("settings apps registry mirror: all checks passed");
    return 0;
}
