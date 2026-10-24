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

/* Task c-7: the Super hold on a fake clock. Each step is a key event at a time, or a tick (code 0) that must give
 * `event`; key steps check their event and consume as above. */
struct Step {
    std::uint64_t at;
    unsigned code; /* 0 = tick */
    int value;
    const char *event;
    bool consume;
};

constexpr unsigned kF14 = 184;

void timed(const char *name, const std::vector<Step> &steps, SwitchKeys &k)
{
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const Step &s = steps[i];
        ++g_checks;
        if (s.code == 0) {
            const SwitchEvent e = k.tick(s.at);
            if (std::string(switch_event_name(e)) != s.event) {
                ++g_failures;
                std::printf("FAIL %s step %zu (tick at %llu): want %s got %s\n", name, i,
                            static_cast<unsigned long long>(s.at), s.event, switch_event_name(e));
            }
            continue;
        }
        const SwitchResult r = k.key(s.code, s.value, s.at);
        if (std::string(switch_event_name(r.event)) != s.event || r.consume != s.consume) {
            ++g_failures;
            std::printf("FAIL %s step %zu (code %u value %d at %llu): want %s/%d got %s/%d\n", name, i, s.code, s.value,
                        static_cast<unsigned long long>(s.at), s.event, s.consume, switch_event_name(r.event), r.consume);
        }
    }
}

void timed(const char *name, const std::vector<Step> &steps)
{
    SwitchKeys k;
    timed(name, steps, k);
}

void expect_due(const char *name, const SwitchKeys &k, std::uint64_t want)
{
    ++g_checks;
    if (k.hold_due() != want) {
        ++g_failures;
        std::printf("FAIL %s: hold_due want %llu got %llu\n", name, static_cast<unsigned long long>(want),
                    static_cast<unsigned long long>(k.hold_due()));
    }
}

