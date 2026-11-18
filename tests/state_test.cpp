#include "doctest.h"

#include <string>
#include <utility>
#include <vector>

#include "dtb/model.hpp"
#include "ui/state.hpp"

namespace {

dtb::Property prop(std::string name) {
    dtb::Property value;
    value.name = std::move(name);
    return value;
}

dtb::Node node(
    std::string name, std::string path, int parent, int depth, std::vector<int> children = {}
) {
    dtb::Node value;
    value.name = std::move(name);
    value.path = std::move(path);
    value.parent = parent;
    value.depth = depth;
    value.children = std::move(children);
    return value;
}

// / (0) -> a (1) -> a1 (2), a2 (3)
//        -> b (4)
dtb::Tree sample_tree() {
    dtb::Tree tree;
    tree.nodes.push_back(node("/", "/", -1, 0, {1, 4}));
    for (int i = 0; i < 5; ++i)
        tree.nodes[0].props.push_back(prop("root-" + std::to_string(i)));
    tree.nodes.push_back(node("a", "/a", 0, 1, {2, 3}));
    tree.nodes[1].props.push_back(prop("a-0"));
    tree.nodes[1].props.push_back(prop("a-1"));
    tree.nodes.push_back(node("a1", "/a/a1", 1, 2));
    tree.nodes.push_back(node("a2", "/a/a2", 1, 2));
    tree.nodes.push_back(node("b", "/b", 0, 1));
    return tree;
}

// / (0) -> child-0 (1) ... child-9 (10)
dtb::Tree wide_tree() {
    dtb::Tree tree;
    std::vector<int> children;
    for (int i = 0; i < 10; ++i)
        children.push_back(i + 1);
    tree.nodes.push_back(node("/", "/", -1, 0, children));
    for (int i = 0; i < 10; ++i) {
        tree.nodes.push_back(node("child-" + std::to_string(i), "/child-" + std::to_string(i), 0, 1)
        );
    }
    return tree;
}

} // namespace

TEST_CASE("state: startup expands only the root") {
    const dtb::Tree tree = sample_tree();
    ui::State state(tree);

    const std::vector<int> expected = {0, 1, 4};
    CHECK(state.visible() == expected);
    CHECK(state.cursor() == 0);
    CHECK(state.current_node().path == "/");
    CHECK(state.is_expanded(0));
    CHECK_FALSE(state.is_expanded(1));
}

TEST_CASE("state: cursor movement clamps at both ends") {
    const dtb::Tree tree = sample_tree();
    ui::State state(tree);

    state.move_cursor(-1);
    CHECK(state.cursor() == 0);
    state.move_cursor(1);
    CHECK(state.current_node().path == "/a");
    state.move_cursor(100);
    CHECK(state.current_node().path == "/b");
    state.move_cursor(1);
    CHECK(state.current_node().path == "/b");
}

TEST_CASE("state: expand reveals one level and keeps the cursor") {
    const dtb::Tree tree = sample_tree();
    ui::State state(tree);
    state.move_cursor(1); // /a

    state.expand_current();
    const std::vector<int> expected = {0, 1, 2, 3, 4};
    CHECK(state.visible() == expected);
    CHECK(state.current_node().path == "/a");

    state.expand_current(); // already expanded: no recursive expansion
    CHECK(state.visible() == expected);
}

TEST_CASE("state: collapse folds the node, then jumps to the parent") {
    const dtb::Tree tree = sample_tree();
    ui::State state(tree);
    state.move_cursor(1); // /a
    state.expand_current();

    state.collapse_current();
    CHECK(state.visible() == std::vector<int>({0, 1, 4}));
    CHECK(state.current_node().path == "/a");

    state.collapse_current();
    CHECK(state.current_node().path == "/");
}

TEST_CASE("state: Space toggles the current node") {
    const dtb::Tree tree = sample_tree();
    ui::State state(tree);
    state.move_cursor(1); // /a

    state.toggle_current();
    CHECK(state.is_expanded(1));
    CHECK(state.visible() == std::vector<int>({0, 1, 2, 3, 4}));

    state.toggle_current();
    CHECK_FALSE(state.is_expanded(1));
    CHECK(state.visible() == std::vector<int>({0, 1, 4}));
}

