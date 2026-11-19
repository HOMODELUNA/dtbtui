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