void test_super_hold()
{
    using namespace multitask::keys;
    ++g_checks;
    if (kSuperHoldMs != 600 || std::string(switch_event_name(SwitchEvent::Switcher)) != "switcher") {
        ++g_failures;
        std::printf("FAIL the hold is 600 ms and its event \"switcher\"\n");
    }
    {
        SwitchKeys k;
        expect_due("idle: nothing due", k, 0);
        timed("Super held 600 ms alone: the switcher once, its release is no Home", {
            {1000, kLeftMeta, 1, "none", false},
        }, k);
        expect_due("hold due at press + 600", k, 1600);
        timed("... ticks", {
            {1300, 0, 0, "none", false},
            {1599, 0, 0, "none", false},
            {1600, kLeftMeta, 2, "none", false}, /* the keyboard's own auto-repeat changes nothing */
            {1600, 0, 0, "switcher", false},
            {1700, 0, 0, "none", false},         /* once */
            {2500, 0, 0, "none", false},
            {2600, kLeftMeta, 0, "none", false}, /* not Home */
        }, k);
        expect_due("nothing due after", k, 0);
    }
    timed("a short tap stays Home (an app in front goes home); no switcher after", {
        {0, kLeftMeta, 1, "none", false},
        {150, 0, 0, "none", false},
        {200, kLeftMeta, 0, "home", false},
        {900, 0, 0, "none", false},
    });
    timed("Super+L (a chord typed on a keyboard): no switcher, no Home, L not consumed", {
        {0, kLeftMeta, 1, "none", false},
        {120, kA, 1, "none", false},
        {180, kA, 0, "none", false},
        {700, 0, 0, "none", false},
        {2000, 0, 0, "none", false},
        {2100, kLeftMeta, 0, "none", false},
    });
    timed("the FZ-M1 rotation-lock button (Win+F14), held: nothing, F14 not consumed", {
        {0, kLeftMeta, 1, "none", false},
        {5, kF14, 1, "none", false},
        {6, kF14, 0, "none", false},
        {900, 0, 0, "none", false},
        {950, kLeftMeta, 0, "none", false},
    });
    timed("Win+F14 pressed quickly: nothing either", {
        {0, kLeftMeta, 1, "none", false},
        {1, kF14, 1, "none", false},
        {2, kF14, 0, "none", false},
        {3, kLeftMeta, 0, "none", false},
        {700, 0, 0, "none", false},
    });
    timed("a key pressed after the switcher opened is the switcher's, Super's release is no Home", {
        {0, kLeftMeta, 1, "none", false},
        {600, 0, 0, "switcher", false},
        {800, kA, 1, "none", false},
        {850, kA, 0, "none", false},
        {900, kLeftMeta, 0, "none", false},
    });
    timed("the right Super key too", {
        {0, kRightMeta, 1, "none", false},
        {650, 0, 0, "switcher", false},
        {700, kRightMeta, 0, "none", false},
    });
    timed("a second Super press restarts the hold (a release never seen, or the second Super key)", {
        {0, kLeftMeta, 1, "none", false},
        {400, kRightMeta, 1, "none", false},
        {650, 0, 0, "none", false},
        {1000, 0, 0, "switcher", false},
        {1100, kRightMeta, 0, "none", false},
    });
    timed("Super with Alt held: no hold (the Alt+Tab keys); its tap stays what it was (Home)", {
        {0, kLeftAlt, 1, "none", false},
        {10, kLeftMeta, 1, "none", false},
        {800, 0, 0, "none", false},
        {900, kLeftMeta, 0, "home", false},
        {950, kLeftAlt, 0, "none", false},
    });
    timed("Alt pressed during the hold cancels it", {
        {0, kLeftMeta, 1, "none", false},
        {100, kLeftAlt, 1, "none", false},
        {100, kLeftAlt, 0, "none", false},
        {800, 0, 0, "none", false},
        {900, kLeftMeta, 0, "none", false},
    });
    timed("during an Alt+Tab session: no hold (the tap's Home is ignored by the supervisor during a session, as before)", {
        {0, kLeftAlt, 1, "none", false},
        {10, kTab, 1, "start", true},
        {20, kTab, 0, "none", true},
        {30, kLeftMeta, 1, "none", false},
        {900, 0, 0, "none", false},
        {950, kLeftMeta, 0, "home", false},
        {960, kLeftAlt, 0, "commit", true},
    });
    {
        SwitchKeys k;
        timed("reset forgets a hold", {{0, kLeftMeta, 1, "none", false}}, k);
        k.reset();
        expect_due("nothing due after reset", k, 0);
        timed("after reset", {{700, 0, 0, "none", false}, {800, kLeftMeta, 0, "none", false}}, k);
    }
    {
        /* c7.4, the tablet as it is: Super down, the kernel's auto-repeat from 250 ms every 30 ms (value 2), up at 3 s; a
         * tick after every event, as the supervisor's loop runs. The hold fires once, at the first tick from 600 ms */
        SwitchKeys k;
        std::vector<Step> steps = {{0, kLeftMeta, 1, "none", false}};
        for (std::uint64_t t = 250; t < 3000; t += 30) {
            steps.push_back({t, kLeftMeta, 2, "none", false});
            steps.push_back({t, 0, 0, t >= 600 && t < 630 ? "switcher" : "none", false});
        }
        steps.push_back({3000, kLeftMeta, 0, "none", false});
        timed("Super with auto-repeat for 3 s", steps, k);
        /* another key held before Super, auto-repeating during the hold (no new press): the hold still fires */
        SwitchKeys s;
        std::vector<Step> shifted = {{0, kLeftShift, 1, "none", false}, {100, kLeftMeta, 1, "none", false}};
        for (std::uint64_t t = 350; t < 900; t += 30) shifted.push_back({t, kLeftShift, 2, "none", false});
        shifted.push_back({900, 0, 0, "switcher", false});
        shifted.push_back({950, kLeftMeta, 0, "none", false});
        timed("a held key's auto-repeat does not cancel the hold", shifted, s);
    }
    {
        /* block_hold(): a Super the launcher reads right after it took the panel back may be the one still held from the
         * app (its hold already opened the switcher there): no second hold; its release is still a tap (c7.5: the second
         * tap of a double tap comes that soon); other keys unchanged */
        SwitchKeys k;
        timed("held across the hand-over", {{0, kLeftMeta, 1, "none", false}}, k);
        k.block_hold();
        expect_due("blocked: nothing due", k, 0);
        timed("after block_hold", {
            {700, 0, 0, "none", false},
            {900, kLeftMeta, 0, "home", false},
            {1000, kLeftAlt, 1, "none", false},
            {1010, kTab, 1, "start", true},
            {1020, kLeftAlt, 0, "commit", true},
            {2000, kLeftMeta, 1, "none", false},
            {2600, 0, 0, "switcher", false},
        }, k);
    }
    {
        /* the stale press of a reader that missed a release (the launcher's filter while an app was in front): the next
         * press restarts the hold from its own time, it does not fire at once */
        SwitchKeys k;
        timed("stale press", {{0, kLeftMeta, 1, "none", false}, {10, kA, 1, "none", false}}, k);
        timed("next press", {{50000, kLeftMeta, 1, "none", false}, {50001, 0, 0, "none", false}}, k);
        expect_due("due from the new press", k, 50600);
        timed("then", {{50600, 0, 0, "switcher", false}}, k);
    }
}

