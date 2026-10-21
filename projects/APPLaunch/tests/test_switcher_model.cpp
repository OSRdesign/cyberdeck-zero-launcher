// SPDX-License-Identifier: MIT
//
// Multitasking P-d (task 015-pd): the switcher screen's pure rules. The card states and dot colours, the keyboard
// selection over the 2x2 card grid and the bottom bar (arrows, Tab / Shift+Tab, cards that went), where an Alt+Tab
// session's selection lands, which canvases get the screen, the first-run hint.
#include "multitask/switcher_model.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace multitask;

namespace {

int g_failures = 0;
int g_checks = 0;

void expect(const std::string &what, const std::string &got, const std::string &want)
{
    ++g_checks;
    if (got == want) return;
    ++g_failures;
    std::printf("FAIL %s\n  want: %s\n  got:  %s\n", what.c_str(), want.c_str(), got.c_str());
}

std::string item_name(int item)
{
    if (item == kItemHome) return "Home";
    if (item == kItemCloseAll) return "CloseAll";
    return "card" + std::to_string(item);
}

std::string tone_name(CardTone tone)
{
    switch (tone) {
    case CardTone::Green: return "green";
    case CardTone::Grey: return "grey";
    case CardTone::Amber: return "amber";
    }
    return "?";
}

std::string label(State s, bool origin)
{
    const CardLabel l = card_label(s, origin);
    return std::string(l.text) + "/" + tone_name(l.tone);
}

void test_labels()
{
    expect("running", label(State::HiddenRunning, false), "Running/green");
    expect("paused", label(State::HiddenFrozen, false), "Paused/grey");
    expect("not responding", label(State::NotResponding, false), "Not responding/amber");
    expect("origin running: In front", label(State::HiddenRunning, true), "In front/green");
    expect("origin frozen: In front", label(State::HiddenFrozen, true), "In front/green");
    expect("origin not responding: amber wins", label(State::NotResponding, true), "Not responding/amber");
    expect("foreground", label(State::Foreground, false), "In front/green");
    expect("held behind the sheet", label(State::Held, false), "Paused/grey");
    expect("closing", label(State::Closing, false), "Closing/grey");
}

struct Move {
    int from;
    SwitcherKey key;
    int cards;
    int want;
};

const char *key_name(SwitcherKey k)
{
    switch (k) {
    case SwitcherKey::Left: return "Left";
    case SwitcherKey::Right: return "Right";
    case SwitcherKey::Up: return "Up";
    case SwitcherKey::Down: return "Down";
    case SwitcherKey::Next: return "Tab";
    case SwitcherKey::Prev: return "Shift+Tab";
    }
    return "?";
}

void test_moves()
{
    using K = SwitcherKey;
    const Move moves[] = {
        /* four cards: the full grid */
        {0, K::Right, 4, 1}, {1, K::Right, 4, 1}, {1, K::Left, 4, 0}, {0, K::Left, 4, 0},
        {0, K::Down, 4, 2},  {1, K::Down, 4, 3},  {2, K::Down, 4, kItemHome}, {3, K::Down, 4, kItemCloseAll},
        {kItemHome, K::Up, 4, 2}, {kItemCloseAll, K::Up, 4, 3}, {2, K::Up, 4, 0}, {0, K::Up, 4, 0},
        {kItemHome, K::Right, 4, kItemCloseAll}, {kItemCloseAll, K::Left, 4, kItemHome},
        {kItemHome, K::Down, 4, kItemHome}, {kItemCloseAll, K::Right, 4, kItemCloseAll},
        /* three cards (the slot "Room for one more" is not selectable) */
        {2, K::Right, 3, 2}, {1, K::Down, 3, kItemCloseAll}, {kItemCloseAll, K::Up, 3, 1}, {kItemHome, K::Up, 3, 2},
        /* one card */
        {0, K::Right, 1, 0}, {0, K::Down, 1, kItemHome}, {kItemCloseAll, K::Up, 1, 0}, {kItemHome, K::Up, 1, 0},
        /* Tab / Shift+Tab: reading order over what exists, wrapping */
        {0, K::Next, 4, 1}, {3, K::Next, 4, kItemHome}, {kItemHome, K::Next, 4, kItemCloseAll},
        {kItemCloseAll, K::Next, 4, 0}, {0, K::Prev, 4, kItemCloseAll}, {kItemHome, K::Prev, 2, 1},
        {1, K::Next, 2, kItemHome}, {kItemCloseAll, K::Next, 1, 0},
        /* a selected card that went (its app was closed): clamped first */
        {3, K::Next, 2, kItemHome}, {3, K::Left, 2, 0}, {2, K::Up, 0, kItemHome},
    };
    for (const Move &m : moves)
        expect(item_name(m.from) + " " + key_name(m.key) + " with " + std::to_string(m.cards) + " cards",
               item_name(switcher_move(m.from, m.key, m.cards)), item_name(m.want));
    expect("clamp: a card that went", item_name(switcher_clamp(3, 3)), "card2");
    expect("clamp: no card left", item_name(switcher_clamp(0, 0)), "Home");
    expect("clamp: the bar stays", item_name(switcher_clamp(kItemCloseAll, 0)), "CloseAll");
}

void test_session_item()
{
    const std::vector<AppId> cards = {7, 3, 9};
    /* P-b's cycle from an app: the app in front, the others in MRU order, then Home */
    const std::vector<AppId> from_app = {7, 3, 9, kNoApp};
    expect("Alt+Tab from an app starts on the previous app", item_name(session_item(from_app, 1, cards)), "card1");
    expect("Tab again", item_name(session_item(from_app, 2, cards)), "card2");
    expect("Tab onto Home", item_name(session_item(from_app, 3, cards)), "Home");
    expect("Shift+Tab back to the app in front", item_name(session_item(from_app, 0, cards)), "card0");
    /* from the launcher: Home first, then the apps */
    const std::vector<AppId> from_home = {kNoApp, 7, 3, 9};
    expect("Alt+Tab from the grid starts on the last app", item_name(session_item(from_home, 1, cards)), "card0");
    expect("an app without a card selects Home", item_name(session_item({5, 6}, 1, cards)), "Home");
    expect("out of range", item_name(session_item(from_app, 9, cards)), "Home");
}

void test_fits_and_hint()
{
    expect("640x480", switcher_fits(640, 480) ? "yes" : "no", "yes");
    expect("800x480", switcher_fits(800, 480) ? "yes" : "no", "yes");
    expect("720x720", switcher_fits(720, 720) ? "yes" : "no", "yes");
    expect("1280x720", switcher_fits(1280, 720) ? "yes" : "no", "yes");
    expect("480x320", switcher_fits(480, 320) ? "yes" : "no", "no");
    expect("480x480", switcher_fits(480, 480) ? "yes" : "no", "no");
    expect("hint with 5 slots", first_run_hint(5), "Hold three fingers to switch apps");
    expect("hint with 3 slots", first_run_hint(3), "Hold three fingers to switch apps");
    expect("hint single touch", first_run_hint(1), "Alt+Tab switches apps");
    expect("hint no touch", first_run_hint(0), "Alt+Tab switches apps");
}

/* On by default where the switcher fits; APPLAUNCH_MULTITASK forces it either way */
void test_default_on()
{
    const char *flag_name[] = {"default", "on", "off"};
    struct {
        const char *value;
        MultitaskFlag want;
    } parse[] = {
        {nullptr, MultitaskFlag::Default}, {"", MultitaskFlag::Default},  {"1", MultitaskFlag::On},
        {"true", MultitaskFlag::On},       {"ON", MultitaskFlag::On},     {"Yes", MultitaskFlag::On},
        {"0", MultitaskFlag::Off},         {"FALSE", MultitaskFlag::Off}, {"off", MultitaskFlag::Off},
        {"No", MultitaskFlag::Off},        {"2", MultitaskFlag::Default}, {"enabled", MultitaskFlag::Default},
        {"yes ", MultitaskFlag::Default},  {"truetrue", MultitaskFlag::Default},
    };
    for (const auto &p : parse)
        expect(std::string("parse '") + (p.value ? p.value : "(unset)") + "'",
               flag_name[static_cast<int>(parse_multitask_flag(p.value))], flag_name[static_cast<int>(p.want)]);

    const auto on = [](MultitaskFlag f, int w, int h) { return multitask_on(f, w, h) ? "on" : "off"; };
    expect("default 640x480 (deck)", on(MultitaskFlag::Default, 640, 480), "on");
    expect("default 800x480", on(MultitaskFlag::Default, 800, 480), "on");
    expect("default 720x720", on(MultitaskFlag::Default, 720, 720), "on");
    expect("default 1280x720", on(MultitaskFlag::Default, 1280, 720), "on");
    expect("default 480x320 (Pi 3A+ SPI)", on(MultitaskFlag::Default, 480, 320), "off");
    expect("default 320x480", on(MultitaskFlag::Default, 320, 480), "off");
    expect("default 320x170", on(MultitaskFlag::Default, 320, 170), "off");
    expect("default unknown canvas", on(MultitaskFlag::Default, 0, 0), "off");
    expect("off 640x480", on(MultitaskFlag::Off, 640, 480), "off");
    expect("off 1280x720", on(MultitaskFlag::Off, 1280, 720), "off");
    expect("on 480x320", on(MultitaskFlag::On, 480, 320), "on");
    expect("on 320x170", on(MultitaskFlag::On, 320, 170), "on");
    expect("on unknown canvas", on(MultitaskFlag::On, 0, 0), "on");
}

} // namespace

int main()
{
    test_labels();
    test_moves();
    test_session_item();
    test_fits_and_hint();
    test_default_on();
    if (g_failures) {
        std::printf("test_switcher_model: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("test_switcher_model: %d checks passed\n", g_checks);
    return 0;
}
