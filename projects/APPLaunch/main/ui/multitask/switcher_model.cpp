/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-d: the switcher screen's pure rules (see switcher_model.hpp).
 */
#include "switcher_model.hpp"

#include <cstddef>
#include <string>

namespace multitask {

CardLabel card_label(State state, bool origin)
{
    switch (state) {
    case State::NotResponding: return {"Not responding", CardTone::Amber};
    case State::Foreground:
    case State::Hiding: return {"In front", CardTone::Green};
    case State::HiddenRunning: return origin ? CardLabel{"In front", CardTone::Green} : CardLabel{"Running", CardTone::Green};
    case State::HiddenFrozen:
    case State::Held: return origin ? CardLabel{"In front", CardTone::Green} : CardLabel{"Paused", CardTone::Grey};
    case State::Closing: return {"Closing", CardTone::Grey};
    case State::Gone: break;
    }
    return {"", CardTone::Grey};
}

int switcher_clamp(int item, int cards)
{
    if (cards < 0) cards = 0;
    if (cards > kSwitcherSlots) cards = kSwitcherSlots;
    if (item == kItemHome || item == kItemCloseAll) return item;
    if (item >= 0 && item < cards) return item;
    if (item >= cards && item < kSwitcherSlots) return cards > 0 ? cards - 1 : kItemHome;
    return kItemHome;
}

int switcher_move(int item, SwitcherKey key, int cards)
{
    if (cards < 0) cards = 0;
    if (cards > kSwitcherSlots) cards = kSwitcherSlots;
    item = switcher_clamp(item, cards);
    const bool bar = item == kItemHome || item == kItemCloseAll;
    const int col = bar ? (item == kItemHome ? 0 : 1) : item % 2;
    const int row = bar ? 2 : item / 2;
    const auto card = [cards](int r, int c) { return r * 2 + c < cards ? r * 2 + c : -1; };
    switch (key) {
    case SwitcherKey::Left:
        if (col == 0) return item;
        return bar ? kItemHome : item - 1;
    case SwitcherKey::Right:
        if (col == 1) return item;
        if (bar) return kItemCloseAll;
        return card(row, 1) >= 0 ? item + 1 : item;
    case SwitcherKey::Up: {
        if (row == 0) return item;
        if (!bar) return card(row - 1, col) >= 0 ? card(row - 1, col) : item;
        for (int r = 1; r >= 0; --r)
            if (card(r, col) >= 0) return card(r, col);
        return cards > 0 ? cards - 1 : item;
    }
    case SwitcherKey::Down:
        if (bar) return item;
        if (row == 0 && card(1, col) >= 0) return card(1, col);
        return col == 0 ? kItemHome : kItemCloseAll;
    case SwitcherKey::Next:
    case SwitcherKey::Prev: {
        /* reading order: the cards, Home, Close all */
        std::vector<int> order;
        for (int i = 0; i < cards; ++i) order.push_back(i);
        order.push_back(kItemHome);
        order.push_back(kItemCloseAll);
        const int n = static_cast<int>(order.size());
        int at = 0;
        for (int i = 0; i < n; ++i)
            if (order[static_cast<std::size_t>(i)] == item) at = i;
        at = (at + (key == SwitcherKey::Next ? 1 : n - 1)) % n;
        return order[static_cast<std::size_t>(at)];
    }
    }
    return item;
}

int session_item(const std::vector<AppId> &cycle, int selected, const std::vector<AppId> &cards)
{
    if (selected < 0 || selected >= static_cast<int>(cycle.size())) return kItemHome;
    const AppId id = cycle[static_cast<std::size_t>(selected)];
    if (id == kNoApp) return kItemHome;
    for (std::size_t i = 0; i < cards.size() && i < static_cast<std::size_t>(kSwitcherSlots); ++i)
        if (cards[i] == id) return static_cast<int>(i);
    return kItemHome;
}

bool switcher_fits(int w, int h)
{
    return w >= kSwitcherMinSide && h >= kSwitcherMinSide;
}

MultitaskFlag parse_multitask_flag(const char *value)
{
    if (!value) return MultitaskFlag::Default;
    char low[8] = {};
    std::size_t n = 0;
    for (; value[n]; ++n) {
        if (n + 1 >= sizeof(low)) return MultitaskFlag::Default; /* longer than any accepted word */
        const char c = value[n];
        low[n] = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    const std::string v(low, n);
    if (v == "1" || v == "true" || v == "on" || v == "yes") return MultitaskFlag::On;
    if (v == "0" || v == "false" || v == "off" || v == "no") return MultitaskFlag::Off;
    return MultitaskFlag::Default;
}

bool multitask_on(MultitaskFlag flag, int w, int h)
{
    if (flag == MultitaskFlag::On) return true;
    if (flag == MultitaskFlag::Off) return false;
    return w > 0 && h > 0 && switcher_fits(w, h);
}

const char *first_run_hint(int touch_slots)
{
    return touch_slots >= 3 ? "Hold three fingers to switch apps" : "Alt+Tab switches apps";
}

} // namespace multitask
