// SPDX-License-Identifier: MIT
//
// The `screen off` / `screen on` state (screen_gate.hpp): off, on, double off, on without off, another client, the
// restore rules (hide / launcher stop = release, the holder gone = poll), a hardware that cannot switch.
#include "screen_gate.hpp"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

namespace {

using app_request::ScreenGate;

struct FakeHw : app_request::ScreenHw {
    int level = 70;          // the raw backlight now
    int suspends = 0;
    int restores = 0;
    bool can_suspend = true;
    std::set<int> running;
    ScreenGate *gate = nullptr; // to see the flag at the moment of the write
    bool flag_during_suspend = false;

    int suspend() override
    {
        if (gate) flag_during_suspend = gate->is_off();
        if (!can_suspend) return -1;
        ++suspends;
        const int before = level;
        level = 0;
        return before;
    }
    void restore(int raw) override
    {
        ++restores;
        level = raw;
    }
    bool alive(int pid) override { return running.count(pid) != 0; }
};

} // namespace

int main()
{
    FakeHw hw;
    ScreenGate g(hw);
    hw.gate = &g;
    hw.running = {100, 200};

    // on without off: ok, nothing touched
    CHECK(!g.is_off() && g.owner() == 0);
    CHECK(g.on(100) == ScreenGate::Result::Ok && hw.restores == 0 && hw.level == 70);
    CHECK(!g.release() && !g.poll() && hw.restores == 0);

    // off, then on from the same client: the level before comes back
    CHECK(g.off(100) == ScreenGate::Result::Ok);
    CHECK(g.is_off() && g.owner() == 100 && hw.level == 0 && hw.suspends == 1);
    CHECK(hw.flag_during_suspend); // the flag is up before the write: nothing raises the light in between
    CHECK(g.on(100) == ScreenGate::Result::Ok);
    CHECK(!g.is_off() && g.owner() == 0 && hw.level == 70 && hw.restores == 1);

    // double off: the second changes nothing (the raw level is not read again: 0 would be restored)
    CHECK(g.off(100) == ScreenGate::Result::Ok && g.off(100) == ScreenGate::Result::Ok);
    CHECK(hw.suspends == 2 && hw.level == 0);
    CHECK(g.on(100) == ScreenGate::Result::Ok && hw.level == 70);
    CHECK(g.on(100) == ScreenGate::Result::Ok && hw.restores == 2); // a second on: nothing

    // another client: cannot switch it off again, cannot switch it on
    CHECK(g.off(100) == ScreenGate::Result::Ok);
    CHECK(g.off(200) == ScreenGate::Result::Busy && g.on(200) == ScreenGate::Result::Busy);
    CHECK(g.is_off() && g.owner() == 100 && hw.level == 0);
    CHECK(g.on(100) == ScreenGate::Result::Ok && hw.level == 70);

    // the panel changes hands (hide, another app in front, launcher in front) and the launcher stops: release
    CHECK(g.off(200) == ScreenGate::Result::Ok);
    CHECK(g.release() && !g.is_off() && hw.level == 70);
    CHECK(!g.release()); // twice: nothing

    // the holder is gone: poll restores; a running holder is left alone
    CHECK(g.off(100) == ScreenGate::Result::Ok);
    CHECK(!g.poll() && g.is_off() && hw.level == 0);
    hw.running.erase(100);
    const int restores = hw.restores;
    CHECK(g.poll() && !g.is_off() && hw.level == 70 && hw.restores == restores + 1);
    CHECK(!g.poll());
    // after that another client may take it
    CHECK(g.off(200) == ScreenGate::Result::Ok && g.owner() == 200);
    CHECK(g.on(200) == ScreenGate::Result::Ok);

    // a hardware that cannot switch off: Failed, the flag drops, nothing to restore
    hw.can_suspend = false;
    CHECK(g.off(100) == ScreenGate::Result::Failed && !g.is_off() && g.owner() == 0);
    CHECK(!g.release() && hw.level == 70);
    hw.can_suspend = true;
    CHECK(g.off(100) == ScreenGate::Result::Ok && hw.level == 0); // and it works again afterwards
    CHECK(g.on(100) == ScreenGate::Result::Ok && hw.level == 70);

    // the level that comes back is the one at the moment of off, not an older one
    hw.level = 40;
    CHECK(g.off(100) == ScreenGate::Result::Ok);
    CHECK(g.release() && hw.level == 40);

    std::puts("screen_gate: ok");
    return 0;
}
