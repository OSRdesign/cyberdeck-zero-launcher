/*
 * SPDX-License-Identifier: MIT
 */

#include "home_move.hpp"

#include <algorithm>
#include <numeric>

namespace home_move {

std::size_t step(std::size_t slot, Dir dir, std::size_t cols, std::size_t count)
{
    if (count == 0) return 0;
    if (slot >= count) return count - 1;
    if (cols == 0) cols = 1;
    const std::size_t last = count - 1;
    switch (dir) {
    case Dir::Left: return slot > 0 ? slot - 1 : slot;
    case Dir::Right: return slot < last ? slot + 1 : slot;
    case Dir::Up: return slot >= cols ? slot - cols : slot;
    case Dir::Down:
        if (slot + cols <= last) return slot + cols;
        return last / cols > slot / cols ? last : slot; /* the partial last row: its last slot, if it is lower */
    }
    return slot;
}

void move(std::vector<std::size_t> &order, std::size_t from, std::size_t to)
{
    if (from >= order.size() || to >= order.size() || from == to) return;
    const auto first = order.begin();
    if (from < to)
        std::rotate(first + static_cast<std::ptrdiff_t>(from), first + static_cast<std::ptrdiff_t>(from) + 1,
                    first + static_cast<std::ptrdiff_t>(to) + 1);
    else
        std::rotate(first + static_cast<std::ptrdiff_t>(to), first + static_cast<std::ptrdiff_t>(from),
                    first + static_cast<std::ptrdiff_t>(from) + 1);
}

Session::Session(std::size_t count, std::size_t picked) : order_(count), picked_(picked), slot_(picked)
{
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    if (picked_ >= count) picked_ = slot_ = 0;
}

std::size_t Session::slot_of(std::size_t index) const
{
    const auto found = std::find(order_.begin(), order_.end(), index);
    return static_cast<std::size_t>(found - order_.begin());
}

bool Session::step(Dir dir, std::size_t cols)
{
    if (order_.empty()) return false;
    const std::size_t to = home_move::step(slot_, dir, cols, order_.size());
    if (to == slot_) return false;
    move(order_, slot_, to);
    slot_ = to;
    return true;
}

bool Session::place(std::size_t index)
{
    const std::size_t to = slot_of(index);
    if (to >= order_.size() || to == slot_) return false;
    move(order_, slot_, to);
    slot_ = to;
    return true;
}

bool Session::moved() const
{
    for (std::size_t slot = 0; slot < order_.size(); ++slot)
        if (order_[slot] != slot) return true;
    return false;
}

} // namespace home_move
