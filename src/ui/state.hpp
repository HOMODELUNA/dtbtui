#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "dtb/classify.hpp"
#include "dtb/model.hpp"

namespace ui {

enum class Focus { Tree, Types, Props };

// Rows moved by one mouse-wheel notch, per the plan's key table.
constexpr int kMouseScrollLines = 3;
// Longest accepted search string; keeps a runaway key repeat from growing it forever.
constexpr size_t kMaxFilterLength = 64;

// Rectangle of a panel in screen coordinates, reported by the panel element.
struct PaneBox {
    int x_min = 0;
    int y_min = 0;
    int x_max = -1;
    int y_max = -1;

    int width() const { return x_max - x_min + 1; }
    int height() const { return y_max - y_min + 1; }

    bool contains(int x, int y) const {
        return x_min <= x && x <= x_max && y_min <= y && y <= y_max;
    }
};

inline std::string to_lower(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(
            character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                 : character
        );
    }
    return lowered;
}

// A node matches the filter when its name (with the unit address) or one of its
// property names contains the needle.  Both arguments must already be lowered.
inline bool node_matches(const dtb::Node& node, std::string_view lowered_filter) {
    if (lowered_filter.empty()) return true;
    if (to_lower(node.name).find(lowered_filter) != std::string::npos) return true;
    for (const dtb::Property& prop : node.props) {
        if (to_lower(prop.name).find(lowered_filter) != std::string::npos) return true;
    }
    return false;
}

inline uint32_t cell_value(const std::vector<uint8_t>& raw, size_t index) {
    const size_t offset = index * sizeof(uint32_t);
    if (offset + sizeof(uint32_t) > raw.size()) return 0;
    return (static_cast<uint32_t>(raw[offset]) << 24)
         | (static_cast<uint32_t>(raw[offset + 1]) << 16)
         | (static_cast<uint32_t>(raw[offset + 2]) << 8) | static_cast<uint32_t>(raw[offset + 3]);
}

// True when at least one reference of the property resolves to a real node.
inline bool has_resolvable_ref(const dtb::Property& prop) {
    return std::any_of(prop.refs.begin(), prop.refs.end(), [](const dtb::Ref& ref) {
        return ref.node >= 0;
    });
}

// One row of the type panel: a device class and how many nodes it contains.
struct TypeEntry {
    dtb::DeviceKind kind = dtb::DeviceKind::Other;
    size_t count = 0;
};

