/*
 * SPDX-License-Identifier: MIT
 *
 * cp0::Tree (cp0_tree.hpp): structure, child walks, stable handles across appends, erase_children, destruction,
 * and the Settings tree's own call pattern.
 */

#include "cp0_tree.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#define CHECK(condition)                                                                                   \
    do {                                                                                                   \
        if (!(condition)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);            \
            std::abort();                                                                                  \
        }                                                                                                  \
    } while (0)

namespace {

/* A value that counts the live instances, to see what the tree destroys. */
struct Counted {
    static int live;
    std::string label;
    explicit Counted(std::string text) : label(std::move(text)) { ++live; }
    Counted(Counted &&other) noexcept : label(std::move(other.label)) { ++live; }
    Counted(const Counted &) = delete;
    Counted &operator=(const Counted &) = delete;
    Counted &operator=(Counted &&) = delete;
    ~Counted() { --live; }
};
int Counted::live = 0;

using CTree = cp0::Tree<Counted>;

/* "label(child,child(...))" of a node and its descendants, children in walk order. */
std::string dump(const CTree::iterator &node)
{
    std::string out = node->label;
    if (node.begin() == node.end()) return out;
    out += '(';
    bool first = true;
    for (auto child = node.begin(); child != node.end(); ++child) {
        if (!first) out += ',';
        first = false;
        out += dump(child);
    }
    return out + ')';
}

int child_count(const CTree::iterator &node)
{
    return static_cast<int>(std::distance(node.begin(), node.end()));
}

/* ---- types: what is supported, what is not ---- */
static_assert(!std::is_copy_constructible<CTree>::value, "a tree is not copyable");
static_assert(!std::is_copy_assignable<CTree>::value, "a tree is not copy-assignable");
static_assert(!std::is_move_constructible<CTree>::value, "a tree is not movable (pages hold iterators into it)");
static_assert(!std::is_move_assignable<CTree>::value, "a tree is not move-assignable");
static_assert(std::is_nothrow_default_constructible<CTree>::value, "an empty tree allocates nothing");
static_assert(std::is_trivially_copyable<CTree::iterator>::value, "an iterator is a plain handle");
static_assert(std::is_nothrow_default_constructible<CTree::iterator>::value, "NodeIter x{} refers to no node");
static_assert(std::is_trivially_copyable<CTree::sibling_iterator>::value, "a child iterator is a plain handle");
static_assert(std::is_same<std::iterator_traits<CTree::sibling_iterator>::iterator_category,
                           std::forward_iterator_tag>::value,
              "children are walked forwards");
static_assert(std::is_same<std::iterator_traits<CTree::sibling_iterator>::reference, Counted &>::value,
              "a child dereferences to a mutable value");
static_assert(std::is_convertible<CTree::sibling_iterator, CTree::iterator>::value,
              "a child converts to the node handle (row.node = child)");
static_assert(!std::is_convertible<CTree::iterator, CTree::sibling_iterator>::value,
              "a node handle is not a child walk");

void test_empty_tree()
{
    CTree tree;
    CHECK(tree.begin().node == nullptr);
    const CTree::iterator none{};
    CHECK(none.node == nullptr);
    CHECK(none.begin() == none.end()); // no node: an empty range, not a crash
    CHECK(tree.append_child(none, Counted{"lost"}).node == nullptr);
    CHECK(Counted::live == 0); // nothing was added
    tree.erase_children(none);  // no node: nothing
    CHECK(tree.begin().node == nullptr);
}

void test_build_and_walk()
{
    {
        CTree tree;
        const CTree::iterator root = tree.set_head(Counted{"root"});
        CHECK(root.node != nullptr);
        CHECK(tree.begin().node == root.node);
        CHECK(root->label == "root");
        CHECK((*root).label == "root");
        CHECK(root.begin() == root.end());
        CHECK(child_count(root) == 0);

        const CTree::iterator a = tree.append_child(root, Counted{"a"});
        const CTree::iterator b = tree.append_child(root, Counted{"b"});
        const CTree::iterator c = tree.append_child(root, Counted{"c"});
        CHECK(a->label == "a" && b->label == "b" && c->label == "c");
        CHECK(child_count(root) == 3);

        // children in the order they were added
        std::vector<std::string> seen;
        for (auto child = root.begin(); child != root.end(); ++child) seen.push_back(child->label);
        CHECK((seen == std::vector<std::string>{"a", "b", "c"}));

        // the walk yields the very nodes append_child returned
        auto walk = root.begin();
        CHECK(&*walk == &*a);
        ++walk;
        CHECK(&*walk == &*b);
        auto previous = walk++; // post-increment: the old position, then the next one
        CHECK(&*previous == &*b);
        CHECK(&*walk == &*c);
        ++walk;
        CHECK(walk == root.end());

        // std::next / std::distance
        CHECK(std::next(root.begin(), 0)->label == "a");
        CHECK(std::next(root.begin(), 2)->label == "c");
        CHECK(std::next(root.begin(), 3) == root.end());
        CHECK(std::distance(root.begin(), std::next(root.begin(), 2)) == 2);

        // a child converts to a handle: children of children
        const CTree::iterator second = std::next(root.begin(), 1);
        CHECK(second.node == b.node);
        tree.append_child(second, Counted{"b1"});
        tree.append_child(std::next(root.begin(), 1), Counted{"b2"});
        tree.append_child(tree.append_child(c, Counted{"c1"}), Counted{"c1x"});
        CHECK(dump(tree.begin()) == "root(a,b(b1,b2),c(c1(c1x)))");

        // values change in place through any handle, even a const one (iterators are pointer-like)
        const CTree::iterator held = b;
        held->label = "B";
        for (auto child = root.begin(); child != root.end(); ++child)
            if (child->label == "c") child->label = "C";
        CHECK(dump(root) == "root(a,B(b1,b2),C(c1(c1x)))");

        // an end() taken before an append stays the end
        const auto old_end = a.end();
        tree.append_child(a, Counted{"a1"});
        CHECK(old_end == a.end());
        CHECK(child_count(a) == 1);
        CHECK(Counted::live == 9); // root, a, b, c, b1, b2, c1, c1x, a1
    }
    CHECK(Counted::live == 0); // the destructor destroyed every value
}

/* Pages, rows and callbacks keep iterators and SettingEntry pointers for the whole session: appends must never move a
 * node. */
void test_handles_stable_across_appends()
{
    {
        CTree tree;
        const CTree::iterator root = tree.set_head(Counted{"root"});
        std::vector<CTree::iterator> sections;
        std::vector<const Counted *> addresses;
        for (int i = 0; i < 16; ++i) {
            sections.push_back(tree.append_child(root, Counted{"section" + std::to_string(i)}));
            addresses.push_back(&*sections.back());
        }
        const CTree::iterator first_child_of_first = tree.append_child(sections[0], Counted{"keep"});
        Counted *keep = &*first_child_of_first;

        // a callback holding a handle, as a SettingEntry's API lambda holds its NodeIter
        std::function<std::string()> callback = [section = sections[3]] { return section->label; };

        for (int round = 0; round < 1000; ++round)
            for (std::size_t s = 0; s < sections.size(); ++s)
                tree.append_child(sections[s], Counted{std::to_string(round)});

        for (std::size_t i = 0; i < sections.size(); ++i) {
            CHECK(&*sections[i] == addresses[i]);
            CHECK(sections[i]->label == "section" + std::to_string(i));
        }
        CHECK(&*first_child_of_first == keep);
        CHECK(keep->label == "keep");
        CHECK(&*sections[0].begin() == keep);
        CHECK(callback() == "section3");
        CHECK(child_count(sections[0]) == 1001);
        CHECK(child_count(sections[1]) == 1000);
        CHECK(std::next(sections[5].begin(), 999)->label == "999");

        // a walk in progress sees a child added behind it
        auto walk = sections[1].begin();
        std::advance(walk, 999);
        tree.append_child(sections[1], Counted{"late"});
        ++walk;
        CHECK(walk != sections[1].end());
        CHECK(walk->label == "late");
        CHECK(Counted::live == 1 + 16 + 1 + 16 * 1000 + 1);
    }
    CHECK(Counted::live == 0);
}

void test_erase_children()
{
    {
        CTree tree;
        const CTree::iterator root = tree.set_head(Counted{"root"});
        const CTree::iterator left = tree.append_child(root, Counted{"left"});
        const CTree::iterator middle = tree.append_child(root, Counted{"middle"});
        const CTree::iterator right = tree.append_child(root, Counted{"right"});
        const CTree::iterator left_leaf = tree.append_child(left, Counted{"l1"});
        for (int i = 0; i < 5; ++i) {
            const CTree::iterator option = tree.append_child(middle, Counted{"m" + std::to_string(i)});
            for (int j = 0; j < 3; ++j) tree.append_child(option, Counted{"x"});
        }
        const CTree::iterator right_leaf = tree.append_child(right, Counted{"r1"});
        const Counted *middle_value = &*middle;
        CHECK(Counted::live == 1 + 3 + 1 + 5 * 4 + 1);

        tree.erase_children(middle); // its 5 children and their 15 children
        CHECK(Counted::live == 1 + 3 + 1 + 1);
        CHECK(middle.begin() == middle.end());
        CHECK(&*middle == middle_value && middle->label == "middle"); // the node itself stays
        CHECK(dump(root) == "root(left(l1),middle,right(r1))");
        CHECK(left_leaf->label == "l1" && right_leaf->label == "r1"); // the rest is untouched

        // rebuilt with another count, twice (the Day list follows the month)
        for (int days : {31, 28}) {
            tree.erase_children(middle);
            for (int day = 1; day <= days; ++day) tree.append_child(middle, Counted{std::to_string(day)});
            CHECK(child_count(middle) == days);
            CHECK(std::next(middle.begin(), days - 1)->label == std::to_string(days));
        }
        CHECK(Counted::live == 6 + 28);

        tree.erase_children(left_leaf); // a leaf: nothing to erase
        CHECK(left_leaf->label == "l1");
        tree.erase_children(root); // everything below the head
        CHECK(Counted::live == 1);
        CHECK(root.begin() == root.end());
        CHECK(tree.begin().node == root.node);
        tree.append_child(root, Counted{"again"}); // the emptied node takes children again
        CHECK(dump(tree.begin()) == "root(again)");
    }
    CHECK(Counted::live == 0);
}

void test_set_head_replaces()
{
    {
        CTree tree;
        const CTree::iterator first = tree.set_head(Counted{"first"});
        tree.append_child(tree.append_child(first, Counted{"a"}), Counted{"b"});
        CHECK(Counted::live == 3);
        const CTree::iterator second = tree.set_head(Counted{"second"});
        CHECK(Counted::live == 1); // the old tree is gone
        CHECK(tree.begin().node == second.node);
        CHECK(dump(tree.begin()) == "second");
    }
    CHECK(Counted::live == 0);
}

/* Destruction walks without recursion: a very deep chain and a very wide level both go. */
void test_destruction_deep_and_wide()
{
    {
        CTree tree;
        CTree::iterator node = tree.set_head(Counted{"0"});
        for (int depth = 1; depth <= 200000; ++depth) node = tree.append_child(node, Counted{"d"});
        CHECK(Counted::live == 200001);
        CTree::iterator wide = tree.append_child(tree.begin(), Counted{"wide"});
        for (int i = 0; i < 50000; ++i) tree.append_child(wide, Counted{"w"});
        CHECK(Counted::live == 250002);
        tree.erase_children(wide);
        CHECK(Counted::live == 200002);
    }
    CHECK(Counted::live == 0);
    {
        CTree tree;
        CTree::iterator node = tree.set_head(Counted{"0"});
        const CTree::iterator top = node;
        for (int depth = 1; depth <= 200000; ++depth) node = tree.append_child(node, Counted{"d"});
        tree.erase_children(top); // a deep erase too
        CHECK(Counted::live == 1);
    }
    CHECK(Counted::live == 0);
}

/* Values only need to be movable. */
void test_move_only_values()
{
    cp0::Tree<std::unique_ptr<int>> tree;
    const auto root = tree.set_head(std::make_unique<int>(1));
    auto value = std::make_unique<int>(2);
    const auto child = tree.append_child(root, std::move(value));
    CHECK(!value);
    CHECK(**root == 1 && **child == 2);
    CHECK(**root.begin() == 2);
}

/* ---- the Settings tree's own call pattern (settings_page.cpp, settings_native_host.cpp, the roller pages) ---- */

struct Entry {
    std::string label;
    std::function<int(int)> api;
    std::shared_ptr<int> state = std::make_shared<int>(0);
    bool icon_enabled = false;
    Entry() = default;
    Entry(const std::string &name) : label(name) {}
    Entry(const std::string &name, std::function<int(int)> callback, bool icon)
        : label(name), api(std::move(callback)), icon_enabled(icon)
    {
    }
};
using Tree = cp0::Tree<Entry>;
using NodeIter = Tree::iterator;

std::string dump_entries(const NodeIter &node)
{
    std::string out = node->label;
    if (node.begin() == node.end()) return out;
    out += '(';
    for (auto child = node.begin(); child != node.end(); ++child) {
        if (child != node.begin()) out += ',';
        out += dump_entries(child);
    }
    return out + ')';
}

void append_numeric_options(Tree &tree, const NodeIter &parent, int first, int last)
{
    for (int value = first; value <= last; ++value) tree.append_child(parent, Entry{std::to_string(value)});
}

/* rtc_page3_factory: the Day options follow the month, rebuilt before the page is made. */
void rebuild_days(Tree &tree, const NodeIter &page_node, int days)
{
    tree.erase_children(page_node);
    for (int day = 1; day <= days; ++day) tree.append_child(page_node, Entry{std::to_string(day)});
}

/* populate_touch_children: the list follows the installed apps, rebuilt every time the section opens. */
void populate_touch(Tree &tree, const NodeIter &parent, const std::vector<std::string> &apps)
{
    tree.erase_children(parent);
    for (const std::string &name : apps) {
        NodeIter app = tree.append_child(parent, Entry{name});
        tree.append_child(app, Entry{"Off"});
        tree.append_child(app, Entry{"Swipe"});
    }
}

int option_count(const NodeIter &node)
{
    int count = 0;
    for (auto option = node.begin(); option != node.end(); ++option) ++count;
    return count;
}

void test_settings_call_pattern()
{
    Tree mode_tree;
    NodeIter root = mode_tree.set_head(Entry{"Settings"});
    NodeIter screen = mode_tree.append_child(root, Entry{"Screen"});
    {
        NodeIter dark_time = mode_tree.append_child(screen, Entry{"DarkTime"});
        mode_tree.append_child(dark_time, Entry{"Never"});
        mode_tree.append_child(dark_time, Entry{"10S"});
    }
    int toggles = 0;
    NodeIter wifi = mode_tree.append_child(root, Entry{"Wi-Fi"});
    NodeIter wifi_enable = mode_tree.append_child(wifi, Entry{"Enable", [&toggles](int) { return ++toggles; }, true});
    Entry hidden{"Join Hidden Network"};
    mode_tree.append_child(wifi, std::move(hidden));
    NodeIter touch = mode_tree.append_child(root, Entry{"Touch"});
    NodeIter date_time = mode_tree.append_child(root, Entry{"Date & Time"});
    NodeIter manual = mode_tree.append_child(date_time, Entry{"Set Manually"});
    NodeIter month = mode_tree.append_child(manual, Entry{"Month"});
    append_numeric_options(mode_tree, month, 1, 3);
    NodeIter day = mode_tree.append_child(manual, Entry{"Day"});
    append_numeric_options(mode_tree, day, 1, 31);
    NodeIter system = mode_tree.append_child(root, Entry{"System"});
    NodeIter quit = mode_tree.append_child(system, Entry{"Quit to desktop"});
    mode_tree.append_child(quit, Entry{"Yes"});
    mode_tree.append_child(quit, Entry{"No"});

    CHECK(option_count(day) == 31);
    rebuild_days(mode_tree, day, 28);
    populate_touch(mode_tree, touch, {"Doom", "Paint"});
    populate_touch(mode_tree, touch, {"Paint"}); // opened again after an app was removed

    CHECK(dump_entries(mode_tree.begin()) ==
          "Settings(Screen(DarkTime(Never,10S)),Wi-Fi(Enable,Join Hidden Network),Touch(Paint(Off,Swipe)),"
          "Date & Time(Set Manually(Month(1,2,3),Day(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,"
          "26,27,28))),System(Quit to desktop(Yes,No)))");

    // the native host: a path of sections from the head, rows made from the children (row.node = child)
    struct Row {
        NodeIter node;
    };
    std::vector<NodeIter> path;
    path.push_back(mode_tree.begin());
    CHECK(path.back()->label == "Settings");
    std::vector<Row> rows;
    for (auto child = path.back().begin(); child != path.back().end(); ++child) {
        Row row;
        row.node = child;
        rows.push_back(row);
    }
    CHECK(rows.size() == 5);
    CHECK(rows[1].node->label == "Wi-Fi");
    path.push_back(rows[1].node); // open Wi-Fi
    const Entry *entry = &*std::next(path.back().begin(), 0);
    CHECK(entry == &*wifi_enable);
    CHECK(entry->icon_enabled && entry->api && entry->api(0) == 1);
    path.pop_back();
    CHECK(path.back().node == root.node);

    // the roller pages: the selected child by index, and the count of the options
    const NodeIter parent_node = date_time;
    const NodeIter selected = std::next(parent_node.begin(), 0);
    CHECK(selected->label == "Set Manually");
    CHECK(std::next(manual.begin(), 1)->label == "Day");
    CHECK(std::distance(month.begin(), month.end()) == 3);

    // a page made with no node (settings_system_page.cpp: `if (page_node.node && ...)`)
    NodeIter page_node{};
    CHECK(!page_node.node);
    page_node = system;
    CHECK(page_node.node && page_node->label == "System");

    // the settings_wifi_section pattern: keep a child as a node handle, use it after more appends
    NodeIter enable;
    for (auto child = wifi.begin(); child != wifi.end(); ++child)
        if (child->icon_enabled && child->api) enable = child;
    for (int i = 0; i < 100; ++i) mode_tree.append_child(system, Entry{"extra"});
    CHECK(enable.node == wifi_enable.node);
    CHECK(enable->api(0) == 2);
    CHECK(toggles == 2);

    // a shared state survives the rebuild of unrelated children
    std::weak_ptr<int> quit_state = quit->state;
    rebuild_days(mode_tree, day, 30);
    CHECK(!quit_state.expired());
    // and goes with its node
    std::weak_ptr<int> first_day_state = day.begin()->state;
    rebuild_days(mode_tree, day, 29);
    CHECK(first_day_state.expired());
}

} // namespace

int main()
{
    test_empty_tree();
    test_build_and_walk();
    test_handles_stable_across_appends();
    test_erase_children();
    test_set_head_replaces();
    test_destruction_deep_and_wide();
    test_move_only_values();
    test_settings_call_pattern();
    CHECK(Counted::live == 0);
    std::puts("test_cp0_tree: ok");
    return 0;
}
