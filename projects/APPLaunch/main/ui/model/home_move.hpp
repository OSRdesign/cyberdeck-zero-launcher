/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * Move mode on the home grid (study 046, phase P2): the part without LVGL.
 *
 * The grid is one sequence of `count` tiles laid out in rows of `cols` (the layout service's column count: 3 on the
 * deck and on PDA portrait, computed elsewhere). A picked tile moves through that sequence; its neighbours shift by one
 * slot, as lv_obj_move_to_index() reflows a flex row.
 *
 *   step     the slot one arrow takes the picked tile to, clamped (no wrap): Left and Right one slot (across a row
 *            end too), Up and Down one row. Left at the first slot, Right at the last one and Up in the first row
 *            stay. Down where the slot below does not exist (a partial last row) goes to the last slot when that slot
 *            is in a lower row, else stays.
 *   Session  one pick-up: the grid order while the tile moves (order[slot] = the tile's index in the app list as it was
 *            at the pick-up), the picked tile, where it is. place() is a tap on a tile: the picked tile takes that
 *            tile's slot. moved() says whether the order differs from the pick-up's.
 *   apply    puts a list (the launcher's app list) in a session's order; false (nothing changes) unless `order` is a
 *            permutation of the list's indices.
 *
 * Hidden tiles are not on the grid, so they are not in a session: the saved order keeps their slots
 * (home_order::merge).
 */

#include <cstddef>
#include <list>
#include <vector>

namespace home_move {

enum class Dir { Left, Right, Up, Down };

std::size_t step(std::size_t slot, Dir dir, std::size_t cols, std::size_t count);

/* order[from] goes to slot `to`, the tiles between shift by one (from, to < order.size()). */
void move(std::vector<std::size_t> &order, std::size_t from, std::size_t to);

/* True when `order` holds each of 0 .. order.size() - 1 once (inline: launch.h uses it through apply()). */
inline bool is_permutation(const std::vector<std::size_t> &order)
{
    std::vector<bool> seen(order.size(), false);
    for (const std::size_t index : order) {
        if (index >= order.size() || seen[index]) return false;
        seen[index] = true;
    }
    return true;
}

class Session {
public:
    Session() = default;
    /* `picked` < `count`; count 0 makes an empty session (nothing moves). */
    Session(std::size_t count, std::size_t picked);

    std::size_t count() const { return order_.size(); }
    std::size_t picked() const { return picked_; }
    std::size_t slot() const { return slot_; }
    /* The slot of tile `index` now; count() when there is no such tile. */
    std::size_t slot_of(std::size_t index) const;
    /* One arrow; true when the picked tile moved. */
    bool step(Dir dir, std::size_t cols);
    /* A tap on tile `index`: the picked tile goes to its slot; true when it moved (false for the picked tile itself or
     * an unknown index). */
    bool place(std::size_t index);
    bool moved() const;
    const std::vector<std::size_t> &order() const { return order_; }

private:
    std::vector<std::size_t> order_;
    std::size_t picked_ = 0;
    std::size_t slot_ = 0;
};

template <class T>
bool apply(std::list<T> &items, const std::vector<std::size_t> &order)
{
    if (order.size() != items.size() || !is_permutation(order)) return false;
    std::vector<typename std::list<T>::iterator> at;
    at.reserve(items.size());
    for (auto it = items.begin(); it != items.end(); ++it) at.push_back(it);
    std::list<T> ordered;
    for (const std::size_t index : order) ordered.splice(ordered.end(), items, at[index]);
    items.swap(ordered);
    return true;
}

} // namespace home_move
