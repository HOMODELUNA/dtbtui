#pragma once

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "dtb/model.hpp"

namespace ui {

// All UI state for the tree panel.  Nodes are referenced by arena index; the
// visible list is a cached pre-order walk over the expanded nodes.
class State {
public:
    explicit State(const dtb::Tree& tree)
        : tree_(tree) {
        if (!tree_.nodes.empty()) expanded_.insert(0);
        rebuild_visible();
    }

    const dtb::Tree& tree() const { return tree_; }
    const std::vector<int>& visible() const { return visible_; }
    int cursor() const { return cursor_; }
    int scroll() const { return scroll_; }
    int window_height() const { return window_height_; }

    bool is_expanded(int node) const { return expanded_.contains(node); }
    bool has_children(int node) const { return !tree_.nodes[node].children.empty(); }

    const dtb::Node& current_node() const {
        if (visible_.empty()) return tree_.nodes.front();
        return tree_.nodes[visible_[cursor_]];
    }

    void set_window_height(int height) {
        window_height_ = std::max(1, height);
        clamp_scroll();
    }

private:
    void rebuild_visible() {
        visible_.clear();
        if (tree_.nodes.empty()) return;

        std::vector<int> stack{0};
        while (!stack.empty()) {
            const int node = stack.back();
            stack.pop_back();
            visible_.push_back(node);
            if (!expanded_.contains(node)) continue;
            const std::vector<int>& children = tree_.nodes[node].children;
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                stack.push_back(*it);
        }

        cursor_ = std::clamp(cursor_, 0, std::max(0, static_cast<int>(visible_.size()) - 1));
        clamp_scroll();
    }

    void clamp_scroll() {
        const int max_scroll = std::max(0, static_cast<int>(visible_.size()) - window_height_);
        scroll_ = std::clamp(scroll_, 0, max_scroll);
    }

    const dtb::Tree& tree_;
    std::unordered_set<int> expanded_;
    std::vector<int> visible_;
    int cursor_ = 0;
    int scroll_ = 0;
    int window_height_ = 1;
};

} // namespace ui