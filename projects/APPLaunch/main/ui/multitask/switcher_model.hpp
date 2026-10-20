/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-d (task 015-pd, study 026 section 6.2, decisions D7-D9, D12, D15): the pure rules of the switcher
 * screen "Open apps" and of the legacy app sheet, without LVGL, so they are unit-tested (tests/test_switcher_model.cpp):
 *   - what a card says about its app (In front / Running / Paused / Not responding) and the colour of its dot;
 *   - the keyboard selection: 4 card slots in a fixed 2x2 grid, then the bottom bar (Home, Close all); arrows move
 *     in the grid, Tab / Shift+Tab in reading order (only over what exists), Enter activates;
 *   - where an Alt+Tab session's selection (P-b's cycle: apps and Home) lands on the screen;
 *   - which screens get the switcher (640x480 and larger; smaller ones keep the P-b / P-c behaviour);
 *   - the one-time hint's text (D15);
 *   - whether multitasking is on at all (APPLAUNCH_MULTITASK and the canvas: on by default where the switcher fits).
 */
#pragma once

#include "multitask_model.hpp"

#include <cstdint>
#include <vector>

namespace multitask {

/* The screen's items: card slots 0..3 (only the first `cards` exist), then the two buttons of the bottom bar. */
inline constexpr int kSwitcherSlots = 4;
inline constexpr int kItemHome = 4;
inline constexpr int kItemCloseAll = 5;

enum class CardTone : std::uint8_t { Green, Grey, Amber };

struct CardLabel {
    const char *text = "";
    CardTone tone = CardTone::Green;
};

/* origin: the app that was in front when the switcher opened ("In front", unless it stopped answering: the amber
 * state wins). Running, Paused (frozen), Not responding (amber). */
CardLabel card_label(State state, bool origin);

enum class SwitcherKey : std::uint8_t { Left, Right, Up, Down, Next, Prev };

/* The item selected after `key` from `item` with `cards` cards shown (0..4). Arrows move in the grid (cards in two
 * rows of two, the bar under them: Home under the left column, Close all under the right one) and stop at the edges;
 * Next / Prev go through the existing items in reading order and wrap. A selection that does not exist (a card that
 * went) is first clamped. */
int switcher_move(int item, SwitcherKey key, int cards);

/* An item that exists for `cards` cards: a card beyond the last becomes the last card (Home when there is none). */
int switcher_clamp(int item, int cards);

/* The item an Alt+Tab session selects: cycle[selected] is an app (its card, by id) or kNoApp (Home). An app without
 * a card (it went meanwhile) selects Home. */
int session_item(const std::vector<AppId> &cycle, int selected, const std::vector<AppId> &cards);

/* The switcher screen exists on canvases of at least 640x480 (the deck, 800x480, 720x720, 1024x600, 1280x720 ...).
 * Smaller ones (480x320 and the 320x480 SPI panel turned to 480x320) keep the P-b / P-c behaviour. */
bool switcher_fits(int w, int h);

/* APPLAUNCH_MULTITASK, any case: 1/true/on/yes = On, 0/false/off/no = Off; unset, empty or anything else = Default. */
enum class MultitaskFlag : std::uint8_t { Default, On, Off };
MultitaskFlag parse_multitask_flag(const char *value);

/* THE rule for multitasking (on by default since task 015 "default on"; read once by multitask_ui::enabled()): the
 * flag forces it on or off; by default it is on where P-d was designed for, a logical canvas the switcher fits
 * (switcher_fits: 640x480 and more), and off below (480x320, the 320x480 SPI panel turned to 480x320, the 320x170
 * board: these keep the launcher as it was before multitasking). An unknown canvas (w or h <= 0) is off by default. */
bool multitask_on(MultitaskFlag flag, int w, int h);

/* The first-run hint (D15): "Hold three fingers to switch apps" when the panel tracks three contacts (G1 works),
 * else "Alt+Tab switches apps". */
const char *first_run_hint(int touch_slots);

} // namespace multitask
