// SPDX-License-Identifier: MIT
//
// Settings > Updates, the visible waits (report 047 P4, the owner's "is it frozen?" requirement): the elapsed counter, the
// stage bar that never moves backwards, the unknown-length fraction, the 20 s rule, the moving segment, the lines.

#include "settings_wait_model.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace settings_wait;

int main()
{
    // elapsed: mm:ss, minutes not capped, never negative
    assert(elapsed_text(0) == "00:00" && elapsed_text(999) == "00:00" && elapsed_text(23000) == "00:23");
    assert(elapsed_text(65400) == "01:05" && elapsed_text(75LL * 60 * 1000 + 2000) == "75:02" && elapsed_text(-5) == "00:00");
    assert(with_elapsed("Reading the package lists", 23500) == "Reading the package lists  00:23");

    // stage n of N: 0..99, the stage's share plus how far inside it; never 100 before the end
    assert(stage_percent(1, 4, 0.0) == 0 && stage_percent(2, 4, 0.0) == 25 && stage_percent(4, 4, 0.5) == 87);
    assert(stage_percent(4, 4, 1.0) == 99 && stage_percent(9, 4, 1.0) == 99 && stage_percent(0, 4, 0.5) == 0);
    assert(stage_percent(2, 0, 0.5) == 0 && stage_percent(3, 4, -1.0) == 50);
    // a stage of unknown length (the closure's checks): grows with each step, never reaches 1
    double last = -1;
    for (int n = 0; n < 500; ++n) {
        const double f = unknown_fraction(n);
        assert(f > last && f < 1.0);
        last = f;
    }
    assert(unknown_fraction(0) == 0.0 && unknown_fraction(3) == 0.5);

    // the bar never moves backwards during one wait (a lower estimate is ignored), until reset
    Monotonic bar;
    assert(bar.value(25) == 25 && bar.value(10) == 25 && bar.value(60) == 60 && bar.value(-3) == 60 && bar.value(140) == 100);
    bar.reset();
    assert(bar.last() == 0 && bar.value(5) == 5);
    // stages with a closure that needed a new round: the raw estimate falls, the bar stays
    Monotonic plan;
    int shown = plan.value(stage_percent(4, 4, unknown_fraction(6)));
    assert(plan.value(stage_percent(3, 4, 0.0)) == shown);

    // the 20 s rule
    assert(!still(0) && !still(19999) && still(20000) && still(600000));

    // the moving segment: 30 % wide at most, inside 0..100, across and back in 2 s, the same at the same time
    for (long long t = 0; t < 6000; t += 50) {
        int a = 0, b = 0;
        sweep(t, a, b);
        assert(a >= 0 && b <= 100 && a <= b && b - a <= kSweepWidth);
        int a2 = 0, b2 = 0;
        sweep(t + kSweepMs, a2, b2);
        assert(a == a2 && b == b2);
    }
    int s0 = 0, e0 = 0, s1 = 0, e1 = 0;
    sweep(0, s0, e0);
    sweep(kSweepMs / 2, s1, e1);
    assert(e0 == 0 && e1 > e0);

    // the lines: the box's sentences, the short ones fit a list row at 480 px
    assert(std::string(slow_line()) == "This board is slow: it can take a minute. Nothing is frozen.");
    assert(std::string(still_line()) == "Still working... the packages list is long");
    assert(std::string(slow_row()).size() <= 32 && std::string(still_row()).size() <= 32);
    std::printf("settings_wait_model: all passed\n");
    return 0;
}
