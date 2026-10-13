/*
 * SPDX-License-Identifier: MIT
 *
 * cp0::Tree<T>: a small header-only tree of values with stable node handles (the launcher's Settings menu tree).
 */

#pragma once

#include <cstddef>
#include <iterator>
#include <utility>

namespace cp0 {

/*
 * Every value lives in its own heap node that never moves, so an iterator, a T& or a T* stays valid until that
 * node itself is destroyed. Invalidation rules:
 *   - set_head() on an empty tree and append_child() invalidate nothing (a child end() stays end());
 *   - erase_children(p) invalidates what refers to p's descendants, nothing else (p itself stays);
 *   - set_head() on a non-empty tree and the destructor invalidate everything.
 * The tree has at most one top-level node, the head. It is neither copyable nor movable: the Settings pages keep
 * iterators into it for the whole session. Values are moved in. Nothing throws except T's move constructor and
 * operator new, and then the tree is left as it was.
 */
template <class T>
class Tree {
    struct Node {
        explicit Node(T &&v) : value(std::move(v)) {}
        T value;
        Node *first_child = nullptr;
        Node *last_child = nullptr;
        Node *next_sibling = nullptr;
    };

public:
    class sibling_iterator;

    /* A handle to one node. *it / it-> give its value, begin() / end() walk its children. It has no ++: it is not a
     * traversal. Default-constructed it refers to no node. */
    class iterator {
    public:
        iterator() noexcept = default;

        T &operator*() const noexcept { return node->value; }
        T *operator->() const noexcept { return &node->value; }

        /* The node's children, first to last (an empty range for no node). */
        sibling_iterator begin() const noexcept { return sibling_iterator(node ? node->first_child : nullptr); }
        sibling_iterator end() const noexcept { return sibling_iterator(); }

        /* The node referred to, nullptr for none: `if (it.node)` tests an iterator. Two iterators refer to the same
         * node when their nodes (or &*a, &*b) are equal. */
        Node *node = nullptr;

    private:
        friend class Tree;
        friend class sibling_iterator;
        explicit iterator(Node *n) noexcept : node(n) {}
    };

    /* Walks the children of one node (a forward iterator, for std::next and std::distance too); end() is the
     * past-the-last position of any child list. Converts to iterator, the handle of the same node. */
    class sibling_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T *;
        using reference = T &;

        sibling_iterator() noexcept = default;

        T &operator*() const noexcept { return node_->value; }
        T *operator->() const noexcept { return &node_->value; }

        sibling_iterator &operator++() noexcept
        {
            node_ = node_->next_sibling;
            return *this;
        }
        sibling_iterator operator++(int) noexcept
        {
            sibling_iterator before = *this;
            node_ = node_->next_sibling;
            return before;
        }

        operator iterator() const noexcept { return iterator(node_); }

        friend bool operator==(const sibling_iterator &a, const sibling_iterator &b) noexcept
        {
            return a.node_ == b.node_;
        }
        friend bool operator!=(const sibling_iterator &a, const sibling_iterator &b) noexcept
        {
            return a.node_ != b.node_;
        }

    private:
        friend class iterator;
        explicit sibling_iterator(Node *n) noexcept : node_(n) {}
        Node *node_ = nullptr;
    };

    Tree() noexcept = default;
    ~Tree() { destroy_chain(release_head()); }

    Tree(const Tree &) = delete;
    Tree &operator=(const Tree &) = delete;
    Tree(Tree &&) = delete;
    Tree &operator=(Tree &&) = delete;

    /* The head, or no node when the tree is empty. */
    iterator begin() noexcept { return iterator(head_); }

    /* Makes value the head and returns it. A non-empty tree is emptied first. */
    iterator set_head(T &&value) { return replace_head(new Node(std::move(value))); }

    /* Adds value as the last child of parent (a node of this tree) and returns it. With no parent node nothing is
     * added and the result refers to no node. */
    iterator append_child(const iterator &parent, T &&value)
    {
        return parent.node ? link_last(parent.node, new Node(std::move(value))) : iterator();
    }

    /* Destroys every descendant of parent; parent itself stays, with no children. No parent node: nothing. */
    void erase_children(const iterator &parent) noexcept
    {
        Node *p = parent.node;
        if (!p) return;
        Node *first = p->first_child;
        p->first_child = nullptr; // unlinked before the values are destroyed: the tree is consistent meanwhile
        p->last_child = nullptr;
        destroy_chain(first);
    }

private:
    Node *release_head() noexcept
    {
        Node *head = head_;
        head_ = nullptr;
        return head;
    }

    iterator replace_head(Node *head) noexcept
    {
        destroy_chain(release_head());
        head_ = head;
        return iterator(head);
    }

    static iterator link_last(Node *parent, Node *child) noexcept
    {
        if (parent->last_child)
            parent->last_child->next_sibling = child;
        else
            parent->first_child = child;
        parent->last_child = child;
        return iterator(child);
    }

    /* Destroys a sibling chain and all its descendants without recursion: a node's children are spliced in front of
     * the rest of the chain before the node is deleted. */
    static void destroy_chain(Node *pending) noexcept
    {
        while (pending) {
            Node *node = pending;
            pending = node->next_sibling;
            if (node->first_child) {
                node->last_child->next_sibling = pending;
                pending = node->first_child;
            }
            delete node;
        }
    }

    Node *head_ = nullptr;
};

} // namespace cp0