// All UI state for the tree, type and property panels.  Nodes are referenced
// by arena index; the visible list is a cached pre-order walk.
class State {
public:
    explicit State(const dtb::Tree& tree)
        : tree_(tree) {
        if (!tree_.nodes.empty()) expanded_.insert(0);
        collect_types();
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
    void set_focus(Focus focus) { focus_ = focus; }
    void toggle_focus() {
        switch (focus_) {
        case Focus::Tree: focus_ = Focus::Types; break;
        case Focus::Types: focus_ = Focus::Props; break;
        case Focus::Props: focus_ = Focus::Tree; break;
        }
    }

    bool help_visible() const { return help_visible_; }
    void toggle_help() { help_visible_ = !help_visible_; }
    void close_help() { help_visible_ = false; }

    // --- transient status message (e.g. "unresolved phandle") ----------------
    const std::string& message() const { return message_; }
    void set_message(std::string message) { message_ = std::move(message); }
    void clear_message() { message_.clear(); }

    // --- search / filter -----------------------------------------------------
    bool search_active() const { return search_active_; }
    bool is_filtering() const { return !filter_.empty(); }
    const std::string& filter() const { return filter_; }
    size_t match_count() const { return match_count_; }

    void begin_search() {
        if (!is_filtering()) snapshot_ = Snapshot{expanded_, current_node_index(), scroll_};
        search_active_ = true;
    }

    // Accept the current filter: input line closes, the filtered tree stays.
    void confirm_search() { search_active_ = false; }

    // Esc: drop the filter and restore the pre-filter expansion, cursor, scroll.
    void cancel_search() {
        search_active_ = false;
        if (!is_filtering()) {
            snapshot_.reset();
            return;
        }
        filter_.clear();
        restore_snapshot();
    }

    void search_append(char character) {
        if (!search_active_ || filter_.size() >= kMaxFilterLength) return;
        filter_.push_back(character);
        rebuild_visible(current_node_index());
    }

    void search_backspace() {
        if (!search_active_ || filter_.empty()) return;
        filter_.pop_back();
        if (filter_.empty()) { restore_snapshot(); }
        else { rebuild_visible(current_node_index()); }
    }

    // --- type panel ----------------------------------------------------------
    const std::vector<TypeEntry>& types() const { return types_; }
    int types_cursor() const { return types_cursor_; }
    int types_scroll() const { return types_scroll_; }
    int types_window_height() const { return types_window_height_; }

    // A kind is enabled by default; disabling it hides its nodes in the tree.
    bool type_enabled(dtb::DeviceKind kind) const { return !disabled_types_.contains(kind); }
    bool is_solo(dtb::DeviceKind kind) const { return solo_ && *solo_ == kind; }
    std::optional<dtb::DeviceKind> solo() const { return solo_; }
    size_t disabled_type_count() const { return disabled_types_.size(); }

    // True when the tree is narrowed by the type panel.
    bool type_filter_active() const { return solo_.has_value() || !disabled_types_.empty(); }

    const TypeEntry* selected_type() const {
        if (types_cursor_ >= static_cast<int>(types_.size())) return nullptr;
        return &types_[static_cast<size_t>(types_cursor_)];
    }

    void set_types_box(PaneBox box) {
        types_box_ = box;
        types_window_height_ = std::max(1, box.height());
        clamp_types_scroll();
        ensure_types_cursor_visible();
    }

    void move_types_cursor(int delta) {
        const int count = static_cast<int>(types_.size());
        if (count == 0) return;
        types_cursor_ = std::clamp(types_cursor_ + delta, 0, count - 1);
        ensure_types_cursor_visible();
    }

    void types_first() {
        types_cursor_ = 0;
        ensure_types_cursor_visible();
    }

    void types_last() {
        if (types_.empty()) return;
        types_cursor_ = static_cast<int>(types_.size()) - 1;
        ensure_types_cursor_visible();
    }

    void types_page(int direction) {
        const int step = std::max(1, types_window_height_ - 2);
        move_types_cursor(direction * step);
    }

    // Space: enable/disable the selected type.  Disabling the soloed kind leaves
    // the solo view, so the panel always has an obvious way out.
    void toggle_type() {
        const TypeEntry* entry = selected_type();
        if (entry == nullptr) return;
        const dtb::DeviceKind kind = entry->kind;
        if (is_solo(kind))
            solo_.reset();
        else if (disabled_types_.contains(kind))
            disabled_types_.erase(kind);
        else
            disabled_types_.insert(kind);
        rebuild_visible(current_node_index());
    }

    // Enter: show only the selected type, or leave that view.  Entering the
    // view lands on the first node of that type instead of on an ancestor.
    void toggle_solo() {
        const TypeEntry* entry = selected_type();
        if (entry == nullptr) return;
        if (is_solo(entry->kind)) {
            solo_.reset();
            rebuild_visible(current_node_index());
            return;
        }
        solo_ = entry->kind;
        rebuild_visible(current_node_index());
        for (size_t i = 0; i < visible_.size(); ++i) {
            if (tree_.nodes[visible_[i]].kind != *solo_) continue;
            cursor_ = static_cast<int>(i);
            ensure_cursor_visible();
            sync_props();
            break;
        }
    }

    // Esc: leave the solo view.  Returns false when no solo was active.
    bool cancel_solo() {
        if (!solo_) return false;
        solo_.reset();
        rebuild_visible(current_node_index());
        return true;
    }

    // --- phandle navigation --------------------------------------------------
    size_t jump_depth() const { return jumps_.size(); }

    // Jump to the node referenced by the selected property.  Returns false when
    // there is nothing to jump to; an unresolved phandle leaves a message.
    bool jump_to_selected_ref() {
        const dtb::Property* prop = selected_property();
        if (prop == nullptr || prop->refs.empty()) return false;

        const dtb::Ref* target = nullptr;
        for (const dtb::Ref& ref : prop->refs) {
            if (ref.node >= 0) {
                target = &ref;
                break;
            }
        }
        if (target == nullptr) {
            set_message(fmt::format(
                "{}: phandle 0x{:08x} 未解析，无法跳转", prop->name,
                cell_value(prop->raw, prop->refs.front().cell_index)
            ));
            return false;
        }

        jumps_.push_back(JumpPoint{expanded_, current_node_index(), scroll_});
        drop_filter();
        for (int node = tree_.nodes[target->node].parent; node >= 0;
             node = tree_.nodes[node].parent) {
            expanded_.insert(node);
        }
        rebuild_visible(target->node);
        return true;
    }

    // Backspace: return to the node the last jump started from.
    bool jump_back() {
        if (jumps_.empty()) return false;
        JumpPoint point = std::move(jumps_.back());
        jumps_.pop_back();
        expanded_ = std::move(point.expanded);
        drop_filter();
        rebuild_visible(point.node);
        scroll_ = point.scroll;
        clamp_scroll();
        return true;
    }

    // --- property panel ------------------------------------------------------
    int props_cursor() const { return props_cursor_; }
    int props_scroll() const { return props_scroll_; }
    int props_window_height() const { return props_window_height_; }

    const dtb::Property* selected_property() const {
        const std::vector<dtb::Property>& props = current_props();
        if (props.empty() || props_cursor_ >= static_cast<int>(props.size())) return nullptr;
        return &props[props_cursor_];
    }

    void set_props_box(PaneBox box) {
        props_box_ = box;
        set_props_window_height(box.height());
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

    // --- tree panel ----------------------------------------------------------
    void set_tree_box(PaneBox box) {
        tree_box_ = box;
        set_window_height(box.height());
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

    // --- mouse ---------------------------------------------------------------
    // Row of `visible` under the given screen coordinate, or -1.
    int tree_row_at(int x, int y) const {
        if (!tree_box_.contains(x, y)) return -1;
        return visible_row(scroll_ + (y - tree_box_.y_min));
    }

    // True when the coordinate falls on the ▸/▾ glyph in front of the row's node.
    bool tree_toggle_at(int x, int y) const {
        const int row = tree_row_at(x, y);
        if (row < 0) return false;
        const dtb::Node& node = tree_.nodes[visible_[row]];
        if (node.children.empty()) return false;
        const int glyph = tree_box_.x_min + node.depth * 2;
        return x == glyph || x == glyph + 1;
    }

    bool select_tree_row(int row) {
        if (visible_row(row) < 0) return false;
        cursor_ = row;
        ensure_cursor_visible();
        sync_props();
        return true;
    }

    // Toggle the row's fold without moving the cursor off its current node.
    bool toggle_tree_row(int row) {
        if (visible_row(row) < 0) return false;
        const int node = visible_[row];
        if (tree_.nodes[node].children.empty()) return false;
        const int keep = current_node_index();
        if (expanded_.contains(node))
            expanded_.erase(node);
        else
            expanded_.insert(node);
        rebuild_visible(keep);
        return true;
    }

    int props_row_at(int x, int y) const {
        if (!props_box_.contains(x, y)) return -1;
        const int row = props_scroll_ + (y - props_box_.y_min);
        if (row < 0 || row >= static_cast<int>(current_props().size())) return -1;
        return row;
    }

    bool select_props_row(int row) {
        const int count = static_cast<int>(current_props().size());
        if (row < 0 || row >= count) return false;
        props_cursor_ = row;
        ensure_props_cursor_visible();
        return true;
    }

    int types_row_at(int x, int y) const {
        if (!types_box_.contains(x, y)) return -1;
        const int row = types_scroll_ + (y - types_box_.y_min);
        if (row < 0 || row >= static_cast<int>(types_.size())) return -1;
        return row;
    }

    // True when the coordinate falls on the `[x]` box in front of a type row.
    bool types_toggle_at(int x, int y) const {
        const int row = types_row_at(x, y);
        if (row < 0) return false;
        const int column = x - types_box_.x_min;
        return column >= 0 && column <= 2;
    }

    bool select_types_row(int row) {
        if (row < 0 || row >= static_cast<int>(types_.size())) return false;
        types_cursor_ = row;
        ensure_types_cursor_visible();
        return true;
    }

    // Toggle the row's kind without moving the type cursor.
    bool toggle_types_row(int row) {
        const int keep = types_cursor_;
        if (!select_types_row(row)) return false;
        toggle_type();
        types_cursor_ = keep;
        ensure_types_cursor_visible();
        return true;
    }

    // Wheel: scroll the pane under the pointer, keeping the cursor inside it.
    void wheel(int x, int y, int direction) {
        if (tree_box_.contains(x, y)) { scroll_tree(direction * kMouseScrollLines); }
        else if (types_box_.contains(x, y)) { scroll_types(direction * kMouseScrollLines); }
        else if (props_box_.contains(x, y)) { scroll_props(direction * kMouseScrollLines); }
    }

    // Left click: focus the pane under the pointer and select the row.
    // Clicking the fold glyph of a tree row only toggles the fold; clicking the
    // `[x]` box of a type row only toggles that type.
    void click(int x, int y) {
        if (tree_box_.contains(x, y)) {
            focus_ = Focus::Tree;
            const int row = tree_row_at(x, y);
            if (row < 0) return;
            if (tree_toggle_at(x, y)) { toggle_tree_row(row); }
            else { select_tree_row(row); }
            return;
        }
        if (types_box_.contains(x, y)) {
            focus_ = Focus::Types;
            const int row = types_row_at(x, y);
            if (row < 0) return;
            if (types_toggle_at(x, y)) { toggle_types_row(row); }
            else { select_types_row(row); }
            return;
        }
        if (props_box_.contains(x, y)) {
            focus_ = Focus::Props;
            const int row = props_row_at(x, y);
            if (row >= 0) select_props_row(row);
        }
    }

    void scroll_tree(int lines) {
        scroll_ += lines;
        clamp_scroll();
        keep_cursor_in_window();
    }

    void scroll_props(int lines) {
        props_scroll_ += lines;
        clamp_props_scroll();
        keep_props_cursor_in_window();
    }

    void scroll_types(int lines) {
        types_scroll_ += lines;
        clamp_types_scroll();
        keep_types_cursor_in_window();
    }

private:
    struct Snapshot {
        std::unordered_set<int> expanded;
        int node = 0;
        int scroll = 0;
    };

    struct JumpPoint {
        std::unordered_set<int> expanded;
        int node = 0;
        int scroll = 0;
    };

    int visible_row(int row) const {
        if (row < 0 || row >= static_cast<int>(visible_.size())) return -1;
        return row;
    }

    int index_of(int node) const {
        const auto it = std::find(visible_.begin(), visible_.end(), node);
        if (it == visible_.end()) return -1;
        return static_cast<int>(it - visible_.begin());
    }

    // Keep the cursor on `focus_node` when it is still visible, otherwise walk
    // up to the nearest visible ancestor (a collapsed or filtered-out case).
    static int focus_index(const dtb::Tree& tree, const std::vector<int>& visible, int focus_node) {
        int candidate = focus_node;
        while (candidate >= 0) {
            const auto it = std::find(visible.begin(), visible.end(), candidate);
            if (it != visible.end()) return static_cast<int>(it - visible.begin());
            candidate = tree.nodes[candidate].parent;
        }
        return 0;
    }

    // Mark every node matching the filter plus its whole ancestor chain.
    std::vector<char> filter_mask(std::string_view lowered) const {
        std::vector<char> mask(tree_.nodes.size(), 0);
        for (size_t i = 0; i < tree_.nodes.size(); ++i) {
            if (!node_matches(tree_.nodes[i], lowered)) continue;
            for (int node = static_cast<int>(i); node >= 0; node = tree_.nodes[node].parent)
                mask[static_cast<size_t>(node)] = 1;
        }
        return mask;
    }

    void collect_types() {
        std::unordered_map<dtb::DeviceKind, size_t> counts;
        for (const dtb::Node& node : tree_.nodes) {
            if (node.kind == dtb::DeviceKind::Structural) continue;
            ++counts[node.kind];
        }
        for (const dtb::DeviceKind kind : dtb::kSelectableKinds) {
            const auto it = counts.find(kind);
            if (it != counts.end()) types_.push_back(TypeEntry{kind, it->second});
        }
    }

    // Whether the node may be shown under the current type filter.  A solo view
    // keeps only that kind; masking keeps every kind except the disabled ones.
    // Structure (containers, pin groups, ...) is never hidden by masking.
    bool node_passes(const dtb::Node& node) const {
        if (solo_) return node.kind == *solo_;
        if (node.kind == dtb::DeviceKind::Structural) return true;
        return !disabled_types_.contains(node.kind);
    }

    void rebuild_visible(int focus_node) {
        visible_.clear();
        match_count_ = 0;
        if (tree_.nodes.empty()) {
            cursor_ = 0;
            scroll_ = 0;
            return;
        }

        const bool searching = is_filtering();
        // The search view and the solo view are "where are the matches" views:
        // they ignore the fold state so every match stays reachable.
        const bool flat = searching || solo_.has_value();
        const std::string lowered = searching ? to_lower(filter_) : std::string();

        std::vector<char> mask;
        if (flat) {
            mask.assign(tree_.nodes.size(), 0);
            for (size_t i = 0; i < tree_.nodes.size(); ++i) {
                const dtb::Node& node = tree_.nodes[i];
                const bool search_hit = searching && node_matches(node, lowered);
                if (search_hit) ++match_count_;
                if (!node_passes(node)) continue;
                if (searching && !search_hit) continue;
                for (int ancestor = static_cast<int>(i); ancestor >= 0;
                     ancestor = tree_.nodes[ancestor].parent) {
                    mask[static_cast<size_t>(ancestor)] = 1;
                }
            }
        }

        std::vector<int> stack{0};
        while (!stack.empty()) {
            const int node = stack.back();
            stack.pop_back();
            const bool keep
                = flat ? mask[static_cast<size_t>(node)] != 0 : node_passes(tree_.nodes[node]);
            if (!keep) {
                if (flat) continue; // a flat view skips whole subtrees, not just rows
            }
            else { visible_.push_back(node); }

            // A flat view is always fully open; otherwise the fold state decides
            // whether the children are reachable.
            if (!flat && !expanded_.contains(node)) continue;
            const std::vector<int>& children = tree_.nodes[node].children;
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                stack.push_back(*it);
        }

        cursor_ = focus_index(tree_, visible_, focus_node);
        clamp_scroll();
        ensure_cursor_visible();
        sync_props();
    }

    void restore_snapshot() {
        if (!snapshot_) {
            rebuild_visible(current_node_index());
            return;
        }
        expanded_ = snapshot_->expanded;
        rebuild_visible(snapshot_->node);
        scroll_ = snapshot_->scroll;
        clamp_scroll();
    }

    // Drop the filter but keep whatever is expanded now (used by jumps).
    void drop_filter() {
        filter_.clear();
        match_count_ = 0;
        snapshot_.reset();
        search_active_ = false;
    }

    void ensure_cursor_visible() {
        if (visible_.empty()) return;
        const int margin = std::min(2, std::max(0, (window_height_ - 1) / 2));
        if (cursor_ < scroll_ + margin) scroll_ = cursor_ - margin;
        if (cursor_ >= scroll_ + window_height_ - margin)
            scroll_ = cursor_ - window_height_ + margin + 1;
        clamp_scroll();
    }

    void keep_cursor_in_window() {
        if (visible_.empty()) return;
        cursor_ = std::clamp(cursor_, 0, static_cast<int>(visible_.size()) - 1);
        if (cursor_ < scroll_) cursor_ = scroll_;
        if (cursor_ >= scroll_ + window_height_) cursor_ = scroll_ + window_height_ - 1;
        cursor_ = std::clamp(cursor_, 0, static_cast<int>(visible_.size()) - 1);
        sync_props();
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

    void keep_props_cursor_in_window() {
        const int count = static_cast<int>(current_props().size());
        if (count == 0) return;
        props_cursor_ = std::clamp(props_cursor_, 0, count - 1);
        if (props_cursor_ < props_scroll_) props_cursor_ = props_scroll_;
        if (props_cursor_ >= props_scroll_ + props_window_height_)
            props_cursor_ = props_scroll_ + props_window_height_ - 1;
        props_cursor_ = std::clamp(props_cursor_, 0, count - 1);
    }

    void clamp_props_scroll() {
        const int count = static_cast<int>(current_props().size());
        const int max_scroll = std::max(0, count - props_window_height_);
        props_scroll_ = std::clamp(props_scroll_, 0, max_scroll);
    }

    void ensure_types_cursor_visible() {
        const int count = static_cast<int>(types_.size());
        if (count == 0) return;
        const int margin = std::min(1, std::max(0, (types_window_height_ - 1) / 2));
        if (types_cursor_ < types_scroll_ + margin) types_scroll_ = types_cursor_ - margin;
        if (types_cursor_ >= types_scroll_ + types_window_height_ - margin)
            types_scroll_ = types_cursor_ - types_window_height_ + margin + 1;
        clamp_types_scroll();
    }

    void keep_types_cursor_in_window() {
        const int count = static_cast<int>(types_.size());
        if (count == 0) return;
        types_cursor_ = std::clamp(types_cursor_, 0, count - 1);
        if (types_cursor_ < types_scroll_) types_cursor_ = types_scroll_;
        if (types_cursor_ >= types_scroll_ + types_window_height_)
            types_cursor_ = types_scroll_ + types_window_height_ - 1;
        types_cursor_ = std::clamp(types_cursor_, 0, count - 1);
    }

    void clamp_types_scroll() {
        const int count = static_cast<int>(types_.size());
        const int max_scroll = std::max(0, count - types_window_height_);
        types_scroll_ = std::clamp(types_scroll_, 0, max_scroll);
    }

    const dtb::Tree& tree_;
    std::unordered_set<int> expanded_;
    std::vector<int> visible_;
    int cursor_ = 0;
    int scroll_ = 0;
    int window_height_ = 1;

    std::string filter_;
    std::string message_;
    size_t match_count_ = 0;
    bool search_active_ = false;
    bool help_visible_ = false;
    std::optional<Snapshot> snapshot_;
    std::vector<JumpPoint> jumps_;

    PaneBox tree_box_;
    PaneBox props_box_;
    PaneBox types_box_;

    std::vector<TypeEntry> types_;
    std::unordered_set<dtb::DeviceKind> disabled_types_;
    std::optional<dtb::DeviceKind> solo_;
    int types_cursor_ = 0;
    int types_scroll_ = 0;
    int types_window_height_ = 1;

    Focus focus_ = Focus::Tree;
    int props_node_ = -1;
    int props_cursor_ = 0;
    int props_scroll_ = 0;
    int props_window_height_ = 1;
};

} // namespace ui