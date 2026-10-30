/*
 * SPDX-License-Identifier: MIT
 *
 * Move mode on the home grid (main/ui/model/home_move.hpp, study 046 P2): the arrows' clamped steps on grids of 3 and
 * 4 columns with full and partial last rows, a single tile, taps that place, the list put in the new order, and the
 * saved order of a move with a hidden tile between (home_order::merge keeps its slot).
 */

#include "../main/ui/model/home_move.hpp"
#include "../main/ui/model/home_order.hpp"

#include <cassert>
#include <cstdio>
#include <list>
#include <string>
#include <vector>

using home_move::Dir;
using home_move::Session;
using home_move::step;

namespace {

std::vector<std::string> names(const std::vector<std::string> &tiles, const std::vector<std::size_t> &order)
{
    std::vector<std::string> out;
    for (const std::size_t index : order) out.push_back(tiles[index]);
    return out;
}

std::vector<std::uint32_t> keys(const std::vector<std::string> &ids)
{
    std::vector<std::uint32_t> out;
    for (const auto &id : ids) out.push_back(home_order::key_of(id));
    return out;
}

} // namespace

int main()
{
    // 3 columns, 7 tiles: rows 0-2, 3-5, 6 (the deck, PDA portrait)
    //   Left / Right: one slot, across a row end, clamped at both ends
    assert(step(0, Dir::Left, 3, 7) == 0);
    assert(step(1, Dir::Left, 3, 7) == 0);
    assert(step(3, Dir::Left, 3, 7) == 2);
    assert(step(2, Dir::Right, 3, 7) == 3);
    assert(step(6, Dir::Right, 3, 7) == 6);
    //   Up: one row, the first row stays (no jump to slot 0, no wrap to the bottom)
    assert(step(0, Dir::Up, 3, 7) == 0);
    assert(step(2, Dir::Up, 3, 7) == 2);
    assert(step(4, Dir::Up, 3, 7) == 1);
    assert(step(6, Dir::Up, 3, 7) == 3);
    //   Down: one row; into the partial last row its only slot; the last row stays
    assert(step(0, Dir::Down, 3, 7) == 3);
    assert(step(3, Dir::Down, 3, 7) == 6);
    assert(step(4, Dir::Down, 3, 7) == 6);
    assert(step(5, Dir::Down, 3, 7) == 6);
    assert(step(6, Dir::Down, 3, 7) == 6);
    // 3 columns, 6 tiles (two full rows): Down from the last row stays, from the first goes straight down
    assert(step(1, Dir::Down, 3, 6) == 4);
    assert(step(4, Dir::Down, 3, 6) == 4);
    assert(step(5, Dir::Down, 3, 6) == 5);
    // 4 columns, 10 tiles (a landscape tablet, 1024x600): rows 0-3, 4-7, 8-9
    assert(step(5, Dir::Down, 4, 10) == 9);
    assert(step(7, Dir::Down, 4, 10) == 9);
    assert(step(9, Dir::Up, 4, 10) == 5);
    assert(step(3, Dir::Down, 4, 10) == 7);
    // the partial row below is not lower than the slot: stays (8 in the last row of 4 x 9)
    assert(step(8, Dir::Down, 4, 9) == 8);
    // a single tile: nothing moves
    for (const Dir dir : {Dir::Left, Dir::Right, Dir::Up, Dir::Down}) assert(step(0, dir, 3, 1) == 0);
    // one row only (fewer tiles than columns): Up and Down stay, Left and Right move
    assert(step(1, Dir::Down, 3, 2) == 1);
    assert(step(1, Dir::Up, 3, 2) == 1);
    assert(step(0, Dir::Right, 3, 2) == 1);
    // one column (never from the layout service, which gives 2 to 8): Up / Down are one slot
    assert(step(2, Dir::Down, 1, 4) == 3);
    assert(step(2, Dir::Up, 1, 4) == 1);
    // degenerate input: no tiles, a slot out of range, 0 columns
    assert(step(0, Dir::Right, 3, 0) == 0);
    assert(step(9, Dir::Left, 3, 4) == 3);
    assert(step(1, Dir::Down, 0, 4) == 2);

    // move: the tile goes to the slot, the others shift (lv_obj_move_to_index)
    {
        std::vector<std::size_t> order = {0, 1, 2, 3, 4};
        home_move::move(order, 1, 3);
        assert((order == std::vector<std::size_t>{0, 2, 3, 1, 4}));
        home_move::move(order, 3, 0);
        assert((order == std::vector<std::size_t>{1, 0, 2, 3, 4}));
        home_move::move(order, 4, 4);
        home_move::move(order, 7, 0);
        assert((order == std::vector<std::size_t>{1, 0, 2, 3, 4}));
    }

    const std::vector<std::string> tiles = {"Settings", "Store", "CLI", "Python", "SSH", "Calculator", "Snake"};
    // a session: arrows, the order follows, the picked tile and its slot
    {
        Session s(tiles.size(), 1); // Store
        assert(!s.moved() && s.slot() == 1 && s.picked() == 1);
        assert(!s.step(Dir::Up, 3)); // first row: stays
        assert(s.step(Dir::Down, 3));
        assert(s.slot() == 4);
        assert((names(tiles, s.order()) ==
                std::vector<std::string>{"Settings", "CLI", "Python", "SSH", "Store", "Calculator", "Snake"}));
        assert(s.step(Dir::Down, 3)); // into the partial last row: the last slot
        assert(s.slot() == 6);
        assert((names(tiles, s.order()) ==
                std::vector<std::string>{"Settings", "CLI", "Python", "SSH", "Calculator", "Snake", "Store"}));
        assert(!s.step(Dir::Right, 3) && !s.step(Dir::Down, 3)); // the last slot: stays
        assert(s.moved());
        // back where it started: the order is the pick-up's again
        assert(s.step(Dir::Up, 3) && s.slot() == 3);
        assert(s.step(Dir::Left, 3) && s.step(Dir::Left, 3) && s.slot() == 1);
        assert(!s.moved());
        assert(s.slot_of(1) == 1 && s.slot_of(0) == 0 && s.slot_of(99) == tiles.size());
    }
    // a tap places: the picked tile takes the tapped tile's slot (forward and backward)
    {
        Session s(tiles.size(), 0); // Settings
        assert(!s.place(0));         // the picked tile itself: no move
        assert(!s.place(42));        // no such tile
        assert(s.place(5));          // Calculator's slot
        assert(s.slot() == 5);
        assert((names(tiles, s.order()) ==
                std::vector<std::string>{"Store", "CLI", "Python", "SSH", "Calculator", "Settings", "Snake"}));
        assert(s.place(2)); // CLI, now at slot 1
        assert(s.slot() == 1);
        assert((names(tiles, s.order()) ==
                std::vector<std::string>{"Store", "Settings", "CLI", "Python", "SSH", "Calculator", "Snake"}));
        assert(s.place(6) && s.slot() == 6); // the last tile: the picked tile goes last
        assert(s.order().back() == 0);
    }
    // a session of one tile, and an empty one
    {
        Session one(1, 0);
        for (const Dir dir : {Dir::Left, Dir::Right, Dir::Up, Dir::Down}) assert(!one.step(dir, 3));
        assert(!one.place(0) && !one.moved());
        Session none;
        assert(!none.step(Dir::Right, 3) && !none.place(0) && !none.moved() && none.count() == 0);
        Session bad(3, 7); // a picked index out of range falls back to the first tile
        assert(bad.picked() == 0 && bad.slot() == 0);
    }

    // apply: the list in the session's order; anything but a permutation changes nothing
    {
        std::list<std::string> list(tiles.begin(), tiles.end());
        Session s(tiles.size(), 6); // Snake
        assert(s.step(Dir::Up, 3) && s.step(Dir::Up, 3)); // 6 -> 3 -> 0
        assert(!s.step(Dir::Left, 3) && s.slot() == 0);
        assert(home_move::apply(list, s.order()));
        assert((std::vector<std::string>(list.begin(), list.end()) ==
                std::vector<std::string>{"Snake", "Settings", "Store", "CLI", "Python", "SSH", "Calculator"}));
        std::list<std::string> keep = {"a", "b", "c"};
        assert(!home_move::apply(keep, {0, 1}));
        assert(!home_move::apply(keep, {0, 0, 1}));
        assert(!home_move::apply(keep, {0, 1, 3}));
        assert((keep == std::list<std::string>{"a", "b", "c"}));
        assert(home_move::is_permutation({}) && home_move::is_permutation({2, 0, 1}) && !home_move::is_permutation({1}));
    }

    // a hidden tile between (Python, switched off in Settings > Launcher): not on the grid, so not in the session; the
    // saved order keeps its slot (the fourth of five) while Store moves past it
    {
        const std::vector<std::string> registered = {"app_Setting", "app_Store", "app_CLI", "app_Python", "app_SSH"};
        const std::vector<std::string> shown = {"app_Setting", "app_Store", "app_CLI", "app_SSH"};
        Session s(shown.size(), 1);  // Store
        assert(s.step(Dir::Down, 3)); // 4 tiles in rows of 3: from slot 1 to the last slot (3)
        assert(s.slot() == 3);
        const std::vector<std::string> visible = names(shown, s.order());
        assert((visible == std::vector<std::string>{"app_Setting", "app_CLI", "app_SSH", "app_Store"}));
        const auto saved = home_order::merge({}, keys(registered), keys(visible));
        assert((saved == keys({"app_Setting", "app_CLI", "app_SSH", "app_Python", "app_Store"})));
        // shown again later, Python comes back between SSH and Store, where the user left it
        const std::vector<std::size_t> grid = home_order::arrange(keys(registered), saved);
        assert((names(registered, grid) ==
                std::vector<std::string>{"app_Setting", "app_CLI", "app_SSH", "app_Python", "app_Store"}));
    }

    std::puts("test_home_move: ok");
    return 0;
}