TEST_CASE("state: g/G and paging stay in range") {
    const dtb::Tree tree = wide_tree();
    ui::State state(tree);
    state.set_window_height(5);

    state.move_to_last();
    CHECK(state.current_node().path == "/child-9");
    CHECK(state.scroll() == 6);

    state.move_to_first();
    CHECK(state.current_node().path == "/");
    CHECK(state.scroll() == 0);

    state.page(1);
    CHECK(state.current_node().path == "/child-2");
    state.page(1);
    CHECK(state.current_node().path == "/child-5");
    state.page(-1);
    CHECK(state.current_node().path == "/child-2");
}

TEST_CASE("state: cursor leaving the window keeps two rows of context") {
    const dtb::Tree tree = wide_tree();
    ui::State state(tree);
    state.set_window_height(5);

    state.move_to_last();
    CHECK(state.scroll() == 6);
    CHECK(state.cursor() >= state.scroll());
    CHECK(state.cursor() < state.scroll() + state.window_height());

    state.move_cursor(-3);
    CHECK(state.cursor() == 7);
    CHECK(state.scroll() == 5);

    state.move_to_first();
    CHECK(state.scroll() == 0);

    state.move_cursor(4);
    CHECK(state.current_node().path == "/child-3");
    CHECK(state.scroll() == 2);
}

TEST_CASE("state: focus toggles between tree and properties") {
    const dtb::Tree tree = sample_tree();
    ui::State state(tree);

    CHECK(state.focus() == ui::Focus::Tree);
    state.toggle_focus();
    CHECK(state.focus() == ui::Focus::Props);
    state.toggle_focus();
    CHECK(state.focus() == ui::Focus::Tree);
}

TEST_CASE("state: property cursor is clamped and follows its window") {
    const dtb::Tree tree = sample_tree();
    ui::State state(tree);

    CHECK(state.props_cursor() == 0);
    CHECK(state.selected_property()->name == "root-0");

    state.move_props_cursor(-1);
    CHECK(state.props_cursor() == 0);
    state.move_props_cursor(100);
    CHECK(state.props_cursor() == 4);
    CHECK(state.selected_property()->name == "root-4");

    state.set_props_window_height(3);
    CHECK(state.props_scroll() == 2);
    state.props_first();
    CHECK(state.props_scroll() == 0);
}

TEST_CASE("state: moving the tree cursor resets the property cursor") {
    const dtb::Tree tree = sample_tree();
    ui::State state(tree);

    state.move_props_cursor(3);
    CHECK(state.props_cursor() == 3);

    state.move_cursor(1); // /a
    CHECK(state.props_cursor() == 0);
    CHECK(state.selected_property()->name == "a-0");
}

TEST_CASE("state: type reason starts collapsed and toggles") {
    const dtb::Tree tree = sample_tree();
    ui::State state(tree);

    CHECK_FALSE(state.show_reason());
    state.toggle_reason();
    CHECK(state.show_reason());
    state.toggle_reason();
    CHECK_FALSE(state.show_reason());
}

namespace {

// Build a big-endian u32 property value.
std::vector<uint8_t> cells(uint32_t value) {
    return {
        static_cast<uint8_t>(value >> 24),
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value),
    };
}

// / (0) -> soc (1) -> serial@5000000 (2) [interrupt-parent -> gic]
//                   -> gic (3) [phandle = 5]
//                   -> uart (4)
dtb::Tree phandle_tree() {
    dtb::Tree tree;
    tree.nodes.push_back(node("/", "/", -1, 0, {1}));
    tree.nodes.push_back(node("soc", "/soc", 0, 1, {2, 3, 4}));
    tree.nodes.push_back(node("serial@5000000", "/soc/serial@5000000", 1, 2));
    tree.nodes.push_back(node("gic", "/soc/gic", 1, 2));
    tree.nodes.push_back(node("uart", "/soc/uart", 1, 2));

    dtb::Property interrupt_parent = prop("interrupt-parent");
    interrupt_parent.raw = cells(5);
    interrupt_parent.kind = dtb::ValueKind::Cells;
    interrupt_parent.refs.push_back(dtb::Ref{0, 3});
    tree.nodes[2].props.push_back(interrupt_parent);

    dtb::Property phandle = prop("phandle");
    phandle.raw = cells(5);
    phandle.kind = dtb::ValueKind::Cells;
    phandle.refs.push_back(dtb::Ref{0, 3});
    tree.nodes[3].props.push_back(phandle);
    return tree;
}

} // namespace

