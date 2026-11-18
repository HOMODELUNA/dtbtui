#pragma once

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "dtb/model.hpp"

namespace ui {

enum class Focus { Tree, Props };

// All UI state for the tree and property panels.  Nodes are referenced by arena
// index; the visible list is a cached pre-order walk over the expanded nodes.
class State {
public:
    explicit State(const dtb::Tree& tree)
        : tree_(tree) {
        if (!tree_.nodes.empty()) expanded_.insert(0);
        rebuild_visible(0);
    }

    const dtb::Tree& tree() const { return tree_; }
    const std::vector<int>& visible() const { return visible_; }
    int cursor() const { return cursor_; }
    int scroll() const { return scroll_; }
    int window_height() const { return window_height_; }

    bool is_expanded(int node) const { return expanded_.contains(node); }
    bool has_children(int node) const { return !tree_.nodes[node].children.empty(); }

    int current_node_index() const {
        if (visible_.empty()) return 0;
        return visible_[cursor_];
    }

    const dtb::Node& current_node() const { return tree_.nodes[current_node_index()]; }
    const std::vector<dtb::Property>& current_props() const { return current_node().props; }

    Focus focus() const { return focus_; }
    void toggle_focus() { focus_ = focus_ == Focus::Tree ? Focus::Props : Focus::Tree; }

    int props_cursor() const { return props_cursor_; }
    int props_scroll() const { return props_scroll_; }
    int props_window_height() const { return props_window_height_; }

    const dtb::Property* selected_property() const {
        const std::vector<dtb::Property>& props = current_props();
        if (props.empty() || props_cursor_ >= static_cast<int>(props.size())) return nullptr;
        return &props[props_cursor_];
    }

    void set_props_window_height(int height) {
        props_window_height_ = std::max(1, height);
        clamp_props_scroll();
        ensure_props_cursor_visible();
    }

    void move_props_cursor(int delta) {
        const int count = static_cast<int>(current_props().size());
        if (count == 0) return;
        props_cursor_ = std::clamp(props_cursor_ + delta, 0, count - 1);
        ensure_props_cursor_visible();
    }

    void props_first() {
        props_cursor_ = 0;
        ensure_props_cursor_visible();
    }

    void props_last() {
        const int count = static_cast<int>(current_props().size());
        if (count == 0) return;
        props_cursor_ = count - 1;
        ensure_props_cursor_visible();
    }

    void props_page(int direction) {
        const int step = std::max(1, props_window_height_ - 2);
        move_props_cursor(direction * step);
    }

    void set_window_height(int height) {
        window_height_ = std::max(1, height);
        clamp_scroll();
        ensure_cursor_visible();
    }

    void move_cursor(int delta) {
        if (visible_.empty()) return;
        cursor_ = std::clamp(cursor_ + delta, 0, static_cast<int>(visible_.size()) - 1);
        ensure_cursor_visible();
        sync_props();
    }

    void move_to_first() {
        cursor_ = 0;
        ensure_cursor_visible();
        sync_props();
    }

    void move_to_last() {
        if (visible_.empty()) return;
        cursor_ = static_cast<int>(visible_.size()) - 1;
        ensure_cursor_visible();
        sync_props();
    }

    void page(int direction) {
        const int step = std::max(1, window_height_ - 2);
        move_cursor(direction * step);
    }

    void expand_current() {
        const int node = current_node_index();
        if (!has_children(node) || expanded_.contains(node)) return;
        expanded_.insert(node);
        rebuild_visible(node);
    }

    void collapse_current() {
        const int node = current_node_index();
        if (expanded_.contains(node)) {
            expanded_.erase(node);
            rebuild_visible(node);
            return;
        }
        const int parent = tree_.nodes[node].parent;
        if (parent < 0) return;
        const int index = index_of(parent);
        if (index < 0) return;
        cursor_ = index;
        ensure_cursor_visible();
        sync_props();
    }

    void toggle_current() {
        const int node = current_node_index();
        if (!has_children(node)) return;
        if (expanded_.contains(node))
            expanded_.erase(node);
        else
            expanded_.insert(node);
        rebuild_visible(node);
    }

private:
    int index_of(int node) const {
        const auto it = std::find(visible_.begin(), visible_.end(), node);
        if (it == visible_.end()) return -1;
        return static_cast<int>(it - visible_.begin());
    }

    // Keep the cursor on `focus_node` when it is still visible, otherwise walk
    // up to the nearest visible ancestor (a collapsed ancestor case).
    static int focus_index(const dtb::Tree& tree, const std::vector<int>& visible, int focus_node) {
        int candidate = focus_node;
        while (candidate >= 0) {
            const auto it = std::find(visible.begin(), visible.end(), candidate);
            if (it != visible.end()) return static_cast<int>(it - visible.begin());
            candidate = tree.nodes[candidate].parent;
        }
        return 0;
    }

    void rebuild_visible(int focus_node) {
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

        cursor_ = focus_index(tree_, visible_, focus_node);
        clamp_scroll();
        ensure_cursor_visible();
        sync_props();
    }

    void ensure_cursor_visible() {
        if (visible_.empty()) return;
        const int margin = std::min(2, std::max(0, (window_height_ - 1) / 2));
        if (cursor_ < scroll_ + margin) scroll_ = cursor_ - margin;
        if (cursor_ >= scroll_ + window_height_ - margin)
            scroll_ = cursor_ - window_height_ + margin + 1;
        clamp_scroll();
    }

    void clamp_scroll() {
        const int max_scroll = std::max(0, static_cast<int>(visible_.size()) - window_height_);
        scroll_ = std::clamp(scroll_, 0, max_scroll);
    }

    void sync_props() {
        const int node = current_node_index();
        if (node == props_node_) return;
        props_node_ = node;
        props_cursor_ = 0;
        props_scroll_ = 0;
    }

    void ensure_props_cursor_visible() {
        const int count = static_cast<int>(current_props().size());
        if (count == 0) return;
        const int margin = std::min(2, std::max(0, (props_window_height_ - 1) / 2));
        if (props_cursor_ < props_scroll_ + margin) props_scroll_ = props_cursor_ - margin;
        if (props_cursor_ >= props_scroll_ + props_window_height_ - margin)
            props_scroll_ = props_cursor_ - props_window_height_ + margin + 1;
        clamp_props_scroll();
    }

    void clamp_props_scroll() {
        const int count = static_cast<int>(current_props().size());
        const int max_scroll = std::max(0, count - props_window_height_);
        props_scroll_ = std::clamp(props_scroll_, 0, max_scroll);
    }

    const dtb::Tree& tree_;
    std::unordered_set<int> expanded_;
    std::vector<int> visible_;
    int cursor_ = 0;
    int scroll_ = 0;
    int window_height_ = 1;

    Focus focus_ = Focus::Tree;
    int props_node_ = -1;
    int props_cursor_ = 0;
    int props_scroll_ = 0;
    int props_window_height_ = 1;
};

} // namespace ui