void check(const char *what, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

/* Task c-7.5: the double tap (owner decision 2026-10-24: the FZ-M1's Windows button is a pulse, down and up at once).
 * SwitchKeys marks the taps (press time), SuperTaps counts them; fed here as the supervisor feeds them. */
void test_super_taps()
{
    using namespace multitask::keys;
    check("the window is 450 ms, the same tap 40 ms", kSuperDoubleTapMs == 450 && kSuperSameTapMs == 40);
    {
        /* the tablet's pulse: down and up 1 ms apart, a tap with its press time; the HomePage key is Home but no tap */
        SwitchKeys k;
        (void)k.key(kLeftMeta, 1, 1000);
        const SwitchResult up = k.key(kLeftMeta, 0, 1001);
        check("pulse: Home, a Super tap pressed at 1000", up.event == SwitchEvent::Home && up.super_tap && up.tap_ms == 1000);
        const SwitchResult hp = k.key(kHomePage, 1, 2000);
        check("KEY_HOMEPAGE: Home, not a Super tap", hp.event == SwitchEvent::Home && !hp.super_tap);
        /* a chord (Super+A) and Win+F14 are no taps */
        (void)k.key(kLeftMeta, 1, 3000);
        (void)k.key(kA, 1, 3001);
        (void)k.key(kA, 0, 3002);
        check("Super+A: no tap", k.key(kLeftMeta, 0, 3003).event == SwitchEvent::None);
        (void)k.key(kLeftMeta, 1, 4000);
        (void)k.key(184, 1, 4000);
        (void)k.key(184, 0, 4001);
        check("Win+F14 pulse: no tap", k.key(kLeftMeta, 0, 4001).event == SwitchEvent::None);
        /* a keyboard that reports the real duration: held past the hold, its release is no tap */
        (void)k.key(kLeftMeta, 1, 5000);
        for (std::uint64_t t = 5250; t < 6000; t += 30) (void)k.key(kLeftMeta, 2, t);
        check("held: the hold fires", k.tick(5600) == SwitchEvent::Switcher);
        check("held: its release is no tap", !k.key(kLeftMeta, 0, 6000).super_tap);
        /* a tap with auto-repeat never reached (a quick press on a real keyboard): a tap */
        (void)k.key(kLeftMeta, 1, 7000);
        const SwitchResult quick = k.key(kLeftMeta, 0, 7120);
        check("a 120 ms press: a tap pressed at 7000", quick.super_tap && quick.tap_ms == 7000);
    }
    {
        SuperTaps t;
        check("a tap: first", t.tap(10000) == TapVerdict::First && !t.too_late());
        check("a second 250 ms later: double", t.tap(10250) == TapVerdict::Double && t.gap() == 250);
        check("a third 150 ms later: ignored", t.tap(10400) == TapVerdict::Ignored && t.gap() == 150);
        check("a fourth 400 ms after the double: still ignored", t.tap(10650) == TapVerdict::Ignored);
        check("after the window: first again", t.tap(10710) == TapVerdict::First);
        check("... and its pair: double", t.tap(11000) == TapVerdict::Double);
    }
    {
        SuperTaps t;
        check("slow pair: first", t.tap(20000) == TapVerdict::First);
        check("slow pair: 451 ms later, too late: a first tap", t.tap(20451) == TapVerdict::First && t.too_late() &&
                                                                    t.gap() == 451);
        check("... which pairs with the next", t.tap(20800) == TapVerdict::Double && t.gap() == 349);
    }
    {
        SuperTaps t;
        check("exactly 450 ms: first", t.tap(30000) == TapVerdict::First);
        check("exactly 450 ms: double", t.tap(30450) == TapVerdict::Double);
    }
    {
        /* the same tap reported by both readers at the hand-over (39 ms apart): once */
        SuperTaps t;
        check("hand-over: first", t.tap(40000) == TapVerdict::First);
        check("hand-over: the same tap, ignored", t.tap(40039) == TapVerdict::Duplicate);
        check("hand-over: the real second tap, double", t.tap(40300) == TapVerdict::Double);
        t.reset();
        check("reset (a hold opened the switcher): first", t.tap(40500) == TapVerdict::First);
    }
    {
        /* the whole chain as the supervisor runs it: pulses through SwitchKeys into SuperTaps */
        SwitchKeys k;
        SuperTaps t;
        std::string verdicts;
        const auto pulse = [&](std::uint64_t at) {
            (void)k.key(kLeftMeta, 1, at);
            const SwitchResult r = k.key(kLeftMeta, 0, at + 1);
            if (r.super_tap) verdicts += std::string(tap_verdict_name(t.tap(r.tap_ms))) + " ";
        };
        pulse(50000);
        pulse(50300); /* double */
        pulse(50500); /* ignored */
        pulse(52000); /* first */
        (void)k.key(kLeftMeta, 1, 52200); /* Super+L: no tap */
        (void)k.key(38, 1, 52210);
        (void)k.key(38, 0, 52220);
        (void)k.key(kLeftMeta, 0, 52230);
        pulse(52700); /* 700 ms after the last tap: first */
        check("pulses: first double ignored first first", verdicts == "first double ignored first first ");
        if (verdicts != "first double ignored first first ") std::printf("  got: %s\n", verdicts.c_str());
    }
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
    {
        /* the names of the session key trace (deck finding, build pb.2) */
        const struct {
            unsigned code;
            const char *name;
        } names[] = {{kEsc, "Esc"}, {kTab, "Tab"}, {kLeftAlt, "Alt"}, {kRightAlt, "RightAlt"}, {kLeftShift, "Shift"},
                     {kRightShift, "RightShift"}, {kLeftMeta, "Super"}, {kRightMeta, "RightSuper"},
                     {kHomePage, "HomePage"}, {kA, "code 30"}};
        for (const auto &n : names) {
            ++g_checks;
            if (switch_key_name(n.code) != n.name) {
                ++g_failures;
                std::printf("FAIL switch_key_name(%u): want %s got %s\n", n.code, n.name, switch_key_name(n.code).c_str());
            }
        }
    }
    run("deck order: Alt, Tab, Esc with autorepeat, Esc up, Alt up: cancel, then nothing", {
        {kLeftAlt, 1, "none", false},
        {kTab, 1, "start", true},
        {kTab, 0, "none", true},
        {kEsc, 1, "cancel", true},
        {kEsc, 2, "none", true},
        {kEsc, 2, "none", true},
        {kEsc, 0, "none", true},
        {kLeftAlt, 0, "none", false},
    });
    /* Backspace cancels like Esc (owner decision 2026-10-20, build pb.4: the M4 keyboard sends no Esc with Alt held) */
    run("Backspace with Alt held cancels; its repeats and release are swallowed; the Alt release does nothing", {
        {kLeftAlt, 1, "none", false},
        {kTab, 1, "start", true},
        {kTab, 0, "none", true},
        {kBackspace, 1, "cancel", true},
        {kBackspace, 2, "none", true},
        {kBackspace, 2, "none", true},
        {kBackspace, 0, "none", true},
        {kLeftAlt, 0, "none", false},
    });
    run("Backspace released after Alt: still swallowed (it was the session's)", {
        {kLeftAlt, 1, "none", false},
        {kTab, 1, "start", true},
        {kTab, 0, "none", true},
        {kBackspace, 1, "cancel", true},
        {kLeftAlt, 0, "none", false},
        {kBackspace, 2, "none", true},
        {kBackspace, 0, "none", true},
    });
    run("Backspace outside a session is the app's or page's, held Alt or not", {
        {kBackspace, 1, "none", false},
        {kBackspace, 2, "none", false},
        {kBackspace, 0, "none", false},
        {kLeftAlt, 1, "none", false},
        {kBackspace, 1, "none", false},
        {kBackspace, 0, "none", false},
        {kLeftAlt, 0, "none", false},
    });
    run("Backspace after a Backspace cancel: the next one types again", {
        {kLeftAlt, 1, "none", false},
        {kTab, 1, "start", true},
        {kBackspace, 1, "cancel", true},
        {kBackspace, 0, "none", true},
        {kLeftAlt, 0, "none", false},
        {kTab, 0, "none", true},
        {kBackspace, 1, "none", false},
        {kBackspace, 0, "none", false},
    });
    run("Esc and Backspace in one session: the first cancels, the second is not the session's", {
        {kLeftAlt, 1, "none", false},
        {kTab, 1, "start", true},
        {kBackspace, 1, "cancel", true},
        {kEsc, 1, "none", false},
        {kEsc, 0, "none", false},
        {kBackspace, 0, "none", true},
        {kLeftAlt, 0, "none", false},
    });
    run("Esc outside a session is untouched too", {
        {kEsc, 1, "none", false},
        {kEsc, 2, "none", false},
        {kEsc, 0, "none", false},
    });
    ++g_checks;
    if (switch_key_name(kBackspace) != "BackSpace") {
        ++g_failures;
        std::printf("FAIL switch_key_name(%u): want BackSpace got %s\n", kBackspace, switch_key_name(kBackspace).c_str());
    }
    test_super_hold();
    test_super_taps();
    if (g_failures) {
        std::printf("test_switch_keys: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("test_switch_keys: %d checks passed\n", g_checks);
    return 0;
}