TEST_CASE("state: search filters to matches and their ancestors") {
    const dtb::Tree tree = phandle_tree();
    ui::State state(tree);
    state.move_cursor(1); // /soc
    state.expand_current();
    CHECK(state.visible().size() == 5);

    state.begin_search();
    CHECK(state.search_active());
    for (const char* character = "UART"; *character != '\0'; ++character)
        state.search_append(*character);

    CHECK(state.is_filtering());
    CHECK(state.filter() == "UART");
    // /soc/uart plus the ancestor chain (/ and /soc).
    const std::vector<int> expected = {0, 1, 4};
    CHECK(state.visible() == expected);
    CHECK(state.match_count() == 1);
    // The cursor keeps its node while it stays visible: /soc came before the filter.
    CHECK(state.current_node().path == "/soc");
}

TEST_CASE("state: search matches property names too") {
    const dtb::Tree tree = phandle_tree();
    ui::State state(tree);
    state.begin_search();
    for (const char* character = "interrupt-parent"; *character != '\0'; ++character)
        state.search_append(*character);

    const std::vector<int> expected = {0, 1, 2};
    CHECK(state.visible() == expected);
    CHECK(state.match_count() == 1);
}

TEST_CASE("state: search ignores the fold state") {
    const dtb::Tree tree = phandle_tree();
    ui::State state(tree);
    CHECK_FALSE(state.is_expanded(1));

    state.begin_search();
    state.search_append('g');
    state.search_append('i');
    state.search_append('c');

    // /soc stays collapsed in the folded tree, but the matching grandchild shows.
    const std::vector<int> expected = {0, 1, 3};
    CHECK(state.visible() == expected);
    CHECK_FALSE(state.is_expanded(1));
}

TEST_CASE("state: cancel search restores folds, cursor and scroll") {
    const dtb::Tree tree = phandle_tree();
    ui::State state(tree);
    state.move_cursor(1); // /soc
    state.expand_current();
    state.move_cursor(1); // /soc/serial@5000000
    const std::vector<int> before = state.visible();
    const int cursor_before = state.cursor();

    state.begin_search();
    state.search_append('u');
    state.search_append('a');
    state.search_append('r');
    CHECK(state.visible() != before);

    state.cancel_search();
    CHECK_FALSE(state.search_active());
    CHECK_FALSE(state.is_filtering());
    CHECK(state.visible() == before);
    CHECK(state.cursor() == cursor_before);
    CHECK(state.current_node().path == "/soc/serial@5000000");
    CHECK(state.is_expanded(1));
}

TEST_CASE("state: backspacing an empty filter restores the tree") {
    const dtb::Tree tree = phandle_tree();
    ui::State state(tree);
    const std::vector<int> before = state.visible();

    state.begin_search();
    state.search_append('z');
    CHECK(state.visible().empty()); // nothing matches "z"
    state.search_backspace();
    CHECK_FALSE(state.is_filtering());
    CHECK(state.visible() == before);

    state.confirm_search();
    CHECK_FALSE(state.search_active());
}

TEST_CASE("state: a filter without matches yields an empty visible list") {
    const dtb::Tree tree = phandle_tree();
    ui::State state(tree);
    state.begin_search();
    state.search_append('q');
    state.search_append('q');
    state.search_append('q');

    CHECK(state.visible().empty());
    CHECK(state.match_count() == 0);
    state.move_cursor(1); // must not crash
    CHECK(state.visible().empty());
}
