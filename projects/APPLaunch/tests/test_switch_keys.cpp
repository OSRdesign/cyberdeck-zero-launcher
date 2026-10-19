// SPDX-License-Identifier: MIT
//
// Alt+Tab / Super key session (task 015, study 026 section 5.3 "Alt+Tab" sequences), table-driven: each row is one
// key event and the event plus consume flag it must give.
#include "multitask/switch_keys.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace multitask;

namespace {

int g_failures = 0;
int g_checks = 0;

struct Row {
    unsigned code;
    int value;
    const char *event; /* switch_event_name */
    bool consume;
};

constexpr unsigned kA = 30; /* KEY_A */
constexpr unsigned kEnter = 28;

void run(const char *name, const std::vector<Row> &rows, SwitchKeys &k)
{
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const SwitchResult r = k.key(rows[i].code, rows[i].value);
        ++g_checks;
        if (std::string(switch_event_name(r.event)) != rows[i].event || r.consume != rows[i].consume) {
            ++g_failures;
            std::printf("FAIL %s row %zu (code %u value %d): want %s/%d got %s/%d\n", name, i, rows[i].code,
                        rows[i].value, rows[i].event, rows[i].consume, switch_event_name(r.event), r.consume);
        }
    }
}

void run(const char *name, const std::vector<Row> &rows)
{
    SwitchKeys k;
    run(name, rows, k);
}

} // namespace

int main()
{
    using namespace multitask::keys;
    run("Alt+Tab, Tab, release Alt", {
        {kLeftAlt, 1, "none", false},
        {kTab, 1, "start", true},
        {kTab, 0, "none", true},
        {kTab, 1, "next", true},
        {kTab, 0, "none", true},
        {kLeftAlt, 0, "commit", true},
    });
    run("Shift+Tab goes back; Shift+Alt+Tab starts reversed", {
        {kLeftAlt, 1, "none", false},
        {kTab, 1, "start", true},
        {kTab, 0, "none", true},
        {kLeftShift, 1, "none", false},
        {kTab, 1, "prev", true},
        {kTab, 0, "none", true},
        {kLeftShift, 0, "none", false},
        {kLeftAlt, 0, "commit", true},
        {kRightShift, 1, "none", false},
        {kRightAlt, 1, "none", false},
        {kTab, 1, "start-reverse", true},
        {kTab, 0, "none", true},
        {kRightAlt, 0, "commit", true},
        {kRightShift, 0, "none", false},
    });
    run("Esc with Alt held cancels; the Alt release then does nothing", {
        {kLeftAlt, 1, "none", false},
        {kTab, 1, "start", true},
        {kTab, 0, "none", true},
        {kEsc, 1, "cancel", true},
        {kEsc, 2, "none", true},
        {kEsc, 0, "none", true},
        {kLeftAlt, 0, "none", false},
    });
    run("Tab alone: nothing, the app or page gets it", {
        {kTab, 1, "none", false},
        {kTab, 2, "none", false},
        {kTab, 0, "none", false},
    });
    run("Alt alone: nothing", {
        {kLeftAlt, 1, "none", false},
        {kLeftAlt, 2, "none", false},
        {kLeftAlt, 0, "none", false},
    });
    run("Tab held with Alt: repeats start nothing more (no lock either: the session owns Tab)", {
        {kLeftAlt, 1, "none", false},
        {kTab, 1, "start", true},
        {kTab, 2, "none", true},
        {kTab, 2, "none", true},
        {kTab, 0, "none", true},
        {kLeftAlt, 0, "commit", true},
    });
    run("both Alts: the session ends when the last one is released", {
        {kLeftAlt, 1, "none", false},
        {kRightAlt, 1, "none", false},
        {kTab, 1, "start", true},
        {kLeftAlt, 0, "none", false},
        {kRightAlt, 0, "commit", true},
        {kTab, 0, "none", true},
    });
    run("other keys during a session are the session's", {
        {kLeftAlt, 1, "none", false},
        {kTab, 1, "start", true},
        {kA, 1, "none", true},
        {kA, 0, "none", true},
        {kEnter, 1, "none", true},
        {kLeftAlt, 0, "commit", true},
        {kEnter, 0, "none", false},
    });
    run("Super tapped alone = Home; Super+A is a chord", {
        {kLeftMeta, 1, "none", false},
        {kLeftMeta, 0, "home", false},
        {kRightMeta, 1, "none", false},
        {kRightMeta, 2, "none", false},
        {kRightMeta, 0, "home", false},
        {kLeftMeta, 1, "none", false},
        {kA, 1, "none", false},
        {kA, 0, "none", false},
        {kLeftMeta, 0, "none", false},
        {kHomePage, 1, "home", false},
        {kHomePage, 0, "none", false},
    });
    {
        SwitchKeys k;
        run("reset ends a session without an event", {{kLeftAlt, 1, "none", false}, {kTab, 1, "start", true}}, k);
        k.reset();
        ++g_checks;
        if (k.active() || k.alt_held()) {
            ++g_failures;
            std::printf("FAIL reset: still active\n");
        }
        run("after reset", {{kLeftAlt, 0, "none", false}, {kTab, 0, "none", false}}, k);
    }
    run("an Alt release unseen (reader opened late) is harmless", {
        {kLeftAlt, 0, "none", false},
        {kTab, 1, "none", false},
        {kTab, 0, "none", false},
    });
    if (g_failures) {
        std::printf("test_switch_keys: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("test_switch_keys: %d checks passed\n", g_checks);
    return 0;
}
