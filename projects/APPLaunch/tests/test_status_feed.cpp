// SPDX-License-Identifier: MIT
//
// The launcher's status feed (report 032): when the publisher rewrites status.state. gen and pid are not part of the
// comparison, every shown value is. (The file itself, the reader and the stale-pid rule: cp0_lvgl tests/test_status_state.c.)
#include "status_feed.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                             \
        }                                                                             \
    } while (0)

int main()
{
    cp0_app_status_t a;
    std::memset(&a, 0, sizeof(a));
    a.pid = 100;
    a.gen = 3;
    a.wifi = 1;
    a.wifi_pct = 60;
    a.battery = 80;
    std::strcpy(a.battery_state, "Discharging");
    cp0_app_status_t b = a;
    CHECK(!status_feed::differs(a, b));

    b.gen = 4;
    b.pid = 200;
    CHECK(!status_feed::differs(a, b)); // a new process or generation alone is no change

    cp0_app_status_t c = a;
    c.wifi_pct = 61;
    CHECK(status_feed::differs(a, c));
    c = a;
    c.wifi = 0;
    CHECK(status_feed::differs(a, c));
    c = a;
    c.eth = 1;
    CHECK(status_feed::differs(a, c));
    c = a;
    c.bt = CP0_APP_BT_ON;
    CHECK(status_feed::differs(a, c));
    c = a;
    c.battery = 79;
    CHECK(status_feed::differs(a, c));
    c = a;
    std::strcpy(c.battery_state, "Charging");
    CHECK(status_feed::differs(a, c));
    // report 035: the levels apps draw their bars from
    c = a;
    c.volume = 40;
    CHECK(status_feed::differs(a, c));
    c = a;
    c.mute = 1;
    CHECK(status_feed::differs(a, c));
    c = a;
    c.brightness_pct = 50;
    CHECK(status_feed::differs(a, c));

    // the SDL stub: start and stop do nothing, twice or unpaired
    status_feed::stop();
    status_feed::start();
    status_feed::start();
    status_feed::stop();
    std::puts("test_status_feed: PASS");
    return 0;
}
